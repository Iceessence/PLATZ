# ADR-001 · Vulkan-first Engine and native Studio

21 November 2026. Current implementation decision. The owner’s Vulkan-first steering supersedes the primary-renderer portion of ADR-000. Its C++ source, SQLite, isolated Runtime and model-authority decisions carry forward. Reproduce the gates in the current handoff before promoting a release.

## Engine and platform

Use Vulkan 1.4 directly for PLATZ draw submission, pipeline state, explicit synchronization, object IDs, depth and resource ownership. Use SDL 3.4.20 for Vulkan surface integration with Studio's native child window. Dependencies are local to this checkout. The Windows driver supplies the runtime loader. The local instance reports Vulkan 1.4.341. The RTX 5080 reports device API 1.4.351, the Intel UHD 770 reports 1.4.323. Device selection must verify the features actually activated. A driver upgrade, a second backend or a project schema change must not change scene identities. The previous D3D12 implementation is recoverable at f1bbbb4.

Use VMA 3.5.0 for Vulkan device memory. PLATZ owns buffer/image lifetime, frame fences, readback, admission limits and diagnostics. VMA's budget estimates describe Vulkan heaps. Competing GPU users and future ML workers require independent host-side concurrency, cancellation and process budgets. There are no model workers in this release. Use the current Windows SDK/VS 2026 x64 compiler and ISO C++20. MSVC 19.51.36260 exposes C++20 as a stable mode. The later feature-set switch is provisional. Qt and the rendering contract do not require it.

Use Slang 2026.22.1 for offline shader compilation with SPIR-V validation from the 1.4.363.1 SDK. The graphics stages operate against the same material and object contract as the scene model. Ship compiled shader code in the build. Re-evaluate DXC only for a reproducible compilation or correctness defect that blocks an active requirement. Track actual shader qualification in the test record.

## Editor decision

Keep the existing Qt 6.12.0 Widgets adapter for text, IME, standard input behavior, accessible roles, clipboard integration, menus and dock layout. Studio owns its object binding, project navigation, layout policy and renderer interaction. The viewport is a native surface and the engine has no Qt dependency. Maintaining two parallel input stacks for the same text controls risks inconsistent editing and accessibility. Consider replacement only after the proposed adapter reproduces focus, input composition, selection, scaling, docking and assisted-technology behavior. Qualification on Windows does not establish other OS support.

## Data and authority

C++ Core owns source IDs and schema, SQLite transactions, revision guards and reversible commands. Runtime receives a snapshot. GPU buffers, selection tokens and camera interaction have no authority to publish source. The staged typed edit API is the first AI integration boundary. Models, untrusted executable content, inference and training remain outside the current capability set.

Schema v4 records mesh-library membership for each saved revision. The embedded SHA-256 blobs, membership, object references, and revision head publish in the same SQLite transaction. Assets can persist with zero instances. The previous revision retains its library and blobs until that revision is pruned. Older schema v3 projects derive a library from their scene references and preserve that library when first saved in v4. Copies and repeats share immutable source bytes and derived mesh data. The importer uses vendored cgltf v1.15 (MIT, upstream commit `360db1a95480fe102ae9c69b27c5d101167ff5ba`) and binds only the GLB's checked BIN chunk. A future large-asset store will need durable publication before database commit, reference-aware reclamation, fault injection, and path containment. [cgltf v1.15](https://github.com/jkuhlmann/cgltf/releases/tag/v1.15)

Schema v5 adds sparse, per-object material slot colors to each retained revision. Slot indices follow first material use within the immutable GLB. Primitives that reference the same source material share a slot and all primitives of an instance share one object ID. Convert GLB's linear RGB base-color factors to the sRGB input expected by PLATZ. Existing v3/v4 projects get an override where needed to preserve the earlier flat color. A different source digest establishes a new slot mapping and assignment clears the old per-slot overrides. Undo restores the old source and overrides together. [Khronos glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)

## Dependencies and review

Pinned build inputs and checksums live in the setup scripts. VMA uses MIT licensing, SDL uses zlib licensing, and Slang uses Apache 2.0 with LLVM exceptions plus component notices. Qt is dynamically linked. The Vulkan SDK combines components with upstream licenses. Review redistribution notices and current Qt security fixes before public packaging. No installer or independent acceptance is implied by this development build.

- [Vulkan version negotiation](https://docs.vulkan.org/guide/latest/versions.html)
- [SDL Vulkan surface contract](https://wiki.libsdl.org/SDL3/SDL_Vulkan_CreateSurface)
- [AMD VMA](https://gpuopen.com/vulkan-memory-allocator/)
- [Slang](https://shader-slang.org/)
- [Qt widget accessibility](https://doc.qt.io/qt-6/accessible-qwidget.html)
