# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Screen Space Info fixtures; invoked in a fresh Blender, never overwrite input blends.

Run with --background --factory-startup --python-exit-code 1 --python this_file --
--output <new-directory>. Host runner and cross-engine oracles live in the bridge tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np


def arguments():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--method", choices=["deferred", "hybrid", "forward"], default="deferred")
    p.add_argument("--background", choices=["planes", "world", "bsdf"], default="planes")
    p.add_argument("--screen", type=int, default=1)
    p.add_argument("--raytracing", type=int, default=1)
    p.add_argument("--offset", type=float, default=0.0)
    p.add_argument("--depth", action="store_true")
    p.add_argument("--mix", type=float, default=-1.0)
    p.add_argument("--volume", action="store_true")
    p.add_argument("--perspective", action="store_true")
    p.add_argument("--resolution", type=int, default=96)
    p.add_argument("--load", type=Path)
    p.add_argument("--save-fixture", action="store_true")
    p.add_argument("--runtime", action="store_true")
    p.add_argument("--aov", action="store_true")
    p.add_argument("--runtime-global", action="store_true")
    p.add_argument("--runtime-view", action="store_true")
    p.add_argument("--probes", action="store_true")
    p.add_argument("--plain", action="store_true")
    p.add_argument("--group", action="store_true")
    p.add_argument("--set-depth", action="store_true")
    p.add_argument("--film-transparent", action="store_true")
    p.add_argument("--camera-x", type=float, default=0.0)
    return p.parse_args(sys.argv[sys.argv.index("--") + 1:])


def material(name, color):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes.clear()
    out = m.node_tree.nodes.new("ShaderNodeOutputMaterial")
    emission = m.node_tree.nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = (*color, 1)
    m.node_tree.links.new(emission.outputs[0], out.inputs["Surface"])
    return m


def make_scene(a):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    s = bpy.context.scene
    s.render.engine = "BLENDER_EEVEE"
    s.render.resolution_x = a.resolution
    s.render.resolution_y = a.resolution
    s.render.resolution_percentage = 100
    s.render.use_compositing = False
    s.render.use_sequencer = False
    s.render.film_transparent = a.film_transparent
    s["SS_multilayer"] = a.aov
    bpy.context.view_layer.use_pass_environment = a.aov
    bpy.context.view_layer.use_pass_z = a.aov
    s.view_settings.view_transform = "Standard"
    s.view_settings.look = "None"
    s.view_settings.exposure = 0
    s.view_settings.gamma = 1
    s.eevee.taa_render_samples = 32
    for key in ("use_bloom", "use_gtao", "use_fast_gi"):
        if hasattr(s.eevee, key):
            setattr(s.eevee, key, False)
    for key in ("use_ssr", "use_ssr_refraction", "use_raytracing"):
        if hasattr(s.eevee, key):
            setattr(s.eevee, key, bool(a.raytracing))
    if hasattr(s.eevee, "ray_tracing_method"):
        s.eevee.ray_tracing_method = "SCREEN"
    w = bpy.data.worlds.new("SS_World")
    w.use_nodes = True
    s.world = w
    w.node_tree.nodes.get("Background").inputs[0].default_value = (
        (0.125, 0.25, 0.5, 1) if a.background == "world" else (0, 0, 0, 1))
    if a.aov:
        for name in ["WorldGuard", "SurfaceGuard"]:
            av = bpy.context.view_layer.aovs.add()
            av.name = name
        n = w.node_tree.nodes.new("ShaderNodeOutputAOV")
        setattr(n, "aov_name" if hasattr(n, "aov_name") else "name", "WorldGuard")
        n.inputs["Color"].default_value = (0.125, 0.5, 0.25, 1)
    if a.background != "world":
        for side, color in [(-1, (0.5, 0.125, 0.03125)), (1, (0.03125, 0.25, 0.5))]:
            bpy.ops.mesh.primitive_plane_add(size=2, location=(side * 5, 0, -2))
            ob = bpy.context.object
            ob.name = "SS_Background_" + str(side)
            ob.scale.x = 5  # Cover clamped viewport edges even under subpixel jitter.
            ob.scale.y = 3
            m = material(ob.name, color)
            if a.background == "bsdf":
                nt = m.node_tree
                out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
                d = nt.nodes.new("ShaderNodeBsdfDiffuse")
                d.inputs["Color"].default_value = (*color, 1)
                nt.links.new(d.outputs[0], out.inputs["Surface"])
            ob.data.materials.append(m)
    bpy.ops.mesh.primitive_cube_add(size=2)
    ob = bpy.context.object
    ob.name = "SS_Receiver"
    m = bpy.data.materials.new("SS_Receiver")
    m.use_nodes = True
    if bpy.app.version[0] < 5:
        m.blend_method = "BLEND" if a.method == "forward" else "OPAQUE"
    else:
        m.surface_render_method = "BLENDED" if a.method == "forward" else "DITHERED"
    m.use_screen_refraction = bool(a.screen)
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    ss = nt.nodes.new("ShaderNodeScreenspaceInfo")
    if a.group:
        nt.nodes.remove(ss)
        inner = bpy.data.node_groups.new("SS_Inner", "ShaderNodeTree")
        inner.interface.new_socket(name="Scene Color", in_out="OUTPUT", socket_type="NodeSocketColor")
        inner.interface.new_socket(name="Scene Depth", in_out="OUTPUT", socket_type="NodeSocketFloat")
        ins = inner.nodes.new("ShaderNodeScreenspaceInfo")
        out_inner = inner.nodes.new("NodeGroupOutput")
        for index in range(2):
            inner.links.new(ins.outputs[index], out_inner.inputs[index])
        outer = bpy.data.node_groups.new("SS_Outer", "ShaderNodeTree")
        outer.interface.new_socket(name="Scene Color", in_out="OUTPUT", socket_type="NodeSocketColor")
        outer.interface.new_socket(name="Scene Depth", in_out="OUTPUT", socket_type="NodeSocketFloat")
        gn = outer.nodes.new("ShaderNodeGroup")
        gn.node_tree = inner
        out_outer = outer.nodes.new("NodeGroupOutput")
        for index in range(2):
            outer.links.new(gn.outputs[index], out_outer.inputs[index])
        ss = nt.nodes.new("ShaderNodeGroup")
        ss.node_tree = outer
    color = ss.outputs["Scene Color"]
    if a.offset:
        # The same +Z-forward view coordinates as the unlinked Goo default.
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        v = nt.nodes.new("ShaderNodeVectorTransform")
        v.vector_type = "POINT"
        v.convert_from = "WORLD"
        v.convert_to = "CAMERA"
        nt.links.new(geo.outputs["Position"], v.inputs[0])
        mul = nt.nodes.new("ShaderNodeVectorMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = (1, 1, -1)
        nt.links.new(v.outputs[0], mul.inputs[0])
        add = nt.nodes.new("ShaderNodeVectorMath")
        add.operation = "ADD"
        add.inputs[1].default_value = (a.offset, 0, 0)
        nt.links.new(mul.outputs[0], add.inputs[0])
        nt.links.new(add.outputs[0], ss.inputs[0])
    if a.plain:
        # Analytic oracle at the receiver's own depth, including for volume composition.
        if a.background == "world":
            rgb = nt.nodes.new("ShaderNodeRGB")
            rgb.outputs[0].default_value = (0.125, 0.25, 0.5, 1)
            color = rgb.outputs[0]
        else:
            geo = nt.nodes.new("ShaderNodeNewGeometry")
            xyz = nt.nodes.new("ShaderNodeSeparateXYZ")
            nt.links.new(geo.outputs["Position"], xyz.inputs[0])
            cmp = nt.nodes.new("ShaderNodeMath")
            cmp.operation = "GREATER_THAN"
            nt.links.new(xyz.outputs[0], cmp.inputs[0])
            cmp.inputs[1].default_value = 0
            mix = nt.nodes.new("ShaderNodeMixRGB")
            mix.inputs[1].default_value = (0.5, 0.125, 0.03125, 1)
            mix.inputs[2].default_value = (0.03125, 0.25, 0.5, 1)
            nt.links.new(cmp.outputs[0], mix.inputs[0])
            color = mix.outputs[0]
    if a.depth:
        scale = nt.nodes.new("ShaderNodeMath")
        scale.operation = "MULTIPLY"
        scale.inputs[1].default_value = 0.1
        nt.links.new(ss.outputs["Scene Depth"], scale.inputs[0])
        rgb = nt.nodes.new("ShaderNodeCombineXYZ")
        for socket in rgb.inputs:
            nt.links.new(scale.outputs[0], socket)
        color = rgb.outputs[0]
    if a.method == "hybrid":
        em = nt.nodes.new("ShaderNodeEmission")
        nt.links.new(color, em.inputs[0])
        srgb = nt.nodes.new("ShaderNodeShaderToRGB")
        nt.links.new(em.outputs[0], srgb.inputs[0])
        color = srgb.outputs[0]
    if a.mix >= 0:
        em = nt.nodes.new("ShaderNodeEmission")
        nt.links.new(color, em.inputs[0])
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = a.mix
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(em.outputs[0], mix.inputs[2])
        color = mix.outputs[0]
    if a.set_depth:
        sd = nt.nodes.new("ShaderNodeSetDepth")
        nt.links.new(color, sd.inputs["Shader"])
        nt.links.new(ss.outputs["Scene Depth"], sd.inputs["View Depth"])
        color = sd.outputs[0]
    nt.links.new(color, out.inputs["Surface"])
    if a.aov:
        n = nt.nodes.new("ShaderNodeOutputAOV")
        setattr(n, "aov_name" if hasattr(n, "aov_name") else "name", "SurfaceGuard")
        n.inputs["Color"].default_value = (0.5, 0.25, 0.125, 1)
    ob.data.materials.append(m)
    if a.background == "bsdf":
        bpy.ops.object.light_add(type="SUN", location=(0, 0, 4))
        bpy.context.object.data.energy = 2
        bpy.context.object.data.use_shadow = False
    if a.volume:
        bpy.ops.mesh.primitive_cube_add(size=8)
        vol = bpy.context.object
        vol.name = "SS_Volume"
        vm = material("SS_Volume", (0, 0, 0))
        vo = next(n for n in vm.node_tree.nodes if n.type == "OUTPUT_MATERIAL")
        for link in list(vm.node_tree.links):
            vm.node_tree.links.remove(link)
        scatter = vm.node_tree.nodes.new("ShaderNodeVolumeAbsorption")
        scatter.inputs["Color"].default_value = (0.75, 0.5, 0.25, 1)
        scatter.inputs["Density"].default_value = 0.15
        vm.node_tree.links.new(scatter.outputs[0], vo.inputs["Volume"])
        vol.data.materials.append(vm)
    if a.probes and bpy.app.version[0] >= 5:
        for kind in ("SPHERE", "PLANE"):
            probe = bpy.data.lightprobes.new("SS_Probe_" + kind, kind)
            obj = bpy.data.objects.new(probe.name, probe)
            s.collection.objects.link(obj)
            obj.location = (0, 0, -1)
    bpy.ops.object.camera_add(location=(a.camera_x, 0, 6))
    s.camera = bpy.context.object
    s.camera.data.type = "PERSP" if a.perspective else "ORTHO"
    s.camera.data.ortho_scale = 4
    s.camera.data.lens = 50
    return s


def render(s, folder, name):
    path = folder / (name + ".exr")
    if hasattr(s.render.image_settings, "media_type"):
        s.render.image_settings.media_type = "MULTI_LAYER_IMAGE" if s.get("SS_multilayer") else "IMAGE"
    s.render.image_settings.file_format = "OPEN_EXR_MULTILAYER" if s.get("SS_multilayer") else "OPEN_EXR"
    s.render.image_settings.color_depth = "32"
    s.render.filepath = str(path)
    result = bpy.ops.render.render(write_still=True)
    assert result == {"FINISHED"}, result
    assert path.is_file(), "render did not produce an image"
    pixel_path = path
    if s.get("SS_multilayer"):
        # Image.pixels has no selected pass for a freshly loaded multilayer image.
        # Save Combined from the same render, without another render or compositor.
        pixel_path = folder / (name + "_combined.exr")
        if hasattr(s.render.image_settings, "media_type"):
            s.render.image_settings.media_type = "IMAGE"
        s.render.image_settings.file_format = "OPEN_EXR"
        bpy.data.images["Render Result"].save_render(str(pixel_path), scene=s)
    im = bpy.data.images.load(str(pixel_path), check_existing=False)
    w, h = im.size
    assert w and h and len(im.pixels) == w * h * 4
    pixels = np.array(im.pixels[:], dtype=np.float32).reshape(h, w, 4)
    assert np.isfinite(pixels).all()
    bpy.data.images.remove(im)
    np.save(folder / (name + ".npy"), pixels)
    # Interior left/right controls, away from the cube silhouette and background seam.
    rois = {}
    for label, x in [("left", 0.375), ("right", 0.625)]:
        cx, cy = int(w*x), int(h*.5)
        rois[label] = pixels[cy-3:cy+3, cx-3:cx+3].mean(axis=(0, 1)).tolist()
    return {"file": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "pixel_sha256": hashlib.sha256(pixels.tobytes()).hexdigest(), "roi": rois}


def main():
    a = arguments()
    a.output.mkdir(parents=True, exist_ok=False)
    if a.load:
        bpy.ops.wm.open_mainfile(filepath=str(a.load))
        s = bpy.context.scene
    else:
        s = make_scene(a)
    receiver = bpy.data.objects["SS_Receiver"]
    if a.save_fixture:
        bpy.ops.wm.save_as_mainfile(filepath=str(a.output / "fixture.blend"))
    records = {"actual": render(s, a.output, "actual")}
    if a.runtime:
        mat = receiver.active_material
        for value in [False, True, False, True]:
            mat.use_screen_refraction = value
            key = "toggle_" + str(len(records)) + "_" + str(int(value))
            records[key] = render(s, a.output, key)
    if a.runtime_global:
        for value in [False, True, False, True]:
            s.eevee.use_raytracing = value
            key = "toggle_global_" + str(len(records)) + "_" + str(int(value))
            records[key] = render(s, a.output, key)
    if a.runtime_view:
        s.camera.location.x += 0.2
        s.render.resolution_x = s.render.resolution_y = 127
        records["resized"] = render(s, a.output, "resized")
    receiver.hide_render = True
    records["reference"] = render(s, a.output, "reference")
    report = {"version": bpy.app.version_string, "build_hash": bpy.app.build_hash.decode(),
              "settings": {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
              "records": records}
    (a.output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("SCREENSPACE_FIXTURE_DONE " + str(a.output / "result.json"))


if __name__ == "__main__":
    main()
