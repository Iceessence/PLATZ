# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **c8eb2dd0c29d8ecf794131d9d3d2adfd25d3e11f**. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier verified milestones are preserved in Git. The authoring package mapping is in docs/STUDIO_AUTHORING_TRACE.md. Independent release acceptance remains pending.

## Active implementation

**Verified viewport transforms.** Studio has Select, Move, Rotate and Scale modes, World and Local axes for Move and Rotate, and a per-mode snap increment. Scale uses the local axes. The Vulkan overlay writes only scene color and preserves the existing depth-based selection and ID attachments. Shortcuts 1–4 apply while the viewport has focus. A drag stages a typed authoring edit for the selected stable ID and exact project revision. Preview frames replace one projected object, and the Inspector follows the draft. Release applies one undoable edit. Escape, capture loss, and Play discard an in-progress viewport drag.

**Verified live Inspector edits.** Typing valid transform values or hex colors updates the viewport, including asset slot tint. Dragging an axis label previews numeric changes with optional Shift precision. The color picker previews adjustments and accepts only on OK. Enter or focus exit commits valid text, Escape cancels, and incomplete text restores the previous value. Step controls, texture toggles and asset assignment continue to update on commit. These changes reuse the existing project schema and per-frame mesh and texture resources.

Static GLB assets can render embedded PNG/JPEG base-color textures through UV0. The importer supports float and normalized unsigned byte/short texture coordinates. Vulkan samples the source as sRGB and combines the linear sample with the base-color factor or per-object tint. GLB wrap modes and nearest/linear base-level filtering are supported. Sampling uses one mip level. Materials remain opaque. External images, secondary UV sets, texture transforms, additional maps, node transforms, animation, and full PBR remain outside this slice.

Under Material in the Inspector, Use imported texture switches the selected slot between its embedded image and flat base color. The Inspector shows the available image dimensions or the reason for flat fallback. Toggle changes are per instance and participate in typed edits, Undo/Redo, Play isolation, duplication, and save/reopen. Replacing the mesh establishes a new mapping. Prior projects keep their flat appearance and can enable supported textures manually.

Schema v6 records disabled slots per retained revision. The exact encoded images stay in the immutable, digest-checked GLB in SQLite. WIC reads only embedded bytes. PNG chunk checksums and dimensions are checked before the codec call. Unsupported or corrupt images use flat base color, with source diagnostics in the Inspector or decode fallback counts in GPU diagnostics. Texture limits are 1024 pixels per side, 64 active source/sampler bindings, and 64 MiB decoded RGBA per scene. The existing 2 MiB per GLB and 8 MiB library source limits still apply. GPU upload creates a texture batch when active bindings change and retires the old batch after waiting for the transfer fence.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **15775220F98CD510250DC9D16C7C0CE14AF9A0CA7405B8DDD2CF3F11862955DE**.

On November 21, 2026, `& .\tools\build.ps1 -Test` configured and built Release and passed all eight CTest cases, with zero failed or skipped. Current logs are build/studio-authoring-final.log and build/Testing/Temporary/LastTest.log. The native executable also opened a disposable validation copy for a physical-window check. Moving Silver block along X changed its position from 1.620 to 2.483. The saved copy reopened at revision 13, with its authored rotation restored after turning the object in Play. Save original GLB exported Harbor signs with the same SHA-256 as the original fixture GLB. The screenshot is build/studio-authoring-evidence/studio-viewport-move.jpg.

| Test | Observed result |
|---|---|
| core-project | 21 scenarios. PNG and JPEG decoding, image bounds, UV0, malformed and oversized image fallback, per-instance override persistence, Undo/Redo, Play, and v5-to-v6 migration. Existing project integrity and asset tests passed. |
| shader_toolchain | Deterministic Slang output, validated SPIR-V, uniform and texture layout reflection, include rebuild, and last-known-good preservation. |
| hardware_frame_readback | Vulkan overlay changed color pixels while preserving every object ID. PNG/JPEG, texture retirement and re-upload, and corrupt-image flat fallback passed. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks with GPU-assisted validation. Zero errors. Existing Vulkan loader warnings about installed capture overlays remain. |
| validation_scene_profile | Seven objects, two base-color textures, 1280×720 SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 changed from 0.1814/8.4418 to 0.1628/8.3606 ms. GPU p50/p95 changed from 0.0201/0.0219 to 0.0217/0.0223 ms. This profile leaves manipulators inactive and does not establish a performance improvement. |
| studio_edit_play_reopen | Real Studio widgets and Vulkan cover signed snap math, local axes, nonuniform scale, Move/Rotate/Scale, shader color overlay, live typing, scrubbing, picker accept/reject, one-step Undo, cancellation, Play, save and pixel-identical reopen. Existing project and asset workflows passed. |
| studio_transform_highdpi | Repeated pointer interactions and persistence under synthetic Qt 150% scale and tested lost release and Escape. |
| studio_process_restart | Actual PLATZ.exe save, shutdown, restart, and stable-ID recovery. Permanent validation project bytes remained intact during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 12. **Harbor signs** adds one object with two slots, **Signal flags** (PNG) and **Harbor mark** (JPEG). The prior six object IDs remain intact. SHA-256 is **D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894**.

Single drag update samples through synchronous Vulkan readback were 6.43 ms at DPR 1 and 7.54 ms at DPR 1.5. These include test capture and do not measure display latency. Physical mixed-DPI monitor transitions, native-device loss injection, accessibility breadth, sustained large scenes, 1080p and higher-resolution workloads, and interactive CPU/GPU/VRAM distributions remain unqualified.

## Continuation

Build reusable native cube, sphere and cylinder sources first, with live parameters, dependency-safe updates, persistence, Undo and generated GLB export. Follow with native texture-source editing and material assignment. Then add named workspace layouts and a legible capital P icon, and profile larger authoring workloads before changing GPU resource lifetimes. Qualify sRGB mip chains and stable texture residency alongside native textures. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training, full PBR, ray tracing, other operating systems, and general scripting remain future work.
