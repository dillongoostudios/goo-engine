# SPDX-FileCopyrightText: 2026 Blender Authors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Additional ordinary surface group cases, shared by the main fixture."""
import bpy
import numpy as np


def run_extra(a, s, receiver, ma, key, fill, groups, render, rgb, check, make_material):
    if a.case == 'high_bits':
        # Keep the complete namespace alive while changing only participating receivers/lights.
        groups(ma, [])
        groups(key.data, [])
        groups(fill.data, [])
        reservoir = bpy.data.lights.new('Namespace', 'SUN')
        reservoir.use_fake_user = True
        groups(reservoir, ['G%03d' % i for i in range(127)])
        fill.hide_render = True
        for i in (30, 62, 94, 126):
            name = 'G%03d' % i
            groups(ma, [name])
            groups(key.data, [name])
            matched = render('bit_%d' % (i + 1))
            check('signed_word_%d' % i, ma.light_group_bits[i // 32] == -2147483648)
            check('matched_%d' % i, rgb(matched)[:, :, 0].mean() > .001)
            groups(key.data, ['G%03d' % (i - 1)])
            mismatch = render('mismatch_%d' % i)
            check('excluded_%d' % i, rgb(mismatch).max() < 1e-4)
    elif a.case == 'table_growth':
        mats, verts, faces = [], [], []
        for i in range(40):
            x, y = (i % 8) * .5 - 2, (i // 8) * .5 - 1.25
            j = len(verts)
            verts.extend([(x,y,0),(x+.5,y,0),(x+.5,y+.5,0),(x,y+.5,0)])
            faces.append((j,j+1,j+2,j+3))
            m = make_material('Cell%02d' % i)
            groups(m, ['Cell%02d' % i])
            mats.append(m)
        groups(key.data, ['Cell%02d' % i for i in range(0,40,2)])
        groups(fill.data, ['Cell%02d' % i for i in range(1,40,2)])
        mesh = bpy.data.meshes.new('FortyGroupPairs')
        mesh.from_pydata(verts, [], faces)
        receiver.data = mesh
        for i,m in enumerate(mats):
            mesh.materials.append(m)
            mesh.polygons[i].material_index = i
        image = render('forty_materials')
        for i in range(40):
            x,y = 4+(i%8)*8, 16+(i//8)*8
            v = image[y-1:y+1,x-1:x+1,:3].mean(axis=(0,1))
            keep, drop = (0,2) if i%2 == 0 else (2,0)
            check('cell_%02d' % i, v[keep] > .001 and v[drop] < 1e-4)
        groups(mats[0], ['Cell01'])
        altered = render('table_reindex')
        check('first_cell_changed', altered[15:17,3:5,2].mean() > .001 and altered[15:17,3:5,0].max() < 1e-4)
        groups(mats[0], ['Cell00'])
        check('table_restore', np.array_equal(render('table_restore'), image))
    elif a.case == 'linking':
        allowed = bpy.data.collections.new('AllowedReceivers')
        allowed.objects.link(receiver)
        allowed.collection_objects[0].light_linking.link_state = 'EXCLUDE'
        key.light_linking.receiver_collection = allowed
        excluded = render('link_excluded')
        check('linking_excludes_red', rgb(excluded)[:,:,0].max() < 1e-4 and rgb(excluded)[:,:,2].mean() > .001)
        allowed.collection_objects[0].light_linking.link_state = 'INCLUDE'
        included = render('link_included')
        check('linking_includes_red', rgb(included)[:,:,0].mean() > .001)
        groups(ma, ['Fill'])
        filtered = render('groups_and_linking')
        check('groups_still_filter', rgb(filtered)[:,:,0].max() < 1e-4 and rgb(filtered)[:,:,2].mean() > .001)
    elif a.case == 'world':
        key.hide_render = fill.hide_render = True
        nt = s.world.node_tree
        sky = nt.nodes.new('ShaderNodeTexSky')
        sky.sky_type = 'SINGLE_SCATTERING'
        sky.sun_elevation = 1.2
        sky.sun_rotation = 0
        nt.links.new(sky.outputs[0], nt.nodes.get('Background').inputs[0])
        s.world.sun_threshold = 0.1
        s.world.use_sun_shadow = False
        env = render('world_groups')
        check('world_positive', rgb(env).mean() > .001)
        groups(ma, [])
        empty = render('world_empty_groups')
        check('environment_not_filtered', np.allclose(empty, env, atol=1e-5, rtol=1e-5))
    elif a.case in ('probe_plane', 'probe_sphere'):
        # Camera Deferred stays default; the only custom mask belongs to a camera-hidden
        # Forward target. Planar capture must allocate its own optional header layer.
        from mathutils import Vector
        s.eevee.use_raytracing = True
        # Deferred planar traces run only in SCREEN mode. Hide the target from the
        # camera so the screen trace cannot substitute for an actual planar capture.
        s.eevee.ray_tracing_method = 'SCREEN' if a.case == 'probe_plane' else 'PROBE'
        s.camera.location = (0,-4,4)
        s.camera.rotation_euler = Vector((0,4,-4)).to_track_quat('-Z','Y').to_euler()
        nt = ma.node_tree
        nt.nodes.clear()
        out = nt.nodes.new('ShaderNodeOutputMaterial')
        glossy = nt.nodes.new('ShaderNodeBsdfGlossy')
        glossy.inputs['Color'].default_value = (.8,.8,.8,1)
        glossy.inputs['Roughness'].default_value = 0
        nt.links.new(glossy.outputs[0], out.inputs['Surface'])
        groups(ma, default=True)
        for ob in (key, fill):
            ob.data.energy = 2
            ob.rotation_euler = Vector((0,4,-6)).to_track_quat('-Z','Y').to_euler()
        bpy.ops.mesh.primitive_cube_add(size=1.5, location=(0,1,1))
        target = bpy.context.object
        target.visible_camera = False
        target_mat = make_material('ProbeTarget', method='forward')
        target.data.materials.append(target_mat)
        kind = 'PLANE' if a.case == 'probe_plane' else 'SPHERE'
        bpy.ops.object.lightprobe_add(type=kind, location=(0,0,.05))
        probe = bpy.context.object
        probe.scale = (5,5,5)
        if kind == 'SPHERE':
            probe.data.influence_distance = 8
        groups(target_mat, ['Key','Fill'])
        all_px = render('probe_all')[8:56,8:56,:3]
        check('probe_positive_control', all_px[:,:,0].max() > .01 and all_px[:,:,2].max() > .01)
        groups(target_mat, ['Key'])
        red = render('probe_key')[8:56,8:56,:3]
        check('probe_key_only', red[:,:,0].max() > .01 and red[:,:,2].max() < 1e-4)
        groups(target_mat, ['Fill'])
        blue = render('probe_fill')[8:56,8:56,:3]
        check('probe_fill_only', blue[:,:,2].max() > .01 and blue[:,:,0].max() < 1e-4)
        groups(target_mat, [])
        check('probe_empty', render('probe_empty')[8:56,8:56,:3].max() < 1e-4)
    elif a.case == 'volume_probe':
        # Only the floor receives real lights. The empty-group sphere receives baked indirect.
        groups(ma, ['Key'])
        s.camera.location = (0,-4,3)
        from mathutils import Vector
        s.camera.rotation_euler = (Vector((0,0,1))-s.camera.location).to_track_quat('-Z','Y').to_euler()
        bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, radius=.65, location=(0,0,1))
        sphere = bpy.context.object
        sphere_mat = make_material('IndirectReceiver')
        groups(sphere_mat, [])
        sphere.data.materials.append(sphere_mat)
        bpy.ops.object.lightprobe_add(type='VOLUME', location=(0,0,1))
        probe = bpy.context.object
        probe.scale = (3,3,2)
        probe.data.resolution_x = probe.data.resolution_y = probe.data.resolution_z = 4
        probe.data.bake_samples = 16
        probe.data.capture_world = False
        # The general square ROI includes floor in its corners; use a smaller sphere-only ROI.
        sphere_rgb = lambda image: image[28:36,28:36,:3]
        before = render('before_bake')
        check('sphere_no_direct', sphere_rgb(before).max() < 1e-4)
        bpy.ops.object.lightprobe_cache_bake(subset='ACTIVE')
        red = render('baked_key')
        check('surfel_key_indirect', sphere_rgb(red)[:,:,0].mean() > .001 and sphere_rgb(red)[:,:,2].max() < .001)
        groups(ma, ['Fill'])
        bpy.ops.object.lightprobe_cache_bake(subset='ACTIVE')
        blue = render('baked_fill')
        check('surfel_fill_indirect', sphere_rgb(blue)[:,:,2].mean() > .001 and sphere_rgb(blue)[:,:,0].max() < .001)
    else:
        return False
    return True
