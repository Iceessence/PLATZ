# Studio authoring assignment and traceability

Baseline d3cdfcf on Original PLATZ. Worktree clean at inspection. Windows 11 Pro build 26300, RTX 5080 driver 617.42, and the repository-pinned Vulkan/SDL/Qt/Slang build. The attachment supplies ordered task requirements. The PLATZ Master and existing ADRs retain product and architecture authority. Challenger is outside this work.

| Package | Master linkage | Baseline, missing link, and regression risk |
|---|---|---|
| 1 · Viewport transforms | Master §§4, 5, 12, 19, 22. Durable typed edits, real 3D Studio, and isolated Play. | GPU pick, camera, and typed Inspector edits work. The viewport has no object manipulation. Keep the Vulkan ID attachment, camera controls, save/reopen, Undo, and runtime isolation intact. |
| 2 · Live inspector | Master §§12, 19, 22. Source and preview remain distinguishable. | Spin boxes disable keyboard tracking and hex colors commit on editing completion. Verify that live previews never save or enter Play, and cleanly cancel or commit while switching tools. |
| 3 · Native editable assets | Master §§7, 8, 12, 22. Durable native source, reference-safe reuse, meaningful interoperability. | Built-in runtime shapes and reusable imported GLB assets work. Native reusable parametric source and an editable image canvas are pending. Preserve GLB exact-source handling, existing asset IDs, v6 saves, and PNG/JPEG rendering. |
| 4 · Workspace controls and brand | PLZ-UX-004, PLZ-UX-005, PLZ-UX-012, PLZ-UX-013. | Qt docking persists a personal layout. Named layouts, deeper customization and the legible capital P remain pending. The supplied assignment calls for a clear P in the application icon, with PZ retained as the formal brand intent. The current polygon mark can read as F at small sizes. |
| 5 · Shared commands and optimization | Master §§14, 19, 22, 28. | Revision-bound typed edits and the existing Vulkan evidence remain the foundation. Qualify actual behavior and measure comparable workloads. Independent review is pending. |

## Implementation and qualification

- **Package 1 · VERIFIED.** Move, Rotate and Scale run through the native viewport. The Vulkan overlay loads the SDR color attachment after the scene pass, so it leaves object picking and scene depth available to their existing consumers. Projected handles and input rays share the camera FOV and clip distances with the renderer. Studio stages a stable object ID and source revision, updates the viewport and Inspector from the draft, and applies a single Core edit on release. Escape, lost capture, focus loss, device errors and Play transitions cancel the active viewport drag.
- **Package 2 · VERIFIED for supported fields.** Typed transforms, Inspector axis scrubbing, hex colors and the Qt color picker reuse the staged object. Valid text applies on Enter or focus loss. Incomplete input restores the prior value. Scrubbing applies on release, and the picker applies on OK. A completed field can be used by Save or Play. Simple previews replace one projected object and use the existing mesh and texture resources.
- **Package 3 · BACKLOG.** The next slice is reusable native geometry with canonical source, live parameters, dependency-safe editing, Undo, portable save and GLB export. Follow with an editable texture source and its material connections.
- **Package 4 · BACKLOG.** The shipped key bindings reserve 1–4 for transforms while the viewport has focus. A future keymap can bind these named commands and surface conflicts with camera and runtime controls. Named workspace profiles and an unmistakable capital P icon remain pending.
- **Package 5 · BACKLOG.** Measure sustained scene and drag workloads, including high-resolution CPU, GPU and VRAM behavior, before changing resource lifetime or generalizing command surfaces.

Packages 1 and 2 did not require a dependency, a project schema change, or edits to the permanent validation project. Independent review remains pending. The current handoff records exact verification and evidence.
