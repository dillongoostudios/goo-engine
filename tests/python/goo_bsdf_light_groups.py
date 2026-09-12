# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ordinary BSDF receiver-group regression. Each worker owns a new output directory.

These workers use ordinary BSDFs (Shader Info has a separate suite). Native Blender can generate
missing-DNA/default-group oracles with --native-save. Render/readback is linear EXR32.
"""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import sys

import bpy
import numpy as np


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--startup', type=Path)
    p.add_argument('--method', choices=['deferred', 'forward', 'hybrid'], default='deferred')
    p.add_argument('--lobe', default='diffuse')
    p.add_argument('--light', default='SUN')
    p.add_argument('--case', default='groups')
    p.add_argument('--native-save', action='store_true')
    p.add_argument('--load', type=Path)
    p.add_argument('--link', type=Path)
    p.add_argument('--resolution', type=int, choices=[64], default=64)
    a = p.parse_args(sys.argv[sys.argv.index('--') + 1:])
    a.output.mkdir(parents=True, exist_ok=False)
    lg = None
    if not a.native_save:
        if a.startup:
            sys.path.insert(0, str(a.startup))
        import goo_engine_light_groups as lg
        if a.startup and Path(lg.__file__).parent.resolve() != a.startup.resolve():
            lg.unregister()
            lg = importlib.reload(lg)
        if not hasattr(bpy.types.Material, 'light_groups'):
            lg.register()
    report = {'arguments': {k: str(v) if isinstance(v, Path) else v for k, v in vars(a).items()},
              'version': bpy.app.version_string, 'checks': {}, 'images': {}, 'masks': {},
              'settings': {'resolution': a.resolution, 'samples': 32, 'frame': 1,
                           'view': 'Standard', 'world': 'black', 'output': 'linear EXR32'}}

    def check(name, ok):
        report['checks'][name] = bool(ok)
        if not ok:
            raise AssertionError(name)

    def groups(owner, names=(), default=False, ignore=False):
        g = owner.light_groups
        g.groups.clear()
        g.use_default = default
        if hasattr(g, 'ignore_default_shadow'):
            g.ignore_default_shadow = ignore
        for name in names:
            item = g.groups.add()
            item.name = name
            if hasattr(item, 'ignore_shadow'):
                item.ignore_shadow = ignore
        lg.sync_light_groups()

    def render(name):
        if lg:
            lg.sync_light_groups()
        bpy.context.view_layer.update()
        s.render.filepath = str(a.output / (name + '.exr'))
        bpy.ops.render.render(write_still=True)
        im = bpy.data.images.load(s.render.filepath, check_existing=False)
        px = np.asarray(im.pixels[:], dtype=np.float32).reshape(a.resolution, a.resolution, 4)
        bpy.data.images.remove(im)
        check(name + '_finite', np.isfinite(px).all())
        lo, hi = a.resolution * 3 // 8, a.resolution * 5 // 8
        roi = px[lo:hi, lo:hi]
        report['images'][name] = {'roi': roi.mean(axis=(0, 1)).tolist(),
                                  'pixel_sha256': hashlib.sha256(px.tobytes()).hexdigest()}
        if lg:
            report['masks'][name] = [list(ma.light_group_bits), list(ma.light_group_shadow_bits)]
        return px

    def rgb(px):
        n = a.resolution
        return px[n*3//8:n*5//8, n*3//8:n*5//8, :3]

    def make_material(name, method=a.method, lobe=a.lobe):
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        if hasattr(m, 'surface_render_method'):
            m.surface_render_method = 'BLENDED' if method == 'forward' else 'DITHERED'
        else:
            m.blend_method = 'BLEND' if method == 'forward' else 'OPAQUE'
        nt = m.node_tree
        nt.nodes.clear()
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        kind = {'diffuse': 'ShaderNodeBsdfDiffuse', 'glossy': 'ShaderNodeBsdfGlossy',
                'translucent': 'ShaderNodeBsdfTranslucent', 'glass': 'ShaderNodeBsdfGlass'}.get(lobe, 'ShaderNodeBsdfPrincipled')
        bsdf = nt.nodes.new(kind)
        if 'Color' in bsdf.inputs:
            bsdf.inputs['Color'].default_value = (.5, .5, .5, 1)
        if 'Roughness' in bsdf.inputs:
            bsdf.inputs['Roughness'].default_value = .4
        if lobe == 'coat':
            bsdf.inputs['Coat Weight'].default_value = 1
        elif lobe == 'sss':
            bsdf.inputs['Subsurface Weight'].default_value = 1
        elif lobe == 'transmission':
            bsdf.inputs['Transmission Weight'].default_value = 1
        socket = bsdf.outputs[0]
        if method == 'hybrid':
            conv = nt.nodes.new('ShaderNodeShaderToRGB')
            em = nt.nodes.new('ShaderNodeEmission')
            mix = nt.nodes.new('ShaderNodeMixShader')
            mix.inputs[0].default_value = .5
            nt.links.new(socket, conv.inputs[0])
            nt.links.new(conv.outputs['Color'], em.inputs['Color'])
            nt.links.new(em.outputs[0], mix.inputs[1])
            nt.links.new(socket, mix.inputs[2])
            socket = mix.outputs[0]
        if a.case == 'modern_alpha':
            trans = nt.nodes.new('ShaderNodeBsdfTransparent')
            mix = nt.nodes.new('ShaderNodeMixShader')
            mix.inputs[0].default_value = .5
            nt.links.new(trans.outputs[0], mix.inputs[1])
            nt.links.new(socket, mix.inputs[2])
            socket = mix.outputs[0]
        nt.links.new(socket, out.inputs['Surface'])
        return m

    def light(name, color):
        ld = bpy.data.lights.new(name, a.light)
        ld.energy = 1 if a.light == 'SUN' else 150
        ld.color = color
        ld.use_shadow = a.case in ('shadows', 'contact', 'self', 'linking')
        if hasattr(ld, 'use_shadow_jitter'):
            ld.use_shadow_jitter = False
        if hasattr(ld, 'use_contact_shadow'):
            ld.use_contact_shadow = False
        if a.light == 'SUN':
            ld.angle = 0
        if a.light == 'AREA':
            ld.size = .1
        ob = bpy.data.objects.new(name, ld)
        s.collection.objects.link(ob)
        z = -3 if a.lobe == 'translucent' else 3
        ob.location = (0, 0, z)
        ob.rotation_euler = (-ob.location).to_track_quat('-Z', 'Y').to_euler()
        return ob

    try:
        if a.load or a.link:
            # Prove SDNA migration without Python UI handlers repairing missing fields.
            lg.unregister()
            lg = None
        if a.link:
            with bpy.data.libraries.load(str(a.link), link=True) as (available, loaded):
                loaded.materials = ['Receiver']
                loaded.lights = ['Key', 'Fill']
            for owner in loaded.materials + loaded.lights:
                check(owner.name + '_linked', owner.library is not None)
                check(owner.name + '_default', tuple(owner.light_group_bits) == (0, 0, 0, 1))
            check('linked_default_shadows', tuple(loaded.materials[0].light_group_shadow_bits) == (0, 0, 0, 1))
            return
        if a.load:
            bpy.ops.wm.open_mainfile(filepath=str(a.load))
            s = bpy.context.scene
            ma = bpy.data.materials['Receiver']
            for owner in [ma] + list(bpy.data.lights):
                check(owner.name + '_default', tuple(owner.light_group_bits) == (0, 0, 0, 1))
            check('default_shadows', tuple(ma.light_group_shadow_bits) == (0, 0, 0, 1))
            render('native_loaded')
            bpy.ops.wm.save_as_mainfile(filepath=str(a.output / 'roundtrip.blend'))
            bpy.ops.wm.open_mainfile(filepath=str(a.output / 'roundtrip.blend'))
            ma = bpy.data.materials['Receiver']
            s = bpy.context.scene
            render('native_reopened')
            check('roundtrip_pixels', report['images']['native_loaded']['pixel_sha256'] == report['images']['native_reopened']['pixel_sha256'])
            return
        bpy.ops.object.select_all(action='SELECT')
        bpy.ops.object.delete(use_global=False)
        s = bpy.context.scene
        s.render.engine = 'BLENDER_EEVEE'
        s.render.resolution_x = s.render.resolution_y = a.resolution
        s.render.resolution_percentage = 100
        s.render.image_settings.file_format = 'OPEN_EXR'
        s.render.image_settings.color_mode = 'RGBA'
        s.render.image_settings.color_depth = '32'
        s.render.film_transparent = True
        s.render.dither_intensity = 0
        s.eevee.taa_render_samples = 32
        if hasattr(s.eevee, 'use_raytracing'):
            s.eevee.use_raytracing = False
        s.view_settings.view_transform = 'Standard'
        s.view_settings.look = 'None'
        s.view_settings.exposure = 0
        s.view_settings.gamma = 1
        s.use_nodes = False
        s.frame_set(1)
        s.world.use_nodes = True
        s.world.node_tree.nodes.get('Background').inputs[0].default_value = (0, 0, 0, 1)
        bpy.ops.object.camera_add(location=(0, 0, 5))
        s.camera = bpy.context.object
        s.camera.data.type = 'ORTHO'
        s.camera.data.ortho_scale = 4
        bpy.ops.mesh.primitive_plane_add(size=10)
        receiver = bpy.context.object
        receiver.name = 'ReceiverObject'
        ma = make_material('Receiver')
        receiver.data.materials.append(ma)
        key = light('Key', (1, 0, 0))
        fill = light('Fill', (0, 0, 1))
        if a.native_save or a.case in ('default', 'modern_alpha'):
            px = render('native')
            check('native_positive_control', rgb(px)[:, :, 0].mean() > .001 and rgb(px)[:, :, 2].mean() > .001)
            bpy.ops.wm.save_as_mainfile(filepath=str(a.output / 'native.blend'))
            return
        groups(key.data, ['Key'])
        groups(fill.data, ['Fill'])
        groups(ma, ['Key', 'Fill'])
        sys.path.insert(0, str(Path(__file__).parent))
        from goo_bsdf_light_groups_cases import run_extra
        if run_extra(a, s, receiver, ma, key, fill, groups, render, rgb, check, make_material):
            bpy.ops.wm.save_as_mainfile(filepath=str(a.output / 'fixture.blend'))
            return
        if a.case == 'groups':
            all_px = render('all')
            check('positive_red_blue', rgb(all_px)[:, :, 0].mean() > .001 and rgb(all_px)[:, :, 2].mean() > .001)
            groups(ma, ['Key'])
            key_px = render('key')
            check('selected_red', rgb(key_px)[:, :, 0].mean() > .001)
            check('excluded_blue', rgb(key_px)[:, :, 2].max() < 1e-4)
            check('selected_energy', np.allclose(rgb(key_px)[:, :, 0], rgb(all_px)[:, :, 0], atol=.001, rtol=.02))
            groups(ma, [])
            empty = render('empty')
            check('empty_is_black', rgb(empty).max() < 1e-4)
            groups(ma, ['Key', 'Fill'])
            restore = render('restore')
            check('runtime_restore', np.array_equal(restore, all_px))
            groups(key.data, ['Key', 'Fill'])
            multi = render('multi')
            check('or_no_double_count', np.array_equal(multi, all_px))
            groups(key.data, [])
            zero = render('zero_light')
            check('real_zero_group_excluded', rgb(zero)[:, :, 0].max() < 1e-4 and rgb(zero)[:, :, 2].mean() > .001)
        elif a.case == 'slots':
            mesh = bpy.data.meshes.new('TwoMaterialSlots')
            mesh.from_pydata([(-5,-5,0),(0,-5,0),(5,-5,0),(-5,5,0),(0,5,0),(5,5,0)], [], [(0,1,4,3),(1,2,5,4)])
            receiver.data = mesh
            other = make_material('ReceiverFill')
            mesh.materials.append(ma)
            mesh.materials.append(other)
            mesh.polygons[1].material_index = 1
            groups(ma, ['Key'])
            groups(other, ['Fill'])
            px = render('slots')
            left, right = px[24:40,8:24,:3], px[24:40,40:56,:3]
            check('left_red', left[:,:,0].mean() > .001 and left[:,:,2].max() < 1e-4)
            check('right_blue', right[:,:,2].mean() > .001 and right[:,:,0].max() < 1e-4)
        elif a.case in ('shadows', 'self', 'contact'):
            fill.hide_render = True
            key.data.color = (1, 1, 1)
            key.location = (-3, 0, 5)
            key.rotation_euler = (-key.location).to_track_quat('-Z','Y').to_euler()
            groups(ma, ['Key'])
            bpy.ops.mesh.primitive_cube_add(size=1, location=(0,0,.6))
            caster = bpy.context.object
            # Contact tracing needs an opaque screen-depth occluder, even for a Forward receiver.
            caster.data.materials.append(make_material('Caster', method='deferred' if a.case == 'contact' else a.method))
            if a.case in ('self', 'contact'):
                bpy.ops.object.select_all(action='DESELECT')
                receiver.select_set(True)
                caster.select_set(True)
                bpy.context.view_layer.objects.active = receiver
                bpy.ops.object.join()
            if a.case == 'contact':
                # Same object + IgnoreSelf removes VSM occlusion, isolating legacy contact.
                ma.check_shadow_id = True
                key.data.use_contact_shadow = False
                base = render('contact_off')
                key.data.use_contact_shadow = True
                key.data.contact_shadow_distance = 3.0
                key.data.contact_shadow_bias = .01
                key.data.contact_shadow_thickness = .2
                contact = render('contact_on')
                groups(ma, ['Key'], ignore=True)
                ignored = render('contact_ignored')
                roi = np.s_[25:39, 40:51, :3]
                check('contact_positive_control', (base[roi] - contact[roi]).mean() > .005)
                check('ignore_disables_contact', np.allclose(base[roi], ignored[roi], atol=.003, rtol=.02))
                groups(ma, ['Key'])
                key.data.use_contact_shadow = False
                restored = render('contact_off_restored')
                check('contact_runtime_restore', np.array_equal(restored, base))
                return
            off = render('shadow_on')
            groups(ma, ['Key'], ignore=True)
            ignored = render('shadow_ignored')
            key.data.use_shadow = False
            control = render('no_shadow_control')
            # Exclude visible caster; use the cast shadow to its right.
            roi = np.s_[25:39, 42:54, :3]
            check('shadow_positive_control', (ignored[roi] - off[roi]).mean() > .005)
            check('ignore_matches_unshadowed', np.allclose(ignored[roi], control[roi], atol=.003, rtol=.02))
            key.data.use_shadow = True
            groups(ma, ['Key'])
            restored = render('shadow_restored')
            check('shadow_runtime_restore', np.array_equal(restored, off))
        else:
            raise ValueError(a.case)
        bpy.ops.wm.save_as_mainfile(filepath=str(a.output / 'fixture.blend'))
    finally:
        report['pass'] = bool(report['checks']) and all(report['checks'].values()) and sys.exc_info()[0] is None
        (a.output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf8')
        print('GOO_BSDF_GROUPS', 'PASS' if report['pass'] else 'FAIL', flush=True)


if __name__ == '__main__':
    main()
