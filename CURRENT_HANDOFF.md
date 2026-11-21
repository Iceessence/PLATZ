# Current handoff and verification ledger

21 November 2026. Windows x64 Release development candidate. Tested software commit **978d81604cc55fda35b1d554fef224a772266b42**. The documentation commit following it records the receipt. Use git status --short --branch and git rev-parse HEAD for the live checkout. Launch and build instructions are in START_HERE.md.

## Recovery and authority
f1bbbb4 preserves the interrupted C++/D3D12 source. 9d8d4a8 preserves the integrated Vulkan implementation before the latest feature batch. fc5a8bd adds repeat, SDL resize repair and separate-process qualification. 978d816 corrects transform field clipping and qualifies the production style. Master and developer directive matched the supplied originals by SHA-256. No identifiable independent snapshot audit or prior current handoff was available when resuming. No release has been independently accepted or owner-frozen.

## Active scope and evidence
Local test host: Windows 11 Pro build 26300, i9-14900K, 96 GB class RAM, GeForce RTX 5080 driver 617.42, device Vulkan 1.4.351, system loader 1.4.341. C++20, MSVC 19.51.36260. Qt Widgets 6.12.0, SDL 3.4.20, Vulkan SDK tools 1.4.363.1, VMA 3.5.0, Slang 2026.22.1. See docs/ADR-001-VULKAN.md. The final PLATZ.exe SHA-256 is 3591E8BD8B2208E934C207D290AD62CE8A8F038FE8C857A78C044E75B82F0EEF.

All seven CTest entries passed on the development workstation. The executable was also launched with the permanent validation project for visual inspection. The named cases below are scoped VERIFICATIONS, pending separate acceptance.

| Test | Observed result |
|---|---|
| core-project | 14 scenarios. Includes normal create/no-clobber, invalid/stale changes, undo/redo, isolated Runtime, competing saves, forced process exit/recovery, schema migration, empty scenes, and all-or-nothing linear repeats. |
| shader_toolchain | Reproducible VS/FS, reflected ABI, include invalidation, failure preserving previous compiled output, source diagnostics and validated debug compute/subgroup SPIR-V. Offline shader tooling. |
| hardware_frame_readback | GPU color and ID readback, occlusion independent of draw order, exact picking, material change, camera, in-flight resize, zero-size suspension and restore. Synchronization validation enabled. |
| hardware_gpu_assisted | Same hardware checks with GPU-assisted validation requested and activated. Both hardware validation runs reported zero errors. Loader reported overlay compatibility warnings and GPU-assisted mode reported feature adjustments. |
| validation_scene_profile | Real four-object validation project, 1280×720, SDR, 1×, FIFO, hidden surface, validation off, 16 warm-up and 96 measured frames. Final CPU submit/present p50/p95 0.2890/8.4516 ms; GPU timestamp p50/p95 0.0204/0.0217 ms. Workload-specific measurements. |
| studio_edit_play_reopen | On an ordinary project and a disposable validation copy: real Hub/file dialog, creation/duplication/deletion, cancellable and approved linear repeat preview, undo/redo and identity, dock hide/restore, Unicode input-method commit, camera, viewport picking, Inspector edits, changed GPU pixels, Play interaction, exact Stop frame, Save and reopen. Runs with production styling and checks visible numeric editor width. |
| studio_process_restart | Two actual PLATZ.exe processes and Windows UI Automation. Pending name edit, normal Save, exit, relaunch, same inspector identity and persisted value; source validation project hash preserved. |

Permanent validation source is projects/PLATZ Validation.platz, revision 5. It contains Display plinth, Jade column, Amber sphere and Silver block. Regressions open a disposable copy for editing. Local verbose CTest evidence is in build/Testing/Temporary/LastTest.log and changes on subsequent test runs.

## Integration and remaining work
src/core/Project.cpp owns the versioned SQLite project, stable identities, revisions, typed edit staging and isolated Runtime copy. Edit > Repeat objects runs the same Core validation for preview and commit, uses ordinary editable primitives and consumes one history step. The committed replicas are independent objects. src/engine owns Slang shading, explicit Vulkan submission, depth, GPU IDs, VMA-backed buffers/images and diagnostics. src/studio binds saved objects to Qt controls, the native viewport and Play. PLATZ AI has a revision-bound typed change boundary. There is no model worker, model training, executable plugin or autonomous approval path.

Resolved defects: SDL wrapping the native window without the resizable flag retained 432×288 after the host requested 376×250. The resize test reproduced the mismatch and now passes after restoration. The production stylesheet clipped the numeric editor even though values were accessible and keyboard-operable. A styled test reproduced the clipped region. Removing the conflicting outer spin-box rule restored usable width.

Next coherent feature: import a local indexed triangle mesh (bounded glTF/GLB subset), retain source provenance and a durable content-addressed blob, and project the same geometry into selection, Studio, repeat and Play. Qualify crash ordering between blob publication and SQLite references before enabling imports. Current geometry is limited to three built-in primitives and 512 objects. Device loss/OOM recovery under pressure, sustained complex scenes, PBR/textures, ray tracing, other operating systems, general scripting, full assistive technology/IME and model resource arbitration remain unqualified. Public packaging requires dependency and security servicing review. No owner decision blocks local development.
