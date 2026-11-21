# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **3735df05e2a2481e0e3c6f8aead9c5fe8736649b**. The documentation commit following it records this receipt. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier tested receipts are preserved in Git. The previous mesh library release was tested at 3726270 and documented at bfa19c9. Independent release acceptance remains pending.

## Active implementation

The GLB importer supports one static mesh with up to 32 indexed triangle primitives. Aggregate limits are 65,536 vertices and 196,608 indices. The importer reads one embedded buffer, float positions, optional float normals, and linear RGB base-color factors. It converts factors to sRGB for the existing renderer. Shared source materials share slots. Each Vulkan draw uses its primitive's index range and resolved material color. Draws for the same instance share its transformation matrices and object picking token. Node transforms, UV texture sampling, alpha blending, other PBR properties, animations, and compressed geometry remain outside this slice. GLB size limits remain 2 MiB per file and 8 MiB across the library.

The Inspector exposes a material selector, color swatch, sRGB hex input, and Reset. Edits apply to one object instance. Duplicate and Repeat copy those edits and preserve subsequent independence. Reassigning a different mesh clears the original material overrides. Undo restores the earlier mesh and colors together. The Assets library continues to track usage per scene object.

Schema v5 persists per-object slot overrides in the same transaction as the library, scene, and revision head. Both retained revisions keep their overrides. Existing v3/v4 flat-colored imported objects preserve the saved appearance through migration. PLATZ still reads v1–v5 projects. Asset projects adopt v5 on the first saved change. Slot changes pass through the validated, revision-bound typed edit boundary used for future AI integration.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **2B37AC716D8A699D3F7C1032376D5E0B43E635BF54FD0B1B3BCEF23AAB806DA9**.

A clean Release build passed all seven CTest suites. The suite launched and closed the current Windows executable twice. Verbose evidence is in build/material-delivery-tests.log.

| Test | Observed result |
|---|---|
| core-project | 19 scenarios. GLB index rebasing, shared slot IDs, sRGB conversion, invalid/stale edit rejection, per-instance overrides, undo/redo, save and recovery. v4 flat-color appearance and prior revision survived migration to v5. Baseline transaction and asset tests passed. |
| shader_toolchain | Deterministic Slang output and validated SPIR-V, material layout reflection, includes, and shader diagnostics. |
| hardware_frame_readback | Multiple indexed draws under one picking token. Shared Copper slot recolored two separated surfaces and left Lagoon pixels untouched. Synchronization validation reported zero errors. Existing resize and geometry lifetime coverage passed. |
| hardware_gpu_assisted | Same hardware checks under GPU-assisted validation. Zero errors. Existing loader overlay warnings remain. |
| validation_scene_profile | Six placed objects at 1280×720, 1×, SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 0.1492/8.4192 ms. GPU p50/p95 0.0199/0.0202 ms. |
| studio_edit_play_reopen | Inspector slot switching, color edits, reset/Undo, Play and Stop, and pixel-identical reopen. Library, GLB and scene authoring regressions passed. |
| studio_process_restart | Actual PLATZ.exe save, normal shutdown, restart, and stable-ID recovery. Permanent validation project bytes remained intact throughout the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 10. Its prior five scene IDs were retained. **Color study** adds one scene object with three primitives and two slots, **Copper** and **Lagoon**. SHA-256 is **84A868F683C8FD6E68746BCC234EAA9FE2C253AF6FA09AEE947101BAC8367D8C**.

## Continuation

Extend material slots with base-color textures. Qualify UV0 mapping, bounded image decoding, sRGB texture sampling, asset lifetime, and project persistence. Qualify durable ordering, integrity, and reclamation before using external bundles for larger assets. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training and inference remain future work. Device loss/OOM stress, sustained large scenes, full PBR, ray tracing, other OSes, general scripting, accessibility breadth, and GPU contention with ML remain unqualified.
