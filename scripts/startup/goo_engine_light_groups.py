# SPDX-License-Identifier: GPL-2.0-or-later

"""
Light group key management for Goo Engine NPR nodes, ported to Blender 5.2 (EEVEE-Next).

Provides the user-facing UI (Material/Light properties + Shader Info node sidebar) to create and
assign named light groups. Named groups are synced into the per-data-block ``light_group_bits``
bitfields (on Materials, Lights and Shader Info nodes) that the shader actually reads.

Materials, lights, and Shader Info nodes participate in the same named light-group namespace.
Material groups filter ordinary direct surface BSDF lighting and received shadows. They also
provide the default Shader Info mask; a Shader Info node with
``use_own_light_groups`` enabled overrides that default with its own mask.
"""

import bpy
from bpy.types import (
    Panel,
    Material,
    Light,
    PropertyGroup,
    UIList,
    Operator,
    ShaderNodeShaderInfo,
    ShaderNodeTree,
)
from bpy.props import (
    StringProperty,
    CollectionProperty,
    IntProperty,
    PointerProperty,
    EnumProperty,
    BoolProperty,
)
from bpy.utils import register_classes_factory
from bpy.app.handlers import persistent
from itertools import chain
from ctypes import c_int32

SIZEOF_INT = 32
MAX_LIGHT_GROUP_BIT = 127
MAX_NAMED_LIGHT_GROUPS = MAX_LIGHT_GROUP_BIT

_sync_in_progress = False


def set_bit(vec, bit):
    if bit < 0 or bit > MAX_LIGHT_GROUP_BIT:
        return
    index = bit // SIZEOF_INT
    mask = 1 << ((bit + 1) % SIZEOF_INT)
    vec[index] = int(c_int32(vec[index] | mask).value)


def _set_array_if_changed(data, property_name, values):
    values = tuple(values)
    if tuple(getattr(data, property_name)) != values:
        setattr(data, property_name, values)


def map_bits(data, mapping):
    bits = [0, 0, 0, 0]
    # Materials and Shader Info nodes carry separate diffuse/shadow masks. Lights only carry
    # their membership mask; material/node shadow masks control received shadows, not casters.
    has_shadow = isinstance(data, (Material, ShaderNodeShaderInfo))
    shadow_bits = [0, 0, 0, 0]

    for grp in data.light_groups.groups:
        index = mapping.get(grp.name)
        if index is not None:
            set_bit(bits, index)
            if has_shadow and not grp.ignore_shadow:
                set_bit(shadow_bits, index)

    if data.light_groups.use_default:
        set_bit(bits, MAX_LIGHT_GROUP_BIT)
        if has_shadow and not data.light_groups.ignore_default_shadow:
            set_bit(shadow_bits, MAX_LIGHT_GROUP_BIT)

    _set_array_if_changed(data, "light_group_bits", bits)
    if has_shadow:
        _set_array_if_changed(data, "light_group_shadow_bits", shadow_bits)


def iter_shader_info_nodes():
    """Yield every Shader Info node, including nodes which currently use the material mask."""
    seen = set()
    node_trees = chain(bpy.data.node_groups, (mat.node_tree for mat in bpy.data.materials))
    for node_tree in node_trees:
        if not node_tree:
            continue
        pointer = node_tree.as_pointer()
        if pointer in seen:
            continue
        seen.add(pointer)
        for node in node_tree.nodes:
            if isinstance(node, ShaderNodeShaderInfo):
                yield node


def iter_light_group_owners():
    # Materials provide the default light-group mask for Shader Info nodes.
    yield from bpy.data.materials
    # Keep all Shader Info node masks synchronized. Nodes that do not use their own mask simply
    # ignore these values until use_own_light_groups is enabled.
    yield from iter_shader_info_nodes()
    yield from bpy.data.lights


def sync_light_groups():
    global _sync_in_progress
    if _sync_in_progress:
        return

    _sync_in_progress = True
    try:
        # Group names are authored on lights; assign deterministic bit indices. Using sorted names
        # makes the bit layout stable across runs, saves, and machines instead of depending on the
        # hash-table iteration order of a Python set.
        sorted_names = sorted({
            grp.name
            for light in bpy.data.lights
            for grp in light.light_groups.groups
        })
        if len(sorted_names) > MAX_NAMED_LIGHT_GROUPS:
            print("WARNING: Max number of named light groups (127) reached. "
                  "Groups after the first 127 sorted names are ignored.")
            sorted_names = sorted_names[:MAX_NAMED_LIGHT_GROUPS]

        # The default group is reserved at bit 127.
        bit_mapping = {name: index for index, name in enumerate(sorted_names)}
        for data in iter_light_group_owners():
            map_bits(data, bit_mapping)
    finally:
        _sync_in_progress = False


def update_handler(_s, _c):
    sync_light_groups()


@persistent
def sync_handler(*_):
    sync_light_groups()


@persistent
def sync_dg_handler(scn, dg):
    if dg.mode == 'RENDER':
        return

    for update in dg.updates:
        uid = update.id
        if not uid:
            continue
        if isinstance(uid, (bpy.types.Material, bpy.types.Light, bpy.types.NodeTree)):
            sync_light_groups()
            return


def rename_group(data, src, tgt):
    if data.id_data.library:
        return
    for grp in data.light_groups.groups:
        if grp.name == src:
            grp.name = tgt


def get_name(self):
    return self.name


# Update all matching names across the file.
def set_name(self, value):
    orig_name = self.name
    for data in iter_light_group_owners():
        rename_group(data, orig_name, value)
    sync_light_groups()


class LightGroup(PropertyGroup):
    name: StringProperty()
    viz_name: StringProperty(name="Name", get=get_name, set=set_name)
    ignore_shadow: BoolProperty(
        name="Ignore Shadows",
        description="Ignore received shadows from this light group, without changing shadow casting",
        default=False,
        options=set(),
        update=update_handler,
    )


class LightGroups(PropertyGroup):
    groups: CollectionProperty(type=LightGroup)
    group_index: IntProperty(name="Active Light Group", update=update_handler)
    use_default: BoolProperty(
        name="Use default Light Group",
        default=True,
        description="Use builtin default light group",
        update=update_handler,
    )
    ignore_default_shadow: BoolProperty(
        name="Ignore default Light Group shadows",
        default=False,
        description="Ignore default light group shadows",
        update=update_handler,
    )


def get_name_set():
    names = set()
    for data in iter_light_group_owners():
        for grp in data.light_groups.groups:
            names.add(grp.name)
    return names


def unique_group_name():
    names = get_name_set()
    i = 1
    name = "LightGroup"
    while name in names:
        name = f"LightGroup.{i:03}"
        i += 1
    return name


def _material_from_context(ctx):
    """Resolve the material shown by a Material Properties context, including pinned data."""
    mat = getattr(ctx, 'material', None)
    if isinstance(mat, Material):
        return mat

    space = getattr(ctx, 'space_data', None)
    pin_id = getattr(space, 'pin_id', None)
    if isinstance(pin_id, Material):
        return pin_id

    obj = getattr(ctx, 'object', None)
    if obj is None:
        return None
    mat = getattr(obj, 'active_material', None)
    if mat is not None:
        return mat
    return None


def get_groups(obj):
    if obj and getattr(obj, 'type', None) == 'LIGHT':
        return obj.data.light_groups
    if obj and hasattr(obj, 'active_material'):
        mat = getattr(obj, 'active_material', None)
        return mat.light_groups if mat is not None else None
    if isinstance(obj, Material):
        return obj.light_groups
    raise ValueError("Light groups are only available on materials, lights, or Shader Info nodes")


def get_groups_ctx(ctx):
    node = getattr(ctx, 'active_node', None)
    if node is not None and isinstance(node, ShaderNodeShaderInfo):
        return node.light_groups
    light = getattr(ctx, 'light', None)
    if light is not None:
        return light.light_groups
    space = getattr(ctx, 'space_data', None)
    pin_id = getattr(space, 'pin_id', None)
    if isinstance(pin_id, Light):
        return pin_id.light_groups
    mat = _material_from_context(ctx)
    if mat is not None:
        return mat.light_groups
    obj = getattr(ctx, 'object', None)
    if obj is not None:
        return get_groups(obj)
    return None


class MAT_UL_LightGroupList(UIList):
    def draw_item(self, context, layout, data, item, icon,
                  active_data, active_property, index=0, flt_flag=0):
        row = layout.row(align=True)
        row.prop(item, "viz_name", emboss=False, text="")
        # Material and Shader Info masks can independently ignore shadows from a group.
        if isinstance(data.id_data, (Material, ShaderNodeTree)):
            row.prop(item, "ignore_shadow", text="",
                     icon="REC" if item.ignore_shadow else "OVERLAY", emboss=False)

    def filter_items(self, context, data, property):
        keys = getattr(data, property)
        flt_flags = []
        flt_order = []
        helper_funcs = bpy.types.UI_UL_list
        if self.filter_name:
            flt_flags = helper_funcs.filter_items_by_name(
                self.filter_name, self.bitflag_filter_item, keys, "name")
        if self.use_filter_sort_alpha:
            flt_order = helper_funcs.sort_items_by_name(keys, "name")
        return flt_flags, flt_order


class ALightGroupPanel(Panel):
    bl_label = "Light Groups"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'

    def get_groups(self, ctx):
        return get_groups_ctx(ctx)

    def draw(self, ctx):
        layout = self.layout
        groups = self.get_groups(ctx)
        if groups is None:
            return
        layout.enabled = groups.id_data.is_editable
        row = layout.row()
        row.template_list("MAT_UL_LightGroupList", "",
                          groups, "groups", groups, "group_index",
                          rows=5, type='DEFAULT')
        # Right-hand operator column.
        col = row.column(align=True)
        col.operator('light_groups.link', icon='LINKED', text="")
        col.operator('light_groups.unlink', icon='UNLINKED', text="")
        col.separator()
        col.operator('light_groups.new', icon='ADD', text="")
        col.operator('light_groups.remove', icon='X', text="")
        col.separator()
        col.operator('light_groups.resync', icon='FILE_REFRESH', text="")

        row = layout.row()
        row.prop(groups, 'use_default', text="Use Default Group")
        # Shadow-related options apply to materials and Shader Info nodes, not lights.
        if getattr(self, 'bl_context', '') != "data" or self.bl_space_type == 'NODE_EDITOR':
            row = row.row()
            row.enabled = groups.use_default
            row.prop(groups, 'ignore_default_shadow', text="Ignore Default Shadows")


class OBJ_PT_MLightGroupPanel(ALightGroupPanel):
    bl_context = 'material'

    def get_groups(self, ctx):
        mat = _material_from_context(ctx)
        return mat.light_groups if mat is not None else None

    @classmethod
    def poll(cls, ctx):
        return _material_from_context(ctx) is not None


class OBJ_PT_LLightGroupPanel(ALightGroupPanel):
    bl_context = 'data'

    def get_groups(self, ctx):
        light = getattr(ctx, 'light', None)
        return light.light_groups if light is not None else None

    @classmethod
    def poll(cls, ctx):
        return getattr(ctx, 'light', None) is not None


class NOD_PT_LightGroupPanel(ALightGroupPanel):
    bl_space_type = 'NODE_EDITOR'
    bl_region_type = 'UI'
    bl_category = 'Node'

    @classmethod
    def poll(cls, ctx):
        return isinstance(getattr(ctx, 'active_node', None), ShaderNodeShaderInfo)

    def get_groups(self, ctx):
        node = ctx.active_node
        if isinstance(node, ShaderNodeShaderInfo):
            return node.light_groups
        return None


class LightGroupOp(Operator):
    bl_options = {'UNDO'}

    @staticmethod
    def get_groups(ctx):
        return get_groups_ctx(ctx)

    @classmethod
    def poll(cls, ctx):
        try:
            return (groups := cls.get_groups(ctx)) is not None and groups.id_data.is_editable
        except Exception:
            return False


class LightGroupSelectionOp(Operator):
    bl_options = {'UNDO'}

    @staticmethod
    def get_groups(ctx):
        return get_groups_ctx(ctx)

    @classmethod
    def poll(cls, ctx):
        try:
            grp = cls.get_groups(ctx)
            return (grp is not None and grp.id_data.is_editable and
                    0 <= grp.group_index < len(grp.groups))
        except Exception:
            return False


class MAT_OT_NewLightGroup(LightGroupOp):
    """Create a new unique light group"""
    bl_label = "New Light Group"
    bl_idname = 'light_groups.new'

    def execute(self, ctx):
        lgs = self.get_groups(ctx)
        name = unique_group_name()
        new = lgs.groups.add()
        new.name = name
        lgs.group_index = lgs.groups.find(new.name)
        sync_light_groups()
        return {'FINISHED'}


class MAT_OT_LinkLightGroup(LightGroupOp):
    """Link an existing light group to this data-block"""
    bl_label = "Link Existing Light Group"
    bl_idname = 'light_groups.link'
    bl_property = "name"

    name: EnumProperty(
        items=lambda scn, ctx: sorted(
            [(x, x, x) for x in get_name_set() if x not in get_groups_ctx(ctx).groups]))

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {'FINISHED'}

    def execute(self, ctx):
        lgs = self.get_groups(ctx)
        new = lgs.groups.add()
        new.name = self.name
        lgs.group_index = lgs.groups.find(new.name)
        sync_light_groups()
        return {'FINISHED'}


class MAT_OT_DeleteLightGroup(LightGroupSelectionOp):
    """Remove this light group from all data-blocks"""
    bl_label = "Delete Light Group"
    bl_idname = 'light_groups.remove'

    def invoke(self, ctx, evt):
        return ctx.window_manager.invoke_confirm(self, evt)

    def execute(self, ctx):
        lgs = self.get_groups(ctx)
        grp_name = lgs.groups[lgs.group_index].name
        for data in iter_light_group_owners():
            if data.id_data.library:
                continue
            groups = data.light_groups.groups
            index = groups.find(grp_name)
            if index != -1:
                groups.remove(index)
        lgs.group_index -= 1
        sync_light_groups()
        return {'FINISHED'}


class MAT_OT_UnlinkLightGroup(LightGroupSelectionOp):
    """Remove this light group from this data-block"""
    bl_label = "Unlink Light Group"
    bl_idname = 'light_groups.unlink'

    def execute(self, ctx):
        lgs = self.get_groups(ctx)
        lgs.groups.remove(lgs.group_index)
        lgs.group_index -= 1
        sync_light_groups()
        return {'FINISHED'}


class MAT_OT_ResyncLightGroups(LightGroupOp):
    """Ensure light group layers are up to date"""
    bl_label = "Resync Light Groups"
    bl_idname = 'light_groups.resync'

    def execute(self, ctx):
        sync_light_groups()
        return {'FINISHED'}


_classes = (
    LightGroup,
    LightGroups,
    MAT_UL_LightGroupList,
    OBJ_PT_MLightGroupPanel,
    OBJ_PT_LLightGroupPanel,
    NOD_PT_LightGroupPanel,
    MAT_OT_NewLightGroup,
    MAT_OT_LinkLightGroup,
    MAT_OT_DeleteLightGroup,
    MAT_OT_UnlinkLightGroup,
    MAT_OT_ResyncLightGroups,
)

_register, _unregister = register_classes_factory(classes=_classes)


def _safe_append(handler_list, fn):
    if fn not in handler_list:
        handler_list.append(fn)


def _safe_remove(handler_list, fn):
    if fn in handler_list:
        handler_list.remove(fn)


def _ensure_pointer_property(owner, name):
    if not hasattr(owner, name):
        setattr(owner, name, PointerProperty(type=LightGroups))


def _remove_pointer_property(owner, name):
    if hasattr(owner, name):
        delattr(owner, name)


def register():
    _register()
    _ensure_pointer_property(Material, "light_groups")
    _ensure_pointer_property(Light, "light_groups")
    _ensure_pointer_property(ShaderNodeShaderInfo, "light_groups")

    _safe_append(bpy.app.handlers.render_init, sync_handler)
    _safe_append(bpy.app.handlers.load_post, sync_handler)
    _safe_append(bpy.app.handlers.blend_import_post, sync_handler)
    _safe_append(bpy.app.handlers.undo_post, sync_handler)
    _safe_append(bpy.app.handlers.redo_post, sync_handler)
    _safe_append(bpy.app.handlers.depsgraph_update_post, sync_dg_handler)


def unregister():
    _safe_remove(bpy.app.handlers.depsgraph_update_post, sync_dg_handler)
    _safe_remove(bpy.app.handlers.redo_post, sync_handler)
    _safe_remove(bpy.app.handlers.undo_post, sync_handler)
    _safe_remove(bpy.app.handlers.blend_import_post, sync_handler)
    _safe_remove(bpy.app.handlers.load_post, sync_handler)
    _safe_remove(bpy.app.handlers.render_init, sync_handler)

    _remove_pointer_property(Material, "light_groups")
    _remove_pointer_property(Light, "light_groups")
    _remove_pointer_property(ShaderNodeShaderInfo, "light_groups")
    _unregister()
