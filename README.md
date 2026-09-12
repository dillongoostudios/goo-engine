<!--
Keep this document short & concise,
linking to external resources instead of including content in-line.
See 'release/text/readme.html' for the end user read-me.
-->

Blender
=======

Blender is the free and open source 3D creation suite.
It supports the entirety of the 3D pipeline—modeling, rigging, animation, simulation, rendering, compositing,
motion tracking and video editing.

![Blender screenshot](https://code.blender.org/wp-content/uploads/2018/12/springrg.jpg "Blender screenshot")

Project Pages
-------------

- [Main Website](https://www.blender.org)
- [Reference Manual](https://docs.blender.org/manual/en/latest/index.html)
- [User Community](https://www.blender.org/community/)

Development
-----------

- [Build Instructions](https://developer.blender.org/docs/handbook/building_blender/)
- [Code Review & Bug Tracker](https://projects.blender.org)
- [Developer Forum](https://devtalk.blender.org)
- [Developer Documentation](https://developer.blender.org/docs/)


License
-------

Blender as a whole is licensed under the GNU General Public License, Version 3.
Individual files may have a different but compatible license.

See [blender.org/about/license](https://www.blender.org/about/license) for details.


Goo Engine 5.2 Port
===================

This repository is an **unofficial port of Goo Engine to Blender 5.2 and EEVEE-Next**.

Goo Engine is an NPR (non-photorealistic / toon rendering) focused fork of Blender
created and maintained by **DillonGoo Studios**. The original project is based on
Blender 4.4.x and the legacy EEVEE render engine:

- Original repository: https://github.com/dillongoostudios/goo-engine
- Original authors: DillonGoo Studios and the Goo Engine contributors
- Studio website: https://www.dillongoo.com

This port takes the Goo Engine v4.4-release feature set and re-implements it on top of
official Blender 5.2.1 (EEVEE-Next render engine, BSL shader pipeline). This repository
is **not affiliated with the Blender Foundation or DillonGoo Studios**. All code remains
under the GNU General Public License.

Upstream Maintenance Baseline (2026-09-12)
------------------------------------------

The port incorporates the complete official Blender **v5.2.1** maintenance release
(`9e2066aef7ef7e20c142ad7bd3303138a4304c93`), preserving the Goo additions on top of
v5.2.0. The program patch version is 5.2.1; the fork file subversion remains
**502.49**, so this update does not downgrade or reclassify existing Goo data.

The declared release validation scope is the Windows full build, existing Goo
regression matrices, and extracted-package checks. Dedicated upstream-fix and
general-compatibility fixtures (stage D of the update plan) are explicitly
**not run, at the user's request**. Do not interpret the maintenance merge as
independent verification of every upstream fix, GPU driver or platform.

What Was Ported and How
-----------------------

- **13 Goo shader nodes**, with their GLSL re-implemented for the EEVEE-Next BSL pipeline:
  Shader Info, Screenspace Info, SDF Primitive, SDF Op, SDF Vector Op, SDF Noise,
  Set Depth, Curvature, Light Info, Hexagon Texture, Twirl, Water Ripples,
  and OKLab Color Ramp.
- **Set Depth** rewritten for the reverse-Z depth convention of EEVEE-Next.
- **Screenspace Info**: with scene Ray Tracing and material Raytraced Transmission enabled,
  Scene Color/Depth use lazy, immutable current-sample camera snapshots. This also works for
  opaque direct-color/emission and Shader-to-RGB materials, without fake transparency or
  diffuse closures. Deferred/Hybrid sample after ordinary opaque geometry; Forward color
  samples after opaque refraction and World, before volume resolve. The private deferred
  World fill cannot write AOV/Environment passes. Both outputs honor linked View Position;
  depth handles reverse-Z, color is filtered/mipmapped, and camera snapshots are never
  exposed to probe/shadow/volume/world shaders. No consumers means no full-size snapshot,
  copy, mip generation or extra World draw. Non-refraction Scene Depth keeps its Hi-Z path.
- **Curvature** ported with the original 8-direction sampling algorithm, and screen-space
  depth sampling aligned with Goo's output.
- **OKLab Color Ramp** aligned with Goo's render path (easing behavior and linear output).
- **Light Groups** management UI (`scripts/startup/goo_engine_light_groups.py`) together
  with Material/Light DNA/RNA extensions; Shader Info inherits its material's diffuse and
  shadow masks unless **Use Own Light Groups** is enabled. Named groups, default-group and
  Ignore Shadows controls are available in Material Properties, including pinned materials.
  Ordinary surface BSDF direct lighting also obeys these material groups in Deferred,
  Forward and Hybrid, including reflection, coat, SSS and transmission lobes. Full 128-bit
  masks use OR membership; Ignore Shadows is receiver-only and gates VSM and legacy contact,
  never caster visibility. Native light linking remains an independent eligibility gate.
  A deduplicated GPU table and optional G-buffer group index carry per-material masks;
  Shader Info own-mask overrides remain independent. Shadow-ID is not redesigned.
  Shader Add menu regrouped into Goo categories.
- **Legacy material semantics** restored through file versioning:
  - `MA_LEGACY_OPAQUE` preserves opaque external coverage while Transparent BSDF weights
    still participate in legacy internal alpha-reciprocal closure/emission recovery.
    The 502.48 migration clears and recomputes the flag using legacy EEVEE provenance,
    the Goo 4.4 DNA fingerprint or recursive Goo node groups, never the old SOLID value
    alone. RNA reads report OPAQUE without side effects; explicitly changing blend/render
    method exits compatibility mode. Native EEVEE-Next transparent cards remain modern.
  - Legacy **Shadow Mode = None** (`blend_shadow`) is honored at render time so shadow
    proxy workflows from Goo scenes keep working.
  - Files older than Blender 2.80 (whose SDNA lacks `Material::blend_shadow`) are
    backfilled to the solid default via an SDNA member-existence check, preventing
    the zero-filled value from silently disabling all shadow casting.
- **Highlight (HL) fix**: purely geometric highlights were being culled by the light
  attenuation gate and light culling; fixed with a bridged gate.
- **Forward pipeline light cap** raised to 512 visible lights per fragment.
- **Contact shadows** bridged to EEVEE-Next equivalents; legacy **Bloom migrated to Glare**
  through versioning; a file browser crash fixed.
- **Build configuration**: `WITH_INTERNATIONAL`, `WITH_INPUT_IME` and `WITH_BULLET`
  enabled (the lite profile shipped with these off, which broke language switching,
  IME input and rigid body physics).

Known Boundaries and Their Handling
-----------------------------------

- **Visible light count per fragment**: 128/256/512 verified working; 1024 fails on the
  tested hardware. The cap is set to 512.
- **Light resource access in material nodes**: EEVEE-Next does not expose light data to
  material shaders in every pass; the affected Goo nodes degrade gracefully per pass.
- **BSL limitation**: conditional attributes/parameters (`#if`) are not supported inside
  fragment signatures by the shader tooling; code is organized around unconditional
  signatures.
- **"Check Self Shadowing" (`check_shadow_id`)** is implemented for reflected EEVEE
  surface lighting with full 32-bit Draw Manager resource IDs. A lazily allocated R32UI
  sidecar mirrors the virtual-shadow-map atlas pages; a depth pass followed by an ID
  resolve pass records the nearest caster without changing the depth atlas. Filtering is
  applied to every soft-shadow tracing sample: ordinary surface lighting and Shader Info
  **Cast Shadows** ignore same-object casters, while Shader Info **Self Shadows** keeps
  only same-object casters. Transmission, translucent-thickness lighting, volumes,
  surfels, probes and baking deliberately keep identity filtering disabled (independent
  of the material Light Groups controls described below).
- **`blend_shadow` zero-value ambiguity (residual)**: a full fix via a dedicated
  `MA_LEGACY_NO_SHADOW` flag (file-level Goo fingerprint at load, flag read at render)
  has been designed but not implemented. Known corner case: appending a shadow-None
  material that itself contains no Goo nodes into a scene without any Goo nodes will
  make it cast shadows.
- **RNA re-entrancy deadlock**: `property_pointer_get` (rna_access.cc) can self-deadlock
  on a non-recursive mutex when an ID-type IDProperty group is overwritten with a flat
  value and later resolved. The engine defect is documented; callers must write geometry
  nodes modifier inputs as `inputs[identifier]["value"] = value` (never overwrite the
  `{value, type}` group), which avoids the code path entirely.
- **File subversions 502.45-49** are used by this fork. If a future official 5.2.x LTS
  release uses the same subversion numbers for its own versioning, files saved by this
  fork could skip those official versioning blocks.

Disclaimer
----------

This is an experimental, unofficial build provided under the GPL without any warranty.
For production NPR work on Blender 4.4, use the original Goo Engine release from
DillonGoo Studios.

Material Light Groups regression
--------------------------------

The self-contained `tests/python/goo_light_groups.py` runs fresh background Blender processes
and records commands, exits, linear EXR pixel hashes, fixtures, and a manifest. Run with a
**new** output directory (requires a full build with file subversion 502.49):

```text
python tests/python/goo_light_groups.py --blender <blender.exe> --output <new-directory>
```

For an incremental developer build whose runtime scripts have not been packaged, add
`--startup <source>/scripts/startup`. The test reloads the existing startup module once;
it never registers a second copy under another name. `--reference <previous-blender.exe>`
compares ordinary EEVEE and modern transparency pixels against a previous release and checks
migration of its saved materials. `--legacy <goo-4.4-blender.exe>` additionally creates an
original Goo fixture and checks material groups, Ignore Shadows, OPAQUE provenance and roundtrip.
`GOO_LIGHT_GROUPS_MATRIX_PASS` requires every case to pass. This tests Material/Light UI
registration and operators, masks including signed bits/default-bit reservation, dynamic
material/node overrides, shared groups, multi-slot materials, Sun/Point/Spot/Area,
Cast/Self with Shadow-ID and Ignore Shadows, and save/reopen/append/link. Shader Info uses
the Hybrid path for DITHERED and Forward for BLENDED; ordinary-material controls cover the
default-group native Deferred and Forward paths.

502.49 restores the original Material mask fields (default `{0, 0, 0, 1}`). Files with
existing Goo DNA retain them; files without the fields receive the default. Named group
collections are persistent ID properties; mask fields are derived caches resynchronized
after load/import, undo/redo, and before render. This does not change 502.48's OPAQUE
provenance migration. Lights beyond the Shader Info bridge's per-fragment record limit
retain the pre-existing always-on overflow policy.

Screen Space Info regression
----------------------------

`tests/python/goo_screenspace.py` builds an isolated split-background receiver fixture in a
fresh Blender process. It requires `--output <new-directory>` after Blender's `--` separator.
The bridge's `tests/01_节点移植验证/run_screenspace_matrix.py` orchestrates independent processes,
strict non-black ROI oracles, reverse-Z depth, linked offsets, World/AOV, volume, runtime
material/global toggles, resize, probes, legacy files and unchanged ordinary-pixel controls.
Use host Python with NumPy and OpenEXR for the complete matrix. `--suite smoke` needs neither
host image library and covers six direct-color/World cases. Diagnostic value 734 reports
CPU snapshot submission counts; normal rendering performs no statistical GPU atomics.
Screen Space Info is a camera-buffer effect, not physically correct transparent geometry:
it cannot reveal off-screen surfaces, its own opaque-refraction layer, or later BLENDED
surfaces. Ordinary BSDF material groups were added subsequently; see the section below.

Ordinary BSDF Material Light Groups
----------------------------------

The receiver lighting and shadow masks now apply to all direct surface lobes. Empty masks
exclude real lamps; environment/extracted World Sun lighting remains native. Probe surface
and surfel direct capture respect material masks, but accumulated indirect probe radiance
is not filtered again. Transmission keeps Shadow-ID disabled and never receives legacy
screen-space contact shadows; its real-light membership and received VSM mask still apply.
Contact requires an opaque screen-depth occluder; transparent Forward casters do not create
one. Native LTC, attenuation and VSM math, and Shader Info normalization are unchanged.

Default index zero is the builtin `{0,0,0,1}` mask pair, not an all-lights sentinel.
Custom pairs use an SSBO and optional G-buffer header layer 2/bit 29. Default pixels skip
the extra layer/table fetch. Shadow-ID header bits 30/31 remain unchanged. Group edits
update draw data, not shader variants. A source-SDNA check initializes missing Light and
Material fields for native files (including linked data), regardless of file subversion,
without UI handlers. Existing Goo masks, including intentional empty groups, are retained.
No new DNA/RNA field or file subversion is introduced by this change.

Permanent workers: `tests/python/goo_bsdf_light_groups.py` and
`tests/python/goo_bsdf_light_groups_cases.py`. A single worker builds and validates an
isolated scene, for example:

```text
blender -b --factory-startup --python-exit-code 1 --python tests/python/goo_bsdf_light_groups.py -- --output <new-directory> --method deferred --case groups
```

Cases include groups, slots, shadows, self, contact, high_bits, table_growth, linking,
world, probe_plane, probe_sphere, volume_probe, default and modern_alpha. `--method forward|hybrid` selects other
paths. `--native-save` creates native-file oracles; `--load` and `--link` verify missing-DNA
migration without Python handlers. The companion bridge runner
`goo_to_eevee52_bridge/tests/02_灯光组验证/run_bsdf_matrix.py` runs the complete strict
matrix in fresh processes and records script/binary/artifact SHA-256 and exit codes.
The older Shader Info/UI suite remains a separate regression, not a substitute for the
ordinary BSDF suite. Planar reflection tests use SCREEN tracing and a camera-hidden
target, so neither a black PROBE fallback nor a screen-visible substitute can pass.

Resource bindings are explicit on every consuming pass, including disabled contact
branches and lookdev. Planar captures size their optional header from their own materials.
Shadow image handles are resolved at submission because end-sync may resize the ID atlas;
the two-pass ID/depth representation and tracing semantics are unchanged.
