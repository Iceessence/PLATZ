# Current handoff and verification ledger

Windows x64 Release development candidate. Tested software commit **43c3c4102fffb753d6cda7f1204c56e3d5ac77d1**. Build and launch instructions live in START_HERE.md.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier verified milestones are preserved in Git. The authoring package mapping is in docs/STUDIO_AUTHORING_TRACE.md. Independent release acceptance remains pending.

## Active implementation

**Verified workspaces and Studio preferences.** Modeling, Materials, Texture and Playtest arrange the existing tools for each task. Per-workspace Qt dock state retains docking, floating positions, visibility, sizes and the Assets tab. Save Workspace As makes a named copy, and Reset Workspace restores its original arrangement or the built-in preset. An earlier single-layout preference migrates to Custom. Invalid Qt layout data falls back to a usable preset. Floating docks are checked against available screen title-bar area after restore and screen removal. The project toolbar remains available in Playtest, and Escape stays assigned to Stop and Cancel.

Customize Studio previews Slate, Light and High contrast themes, four accent choices and interface scale from 85% to 140%. Save, Play/Stop, Frame Selection and Create Image shortcuts validate against each other, reserved controls and text editing. The Ctrl+K finder searches available menu commands and executes their existing actions. Workspace and preference records are stored in local QSettings independently of project data. Portable project bytes and schemas are unaffected.

**Verified capital P resources.** One deterministic drawing generates 16, 24, 32, 48, 64, 128 and 256 px artwork for the Qt icon and the Windows executable. Hub and title-bar visuals were inspected on Windows. The Windows loader opened every embedded icon size. The executable sets its window icon through Qt. Direct visual inspection of the shell taskbar and Explorer is pending.

**Verified native texture authoring.** Create → Image / Texture opens a 1–512 pixel-per-axis RGBA canvas with pencil, eraser, connected fill, eyedropper, alpha-aware color, zoom, grid and 32 local Undo steps. Apply publishes the source and its optional material assignment in one project edit. Linked slots preview the current draft and Cancel restores the authored texture. Assets → Images offers lookup, usage counts, rename, duplicate, assign, make unique, detach and removal of unused sources. PNG export and reimport preserve all four channels, including RGB values at alpha zero. PNG import is bounded to 2 MiB, and the image library supports 32 sources and 8 MiB of RGBA.

Schema v8 saves pixel blobs, dimensions, source IDs and material-slot links in the same SQLite transaction. The GPU accepts project images on native or imported mesh slots where each primitive has UV0. A project image uses sRGB nearest sampling at one mip level. Studio evaluates RGB on opaque surfaces. Detaching restores the slot's existing embedded texture or flat-color setting. The renderer's existing batch path re-uploads the active texture set on source changes. Texture residency and mip chains remain open work.

**Verified native mesh authoring.** Create, edit and reuse cube, sphere and cylinder assets in Studio. Cube supports 1–24 divisions per face edge. Sphere supports 3–128 radial and 2–64 latitude divisions. Cylinder supports 3–128 radial and 1–32 height divisions, with top diameters from 0.1 to 2 times the base. Shape dimensions are bounded to 0.01–100 m. Each shape generates positions, normals, UV0 and indexed triangles. Creation can place an instance or leave the asset in the library. Shared edits preview on all linked objects and commit in one Undo step. Instance transforms and tint are preserved. Rename, duplicate, assign, make unique, usage counts and Locate Source work through the Assets dock and Inspector.

Schema v7 retains editable recipe parameters and stable native IDs per saved revision. Native source and imported GLB assets coexist. Changes in derived geometry replace the viewport atlas while keeping source identity. GPU preview failures disable Apply and restore the authored scene for renderer retry. Export writes the derived GLB geometry. That file reimports as static geometry, while the recipe, transforms and material overrides stay in the .platz project. The native and imported meshes share an 8 MiB library budget. Changed saves with a native asset require a v7-capable build.

**Verified viewport transforms.** Studio has Select, Move, Rotate and Scale modes, World and Local axes for Move and Rotate, and a per-mode snap increment. Scale uses the local axes. The Vulkan overlay writes only scene color and preserves the existing depth-based selection and ID attachments. Shortcuts 1–4 apply while the viewport has focus. A drag stages a typed authoring edit for the selected stable ID and exact project revision. Preview frames replace one projected object, and the Inspector follows the draft. Release applies one undoable edit. Escape, capture loss, and Play discard an in-progress viewport drag.

**Verified live Inspector edits.** Typing valid transform values or hex colors updates the viewport, including asset slot tint. Dragging an axis label previews numeric changes with optional Shift precision. The color picker previews adjustments and accepts only on OK. Enter or focus exit commits valid text, Escape cancels, and incomplete text restores the previous value. Step controls, texture toggles and asset assignment continue to update on commit. These changes reuse the existing project schema and per-frame mesh and texture resources.

Static GLB assets can render embedded PNG/JPEG base-color textures through UV0. The importer supports float and normalized unsigned byte/short texture coordinates. Vulkan samples the source as sRGB and combines the linear sample with the base-color factor or per-object tint. GLB wrap modes and nearest/linear base-level filtering are supported. Sampling uses one mip level. Materials remain opaque. External images, secondary UV sets, texture transforms, additional maps, node transforms, animation, and full PBR remain outside this slice.

Under Material in the Inspector, Use imported texture switches the selected slot between its embedded image and flat base color. The Inspector shows the available image dimensions or the reason for flat fallback. Toggle changes are per instance and participate in typed edits, Undo/Redo, Play isolation, duplication, and save/reopen. Replacing the mesh establishes a new mapping. Prior projects keep their flat appearance and can enable supported textures manually.

Schema v6 records disabled slots per retained revision. The exact encoded images stay in the immutable, digest-checked GLB in SQLite. WIC reads only embedded bytes. PNG chunk checksums and dimensions are checked before the codec call. Unsupported or corrupt images use flat base color, with source diagnostics in the Inspector or decode fallback counts in GPU diagnostics. Texture limits are 1024 pixels per side, 64 active source/sampler bindings, and 64 MiB decoded RGBA per scene. The existing 2 MiB per GLB and 8 MiB library source limits still apply. GPU upload creates a texture batch when active bindings change and retires the old batch after waiting for the transfer fence.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. Release executable SHA-256 **B935866B785172C078BCDE2F732E6CC20807524C53084DE9BBEF1A769F7B2733**.

On November 21, 2026, `& .\tools\build.ps1 -Test` configured and built Release and passed all twelve CTest cases, with zero failed or skipped. Current logs are build/workspaces-final.log and build/Testing/Temporary/LastTest.log. The Release application opened a disposable copy, hid Assets, saved Harbor Review, and restored the saved name and hidden panel in a new process. The review project SHA-256 remained unchanged. Hub, title bar and restored-layout screenshots are in build/workspace-evidence.

Early Studio qualification found that Qt could defer the first viewport repaint while a modal editing dialog opened. Preview now explicitly calls the shared viewport render path. The test captures that result directly and sets a deadline for dialog readiness.

| Test | Observed result |
|---|---|
| core-project | 29 scenarios. Native image bounds, budgets, v2/v6-to-v8 migration, exact pixels, shared/unique links, typed revisions, interrupted-write rollback and corrupt blob rejection. Previous geometry, project and GLB checks passed. |
| shader_toolchain | Deterministic Slang output, validated SPIR-V, uniform and texture layout reflection, include rebuild, and last-known-good preservation. |
| hardware_frame_readback | Project RGBA upload changed just the assigned slot, left object IDs intact, rendered opaque even at alpha zero and restored the underlying GLB texture on detach. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks with GPU-assisted validation. Zero errors. Existing Vulkan loader warnings about installed capture overlays remain. |
| validation_scene_profile | Seven objects, two base-color textures, 1280×720 SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 changed from 0.2056/8.4430 to 0.1895/8.4300 ms. GPU p50/p95 changed from 0.0202/0.0205 to 0.0203/0.0207 ms. This run checks the existing rendering workload. |
| studio_edit_play_reopen | Real Studio widgets and Vulkan cover signed snap math, local axes, nonuniform scale, Move/Rotate/Scale, shader color overlay, live typing, scrubbing, picker accept/reject, one-step Undo, cancellation, Play, save and pixel-identical reopen. Existing project and asset workflows passed. |
| studio_transform_highdpi | Repeated pointer interactions and persistence under synthetic Qt 150% scale and tested lost release and Escape. |
| studio_native_geometry | Live creation and cancellation, linked edit, asset rename, copy, detach, Locate Source, one-step Undo/Redo, Play, reimportable GLB export, dense library-only sphere and v7 reopen. |
| studio_native_images | Canvas input and stroke history, Vulkan preview, cancellation, an additional imported GLB slot, single-step project Undo/Redo, pixel-exact PNG export/reimport, geometry edits, Play, v8 reopen and editing after reopen. |
| studio_workspaces | Legacy layout migration, custom dock persistence, offscreen recovery, appearance preview/cancel, shortcut conflicts and remapping, searchable menu actions, reset, malformed-settings recovery, packaged P geometry and unchanged portable project bytes. |
| studio_workspaces_highdpi | Same workflow with 150% Qt display scaling and a 125% Studio-scale setting. Physical mixed-monitor transitions remain unqualified. |
| studio_process_restart | Windows icon-group size table and all seven icon loads, window small-icon handle, actual PLATZ.exe save, shutdown, restart and stable-ID recovery. Permanent validation project bytes remained intact during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 12. **Harbor signs** adds one object with two slots, **Signal flags** (PNG) and **Harbor mark** (JPEG). The prior six object IDs remain intact. SHA-256 is **D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894**.

Four native image samples took 61.9–62.8 ms from final synthetic input through synchronous Vulkan capture, including the 55 ms preview timer. This workload uses a small canvas and one active linked slot. Display latency, interactive 1080p+ and large-scene CPU/GPU/VRAM distributions, device loss and mixed-monitor DPI remain unqualified.

## Continuation

Next profile texture editing on larger scenes and qualify sRGB mip chains and stable residency. Then expose reusable typed descriptors for existing commands and editable properties, and add direct mesh editing beyond shape parameters. Shareable workspace exports and visual composition of new UI controls remain open. Typed revision-bound edits remain the PLATZ AI authority boundary. Model training, full PBR, ray tracing, other operating systems and general scripting remain future work.
