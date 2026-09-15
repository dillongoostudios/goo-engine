<!--
Fork-specific README. See COPYING for licensing (GPL, inherited from Blender).
-->

Goo Engine 4.4.3 — Apple Metal (MSL) Port
=========================================

An **unofficial** port of [Goo Engine](https://github.com/dillongoostudios/goo-engine) 4.4.3 —
the NPR / anime-focused Blender fork by [DillonGoo Studios](https://www.youtube.com/dillongoo) —
to the **Apple Metal** backend, so that it runs on Apple Silicon Macs.

Goo Engine v4.4 was written against OpenGL. On macOS it crashed at startup on many scenes and, where
it did run, rendered shadows incorrectly. Blender's `mtl_shader_generator` translates GLSL to MSL
automatically, but a number of things it cannot translate had to be fixed by hand. This branch is
that work.

Status: **working, and verified against Windows/OpenGL to pixel parity** (see below).

| | |
|---|---|
| Base | `goo-engine-v4.4-release` (commit `1d30f5bdfe0`) |
| Branch | `metal-port-v4.4.3` |
| Scope | 48 files under `source/`, +1507 / −369 |
| Platform | Apple Silicon (arm64), macOS 13+ |
| Generator | `mtl_shader_generator.mm` is **not** modified |

Successor to `metal_support_v2.patch` ([goo-engine issue #114](https://github.com/dillongoostudios/goo-engine/issues/114)).

What this fixes
---------------

### Crashes

- **Scenes with Volume objects crashed at startup.** Metal has no geometry shaders. The `_no_geom`
  shader variants are now selected on Metal for the volumetrics, lightprobe and downsample passes.
- **The Set Depth node segfaulted.** The second switch in the weight-tree invert path was missing
  `SH_NODE_SET_DEPTH`, so a RELEASE build (asserts compiled out) handed an uninitialized node and
  socket to `node_add_link`. This one is not Metal-specific — it affects OpenGL builds too.
- **Draw-time crash on heavy NPR materials.** The pipeline-state bake path dereferenced
  `shd_builder_` after it had been freed at finalize, so even a *benign* "Compilation succeeded with
  warnings" from `newFunctionWithName:constantValues:error:` killed the process mid-frame.
- **Sampler argument buffer overflowed bind index 31** on face materials with many textures. The
  unused null-attribute buffer slot is now reclaimed, for the affected shader only.
- SSR null-shader crash on UI interaction, and SDF non-const-reference-to-vector-element MSL compile
  failures (magenta materials: 102 → 0).

### Rendering correctness

- **Texture binding lifetime — the big one.** Several Metal-only defensive fallbacks bound textures
  *by value* at shading-group setup time, because the texture pool was still `NULL` at that point. A
  value bind never updates, so the shader went on sampling a stale or dummy texture forever. This
  affected the shadow cube/cascade pools (causing trapezoidal self-shadow acne), the shadow-ID pools,
  and the volumetric scattering/transmittance textures. All are now unconditional `_ref` binds,
  matching the OpenGL path.
- **Shadow-ID self-shadow suppression was dead.** The material shadow variants had lost their
  `resource_id_out` writer, so the ID pool stayed permanently zero and same-object self-shadow
  suppression never ran — every self-shadow that Windows suppresses by design was visible on Mac.
  The writer is restored.
- **Shadow depth range was double-converted.** A C++-side projection-matrix `[0,1]` correction
  collided with the generator's automatic `z = (z+w)/2`, compressing shadow maps into `[0.5, 1.0]`.
- The depth prepass `out_normal` was moved to its correct attachment location (RG16), fixing 64 of 65
  failing pipeline states.

Verification
------------

Measured on macOS 26.5.2, Apple Silicon, RELEASE build, against Windows/OpenGL reference renders.

- **Pixel parity:** 8 NPR character scenes (2 characters × 4 frames) — mean difference **0.00008**,
  where 1 LSB (8-bit) = 0.00392. Over-shadowing 0.00004, light leak 0.00003. Cast shadows intact
  (Mac/Windows ratio 1.018).
- **Node coverage:** 105 Goo-Engine-specific node and SDF variants compared against Windows
  references — 104 below 1 LSB. The single outlier (SDF `STAR_2D`) is a degenerate default-parameter
  case that renders garbage on *both* platforms (`inradius = 1` hits a `1/cos(π/2)` singularity), not
  a Metal defect.
- **Smoke test:** 9 real-world NPR character `.blend` files (Wuthering Waves / Honkai: Star Rail /
  Zenless Zone Zero shader setups) open and render headless with no crashes, no shader-compile errors
  and no Metal validation errors.

Building
--------

A standard Blender build. It needs the precompiled libraries for macOS arm64:

```sh
git clone https://github.com/Kazoo512/goo-engine-v4.4.3-metal.git
cd goo-engine-v4.4.3-metal
make update
make
```

If `make update` fails on the GitHub Git LFS quota, point LFS at Blender's own server first:

```sh
git config lfs.url "https://projects.blender.org/blender/blender.git/info/lfs"
```

Notes for reviewers
-------------------

Metal-specific code is guarded with `GPU_METAL` (GLSL), `WITH_METAL_BACKEND` (shader-info `.hh`), or
`WITH_METAL_BACKEND` + `GPU_backend_get_type() == GPU_BACKEND_METAL` (C++). There are no new
`#ifdef __APPLE__` guards, and the OpenGL path is unchanged: every behavioural edit is either inside
a Metal guard, or a restoration of upstream-equivalent behaviour (the shadow-ID writer). A Windows
regression re-render would be a welcome confirmation — I have not been able to run one.

Some intentionally-retained, environment-variable-gated debug scaffolding is included (sampler
diagnostics, GPU capture anchors). Happy to strip it for a merge-ready patch on request.

Credits & License
-----------------

All credit for Goo Engine itself belongs to [DillonGoo Studios](https://www.youtube.com/dillongoo)
and its maintainer CodyWinch, and to the Blender Foundation for Blender. This fork only adds Metal
backend support.

- Goo Engine: <https://github.com/dillongoostudios/goo-engine> · [Patreon](https://www.patreon.com/dillongoo)
- Blender: <https://www.blender.org>

Blender as a whole is licensed under the **GNU General Public License, Version 3**; individual files
may carry a different but compatible license. This fork inherits that license. See `COPYING` and
[blender.org/about/license](https://www.blender.org/about/license).
