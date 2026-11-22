# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **972a69427a7d63245b0feba37685c64c09b76c29**. Native mesh implementation landed at b45e3e3. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier verified milestones are preserved in Git. The authoring package mapping is in docs/STUDIO_AUTHORING_TRACE.md. Independent release acceptance remains pending.

## Active implementation

**Verified native mesh authoring.** Create, edit and reuse cube, sphere and cylinder assets in Studio. Cube supports 1–24 divisions per face edge. Sphere supports 3–128 radial and 2–64 latitude divisions. Cylinder supports 3–128 radial and 1–32 height divisions, with top diameters from 0.1 to 2 times the base. Shape dimensions are bounded to 0.01–100 m. Each shape generates positions, normals, UV0 and indexed triangles. Creation can place an instance or leave the asset in the library. Shared edits preview on all linked objects and commit in one Undo step. Instance transforms and tint are preserved. Rename, duplicate, assign, make unique, usage counts and Locate Source work through the Assets dock and Inspector.

Schema v7 retains editable recipe parameters and stable native IDs per saved revision. Native source and imported GLB assets coexist. Changes in derived geometry replace the viewport atlas while keeping source identity. GPU preview failures disable Apply and restore the authored scene for renderer retry. Export writes the derived GLB geometry. That file reimports as static geometry, while the recipe, transforms and material overrides stay in the .platz project. The native and imported meshes share an 8 MiB library budget. Changed saves with a native asset require a v7-capable build.

**Verified viewport transforms.** Studio has Select, Move, Rotate and Scale modes, World and Local axes for Move and Rotate, and a per-mode snap increment. Scale uses the local axes. The Vulkan overlay writes only scene color and preserves the existing depth-based selection and ID attachments. Shortcuts 1–4 apply while the viewport has focus. A drag stages a typed authoring edit for the selected stable ID and exact project revision. Preview frames replace one projected object, and the Inspector follows the draft. Release applies one undoable edit. Escape, capture loss, and Play discard an in-progress viewport drag.

**Verified live Inspector edits.** Typing valid transform values or hex colors updates the viewport, including asset slot tint. Dragging an axis label previews numeric changes with optional Shift precision. The color picker previews adjustments and accepts only on OK. Enter or focus exit commits valid text, Escape cancels, and incomplete text restores the previous value. Step controls, texture toggles and asset assignment continue to update on commit. These changes reuse the existing project schema and per-frame mesh and texture resources.

Static GLB assets can render embedded PNG/JPEG base-color textures through UV0. The importer supports float and normalized unsigned byte/short texture coordinates. Vulkan samples the source as sRGB and combines the linear sample with the base-color factor or per-object tint. GLB wrap modes and nearest/linear base-level filtering are supported. Sampling uses one mip level. Materials remain opaque. External images, secondary UV sets, texture transforms, additional maps, node transforms, animation, and full PBR remain outside this slice.

Under Material in the Inspector, Use imported texture switches the selected slot between its embedded image and flat base color. The Inspector shows the available image dimensions or the reason for flat fallback. Toggle changes are per instance and participate in typed edits, Undo/Redo, Play isolation, duplication, and save/reopen. Replacing the mesh establishes a new mapping. Prior projects keep their flat appearance and can enable supported textures manually.

Schema v6 records disabled slots per retained revision. The exact encoded images stay in the immutable, digest-checked GLB in SQLite. WIC reads only embedded bytes. PNG chunk checksums and dimensions are checked before the codec call. Unsupported or corrupt images use flat base color, with source diagnostics in the Inspector or decode fallback counts in GPU diagnostics. Texture limits are 1024 pixels per side, 64 active source/sampler bindings, and 64 MiB decoded RGBA per scene. The existing 2 MiB per GLB and 8 MiB library source limits still apply. GPU upload creates a texture batch when active bindings change and retires the old batch after waiting for the transfer fence.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **241BE2E0565DF09EB276D1DD3D6E0316CBF3E1999FA01642231BCDE1753C02E2**.

On November 21, 2026, `& .\tools\build.ps1 -Test` configured and built Release and passed all nine CTest cases, with zero failed or skipped. Current logs are build/native-geometry-final.log and build/Testing/Temporary/LastTest.log. The Release application created a cylinder on a disposable validation copy. After saving and restarting at revision 13, Studio reopened the recipe with height 1.600 m, top diameter multiplier 0.400 and 32 radial segments. Screenshots and the editable copy are in build/native-geometry-evidence.

The initial crash test accepted any nonzero process exit. The final test runs the child via the existing Windows process launcher and requires exit 73 after SQLite flushes its open transaction. A high-detail GLB regression reproduced a legacy SQLite row limit that rejected valid exports at save. The final build sizes the row limit for the 2 MiB asset contract and verified that a dense native sphere can reimport, save, and reopen byte for byte.

| Test | Observed result |
|---|---|
| core-project | 26 scenarios. Native topology, winding and UV0, high-detail export/reimport, linked edit and detach, v6-to-v7 migration, unknown recipe rejection and interrupted-write rollback. Existing project, imported GLB and texture checks passed. |
| shader_toolchain | Deterministic Slang output, validated SPIR-V, uniform and texture layout reflection, include rebuild, and last-known-good preservation. |
| hardware_frame_readback | Native taper changed coverage and picked the same object after atlas replacement. Overlay preservation, PNG/JPEG and corrupt-image flat fallback passed. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks with GPU-assisted validation. Zero errors. Existing Vulkan loader warnings about installed capture overlays remain. |
| validation_scene_profile | Seven objects, two base-color textures, 1280×720 SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 changed from 0.1628/8.3606 to 0.1703/8.3509 ms. GPU p50/p95 changed from 0.0217/0.0223 to 0.0216/0.0223 ms. This baseline profile uses no native meshes. |
| studio_edit_play_reopen | Real Studio widgets and Vulkan cover signed snap math, local axes, nonuniform scale, Move/Rotate/Scale, shader color overlay, live typing, scrubbing, picker accept/reject, one-step Undo, cancellation, Play, save and pixel-identical reopen. Existing project and asset workflows passed. |
| studio_transform_highdpi | Repeated pointer interactions and persistence under synthetic Qt 150% scale and tested lost release and Escape. |
| studio_native_geometry | Live creation and cancellation, linked edit, asset rename, copy, detach, Locate Source, one-step Undo/Redo, Play, reimportable GLB export, dense library-only sphere and v7 reopen. |
| studio_process_restart | Actual PLATZ.exe save, shutdown, restart, and stable-ID recovery. Permanent validation project bytes remained intact during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 12. **Harbor signs** adds one object with two slots, **Signal flags** (PNG) and **Harbor mark** (JPEG). The prior six object IDs remain intact. SHA-256 is **D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894**.

Four native preview updates took 50–61 ms through synchronous Vulkan capture, including the 45 ms input debounce. Samples include capped maximum sphere tessellation and edits to two linked instances. Display latency and sustained CPU, GPU and VRAM under large-scene edit workloads remain unqualified. Physical mixed-DPI monitor transitions, device loss injection, accessibility breadth and higher-resolution workloads also remain unqualified.

## Continuation

Next build editable RGBA image sources, material-slot assignment, and lossless PNG export. Qualify sRGB mip chains and texture residency alongside that work. Follow with direct mesh editing beyond shape parameters, named workspace layouts and a legible capital P icon. Profile larger authoring workloads before changing GPU resource lifetimes. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training, full PBR, ray tracing, other operating systems and general scripting remain future work.
