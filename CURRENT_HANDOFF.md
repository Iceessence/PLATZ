# Current handoff and verification ledger

21 November 2026. Windows x64 Release development candidate. Tested software commit **8500577f6daa6d4fd0d4f7ebf5dd8f9e094802ca**. The following documentation commit records its receipt. Use git status --short --branch and git rev-parse HEAD for the live checkout. Launch and build instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. f1bbbb4 preserves the interrupted D3D12 work. 9d8d4a8 contains the Vulkan Studio foundation. fc5a8bd added linear repeat, viewport resize recovery, and a process restart check. 978d816 fixed transform editor clipping. The previous baseline receipt is preserved at 79c04ec. Independent release acceptance remains pending.

## Active implementation

Studio imports local GLB 2.0 geometry via **Add object → Import mesh from GLB…**. The new object is editable, pickable, and compatible with undo, duplicate, repeat, delete, save, and isolated Play. The importer accepts one static indexed triangle primitive with one embedded BIN buffer, float positions, and optional float normals. Missing normals are generated. The limits are 2 MiB per source, 8 MiB of unique embedded sources per scene, and 65,536 vertices / 196,608 indices per mesh. Geometry uses mesh-local coordinates and PLATZ transform and color controls. Imported materials, textures, scene hierarchy, node transforms, animation, and compressed geometry are outside the supported scope. The parser does not resolve external URIs.

Core stores original GLB bytes once per SHA-256 digest in SQLite schema v3. Blobs, references, and revision changes publish in one SQLite transaction. Both retained revisions keep referenced blobs. Versions 1 and 2 remain readable. Saving imported geometry moves the project to v3. The Inspector can save the exact embedded GLB to a new file. Existing files are protected by atomic no-clobber publication. Scene edits remain in the .platz project. The Vulkan renderer rebuilds its indexed mesh atlas when the active asset set changes and retires the old buffer after the transfer fence. Imported instances share the geometry.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **18CD91D5EEC953E49F9AED01FF121F5BAD86BB8BF09FDE8182F45E6CEE104A43**.

Clean Release build passed. All seven CTest entries passed with zero failures. The suite launched and shut down the real local executable twice. Verbose evidence is in build/mesh-delivery-tests.log.

| Test | Observed result |
|---|---|
| core-project | 15 scenarios. GLB bytes/bounds, shared mesh references, stable IDs, Runtime isolation, previous revision restoration, rejected corrupt blobs, and subprocess exit during SQLite asset/revision changes. Baseline migration and recovery checks still pass. |
| shader_toolchain | Deterministic Slang output and validated SPIR-V, material layout reflection, includes, failure preservation, and shader diagnostics. |
| hardware_frame_readback | Real depth/color/ID readback, picking, viewport resize/restore, imported atlas upload, retirement, re-upload, and matching built-in rendering after removal. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same GPU checks under GPU-assisted validation. Zero Vulkan validation errors. The loader reported existing overlay warnings. |
| validation_scene_profile | Five-object validation scene, 1280×720, 1×, SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 0.1720/8.4179 ms. GPU p50/p95 0.0197/0.0202 ms. |
| studio_edit_play_reopen | Real Studio controls for GLB import, Inspector edits, Vulkan picking, and Play/Stop. Save/reopen produced identical color and ID buffers after removing the disposable GLB. Extraction matched original bytes and preserved an occupied destination. Ordinary/validation scene authoring regressions also passed. |
| studio_process_restart | Actual PLATZ.exe save, normal shutdown, and restart with same object ID and edited name. The permanent project's bytes were preserved during qualification. |

The permanent validation project is projects/PLATZ Validation.platz at source revision 7. It contains Display plinth, Jade column, Amber sphere, Silver block, and imported Oblique spire. Its first four object IDs were retained. SHA-256 is **D78E5F97588EAA18C539D4D672D251518F939E828FECDD5069FBAE5A15A9312A**.

## Continuation

The next related feature is an **Assets panel** with reusable mesh assets, instance counts, explicit replacement and undoable reassignments. Follow with bigger asset packages only after qualifying crash-safe ordering, integrity, and reclamation across the database and blob store. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training and inference remain future work. Device loss/OOM stress, sustained large scenes, PBR/textures, ray tracing, other OSes, general scripting, accessibility breadth and GPU contention with ML remain unqualified.
