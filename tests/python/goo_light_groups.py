# SPDX-License-Identifier: GPL-2.0-or-later
"""Goo Material Light Groups regression (host Python, fresh background Blender per case).

python goo_light_groups.py --blender <blender.exe> --output <new-directory>
Optional --startup <source/scripts/startup> tests source UI before packaging.
Optional --reference <previous/blender.exe> compares ordinary EEVEE and modern alpha pixels.
Artifacts, commands, exit codes and hashes are retained in output/manifest.json.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def worker(args):
    import bpy
    import numpy as np
    from types import SimpleNamespace
    import goo_engine_light_groups as lg

    if args.worker == 'legacy_save':
        ma = bpy.data.materials.new('LegacyGroups')
        ma.use_nodes = True
        ma.use_fake_user = True
        ma.blend_method = 'OPAQUE'
        ma.light_groups.use_default = False
        ma.light_groups.groups.add().name = 'Key'
        ma.light_groups.groups[0].ignore_shadow = True
        node = ma.node_tree.nodes.new('ShaderNodeShaderInfo')
        node.light_groups.use_default = False
        node.light_groups.groups.add().name = 'Other'
        ld = bpy.data.lights.new('LegacyKey', 'SUN')
        ld.use_fake_user = True
        ld.light_groups.use_default = False
        ld.light_groups.groups.add().name = 'Key'
        lg.sync_light_groups()
        args.output.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'legacy.blend'))
        print('GOO_LIGHT_GROUPS_LEGACY_SAVED', flush=True)
        return

    if args.startup and Path(lg.__file__).parent != args.startup.resolve():
        # Reload the existing startup module, never register a second copy under another name.
        import importlib
        lg.unregister()
        sys.path.insert(0, str(args.startup.resolve()))
        lg = importlib.reload(lg)
        lg.register()
    folder = args.output
    folder.mkdir(parents=True, exist_ok=True)
    report = {"checks": {}, "pixels": {}, "module": lg.__file__,
              "module_sha256": sha(lg.__file__), "blender_version": bpy.app.version_string,
              "render_settings": {"resolution": [64, 64], "samples": 32,
                                  "frame": 1, "view_transform": "Standard", "look": "None",
                                  "exposure": 0, "gamma": 1, "output": "linear EXR32"}}

    def check(name, condition):
        report['checks'][name] = bool(condition)
        if not condition:
            raise AssertionError(name)

    def groups(owner, names=(), default=False, ignore=False):
        g = owner.light_groups
        g.groups.clear()
        g.use_default = default
        g.ignore_default_shadow = ignore
        for name in names:
            item = g.groups.add()
            item.name = name
            item.ignore_shadow = ignore
        lg.sync_light_groups()

    def reset():
        for ob in list(bpy.data.objects):
            bpy.data.objects.remove(ob, do_unlink=True)
        for ma in list(bpy.data.materials):
            bpy.data.materials.remove(ma)
        for light in list(bpy.data.lights):
            bpy.data.lights.remove(light)
        s = bpy.context.scene
        s.render.engine = 'BLENDER_EEVEE'
        s.render.resolution_x = s.render.resolution_y = 64
        s.render.resolution_percentage = 100
        s.render.image_settings.file_format = 'OPEN_EXR'
        s.render.image_settings.color_mode = 'RGBA'
        s.render.image_settings.color_depth = '32'
        s.render.film_transparent = True
        if hasattr(s.eevee, 'taa_render_samples'):
            s.eevee.taa_render_samples = 32
        s.frame_set(1)
        s.view_settings.view_transform = 'Standard'
        s.view_settings.look = 'None'
        s.view_settings.exposure = 0
        s.view_settings.gamma = 1
        s.world.use_nodes = True
        s.world.node_tree.nodes.get('Background').inputs['Color'].default_value = (0, 0, 0, 1)
        bpy.ops.object.camera_add(location=(0, 0, 5))
        s.camera = bpy.context.object
        s.camera.data.type = 'ORTHO'
        s.camera.data.ortho_scale = 4
        return s

    def light(name, kind, color=(1, 1, 1), named=True):
        ld = bpy.data.lights.new(name, kind)
        lo = bpy.data.objects.new(name, ld)
        bpy.context.scene.collection.objects.link(lo)
        lo.location = (-3, 0, 5)
        lo.rotation_euler = (-lo.location).to_track_quat('-Z', 'Y').to_euler()
        ld.energy = 1 if kind == 'SUN' else 500
        ld.color = color
        if kind == 'AREA':
            ld.size = 0.1
        if kind == 'SUN':
            ld.angle = 0
        if hasattr(ld, 'use_shadow_jitter'):
            ld.use_shadow_jitter = False
        if hasattr(ld, 'use_shadow_contact'):
            ld.use_shadow_contact = False
        if named:
            groups(ld, [name])
        return lo

    def receiver(method, output='Diffuse Shading', ordinary=False):
        bpy.ops.mesh.primitive_plane_add(size=10)
        ob = bpy.context.object
        ob.name = 'Receiver'
        ma = bpy.data.materials.new('ReceiverMaterial')
        ma.surface_render_method = method
        ob.data.materials.append(ma)
        nt = ma.node_tree
        nt.nodes.clear()
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        if ordinary:
            bsdf = nt.nodes.new('ShaderNodeBsdfDiffuse')
            bsdf.inputs['Color'].default_value = (0.4, 0.4, 0.4, 1)
            nt.links.new(bsdf.outputs[0], out.inputs['Surface'])
            return ob, ma, None
        info = nt.nodes.new('ShaderNodeShaderInfo')
        em = nt.nodes.new('ShaderNodeEmission')
        if output in ('Cast Shadows', 'Self Shadows'):
            rgb = nt.nodes.new('ShaderNodeCombineXYZ')
            for socket in rgb.inputs:
                nt.links.new(info.outputs[output], socket)
            nt.links.new(rgb.outputs[0], em.inputs[0])
        else:
            nt.links.new(info.outputs[output], em.inputs[0])
        nt.links.new(em.outputs[0], out.inputs['Surface'])
        return ob, ma, info

    def render(name):
        lg.sync_light_groups()
        bpy.context.view_layer.update()
        scene = bpy.context.scene
        path = folder / (name + '.exr')
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(str(path), check_existing=False)
        pixels = np.array(image.pixels[:], dtype=np.float32).reshape(64, 64, 4)
        bpy.data.images.remove(image)
        report['pixels'][name] = {
            'sha256': hashlib.sha256(pixels.tobytes()).hexdigest(),
            'center': pixels[24:40, 24:40].mean(axis=(0, 1)).tolist(),
            'min': float(pixels[:, :, 0].min()),
        }
        return pixels

    try:
        case = args.worker
        s = reset()
        if case == 'api':
            ob, ma, node = receiver('DITHERED')
            check('panel_registered', hasattr(bpy.types, 'OBJ_PT_MLightGroupPanel'))
            check('default_material', tuple(ma.light_group_bits) == (0, 0, 0, 1))
            check('default_node_initialized', tuple(node.light_group_bits) == (0, 0, 0, 1))
            ld = light('Key', 'SUN').data
            groups(ma, ['Key'])
            check('same_membership', tuple(ma.light_group_bits) == tuple(ld.light_group_bits))
            ma.light_groups.groups[0].ignore_shadow = True
            check('ignore_shadows', tuple(ma.light_group_shadow_bits) == (0, 0, 0, 0))
            groups(node, ['Key'])
            ma.light_groups.groups[0].viz_name = 'Renamed'
            check('rename_all_including_disabled_nodes', node.light_groups.groups[0].name == 'Renamed' and
                  ld.light_groups.groups[0].name == 'Renamed')
            # Context resolution must follow pinned IDs and never edit a different material slot.
            other = bpy.data.materials.new('Pinned')
            ctx = SimpleNamespace(material=other, object=ob, space_data=None)
            check('pinned_material', lg.get_groups_ctx(ctx) == other.light_groups)
            ctx = SimpleNamespace(light=ld, object=None, space_data=None)
            check('pinned_light', lg.get_groups_ctx(ctx) == ld.light_groups)
            ob.data.materials.append(None)
            ob.active_material_index = 1
            check('empty_slot', lg.get_groups(ob) is None)
            ob.active_material_index = 0
            bpy.context.view_layer.objects.active = ob
            bpy.ops.light_groups.new()
            check('operator_new', len(ma.light_groups.groups) == 2)
            bpy.ops.light_groups.unlink()
            check('operator_unlink', len(ma.light_groups.groups) == 1)
            groups(ld, ['Renamed', 'Linkable'])
            groups(node, ['Linkable'])
            bpy.ops.light_groups.link(name='Linkable')
            check('operator_link', 'Linkable' in ma.light_groups.groups)
            bpy.ops.light_groups.remove()
            check('operator_delete_global', all('Linkable' not in owner.light_groups.groups
                                                for owner in (ma, ld, node)))
            check('operator_resync', bpy.ops.light_groups.resync() == {'FINISHED'})
            # Reserve the default bit, including the signed bit in each word.
            groups(ld, ['G%03d' % i for i in range(128)])
            groups(ma, ['G030', 'G062', 'G094', 'G126'])
            check('full_128bit_transport', tuple(ma.light_group_bits) == (-2147483648,) * 4)
            groups(ma, ['G127'])
            check('overflow_does_not_alias_default', tuple(ma.light_group_bits) == (0, 0, 0, 0))
            groups(ma, default=True)
            check('default_still_reserved', tuple(ma.light_group_bits) == (0, 0, 0, 1))
            # A no-op sync must not issue new RNA updates / endlessly invalidate GPU materials.
            calls = []
            original = lg._set_array_if_changed
            def observe(data, prop, values):
                if tuple(getattr(data, prop)) != tuple(values):
                    calls.append(prop)
                return original(data, prop, values)
            lg._set_array_if_changed = observe
            lg.sync_light_groups()
            check('idempotent_sync', not calls)
            lg._set_array_if_changed = original
        elif case.startswith('inherit_'):
            _, method, kind = case.split('_')
            ob, ma, node = receiver(method)
            key = light('Key', kind, (1, 0, 0))
            fill = light('Fill', kind, (0, 0, 1))
            def red(p):
                c = p[24:40, 24:40].mean(axis=(0, 1))
                return c[0] > 0.01 and c[2] < 0.0001
            def blue(p):
                c = p[24:40, 24:40].mean(axis=(0, 1))
                return c[2] > 0.01 and c[0] < 0.0001
            groups(ma, ['Key']); a = render('material_key'); check('material_key', red(a))
            groups(ma, ['Fill']); b = render('material_fill'); check('runtime_material_change', blue(b))
            groups(ma, ['Key']); groups(node, ['Fill']); node.use_own_light_groups = True
            check('node_overrides_material', blue(render('node_fill')))
            node.use_own_light_groups = False
            check('node_returns_to_material', red(render('node_off')))
            groups(ma)
            check('empty_material_black', render('empty')[:, :, :3].max() < 0.0001)
            groups(key.data, default=True); groups(ma, default=True)
            check('default_group', red(render('default')))
            groups(key.data, ['Key', 'Other']); groups(ma, ['Key', 'Other'])
            check('or_no_double_count', np.allclose(render('multi_group'), a, atol=0.0001))
            # Shared nested group must inherit each material, never bake the first owner's mask.
            group = bpy.data.node_groups.new('Shared', 'ShaderNodeTree')
            group.interface.new_socket(name='Light', in_out='OUTPUT', socket_type='NodeSocketColor')
            shared = group.nodes.new('ShaderNodeShaderInfo')
            gout = group.nodes.new('NodeGroupOutput')
            group.links.new(shared.outputs[0], gout.inputs[0])
            nt = ma.node_tree
            gnode = nt.nodes.new('ShaderNodeGroup'); gnode.node_tree = group
            nt.links.new(gnode.outputs[0], nt.nodes.get('Emission').inputs[0])
            ma2 = ma.copy(); ma2.name = 'SecondMaterial'
            groups(ma, ['Key']); groups(ma2, ['Fill'])
            ob.data.materials[0] = ma2
            check('shared_group_second_material', blue(render('shared_second')))
            ob.data.materials[0] = ma
            check('shared_group_first_material', red(render('shared_first')))
            if kind == 'AREA':
                groups(key.data, ['G%03d' % i for i in range(127)])
                groups(fill.data, ['Fill'])
                groups(ma, ['G029', 'G061', 'G093', 'G125'])
                check('signed_bits_render', red(render('signed_bits')))
                groups(key.data, ['Key']); groups(ma, ['Key'])
            # A .blend with both materials/group is consumed by save/reopen and append/link tests.
            ma2.use_fake_user = True
            if kind == 'SUN':
                mesh = bpy.data.meshes.new('TwoSlots')
                mesh.from_pydata([(-5, -5, 0), (0, -5, 0), (5, -5, 0),
                                  (-5, 5, 0), (0, 5, 0), (5, 5, 0)], [],
                                 [(0, 1, 4, 3), (1, 2, 5, 4)])
                ob.data = mesh
                mesh.materials.append(ma); mesh.materials.append(ma2)
                mesh.polygons[1].material_index = 1
                pixels = render('two_material_slots')
                check('two_material_slots', pixels[32, 16, 0] > 0.01 and
                      pixels[32, 16, 2] < 0.0001 and pixels[32, 48, 2] > 0.01 and
                      pixels[32, 48, 0] < 0.0001)
            bpy.ops.wm.save_as_mainfile(filepath=str(folder / 'fixture.blend'))
        elif case.startswith('shadow_'):
            kind = case.split('_')[1]
            ob, ma, node = receiver('DITHERED', 'Cast Shadows')
            ld = light('Key', kind).data
            groups(ma, ['Key'])
            bpy.ops.mesh.primitive_cube_add(size=0.6, location=(-0.6, 0, 1))
            caster = bpy.context.object
            # Separate geometry produces cross shadows in front of the receiver.
            off = render('cast'); groups(ma, ['Key'], ignore=True); no_shadow = render('ignore')
            # Camera maps world x to 32 + 16*x. The cube projects through x=27;
            # leave a reconstruction-filter margin and sample only the receiver at x>=30.
            # Sampling the cube's ordinary BSDF would not measure the Shader Info output.
            roi = (slice(28, 36), slice(30, 38), 0)
            report['shadow_roi'] = {'x': [30, 38], 'y': [28, 36]}
            check('unshadowed_receiver', no_shadow[roi].min() > 0.95)
            check('cross_shadow_present', (no_shadow[roi] - off[roi]).max() > 0.5)
            groups(node, ['Key'])
            node.use_own_light_groups = True
            check('node_shadow_override', np.max(np.abs(render('node_shadow')[roi] - off[roi])) < 0.03)
            node.use_own_light_groups = False
            check('return_to_material_ignore', render('material_ignore_again')[roi].min() > 0.95)
            groups(ld, default=True)
            groups(ma, default=True)
            check('default_cast', np.max(np.abs(render('default_cast')[roi] - off[roi])) < 0.03)
            ma.light_groups.ignore_default_shadow = True
            check('ignore_default_shadow', render('default_ignore')[roi].min() > 0.95)
            groups(ld, ['Key'])
            groups(ma, ['Key'])
            ma.check_shadow_id = True
            cast = render('id_cast')
            check('id_cast_preserves_cross', np.max(np.abs(cast[roi] - off[roi])) < 0.03)
            nt = ma.node_tree; xyz = nt.nodes.get('Combine XYZ')
            for socket in xyz.inputs:
                nt.links.new(node.outputs['Self Shadows'], socket)
            self_cross = render('id_self_cross')
            check('id_self_excludes_cross', self_cross[roi].min() > 0.95)
            # Join caster to receiver: same resource ID, but exactly the same geometry.
            bpy.ops.object.select_all(action='DESELECT'); caster.select_set(True); ob.select_set(True)
            bpy.context.view_layer.objects.active = ob; bpy.ops.object.join()
            for poly in ob.data.polygons:
                poly.material_index = 0
            self_same = render('id_self_same')
            check('id_self_preserves_self', (self_cross[roi] - self_same[roi]).max() > 0.5)
            for socket in xyz.inputs:
                nt.links.new(node.outputs['Cast Shadows'], socket)
            cast_same = render('id_cast_same')
            check('id_cast_excludes_self', cast_same[roi].min() > 0.95)
            groups(ma, ['Key'], ignore=True)
            check('ignore_with_id', render('id_ignore')[roi].min() > 0.95)
        elif case == 'legacy_load':
            bpy.ops.wm.open_mainfile(filepath=str(args.fixture))
            ma = bpy.data.materials['LegacyGroups']
            ld = bpy.data.lights['LegacyKey']
            check('legacy_names', ma.light_groups.groups[0].name == 'Key')
            check('legacy_ignore', ma.light_groups.groups[0].ignore_shadow)
            check('legacy_bits', tuple(ma.light_group_bits) == tuple(ld.light_group_bits))
            check('legacy_shadow_mask', tuple(ma.light_group_shadow_bits) == (0, 0, 0, 0))
            check('opaque_provenance_unchanged', ma.blend_method == 'OPAQUE')
            check('legacy_node_not_overwritten', ma.node_tree.nodes.get('Shader Info').light_groups.groups[0].name == 'Other')
            bpy.ops.wm.save_as_mainfile(filepath=str(folder/'roundtrip.blend'))
            bpy.ops.wm.open_mainfile(filepath=str(folder/'roundtrip.blend'))
            ma = bpy.data.materials['LegacyGroups']
            check('legacy_roundtrip', ma.blend_method == 'OPAQUE' and ma.light_groups.groups[0].ignore_shadow)
        elif case == 'old_port_load':
            bpy.ops.wm.open_mainfile(filepath=str(args.fixture))
            ma = bpy.data.materials['ReceiverMaterial']
            check('old_port_default', tuple(ma.light_group_bits) == (0, 0, 0, 1))
            check('old_port_shadow_default', tuple(ma.light_group_shadow_bits) == (0, 0, 0, 1))
            check('old_port_modern_not_legacy', ma.blend_method != 'OPAQUE')
            render('modern_alpha')
        elif case in ('reopen', 'append', 'link'):
            source = args.fixture
            if case == 'reopen':
                bpy.ops.wm.open_mainfile(filepath=str(source))
            else:
                with bpy.data.libraries.load(str(source), link=(case == 'link')) as (src, dst):
                    dst.materials = ['ReceiverMaterial', 'SecondMaterial']
                    dst.lights = ['Key', 'Fill']
            ma = bpy.data.materials['ReceiverMaterial']
            ma2 = bpy.data.materials['SecondMaterial']
            check('names_persist', ma.light_groups.groups[0].name == 'Key' and
                  ma2.light_groups.groups[0].name == 'Fill')
            lg.sync_light_groups()
            check('distinct_masks_after_load', tuple(ma.light_group_bits) != tuple(ma2.light_group_bits))
            check('groups_preserved', any(n.type == 'GROUP' for n in ma.node_tree.nodes))
        elif case.startswith('ordinary_'):
            method = case.split('_')[1]
            ob, ma, node = receiver(method, ordinary=True)
            light('Key', 'SUN', named=False)
            a = render('ordinary')
            if hasattr(ma, 'light_groups'):
                # Native/default regression only. Empty groups now intentionally suppress
                # ordinary BSDF direct light; goo_bsdf_light_groups.py verifies that separately.
                groups(ma, default=True)
                check('ordinary_default_unchanged', np.array_equal(render('ordinary_default_groups'), a))
            # Modern alpha card uses real transparent closure weights, not legacy OPAQUE.
            nt = ma.node_tree; out = nt.nodes.get('Material Output')
            trans = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
            mix.inputs[0].default_value = 0.5
            nt.links.new(trans.outputs[0], mix.inputs[1])
            nt.links.new(nt.nodes.get('Diffuse BSDF').outputs[0], mix.inputs[2])
            nt.links.new(mix.outputs[0], out.inputs['Surface']); render('modern_alpha')
            check('modern_not_legacy', ma.blend_method != 'OPAQUE')
        else:
            raise ValueError(case)
        bpy.ops.wm.save_as_mainfile(filepath=str(folder / 'final.blend'))
        report['pass'] = True
    finally:
        (folder/'result.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    print('GOO_LIGHT_GROUPS_CASE_PASS', args.worker, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--legacy', type=Path)
    parser.add_argument('--startup', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--worker')
    parser.add_argument('--fixture', type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else None)
    if args.worker:
        worker(args)
        return
    if args.blender is None:
        parser.error('--blender is required for the matrix runner')
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = {'binary': str(args.blender), 'binary_sha256': sha(args.blender),
                'binary_mtime_ns': args.blender.stat().st_mtime_ns,
                'script_sha256': sha(__file__), 'cases': [], 'started': time.time()}
    version = subprocess.run([str(args.blender), '--version'], capture_output=True, timeout=60)
    (args.output / 'blender_version.txt').write_bytes(version.stdout + version.stderr)
    manifest['version_exit_code'] = version.returncode
    cases = ['api'] + ['inherit_%s_%s' % (m, k) for m in ('DITHERED', 'BLENDED')
                      for k in ('SUN', 'POINT', 'SPOT', 'AREA')]
    cases += ['shadow_'+k for k in ('SUN', 'POINT', 'SPOT', 'AREA')]
    cases += ['reopen', 'append', 'link', 'ordinary_DITHERED', 'ordinary_BLENDED']
    if args.legacy:
        legacy_dir = args.output/'legacy_source'
        legacy_dir.mkdir()
        cmd = [str(args.legacy), '-b', '--factory-startup', '--python-exit-code', '1',
               '--python', str(Path(__file__).resolve()), '--', '--worker', 'legacy_save', '--output', str(legacy_dir)]
        with (legacy_dir/'run.log').open('w', encoding='utf8') as log:
            proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        manifest['legacy_sha256'] = sha(args.legacy)
        manifest['cases'].append({'case': 'legacy_save', 'command': cmd, 'exit_code': proc.returncode,
                                  'pass': proc.returncode == 0})
        cases.append('legacy_load')
    for case in cases:
        folder = args.output/case
        folder.mkdir()
        cmd = [str(args.blender), '-b', '--factory-startup', '--python-exit-code', '1',
               '--python', str(Path(__file__).resolve()), '--', '--worker', case, '--output', str(folder)]
        if args.startup:
            cmd += ['--startup', str(args.startup)]
        if case in ('reopen', 'append', 'link'):
            cmd += ['--fixture', str(args.output/'inherit_DITHERED_SUN/fixture.blend')]
        if case == 'legacy_load':
            cmd += ['--fixture', str(args.output/'legacy_source/legacy.blend')]
        with (folder/'run.log').open('w', encoding='utf8') as log:
            proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=900)
        result_path = folder/'result.json'
        data = json.loads(result_path.read_text(encoding='utf8')) if result_path.exists() else {}
        ok = proc.returncode == 0 and data.get('pass', False)
        manifest['cases'].append({'case': case, 'command': cmd, 'exit_code': proc.returncode, 'pass': ok})
        print(case, 'PASS' if ok else 'FAIL', flush=True)
        if not ok:
            print((folder/'run.log').read_text(encoding='utf8', errors='replace')[-1800:], flush=True)
    if args.reference:
        manifest['reference_sha256'] = sha(args.reference)
        for method in ('DITHERED', 'BLENDED'):
            case = 'ordinary_'+method
            folder = args.output/('reference_'+case); folder.mkdir()
            cmd = [str(args.reference), '-b', '--factory-startup', '--python-exit-code', '1',
                   '--python', str(Path(__file__).resolve()), '--', '--worker', case, '--output', str(folder)]
            with (folder/'run.log').open('w', encoding='utf8') as log:
                proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=900)
            old_path = folder/'result.json'
            new_path = args.output/case/'result.json'
            old = json.loads(old_path.read_text(encoding='utf8')) if old_path.exists() else {}
            new = json.loads(new_path.read_text(encoding='utf8')) if new_path.exists() else {}
            ok = proc.returncode == 0 and old.get('pass') and new.get('pass') and all(
                old['pixels'][n]['sha256'] == new['pixels'][n]['sha256']
                for n in ('ordinary', 'modern_alpha'))
            manifest['cases'].append({'case': 'reference_'+case, 'command': cmd,
                                      'exit_code': proc.returncode, 'pass': bool(ok)})
            print('reference_'+case, 'PASS' if ok else 'FAIL', flush=True)
            migrated = args.output/('old_port_'+method)
            migrated.mkdir()
            cmd = [str(args.blender), '-b', '--factory-startup', '--python-exit-code', '1',
                   '--python', str(Path(__file__).resolve()), '--', '--worker', 'old_port_load',
                   '--fixture', str(folder/'final.blend'), '--output', str(migrated)]
            if args.startup:
                cmd += ['--startup', str(args.startup)]
            with (migrated/'run.log').open('w', encoding='utf8') as log:
                proc = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=900)
            path = migrated/'result.json'
            data = json.loads(path.read_text(encoding='utf8')) if path.exists() else {}
            ok = (proc.returncode == 0 and data.get('pass') and old.get('pass') and
                  data['pixels']['modern_alpha']['sha256'] == old['pixels']['modern_alpha']['sha256'])
            manifest['cases'].append({'case': 'old_port_'+method, 'command': cmd,
                                      'exit_code': proc.returncode, 'pass': bool(ok)})
            print('old_port_'+method, 'PASS' if ok else 'FAIL', flush=True)
    manifest['files'] = {str(p.relative_to(args.output)): sha(p) for p in args.output.rglob('*') if p.is_file()}
    manifest['pass'] = version.returncode == 0 and all(c['pass'] for c in manifest['cases'])
    manifest['finished'] = time.time()
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    print('GOO_LIGHT_GROUPS_MATRIX_PASS' if manifest['pass'] else 'GOO_LIGHT_GROUPS_MATRIX_FAIL')
    sys.exit(0 if manifest['pass'] else 1)


if __name__ == '__main__':
    main()
