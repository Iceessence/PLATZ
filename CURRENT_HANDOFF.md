# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **070998f12effba051a4bca2fb4227bd47cf0baa9**. The next documentation commit records this receipt. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier verified milestones are preserved in Git. Material slots were tested at 3735df0 and documented at 867f127. Independent release acceptance remains pending.

## Active implementation

Static GLB assets can render embedded PNG/JPEG base-color textures through UV0. The importer supports float and normalized unsigned byte/short texture coordinates. Vulkan samples the source as sRGB and combines the linear sample with the base-color factor or per-object tint. GLB wrap modes and nearest/linear base-level filtering are supported. Sampling uses one mip level. Materials remain opaque. External images, secondary UV sets, texture transforms, additional maps, node transforms, animation, and full PBR remain outside this slice.

Under Material in the Inspector, Use imported texture switches the selected slot between its embedded image and flat base color. The Inspector shows the available image dimensions or the reason for flat fallback. Toggle changes are per instance and participate in typed edits, Undo/Redo, Play isolation, duplication, and save/reopen. Replacing the mesh establishes a new mapping. Prior projects keep their flat appearance and can enable supported textures manually.

Schema v6 records disabled slots per retained revision. The exact encoded images stay in the immutable, digest-checked GLB in SQLite. WIC reads only embedded bytes. PNG chunk checksums and dimensions are checked before the codec call. Unsupported or corrupt images use flat base color, with source diagnostics in the Inspector or decode fallback counts in GPU diagnostics. Texture limits are 1024 pixels per side, 64 active source/sampler bindings, and 64 MiB decoded RGBA per scene. The existing 2 MiB per GLB and 8 MiB library source limits still apply. GPU upload creates a texture batch when active bindings change and retires the old batch after waiting for the transfer fence.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **3475D3EE342F8D565D0129E5A7ACDCF3112F888B226F1823C2788F5FA1604C98**.

A clean Release build passed all seven CTest suites. The suite launched and closed the current Windows executable twice. Verbose evidence is in build/texture-delivery-tests.log.

| Test | Observed result |
|---|---|
| core-project | 21 scenarios. PNG and JPEG decoding, image bounds, UV0, malformed and oversized image fallback, per-instance override persistence, Undo/Redo, Play, and v5-to-v6 migration. Existing project integrity and asset tests passed. |
| shader_toolchain | Deterministic Slang output, validated SPIR-V, uniform and texture layout reflection, include rebuild, and last-known-good preservation. |
| hardware_frame_readback | PNG orientation, normalized UV0 on a JPEG surface, sRGB equivalence with a flat albedo, unchanged adjacent slot pixels and object IDs, texture retirement and re-upload, and a corrupt-PNG flat fallback. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks under GPU-assisted validation. Zero errors. Existing loader overlay warnings remain. |
| validation_scene_profile | Seven objects, two base-color textures, 1280×720 SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 0.1814/8.4418 ms. GPU p50/p95 0.0201/0.0219 ms. |
| studio_edit_play_reopen | Inspector texture toggle, per-slot isolation, undo and redo, Play, and pixel-identical reopen after deletion of the disposable source GLB. Baseline material, asset library, and scene tests passed. |
| studio_process_restart | Actual PLATZ.exe save, shutdown, restart, and stable-ID recovery. Permanent validation project bytes remained intact during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 12. **Harbor signs** adds one object with two slots, **Signal flags** (PNG) and **Harbor mark** (JPEG). The prior six object IDs remain intact. SHA-256 is **D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894**.

## Continuation

Add sRGB-aware mip chains and stable texture residency so textures remain consistent at a distance and unchanged textures can stay resident across slot edits. Follow with project-managed image assignment and additional material channels after qualifying asset lifetime and persistence. Qualify durable ordering, integrity, and reclamation before introducing external bundles for larger assets. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training and inference remain future work. Device loss/OOM stress, sustained large scenes, full PBR, ray tracing, other OSes, general scripting, accessibility breadth, and GPU contention with ML remain unqualified.
