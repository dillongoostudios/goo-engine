/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup blenloader
 */

#define DNA_DEPRECATED_ALLOW

/* Define macros in `DNA_genfile.h` for versioning provenance checks. */
#define DNA_GENFILE_VERSIONING_MACROS
#include "DNA_genfile.h"
#undef DNA_GENFILE_VERSIONING_MACROS

#include "NOD_geometry_nodes_srna.hh"

#include "DNA_ID.h"
#include "DNA_brush_types.h"
#include "DNA_camera_types.h"
#include "DNA_curve_types.h"
#include "DNA_light_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_tree_interface_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_xr_types.h"

#include "BLI_listbase_iterator.hh"
#include "BLI_math_vector.hh"
#include "BLI_math_vector_types.hh"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_string_utils.hh"
#include "BLI_sys_types.h"
#include "BLI_vector.hh"

#include <algorithm>
#include <cmath>

#include "BKE_anim_visualization.h"
#include "BKE_animsys.h"
#include "BKE_attribute.hh"
#include "BKE_colortools.hh"
#include "BKE_context.hh"
#include "BKE_curves.hh"
#include "BKE_idprop.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_override.hh"
#include "BKE_main.hh"
#include "BKE_mesh_legacy_convert.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"

#include "SEQ_effects.hh"
#include "SEQ_iterator.hh"
#include "SEQ_sequencer.hh"

#include "BLO_read_write.hh"
#include "readfile.hh"

#include "versioning_common.hh"

// #include "CLG_log.h"

namespace blender {

// static CLG_LogRef LOG = {"blend.doversion"};

static void version_geometry_nodes_properties(FileData &fd,
                                              Main &bmain,
                                              Object &object,
                                              NodesModifierData &nmd)
{
  const IDProperty *old_props = nmd.settings_legacy.properties;
  if (!old_props) {
    /* Versioning has already been done, this check makes the function idempotent. */
    return;
  }
  if (!nmd.node_group) {
    IDP_FreeProperty(nmd.settings_legacy.properties);
    nmd.settings_legacy.properties = nullptr;
    BLO_reportf_wrap(fd.reports,
                     RPT_WARNING,
                     "Modifier '%s' from Object '%s' is missing its Geometry Node Group, its "
                     "settings will be lost (reset to default).",
                     nmd.modifier.name,
                     BKE_id_name(object.id));
    return;
  }
  if (ID_MISSING(&nmd.node_group->id)) {
    /* Keeping the old idproperties is not an option, and not really useful, since if the
     * blend-file is saved in this current state, it won't be re-versioned here later anyway.
     *
     * Furthermore, the whole remaining part of the code expects this to be nullptr, and keeping it
     * at runtime actually causes weird issues in depsgraph nodes building phase.
     *
     * So all in all, it's simpler and safer to also just lose these values here - if file is not
     * saved in this state, next loading will do the versioning if the node-group is available
     * again, otherwise that data is lost.
     */
    IDP_FreeProperty(nmd.settings_legacy.properties);
    nmd.settings_legacy.properties = nullptr;
    BLO_reportf_wrap(
        fd.reports,
        RPT_WARNING,
        "Modifier '%s' from Object '%s' is using a missing linked Geometry Node Group, its "
        "settings will be lost (reset to default) if the file is saved in this state.",
        nmd.modifier.name,
        BKE_id_name(object.id));
    return;
  }
  const bNodeTree &ntree = *nmd.node_group;
  ntree.ensure_interface_cache();

  IDProperty *system_props = bke::idprop::create_group("NodesModifierProperties").release();

  IDProperty *inputs = bke::idprop::create_group("inputs").release();
  IDP_AddToGroup(system_props, inputs);

  const std::string inputs_path_prefix = fmt::format("modifiers[\"{}\"]", nmd.modifier.name);
  for (const bNodeTreeInterfaceSocket *input : ntree.interface_inputs()) {
    const StringRefNull identifier = input->identifier;
    IDProperty *old_value_prop = IDP_GetPropertyFromGroup(old_props, identifier);
    if (!old_value_prop) {
      continue;
    }

    IDProperty *group = bke::idprop::create_group(identifier).release();
    IDP_AddToGroup(inputs, group);

    if (input->flag & NODE_INTERFACE_SOCKET_LAYER_SELECTION) {
      IDP_AddToGroup(
          group, bke::idprop::create("type", int(nodes::GeometryNodesInputType::Layer)).release());
      const StringRefNull layer_name = [&]() {
        const IDProperty *layer_name = IDP_GetPropertyTypeFromGroup(
            old_props, identifier, IDP_STRING);
        if (layer_name) {
          return StringRefNull(IDP_string_get(layer_name));
        }
        return StringRefNull();
      }();
      IDP_AddToGroup(group, bke::idprop::create("layer_name", layer_name).release());
      continue;
    }

    IDProperty *new_value_prop = IDP_CopyProperty(old_value_prop);
    STRNCPY(new_value_prop->name, "value");
    IDP_AddToGroup(group, new_value_prop);

    const std::string old_value_path = fmt::format("[\"{}\"]", identifier);
    const std::string new_value_path = fmt::format(".properties.inputs.{}.value", identifier);
    BKE_animdata_fix_paths_rename_all_ex(&bmain,
                                         &object.id,
                                         inputs_path_prefix.c_str(),
                                         old_value_path.c_str(),
                                         new_value_path.c_str(),
                                         0,
                                         0,
                                         false,
                                         false);

    if (IDOverrideLibrary *override_library = object.id.override_library) {
      for (IDOverrideLibraryProperty &prop : override_library->properties) {
        const StringRef path = prop.rna_path;
        const int64_t i = path.find(inputs_path_prefix);
        if (i == StringRef::not_found) {
          continue;
        }
        if (path.drop_known_prefix(inputs_path_prefix) != old_value_path) {
          continue;
        }
        MEM_delete(prop.rna_path);
        prop.rna_path = BLI_sprintfN("%s%s", inputs_path_prefix.c_str(), new_value_path.c_str());
      }
    }

    bool use_attribute = false;
    if (const IDProperty *use_attribute_prop = IDP_GetPropertyFromGroup(
            old_props, identifier + "_use_attribute"))
    {
      /* This property changed to an enum property and animation is not versioned. */
      if (use_attribute_prop->type == IDP_INT) {
        use_attribute = bool(IDP_int_get(use_attribute_prop));
      }
      else if (use_attribute_prop->type == IDP_BOOLEAN) {
        use_attribute = bool(IDP_bool_get(use_attribute_prop));
      }
    }

    const auto input_type = use_attribute ? nodes::GeometryNodesInputType::Attribute :
                                            nodes::GeometryNodesInputType::Value;
    IDP_AddToGroup(group, bke::idprop::create("type", int(input_type)).release());
    const StringRefNull attribute_name = [&]() {
      const IDProperty *attribute_name = IDP_GetPropertyTypeFromGroup(
          old_props, identifier + "_attribute_name", IDP_STRING);
      if (attribute_name) {
        return StringRefNull(IDP_string_get(attribute_name));
      }
      return StringRefNull();
    }();
    IDP_AddToGroup(group, bke::idprop::create("attribute_name", attribute_name).release());
  }

  IDProperty *outputs = bke::idprop::create_group("outputs").release();
  IDP_AddToGroup(system_props, outputs);
  for (const bNodeTreeInterfaceSocket *output : ntree.interface_outputs()) {
    const StringRef identifier = output->identifier;
    IDProperty *old_name_prop = IDP_GetPropertyTypeFromGroup(
        old_props, identifier + "_attribute_name", IDP_STRING);
    if (!old_name_prop) {
      continue;
    }
    IDProperty *group = bke::idprop::create_group(identifier).release();
    IDP_AddToGroup(outputs, group);

    IDProperty *new_value_prop = IDP_CopyProperty(old_name_prop);
    STRNCPY(new_value_prop->name, "attribute_name");
    IDP_AddToGroup(group, new_value_prop);
  }

  if (nmd.modifier.system_properties) {
    IDP_FreeProperty(nmd.modifier.system_properties);
  }
  nmd.modifier.system_properties = system_props;
  IDP_FreeProperty(nmd.settings_legacy.properties);
  nmd.settings_legacy.properties = nullptr;
}

static void sanitize_node_tree_interface_socket_identifiers(bNodeTree &node_tree)
{
  node_tree.ensure_interface_cache();
  Set<StringRef> all_identifiers;
  Map<std::string, StringRefNull> identifier_map;
  for (bNodeTreeInterfaceItem *item : node_tree.interface_items()) {
    if (item->item_type == NodeTreeInterfaceItemType::Panel) {
      continue;
    }
    auto &socket = *bke::node_interface::get_item_as<bNodeTreeInterfaceSocket>(item);
    /* Socket identifiers are required to be valid RNA identifiers and unique. */
    if (!RNA_validate_identifier(socket.identifier, true)) {
      std::string prev_identifier(socket.identifier);
      RNA_identifier_sanitize(socket.identifier, true);
      if (all_identifiers.contains(socket.identifier)) {
        std::string new_identifier = BLI_uniquename_cb(
            [&](StringRef name) { return all_identifiers.contains(name); },
            '_',
            socket.identifier);
        MEM_SAFE_DELETE(socket.identifier);
        socket.identifier = BLI_strdup(new_identifier.c_str());
      }
      identifier_map.add(std::move(prev_identifier), socket.identifier);
    }
    all_identifiers.add(socket.identifier);
  }

  /* Rename all the node socket identifiers that got changed in the interface. */
  if (!identifier_map.is_empty()) {
    for (bNode &node : node_tree.nodes) {
      if (!(node.is_group_input() || node.is_group_output())) {
        continue;
      }
      ListBaseT<bNodeSocket> sockets = node.is_group_output() ? node.inputs : node.outputs;
      for (bNodeSocket &socket : sockets) {
        if (identifier_map.contains(socket.identifier)) {
          version_node_socket_identifier_set(socket, identifier_map.lookup(socket.identifier));
        }
      }
    }
  }
}

/* Saving file extension is now a property of the File Output node. So inherit this
 * setting from the active scene to restore the old behavior.
 * Note: One limitation is that node groups containing file outputs that are not part of any
 * scene are not affected by versioning. */
static void do_version_file_output_use_file_extension_recursive(bNodeTree &node_tree,
                                                                const Scene &scene)
{
  for (bNode &node : node_tree.nodes) {
    if (node.type_legacy == CMP_NODE_OUTPUT_FILE) {
      NodeCompositorFileOutput *data = static_cast<NodeCompositorFileOutput *>(node.storage);
      data->use_file_extension = (scene.r.scemode & R_EXTENSION) != 0;
    }
    else if (node.type_legacy == NODE_GROUP) {
      bNodeTree *ngroup = id_cast<bNodeTree *>(node.id);
      if (ngroup) {
        do_version_file_output_use_file_extension_recursive(*ngroup, scene);
      }
    }
  }
}

static void version_clear_strip_linear_modifier_flag(Main &bmain)
{
  for (Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed != nullptr) {
      seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
        constexpr eStripFlag flag_linear_modifiers = eStripFlag(1 << 23);
        strip->flag &= ~flag_linear_modifiers;
        return true;
      });
    }
  }
}

static void version_text_strip_space_line(Main &bmain)
{
  for (Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed == nullptr) {
      continue;
    }

    seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
      if (strip->type == STRIP_TYPE_TEXT && strip->effectdata != nullptr) {
        TextVars *data = static_cast<TextVars *>(strip->effectdata);
        data->space_line = 1.0f;
      }
      return true;
    });
  }
}

static void version_compositor_effect_initialized(Main &bmain)
{
  /* A file with compositor effects that was saved, opened in
   * previous version and saved there, would have lost the
   * compositor effect data since earlier versions would not
   * write it. Ensure the effect data is not null. */
  for (Scene &scene : bmain.scenes) {
    if (scene.ed) {
      seq::foreach_strip(&scene.ed->seqbase, [&](Strip *strip) {
        if (strip->type == STRIP_TYPE_COMPOSITOR) {
          seq::effect_ensure_initialized(strip);
        }
        return true;
      });
    }
  }
}

static void version_text_strip_abs_space_line(Main &bmain)
{
  for (Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed == nullptr) {
      continue;
    }

    seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
      if (strip->type == STRIP_TYPE_TEXT && strip->effectdata != nullptr) {
        TextVars *data = static_cast<TextVars *>(strip->effectdata);
        data->abs_space_line = 60.0f;
        data->flag &= ~SEQ_TEXT_USE_ABSOLUTE_LINE_SPACING;
      }
      return true;
    });
  }
}

static void fix_single_point_curves_custom_knots(Main *bmain)
{
  /* Fix corrupted flagu/flagv values created by older versions of the Curve Pen tool.
   * The tool could create loose vertices with invalid flag values (e.g. -2), where
   * CU_NURB_CUSTOM was set alongside other flags and knotsu/knotsv was left null,
   * causing a crash when opening these files in newer versions. */
  for (Curve &cu : bmain->curves) {
    for (Nurb *nu = static_cast<Nurb *>(cu.nurb.first); nu != nullptr; nu = nu->next) {
      if (nu->knotsu == nullptr && (nu->flagu & CU_NURB_CUSTOM)) {
        nu->flagu &= (CU_NURB_CYCLIC | CU_NURB_BEZIER | CU_NURB_ENDPOINT);
      }
      if (nu->knotsv == nullptr && (nu->flagv & CU_NURB_CUSTOM)) {
        nu->flagv &= (CU_NURB_CYCLIC | CU_NURB_BEZIER | CU_NURB_ENDPOINT);
      }
    }
  }
}

static void version_strip_modifier_show_preview_flag(Main &bmain)
{
  for (Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed == nullptr) {
      continue;
    }
    seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
      for (StripModifierData &smd : strip->modifiers) {
        if ((smd.flag & STRIP_MODIFIER_FLAG_MUTE) == 0) {
          smd.flag |= STRIP_MODIFIER_FLAG_SHOW_PREVIEW;
        }
      }
      return true;
    });
  }
}

static void version_scene_strip_view_layer_name(Main &bmain)
{
  for (const Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed == nullptr) {
      continue;
    }

    seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
      if (strip->type != STRIP_TYPE_SCENE || strip->scene == nullptr) {
        return true;
      }
      strip->scene_view_layer_name = BLI_strdup(BKE_view_layer_default_render(strip->scene)->name);
      return true;
    });
  }
}

/* Compositor node trees with an image input and an image output can likely be used as strip
 * modifiers. */
static void enable_compositor_nodes_is_strip_modifier(Main &bmain)
{
  for (bNodeTree &group : bmain.nodetrees) {
    if (group.type != NTREE_COMPOSIT) {
      continue;
    }
    bool has_image_input = false;
    bool has_image_output = false;
    group.tree_interface.foreach_item([&](const bNodeTreeInterfaceItem &item) {
      if (item.item_type != NodeTreeInterfaceItemType::Socket) {
        /* Continue. */
        return true;
      }
      const auto &socket = reinterpret_cast<const bNodeTreeInterfaceSocket &>(item);
      if (socket.flag & NODE_INTERFACE_SOCKET_INPUT) {
        has_image_input = has_image_input || STREQ(socket.socket_type, "NodeSocketColor");
        /* Continue. */
        return true;
      }
      if (socket.flag & NODE_INTERFACE_SOCKET_OUTPUT) {
        has_image_output = has_image_output || STREQ(socket.socket_type, "NodeSocketColor");
        /* Continue. */
        return true;
      }
      /* Break. */
      return false;
    });

    if (has_image_input && has_image_output) {
      if (!group.compositor_node_asset_traits) {
        group.compositor_node_asset_traits = MEM_new<CompositorNodeAssetTraits>(__func__);
      }
      group.compositor_node_asset_traits->flag |= COMPOSIT_NODE_ASSET_STRIP_MODIFIER;
      bke::node_update_asset_metadata(group);
    }
  }
}

static void versioning_replace_legacy_compositor_switch_node(bNodeTree *node_tree)
{
  version_node_input_socket_name(node_tree, CMP_NODE_SWITCH, "On", "True");
  version_node_input_socket_name(node_tree, CMP_NODE_SWITCH, "Off", "False");
  version_node_output_socket_name(node_tree, CMP_NODE_SWITCH, "Image", "Output");

  for (bNode &node : node_tree->nodes) {
    if (node.type_legacy == CMP_NODE_SWITCH) {
      node.type_legacy = GEO_NODE_SWITCH;
      NodeSwitch *storage = MEM_new<NodeSwitch>(__func__);
      storage->input_type = SOCK_RGBA;
      STRNCPY_UTF8(node.idname, "GeometryNodeSwitch");
      node.storage = storage;
    }
  }
}

/* Goo/legacy EEVEE built-in Bloom was removed in EEVEE-Next; official Blender never migrated it
 * (the legacy renderer died with 4.1 files, whose users re-authored their compositing). Goo 4.4
 * kept legacy EEVEE alive, so its files reach 5.2 with SCE_EEVEE_BLOOM_ENABLED still set and the
 * bloom silently disappears. Recreate the effect with a Bloom Glare node inserted right after
 * every Render Layers node of the scene compositor (creating a minimal compositor when the scene
 * has none). Parameter mapping is approximate: legacy bloom and the glare node share threshold /
 * knee / tint semantics, radius maps onto the normalized glare size, intensity onto strength. */
static void do_version_goo_bloom_to_glare(Main *bmain, Scene *scene)
{
  if (!(scene->eevee.flag & SCE_EEVEE_BLOOM_ENABLED)) {
    return;
  }
  scene->eevee.flag &= ~SCE_EEVEE_BLOOM_ENABLED;

  bNodeTree *node_tree = version_get_scene_compositor_node_tree(bmain, scene);
  const bool tree_is_new = (node_tree == nullptr);
  if (tree_is_new) {
    /* Bloom-only files often have no compositor at all. Build a minimal Render Layers -> Group
     * Output chain; pre-5.0 files store it as an embedded scene tree which the after-setup
     * versioning converts into a reusable node group like any other legacy file. */
    node_tree = bke::node_tree_add_tree_embedded(
        nullptr, &scene->id, "Compositing Nodetree", "CompositorNodeTree");
    scene->r.scemode |= R_DOCOMP;
  }
  bke::node_tree_set_type(*node_tree);

  bNode *new_output = nullptr;
  if (tree_is_new) {
    node_tree->tree_interface.add_socket(
        "Image", "", "NodeSocketColor", NODE_INTERFACE_SOCKET_INPUT, nullptr);
    node_tree->tree_interface.add_socket(
        "Image", "", "NodeSocketColor", NODE_INTERFACE_SOCKET_OUTPUT, nullptr);

    /* The Render Layers node initializer resolves the scene through the context (versioning has
     * none), so build a minimal one; a null context crashes in `CTX_data_scene`. */
    bContext *C = CTX_create();
    CTX_data_main_set(C, bmain);
    CTX_data_scene_set(C, scene);
    bNode *render_layers = bke::node_add_node(C, *node_tree, "CompositorNodeRLayers"_ustr);
    CTX_free(C);
    render_layers->location[0] = -300.0f;
    render_layers->location[1] = 0.0f;

    new_output = bke::node_add_node(nullptr, *node_tree, "NodeGroupOutput"_ustr);
    new_output->flag |= NODE_DO_OUTPUT;
    new_output->location[0] = 300.0f;
    new_output->location[1] = 0.0f;

    bNodeSocket *rl_image = bke::node_find_socket(*render_layers, SOCK_OUT, "Image"_ustr);
    bNodeSocket *out_image = static_cast<bNodeSocket *>(new_output->inputs.first);
    if (rl_image && out_image) {
      version_node_add_link(*node_tree, *render_layers, *rl_image, *new_output, *out_image);
    }
  }

  /* Snapshot the Render Layers nodes before adding new nodes to the tree. */
  Vector<bNode *> render_layer_nodes;
  for (bNode &node : node_tree->nodes) {
    if (node.type_legacy == CMP_NODE_R_LAYERS) {
      render_layer_nodes.append(&node);
    }
  }

  for (bNode *render_layers : render_layer_nodes) {
    bNodeSocket *rl_image = bke::node_find_socket(*render_layers, SOCK_OUT, "Image"_ustr);
    if (rl_image == nullptr) {
      continue;
    }
    Vector<bNodeLink *> out_links;
    for (bNodeLink &link : node_tree->links) {
      if (link.fromsock == rl_image) {
        out_links.append(&link);
      }
    }
    if (out_links.is_empty()) {
      continue;
    }

    bNode *glare = bke::node_add_node(nullptr, *node_tree, "CompositorNodeGlare"_ustr);
    glare->parent = render_layers->parent;
    glare->location[0] = render_layers->location[0] + render_layers->width + 60.0f;
    glare->location[1] = render_layers->location[1];
    STRNCPY_UTF8(glare->label, "Goo Bloom");

    bNodeSocket *g_image_in = bke::node_find_socket(*glare, SOCK_IN, "Image"_ustr);
    bNodeSocket *g_image_out = bke::node_find_socket(*glare, SOCK_OUT, "Image"_ustr);
    bNodeSocket *g_type = bke::node_find_socket(*glare, SOCK_IN, "Type"_ustr);
    bNodeSocket *g_quality = bke::node_find_socket(*glare, SOCK_IN, "Quality"_ustr);
    bNodeSocket *g_threshold = bke::node_find_socket(*glare, SOCK_IN, "Highlights Threshold"_ustr);
    bNodeSocket *g_smooth = bke::node_find_socket(*glare, SOCK_IN, "Highlights Smoothness"_ustr);
    bNodeSocket *g_clamp = bke::node_find_socket(*glare, SOCK_IN, "Clamp Highlights"_ustr);
    bNodeSocket *g_maximum = bke::node_find_socket(*glare, SOCK_IN, "Maximum Highlights"_ustr);
    bNodeSocket *g_strength = bke::node_find_socket(*glare, SOCK_IN, "Strength"_ustr);
    bNodeSocket *g_saturation = bke::node_find_socket(*glare, SOCK_IN, "Saturation"_ustr);
    bNodeSocket *g_tint = bke::node_find_socket(*glare, SOCK_IN, "Tint"_ustr);
    bNodeSocket *g_size = bke::node_find_socket(*glare, SOCK_IN, "Size"_ustr);
    if (!g_image_in || !g_image_out) {
      continue;
    }

    const SceneEEVEE &eevee = scene->eevee;
    const float glare_size = std::clamp(eevee.bloom_radius / 8.5f, 0.0f, 1.0f);

    /* Legacy bloom added `intensity * bloom` unnormalized; the glare node divides the bloom by
     * its up/down-sample chain length (see `compute_bloom_chain_length`), so multiply it back.
     * The chain length depends on the runtime image size; assume the render resolution. */
    int2 render_size;
    BKE_render_resolution(&scene->r, true, &render_size.x, &render_size.y);
    const int smaller_dimension = math::reduce_min(render_size); /* Quality HIGH: no division. */
    const int chain_length = int(std::log2(std::max(1.0f, smaller_dimension * glare_size)));
    const float glare_strength = std::clamp(
        eevee.bloom_intensity * std::max(1, chain_length), 0.0f, 1.0f);

    g_type->default_value_typed<bNodeSocketValueMenu>()->value = CMP_NODE_GLARE_BLOOM;
    g_quality->default_value_typed<bNodeSocketValueMenu>()->value = CMP_NODE_GLARE_QUALITY_HIGH;
    g_threshold->default_value_typed<bNodeSocketValueFloat>()->value = eevee.bloom_threshold;
    g_smooth->default_value_typed<bNodeSocketValueFloat>()->value = std::clamp(
        eevee.bloom_knee, 0.0f, 1.0f);
    g_clamp->default_value_typed<bNodeSocketValueBoolean>()->value = (eevee.bloom_clamp > 0.0f);
    g_maximum->default_value_typed<bNodeSocketValueFloat>()->value = eevee.bloom_clamp;
    g_strength->default_value_typed<bNodeSocketValueFloat>()->value = glare_strength;
    g_saturation->default_value_typed<bNodeSocketValueFloat>()->value = 1.0f;
    float *tint = g_tint->default_value_typed<bNodeSocketValueRGBA>()->value;
    tint[0] = eevee.bloom_color[0];
    tint[1] = eevee.bloom_color[1];
    tint[2] = eevee.bloom_color[2];
    tint[3] = 1.0f;
    g_size->default_value_typed<bNodeSocketValueFloat>()->value = glare_size;

    version_node_add_link(*node_tree, *render_layers, *rl_image, *glare, *g_image_in);
    for (bNodeLink *link : out_links) {
      version_node_add_link(*node_tree, *glare, *g_image_out, *link->tonode, *link->tosock);
      bke::node_remove_link(node_tree, *link);
    }
  }
}

/* Recursively check whether a material's node tree (including nested node groups) uses any
 * Goo-Engine-ported shader node. Used to gate legacy-OPAQUE versioning so that only materials
 * that actually come from the Goo world get the legacy behavior, while native materials of
 * official files are left untouched. Goo node type IDs are defined in BKE_node_legacy_types.hh. */
static bool material_node_tree_uses_goo_node(bNodeTree &node_tree)
{
  for (bNode &node : node_tree.nodes) {
    switch (node.type_legacy) {
      case SH_NODE_SHADER_INFO:
      case SH_NODE_SCREENSPACE_INFO:
      case SH_NODE_SDF_PRIMITIVE:
      case SH_NODE_SDF_OP:
      case SH_NODE_SDF_VECTOR_OP:
      case SH_NODE_SDF_NOISE:
      case SH_NODE_SET_DEPTH:
      case SH_NODE_CURVATURE:
      case SH_NODE_LIGHT_INFO:
      case SH_NODE_TEX_HEXAGON:
      case SH_NODE_TWIRL:
      case SH_NODE_WATER_RIPPLES:
      case SH_NODE_OKLAB_COLOR_RAMP:
        return true;
      default:
        break;
    }
    if (node.type_legacy == NODE_GROUP) {
      bNodeTree *ngroup = id_cast<bNodeTree *>(node.id);
      if (ngroup && material_node_tree_uses_goo_node(*ngroup)) {
        return true;
      }
    }
  }
  return false;
}

void do_versions_after_linking_520(FileData *fd, Main *bmain)
{
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 2)) {
    for (Scene &scene : bmain->scenes) {
      bNodeTree *node_tree = version_get_scene_compositor_node_tree(bmain, &scene);
      if (node_tree == nullptr) {
        continue;
      }
      do_version_file_output_use_file_extension_recursive(*node_tree, scene);
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 16)) {
    for (Object &object : bmain->objects) {
      for (ModifierData &md : object.modifiers) {
        if (md.type == eModifierType_Nodes) {
          version_geometry_nodes_properties(
              *fd, *bmain, object, reinterpret_cast<NodesModifierData &>(md));
        }
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 27)) {
    version_scene_strip_view_layer_name(*bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 36)) {
    /* Shift animation data to accommodate the new thin wall input. */
    version_node_socket_index_animdata(bmain, NTREE_SHADER, SH_NODE_BSDF_PRINCIPLED, 5, 1, 31);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 44)) {
    /* We have to remove the invalid motion paths. Re-baking into clip space on file load would be
     * very expensive. */
    for (Object &object : bmain->objects) {
      if (object.mpath && (object.avs.path_bakeflag & MOTIONPATH_BAKE_CAMERA_SPACE)) {
        animviz_free_motionpath(object.mpath);
        object.mpath = nullptr;
        object.avs.path_bakeflag &= ~MOTIONPATH_BAKE_HAS_PATHS;
      }
      if (object.pose && (object.pose->avs.path_bakeflag & MOTIONPATH_BAKE_CAMERA_SPACE)) {
        for (bPoseChannel &pose_bone : object.pose->chanbase) {
          if (pose_bone.mpath) {
            animviz_free_motionpath(pose_bone.mpath);
            pose_bone.mpath = nullptr;
          }
        }
        object.pose->avs.path_bakeflag &= ~MOTIONPATH_BAKE_HAS_PATHS;
      }
    }
  }

  /* Goo-only fields are absent from native files irrespective of Blender's subversion.
   * Test source SDNA, not a fork subversion which upstream can also reach. Preserve authored
   * Goo masks (including intentional empty masks), and support linked/headless data without
   * relying on the Python UI load handler to initialize missing fields. */
  if (!DNA_struct_member_exists(fd->filesdna, "Material", "int", "light_group_bits[4]")) {
    for (Material &mat : bmain->materials) {
      mat.light_group_bits[0] = mat.light_group_bits[1] = mat.light_group_bits[2] = 0;
      mat.light_group_bits[3] = 1;
    }
  }
  if (!DNA_struct_member_exists(fd->filesdna, "Material", "int", "light_group_shadow_bits[4]")) {
    for (Material &mat : bmain->materials) {
      mat.light_group_shadow_bits[0] = mat.light_group_shadow_bits[1] =
          mat.light_group_shadow_bits[2] = 0;
      mat.light_group_shadow_bits[3] = 1;
    }
  }
  if (!DNA_struct_member_exists(fd->filesdna, "Light", "int", "light_group_bits[4]")) {
    for (Light &light : bmain->lights) {
      light.light_group_bits[0] = light.light_group_bits[1] = light.light_group_bits[2] = 0;
      light.light_group_bits[3] = 1;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 48)) {
    /* Goo/legacy EEVEE "OPAQUE" materials had a real opaque surface mode even when their node
     * tree contained a Transparent BSDF. EEVEE-Next has no equivalent public mode, so retain an
     * internal provenance flag and let the material shader provide the compatibility behavior.
     *
     * The deprecated DNA `blend_method` defaults to MA_BM_SOLID for modern files too. Therefore
     * the flag must be recomputed from provenance rather than from blend_method alone, otherwise
     * native EEVEE-Next alpha cards would be incorrectly made opaque. Clearing first also repairs
     * files that were loaded by an earlier port build which set the flag too broadly. */
    const bool legacy_eevee_era = bmain->versionfile < 402;
    /* Goo 4.4 kept the legacy EEVEE data model alive after official Blender had moved on.
     * Its files are version 4.4 (not < 4.2), and an OPAQUE material need not contain a Goo node,
     * so node-tree inspection alone cannot identify the provenance. The combination of Goo-only
     * DNA members is a stable marker for those files. Keep the version guard so a material saved
     * by this port is never reclassified merely because the current DNA also contains these
     * members. */
    const bool goo_file_dna =
        bmain->versionfile < 500 &&
        DNA_struct_member_exists(fd->filesdna, "Material", "char", "check_shadow_id") &&
        DNA_struct_member_exists(fd->filesdna, "Material", "int", "light_group_shadow_bits[4]") &&
        DNA_struct_member_exists(
            fd->filesdna, "NodeShaderInfo", "int", "light_group_shadow_bits[4]");
    for (Material &mat : bmain->materials) {
      /* `blend_method` is still the legacy DNA value at this point in the load sequence. The
       * 4.2 conversion that maps legacy modes to HASHED/BLEND runs in the earlier generation
       * versioning pass, while this after-linking pass is where Goo node groups are available. */
      const bool from_goo = mat.nodetree != nullptr &&
                            material_node_tree_uses_goo_node(*mat.nodetree);
      mat.flag &= ~MA_LEGACY_OPAQUE;
      if (mat.blend_method == MA_BM_SOLID && (legacy_eevee_era || goo_file_dna || from_goo)) {
        mat.flag |= MA_LEGACY_OPAQUE;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 46)) {
    /* Goo/legacy EEVEE built-in Bloom -> compositor Glare (Bloom) node. */
    for (Scene &scene : bmain->scenes) {
      do_version_goo_bloom_to_glare(bmain, &scene);
    }
  }

  /**
   * Always bump subversion in BKE_blender_version.h when adding versioning
   * code here, and wrap it inside a MAIN_VERSION_FILE_ATLEAST check.
   *
   * \note Keep this message at the bottom of the function.
   */
}

static void version_solid_color_width_height_defaults(Main &bmain)
{
  for (Scene &scene : bmain.scenes) {
    Editing *ed = seq::editing_get(&scene);
    if (ed == nullptr) {
      continue;
    }
    seq::foreach_strip(&ed->seqbase, [&](Strip *strip) {
      if (strip->type == STRIP_TYPE_COLOR && strip->effectdata != nullptr) {
        SolidColorVars *data = static_cast<SolidColorVars *>(strip->effectdata);
        data->width = scene.r.xsch;
        data->height = scene.r.ysch;
      }
      return true;
    });
  }
}

void blo_do_versions_520(FileData *fd, Library * /*lib*/, Main *bmain)
{
  /* Files older than 2.80 do not contain Material::blend_shadow in their SDNA; on load the member
   * is zero-filled (see RECONSTRUCT_STEP_INIT_ZERO), and 0 happens to be MA_BS_NONE ("cast no
   * shadow"). The Goo/legacy shadow-mode branch (see `legacy_no_shadow` in eevee_material.cc)
   * honors that value, which would silently disable shadow casting for every material of such
   * ancient files. Backfill the member to its real default. Only still-zero values are touched:
   * if earlier versioning (e.g. 4.2 EEVEE conversion) already rewrote the field, keep its result
   * to stay behavior-identical with official Blender. Files from 2.80+ (official and Goo) store
   * the member and keep their authored values untouched. */
  if (!DNA_struct_member_exists_with_alias(fd->filesdna, "Material", "char", "blend_shadow")) {
    for (Material &mat : bmain->materials) {
      if (mat.blend_shadow == MA_BS_NONE) {
        mat.blend_shadow = MA_BS_SOLID;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 1)) {
    for (Scene &scene : bmain->scenes) {
      scene.r.mode |= R_SAVE_OUTPUT;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 4)) {
    for (Brush &brush : bmain->brushes) {
      if (brush.gpencil_settings != nullptr) {
        brush.blend = 0;
      }
    }
  }
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 5)) {
    FOREACH_NODETREE_BEGIN (bmain, node_tree, id_owner) {
      for (bNode &node : node_tree->nodes) {
        if (node.type_legacy == FN_NODE_INPUT_VECTOR) {
          auto &data = *static_cast<NodeInputVector *>(node.storage);
          data.vector[3] = 0.0f;
          data.dimensions = 3;
        }
      }
    }
    FOREACH_NODETREE_END;
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 6)) {
    for (Scene &scene : bmain->scenes) {
      SequencerToolSettings *sequencer_tool_settings = seq::tool_settings_ensure(&scene);
      sequencer_tool_settings->snap_flag |= SEQ_SNAP_TO_ALL_CHANNEL_STRIPS;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 7)) {
    for (Scene &scene : bmain->scenes) {
      scene.r.anisotropic_filter = 2;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 9)) {
    for (Mesh &mesh : bmain->meshes) {
      bke::mesh_freestyle_marks_to_generic(mesh);
    }
  }

  /* Convert H.264 codec value for older files (2.79), see #155775. */
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 10)) {
    for (Scene &scene : bmain->scenes) {
      if (scene.r.ffcodecdata.codec == 28) {
        scene.r.ffcodecdata.codec = 27;
      }
    }
  }

  /* Disable "unified" flags for Grease Pencil Draw mode. */
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 11)) {
    for (Scene &scene : bmain->scenes) {
      if (scene.toolsettings->gp_paint) {
        UnifiedPaintSettings &settings =
            scene.toolsettings->gp_paint->paint.unified_paint_settings;
        settings.flag &= ~(UNIFIED_PAINT_SIZE | UNIFIED_PAINT_ALPHA | UNIFIED_PAINT_COLOR);
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 12)) {
    for (bScreen &screen : bmain->screens) {
      for (ScrArea &area : screen.areabase) {
        for (SpaceLink &space : area.spacedata) {
          if (space.spacetype == SPACE_NODE) {
            SpaceNode *space_node = reinterpret_cast<SpaceNode *>(&space);
            space_node->overlay.flag |= SN_OVERLAY_SHOW_RENDER_REGION;
            space_node->overlay.passepartout_alpha = 0.5f;
          }
        }
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 13)) {
    version_clear_strip_linear_modifier_flag(*bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 14)) {
    fix_single_point_curves_custom_knots(bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 15)) {
    for (Scene &scene : bmain->scenes) {
      scene.r.scemode |= R_USE_TEXTURE_CACHE;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 16)) {
    for (Brush &brush : bmain->brushes) {
      if (brush.gpencil_settings != nullptr) {
        brush.gpencil_settings->curve_type = CURVE_TYPE_POLY;
        brush.gpencil_settings->conversion_threshold = 0.001f;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 17)) {
    for (Material &materials : bmain->materials) {
      if (materials.gp_style != nullptr) {
        materials.gp_style->placement_mode = GP_MATERIAL_PLACEMENT_COUNT;
        materials.gp_style->placement_count = 1;
        materials.gp_style->placement_density = 10.0f;
        materials.gp_style->placement_radius_spacing = 100.0f;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 18)) {
    for (Scene &scene : bmain->scenes) {
      if (scene.toolsettings->sculpt) {
        Sculpt &sculpt = *scene.toolsettings->sculpt;
        MeshAutomaskingSettings *settings = MEM_new<MeshAutomaskingSettings>(__func__);
        settings->flags = sculpt.automasking_flags;
        settings->boundary_edges_propagation_steps =
            sculpt.automasking_boundary_edges_propagation_steps;
        settings->cavity_blur_steps = sculpt.automasking_cavity_blur_steps;
        settings->cavity_factor = sculpt.automasking_cavity_factor;
        settings->start_normal_limit = sculpt.automasking_start_normal_limit;
        settings->start_normal_falloff = sculpt.automasking_start_normal_falloff;
        settings->view_normal_limit = sculpt.automasking_view_normal_limit;
        settings->view_normal_falloff = sculpt.automasking_view_normal_falloff;
        settings->cavity_curve = BKE_curvemapping_copy(sculpt.automasking_cavity_curve);
        settings->cavity_curve_op = BKE_curvemapping_copy(sculpt.automasking_cavity_curve_op);

        scene.toolsettings->sculpt->paint.mesh_automasking_settings = settings;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 19)) {
    for (bNodeTree &tree : bmain->nodetrees) {
      sanitize_node_tree_interface_socket_identifiers(tree);
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 20)) {
    for (Brush &brush : bmain->brushes) {
      if (brush.ob_mode != OB_MODE_SCULPT) {
        continue;
      }

      brush.mesh_automasking_settings = MEM_new<MeshAutomaskingSettings>(__func__);
      brush.mesh_automasking_settings->flags = brush.automasking_flags;
      brush.mesh_automasking_settings->boundary_edges_propagation_steps =
          brush.automasking_boundary_edges_propagation_steps;
      brush.mesh_automasking_settings->cavity_blur_steps = brush.automasking_cavity_blur_steps;
      brush.mesh_automasking_settings->cavity_factor = brush.automasking_cavity_factor;
      brush.mesh_automasking_settings->start_normal_falloff =
          brush.automasking_start_normal_falloff;
      brush.mesh_automasking_settings->start_normal_limit = brush.automasking_start_normal_limit;
      brush.mesh_automasking_settings->view_normal_falloff = brush.automasking_view_normal_falloff;
      brush.mesh_automasking_settings->view_normal_limit = brush.automasking_view_normal_limit;
      brush.mesh_automasking_settings->cavity_curve = BKE_curvemapping_copy(
          brush.automasking_cavity_curve);
      brush.mesh_automasking_settings->cavity_curve_op = nullptr;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 21)) {
    for (Material &materials : bmain->materials) {
      if (materials.gp_style != nullptr) {
        materials.gp_style->random_size_factor = 0.0f;
        materials.gp_style->random_strength_factor = 0.0f;
        materials.gp_style->random_rotation_factor = 0.0f;
        materials.gp_style->random_hue_factor = 0.0f;
        materials.gp_style->random_saturation_factor = 0.0f;
        materials.gp_style->random_value_factor = 0.0f;
        materials.gp_style->random_noise_scale = 1.0f;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 22)) {
    version_strip_modifier_show_preview_flag(*bmain);
  }

  /* The ID member of the Viewer node is no longer initialized to the Viewer Image, so clear that
   * member. */
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 23)) {
    FOREACH_NODETREE_BEGIN (bmain, node_tree, id) {
      if (node_tree->type == NTREE_COMPOSIT) {
        for (bNode &node : node_tree->nodes) {
          if (node.type_legacy == CMP_NODE_VIEWER) {
            node.id = nullptr;
          }
        }
      }
    }
    FOREACH_NODETREE_END;
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 24)) {
    FOREACH_NODETREE_BEGIN (bmain, node_tree, id) {
      if (node_tree->type == NTREE_SHADER) {
        for (bNode &node : node_tree->nodes) {
          if (node.type_legacy == SH_NODE_RAYCAST && node.storage == nullptr) {
            node.storage = MEM_new<NodeShaderRaycast>(__func__);
          }
        }
      }
    }
    FOREACH_NODETREE_END;
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 25)) {
    for (bScreen &screen : bmain->screens) {
      for (ScrArea &area : screen.areabase) {
        for (SpaceLink &space : area.spacedata) {
          if (space.spacetype == SPACE_OUTLINER) {
            SpaceOutliner *space_outliner = reinterpret_cast<SpaceOutliner *>(&space);
            space_outliner->flag |= SO_SCROLL_TO_ACTIVE;
          }
        }
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 26)) {
    FOREACH_NODETREE_BEGIN (bmain, tree, id) {
      if (tree->type != NTREE_GEOMETRY) {
        continue;
      }
      for (bNode &node : tree->nodes) {
        switch (node.type_legacy) {
          case FN_NODE_COMPARE:
          case FN_NODE_RANDOM_VALUE: {
            version_socket_identifier_suffixes_for_dynamic_types(node.inputs, "_");
            version_socket_identifier_suffixes_for_dynamic_types(node.outputs, "_");
            break;
          }
        }
      }
    }
    FOREACH_NODETREE_END;
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 28)) {
    version_text_strip_space_line(*bmain);
    version_compositor_effect_initialized(*bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 29)) {
    for (bScreen &screen : bmain->screens) {
      for (ScrArea &area : screen.areabase) {
        for (SpaceLink &sl : area.spacedata) {
          if (sl.spacetype != SPACE_SEQ) {
            continue;
          }
          ListBaseT<ARegion> *regionbase = (&sl == area.spacedata.first) ? &area.regionbase :
                                                                           &sl.regionbase;
          ARegion *scrubbing_region = do_versions_add_region_if_not_found(
              regionbase, RGN_TYPE_SCRUBBING, "Scrubbing Region", RGN_TYPE_FOOTER);
          if (scrubbing_region) {
            scrubbing_region->alignment = RGN_ALIGN_BOTTOM | RGN_STACK_ON_PREV |
                                          RGN_ALIGN_HIDE_WITH_PREV;
          }
        }
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 30)) {
    enable_compositor_nodes_is_strip_modifier(*bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 31)) {
    for (Mesh &mesh : bmain->meshes) {
      if (mesh.attributes().contains(".uv_seam")) {
        mesh.attributes_for_write().rename(".uv_seam", "uv_seam");
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 34)) {
    FOREACH_NODETREE_BEGIN (bmain, ntree, id) {
      if (ntree->type == NTREE_COMPOSIT) {
        versioning_replace_legacy_compositor_switch_node(ntree);
      }
    }
    FOREACH_NODETREE_END;
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 35)) {
    for (Object &object : bmain->objects) {
      object.parent_bone_head_tail_factor = 1.0;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 37)) {
    version_text_strip_abs_space_line(*bmain);
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 38)) {
    for (Brush &brush : bmain->brushes) {
      if (brush.gpencil_settings != nullptr) {
        brush.gpencil_settings->fill_gap_factor = 0.4f;
        brush.gpencil_settings->flag |= GP_BRUSH_FILL_INTERNAL_GAPS;
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 39)) {
    for (bScreen &screen : bmain->screens) {
      for (ScrArea &area : screen.areabase) {
        for (SpaceLink &sl : area.spacedata) {
          if (sl.spacetype == SPACE_SEQ) {
            SpaceSeq *sseq = reinterpret_cast<SpaceSeq *>(&sl);
            sseq->preview_overlay.flag |= SEQ_PREVIEW_SHOW_COMPOSITION_GUIDES;
            float default_col[4] = {0.5f, 0.5f, 0.5f, 1.0f};
            copy_v4_v4(sseq->preview_overlay.composition_guide_color, default_col);
          }
        }
      }
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 40)) {
    for (wmWindowManager &wm : bmain->wm) {
      wm.xr.session_settings.viewfinder_enabled = false;
      wm.xr.session_settings.viewfinder_crosshair_enabled = true;

      wm.xr.session_settings.viewfinder_hand = XR_VIEWFINDER_HAND_RIGHT;
      wm.xr.session_settings.viewfinder_scale = 1.0f;

      wm.xr.session_settings.viewfinder_passepartout_overscan = 0.5f;
      wm.xr.session_settings.viewfinder_passepartout_opacity = 0.5f;
    }
  }

  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 41)) {
    version_solid_color_width_height_defaults(*bmain);
  }

  /* Fix the fact that previously, making a linked data local and/or clearing a liboverride would
   * not properly flag some sub-data like modifiers or constraints as local. */
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 43)) {
    for (ID &id : MainAllIDsIterator{*bmain}) {
      if (!ID_IS_LINKED(&id) && !ID_IS_OVERRIDE_LIBRARY(&id)) {
        BKE_lib_override_flag_subdata_local(id);
      }
    }
  }

  /* The compositor previously did not support default inputs for group nodes, but some built-in
   * nodes had the position field default type for some inputs, so node groups would gain it as a
   * default type through some operators. Later, the default inputs were supported for group nodes,
   * though position field were not supported in the compositor, so it would assert. To fix this,
   * we reset any position field default input to the default value. */
  if (!MAIN_VERSION_FILE_ATLEAST(bmain, 502, 44)) {
    FOREACH_NODETREE_BEGIN (bmain, node_tree, id) {
      if (node_tree->type == NTREE_COMPOSIT) {
        node_tree->ensure_interface_cache();
        for (bNodeTreeInterfaceSocket *input : node_tree->interface_inputs()) {
          if (input->default_input == NODE_DEFAULT_INPUT_POSITION_FIELD) {
            input->default_input = NODE_DEFAULT_INPUT_VALUE;
          }
        }
      }
    }
    FOREACH_NODETREE_END;
  }

  /**
   * Always bump subversion in BKE_blender_version.h when adding versioning
   * code here, and wrap it inside a MAIN_VERSION_FILE_ATLEAST check.
   *
   * \note Keep this message at the bottom of the function.
   */
}

}  // namespace blender
