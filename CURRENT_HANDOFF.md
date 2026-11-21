# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **3726270584da795a51a93a9ea8b9c881af683493**. The documentation commit following it records this receipt. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. f1bbbb4 preserves the interrupted D3D12 work. 9d8d4a8 contains the Vulkan Studio foundation. fc5a8bd added linear repeat, viewport resize recovery, and a process restart check. 978d816 fixed transform editor clipping. 8500577 added GLB import, persistence, and original-source extraction. The previous receipt is preserved at a840cef. Independent release acceptance remains pending.

## Active implementation

The Studio **Assets** dock lists project meshes and live instance counts. Import can retain a mesh without placing it. Add instance reuses an existing source. Assign to selection changes one object's geometry while preserving its ID, name, transform, and color. Manage can replace all instances from a GLB in one undoable step, save the original bytes, or remove an asset with no instances. Bulk replacement retains the old mesh in the library for reuse. During Play the dock can be browsed and its authoring commands are disabled.

Core schema v4 records the catalog for each retained revision, including unreferenced assets. Undo/redo and Restore previous saved revision cover the scene and library together. The transaction writes catalog membership, SHA-256 blobs, and the scene head. Pruning retains all assets needed by either saved revision. Open supports schema v1–v4. Asset projects migrate to v4 when saved with changes. v3 migration uses the first referencing object's name for the initial catalog label. Source bytes stay immutable and deduplicated by hash. The renderer uploads meshes with active instances. The existing typed project edit boundary now checks that mesh references belong to the project.

The current GLB subset is one static indexed triangle primitive with an embedded BIN buffer, float positions, and optional float normals. Limits are 2 MiB per source, 8 MiB across the library, and 512 entries and 512 scene objects. Geometry uses mesh-local coordinates and PLATZ placement and base color. Node transforms, imported materials, textures, animations, and compressed geometry remain outside this slice.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **51C937AA171B75BC0BB8FD7A9B7312DC7BED17F3132B9A433ABCEC9C162D1B00**.

A clean Release build passed all seven CTest suites. The suite launched and closed the current Windows executable twice. Verbose evidence is in build/asset-delivery-tests.log.

| Test | Observed result |
|---|---|
| core-project | 17 scenarios. Library-only assets survive reopen. Assignment preserves identity and properties. Bulk replacement, Undo/Redo, and previous revision restore include the catalog. Stale saves and removal of referenced assets are rejected. v3 mesh identities and prior revision survive migration. Existing interrupted-save and atomic asset recovery checks passed. |
| shader_toolchain | Deterministic Slang output and validated SPIR-V, material layout reflection, includes, failure preservation, and shader diagnostics. |
| hardware_frame_readback | Vulkan depth/color/ID readback, picking, resize/restore, atlas upload, retirement, and re-upload. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks under GPU-assisted validation. Zero errors. Existing driver loader overlay warnings remain. |
| validation_scene_profile | Five placed objects and two library assets at 1280×720, 1×, SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 0.1566/8.4465 ms. GPU p50/p95 0.0196/0.0201 ms. |
| studio_edit_play_reopen | Real Studio library import, placement, assignment, replacing two instances with changed GPU pixels, undo/redo, Play and Stop, unused removal, and reopen with matching objects and use counts. Baseline GLB, authoring, and project checks also passed. |
| studio_process_restart | Actual PLATZ.exe save, normal shutdown, and restart with the same object ID and edited name. Permanent validation project bytes were preserved during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 8. The five placed objects retain their IDs. **Oblique spire** has one use and **Low spire** has zero. Validation project SHA-256 is **333249DEFD12D24B4A6D3DE930CCD0657654D918FF863428F01D892F2FA1398B**.

## Continuation

Extend the mesh pipeline to support a static GLB with multiple primitives and editable material slots. Add project-level coverage for slot assignment, rendering, persistence, and undo. Qualify durable ordering, integrity, and reclamation across the database and blob store before using external bundles for larger assets. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training and inference remain future work. Device loss/OOM stress, sustained large scenes, PBR/textures, ray tracing, other OSes, general scripting, accessibility breadth, and GPU contention with ML remain unqualified.
