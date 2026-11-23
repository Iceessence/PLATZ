# Decisions and feature ledger

The [Master](docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md) defines product intent, requirements and acceptance. The [handoff](CURRENT_HANDOFF.md) holds the current candidate and exact evidence. Review results govern independent acceptance.

## Active decisions

| Area | Authority and current decision | Revisit when |
|---|---|---|
| One engine-centered application | [ADR-000](docs/ADR-000-FOUNDATION.md) and [ADR-001](docs/ADR-001-VULKAN.md). Native C++20, Qt Widgets Studio, SDL3 Windows surface, Vulkan 1.4, VMA and Slang. The native viewport and runtime use the same engine. | A representative workload or supported platform demonstrates a consequential limitation. |
| Canonical data and runtime | SQLite, stable IDs, bounded embedded source formats and isolated Play. Studio previews are transient, and a confirmed edit uses existing typed and revision-checked Core operations. | The owner approves v9 or an external asset store after a compatibility and migration plan. |
| Source-connected paint workspace | The existing RGBA editor lives in the central Studio split with the existing Vulkan viewport. One draft owns its target and source revision. Apply uses the existing image operation, then Undo and Save operate on the same project. The texture batch reuses images whose immutable content and sampling key match. | Streaming, a second renderer viewport, or larger pixel sources justify a new resource-lifetime model. |
| One enduring test project | The tracked project is the common reference. Studio opens its stable editable working file, seeded once, and checks it is distinct from the reference. Tests isolate operations that intentionally save, restart or damage input. | The owner approves verified station expansion or a fixture migration. |
| AI and code | Object editing has revision-bound staging and Core validation. No-code, an exposed command registry, and PLATZ AI tools are unavailable. | One concrete authoring operation can be exposed with scoped authority, validation, cancellation and audit evidence. |

## Active scopes and continuity

| Scope and Master contracts | Verified or qualified subset | Evidence / remaining gap |
|---|---|---|
| Scene and Play · PLZ-FOUND-001, PLZ-RUNTIME-001, PLZ-INTEG-005 | Actual GPU render, ID pick, scene selection and transforms, save/reopen, isolated runtime interactions | `studio_edit_play_reopen`, `studio_process_restart`. Gameplay persistence, collision, authored rules and standalone export are backlog. |
| Core source · PLZ-CORE-001–004, PLZ-ARCH-002 | v1–v8 compatibility, transactional SQLite revisions, editable cube/sphere/cylinder recipes, reference-safe GLB and RGBA sources, undo | `core-project`, `studio_native_geometry`, `studio_native_images`. Autosave, deep history, scalable undo and transform parenting are open. |
| Studio · PLZ-UX-001, 003, 004, 006, 009 | Search, personal workspace layouts, themes, saved shortcuts, scene/asset folders, selection-aware Inspector, split image authoring | Workspace, organizer, image and DPI suites. Default layouts now route to task controls for Texture and Playtest. Broader assistive coverage, additional device UI and other workspaces remain open. |
| GPU · PLZ-RENDER-001, 010, PLZ-ARCH-004 | Hardware Vulkan, conservative visibility culling, mipmaps on eligible imports, resource reuse for unchanged textures | Readback, GPU-AV, and visible input profile. Atlas changes still rebuild geometry. Descriptor replacement still waits on the queue. AA, soft shadows, PBR and shared multi-viewport device resources remain open. |
| Creative asset workflow · PLZ-ART-007, PLZ-PIPE-009, PLZ-INTEG-011 | Image source edits appear on linked base-color slots. Preserve RGBA on save/reopen and PNG export. Parametric mesh source edit, native shapes, and identity-preserving source assignment work. | Image and native mesh suites. UV overlay, multiplane 3D paint, layers, and reusable materials remain open. |
| Validation and delivery · PLZ-TEST-001, 007, PLZ-OPS-007, 008, 013, 017 | One reference validation project, stable working path, exact version and artifact evidence, author/reviewer separation | See the handoff’s station route and earlier evidence in Git. No release promotion without independent review. |

## Paint workspace surface receipt

| Surface | Current result |
|---|---|
| Function, UI, accessibility | Canvas and live scene in one window. Mouse tools and keyboard pixel cursor, local stroke history, accessible names, and supported-scale checks. |
| Canonical data, undo, runtime | Existing image apply and revision checks, one project Undo, PNG round-trip, Save/reopen, Play after closing the draft, and Cancel restoration. |
| Resources and errors | One changed texture uploaded during a linked edit. Sync2 and GPU-AV exercise descriptor rebuild, mipmaps, and fallback. Draft status keeps invalid edits from applying. |
| Discovery and docs | Texture task controls, image library, Create/Search Studio, command help and launch guide. |
| Verification and persistent project | Image suite runs on an isolated copy of the reference project. Live manual route is in the handoff. |
| Visual logic, plugin and PLATZ AI parity | Not yet available. This batch adds no tool-grant or plugin execution surface. |
