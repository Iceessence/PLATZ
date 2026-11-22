# Current handoff and verification ledger

**PENDING INDEPENDENT REVIEW.** Windows x64 Release development candidate. Tested software commit `12180fc03699d6ec1454606ce5f9326687e44b5c`. Build and launch instructions live in [START_HERE.md](START_HERE.md). The NEON V2 audit covers `94acce51` and tested software `43c3c410`. This handoff records the first repair package for that audit.

## Authority and history

The permanent product specification is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. Its developer directive and Vulkan steering live alongside it. ADR-001 records the Vulkan, Studio, and project architecture. Earlier verified milestones are preserved in Git. The authoring package mapping is in docs/STUDIO_AUTHORING_TRACE.md. Independent release acceptance remains pending.

## NEON V2 repair package 1

The original 12 test suites passed on the audited build. A new visible-window Windows input probe reproduced the frame-pacing and Inspector workload defects. At 240 Hz, four runs across two viewport sizes had 6.27–6.53 ms p95 from the oldest delivered mouse event through paint completion, and 3,819–3,974 Inspector widget paints per run. The extended process check also reproduced the Windows UI Automation selection desynchronization. The baseline logs live under `build/post-neon-v2-baseline-*`.

Studio negotiates Vulkan Mailbox for interactive displays. The FIFO fallback requests the surface minimum image count and uses one active frame slot. Paint uses nonblocking acquire and frame-fence status checks, then retries via a Qt timer when resources are busy. Drag input keeps its latest preview for the next paint. Studio writes only changed Inspector values at up to 20 Hz and applies the exact final sample on release. The existing hidden FIFO renderer profile remains available for static-render comparisons. Surface resize and asset uploads still have separate synchronous resource work.

Mouse picking queues a one-pixel GPU read from the scene ID attachment and resolves it from a fence poll. The request carries the submitted frame's object IDs. Selection drops stale results on scene or surface changes. The 100-click report records dispatch and Studio selection as separate measurements. The reference click dispatch target is below 1 ms p95. End-to-end results are still above 1 ms p95.

Move and Scale have three axes, three camera-facing planes and a center handle. Rotate has three 96-segment axis rings, an outer view-axis ring, back-arc fading and a live sector. The Vulkan overlay draws shaded cone and cube ends, DPI-aware antialiased strokes and axis badges. Occluded parts blend at 30% against the rendered depth attachment. The growing per-frame overlay buffer keeps identity data intact. The transform bar has stable World/Local controls and a pivot selector. Ctrl temporarily toggles snap, Shift applies one-tenth sensitivity and right-click or Escape cancels. The strip below the viewport reports deltas. All commits use the existing revision-bound edit and Undo path.

Scene selection follows the selected Qt row, including Windows UI Automation. Asset actions follow selected rows too. Negative scale text keeps its sign while typing and restores the previous value on validation failure. Invalid hex and other incomplete drafts leave Save disabled in a clean project. GLB and PNG export confirm an overwrite and publish via Qt's atomic save file. The obsolete HLSL scene source has been removed.

**Scope still open.** Physical pointer-to-photon latency, mixed-monitor DPI, and the live fallback on a display that only exposes FIFO remain unmeasured. Whole-scene antialiasing, incremental asset uploads, shared Vulkan resources, task-specific workspaces and direct mesh reshaping are later packages. The project schema and permanent validation fixture remain at their existing revisions.

## Active implementation

**Verified workspaces and Studio preferences.** Modeling, Materials, Texture and Playtest arrange the existing tools for each task. Per-workspace Qt dock state retains docking, floating positions, visibility, sizes and the Assets tab. Save Workspace As makes a named copy, and Reset Workspace restores its original arrangement or the built-in preset. An earlier single-layout preference migrates to Custom. Invalid Qt layout data falls back to a usable preset. Floating docks are checked against available screen title-bar area after restore and screen removal. The project toolbar remains available in Playtest, and Escape stays assigned to Stop and Cancel.

Customize Studio previews Slate, Light and High contrast themes, four accent choices and interface scale from 85% to 140%. Save, Play/Stop, Frame Selection and Create Image shortcuts validate against each other, reserved controls and text editing. The Ctrl+K finder searches available menu commands and executes their existing actions. Workspace and preference records are stored in local QSettings independently of project data. Portable project bytes and schemas are unaffected.

**Verified capital P resources.** One deterministic drawing generates 16, 24, 32, 48, 64, 128 and 256 px artwork for the Qt icon and the Windows executable. Hub and title-bar visuals were inspected on Windows. The Windows loader opened every embedded icon size. The executable sets its window icon through Qt. Direct visual inspection of the shell taskbar and Explorer is pending.

**Verified native texture authoring.** Create → Image / Texture opens a 1–512 pixel-per-axis RGBA canvas with pencil, eraser, connected fill, eyedropper, alpha-aware color, zoom, grid and 32 local Undo steps. Apply publishes the source and its optional material assignment in one project edit. Linked slots preview the current draft and Cancel restores the authored texture. Assets → Images offers lookup, usage counts, rename, duplicate, assign, make unique, detach and removal of unused sources. PNG export and reimport preserve all four channels, including RGB values at alpha zero. PNG import is bounded to 2 MiB, and the image library supports 32 sources and 8 MiB of RGBA.

Schema v8 saves pixel blobs, dimensions, source IDs and material-slot links in the same SQLite transaction. The GPU accepts project images on native or imported mesh slots where each primitive has UV0. A project image uses sRGB nearest sampling at one mip level. Studio evaluates RGB on opaque surfaces. Detaching restores the slot's existing embedded texture or flat-color setting. The renderer's existing batch path re-uploads the active texture set on source changes. Texture residency and mip chains remain open work.

**Verified native mesh authoring.** Create, edit and reuse cube, sphere and cylinder assets in Studio. Cube supports 1–24 divisions per face edge. Sphere supports 3–128 radial and 2–64 latitude divisions. Cylinder supports 3–128 radial and 1–32 height divisions, with top diameters from 0.1 to 2 times the base. Shape dimensions are bounded to 0.01–100 m. Each shape generates positions, normals, UV0 and indexed triangles. Creation can place an instance or leave the asset in the library. Shared edits preview on all linked objects and commit in one Undo step. Instance transforms and tint are preserved. Rename, duplicate, assign, make unique, usage counts and Locate Source work through the Assets dock and Inspector.

Schema v7 retains editable recipe parameters and stable native IDs per saved revision. Native source and imported GLB assets coexist. Changes in derived geometry replace the viewport atlas while keeping source identity. GPU preview failures disable Apply and restore the authored scene for renderer retry. Export writes the derived GLB geometry. That file reimports as static geometry, while the recipe, transforms and material overrides stay in the .platz project. The native and imported meshes share an 8 MiB library budget. Changed saves with a native asset require a v7-capable build.

**Verified live Inspector edits.** Typing valid transform values or hex colors updates the viewport, including asset slot tint. Dragging an axis label previews numeric changes with optional Shift precision. The color picker previews adjustments and accepts only on OK. Enter or focus exit commits valid text, Escape cancels, and incomplete text restores the previous value. Step controls, texture toggles and asset assignment continue to update on commit. These changes reuse the existing project schema and per-frame mesh and texture resources.

Static GLB assets can render embedded PNG/JPEG base-color textures through UV0. The importer supports float and normalized unsigned byte/short texture coordinates. Vulkan samples the source as sRGB and combines the linear sample with the base-color factor or per-object tint. GLB wrap modes and nearest/linear base-level filtering are supported. Sampling uses one mip level. Materials remain opaque. External images, secondary UV sets, texture transforms, additional maps, node transforms, animation, and full PBR remain outside this slice.

Under Material in the Inspector, Use imported texture switches the selected slot between its embedded image and flat base color. The Inspector shows the available image dimensions or the reason for flat fallback. Toggle changes are per instance and participate in typed edits, Undo/Redo, Play isolation, duplication, and save/reopen. Replacing the mesh establishes a new mapping. Prior projects keep their flat appearance and can enable supported textures manually.

Schema v6 records disabled slots per retained revision. The exact encoded images stay in the immutable, digest-checked GLB in SQLite. WIC reads only embedded bytes. PNG chunk checksums and dimensions are checked before the codec call. Unsupported or corrupt images use flat base color, with source diagnostics in the Inspector or decode fallback counts in GPU diagnostics. Texture limits are 1024 pixels per side, 64 active source/sampler bindings, and 64 MiB decoded RGBA per scene. The existing 2 MiB per GLB and 8 MiB library source limits still apply. GPU upload creates a texture batch when active bindings change and retires the old batch after waiting for the transfer fence.

## Verified on the development workstation

Windows 11 Pro build 26300, i9-14900K, RTX 5080 driver 617.42, Vulkan device 1.4.351, system loader 1.4.341, C++20 with MSVC 19.51.36260, Qt 6.12.0, SDL 3.4.20, SDK 1.4.363.1, VMA 3.5.0, and Slang 2026.22.1. `C:\PLATZ\out\Release\PLATZ.exe` SHA-256 **B565A33711E8B9D6614281C1DC3CE6173043069EC07EF94A2EA45987C036B0BB**.

On November 21, 2026, `& .\tools\build.ps1 -Test` configured and built Release and passed all 14 CTest cases in 32.61 seconds. No cases failed or were skipped. Vulkan synchronization and GPU-assisted validation reported zero errors. The process test launched `PLATZ.exe` twice, used Windows UI Automation to select Harbor signs and return to the original row, edited and saved a disposable copy, then reopened and checked the saved name and stable ID. The temporary rotation, move and scale captures were taken from a visible Studio window at 100% and synthetic 150% DPI.

| Visible 240 Hz RTX 5080 workload | Delivered event rate | Oldest input to submitted paint p95 | Inspector widget paints per run |
|---|---|---|---|
| 7 objects, original software and trajectory | 224–244 events per second | 6.27–6.53 ms | 3,819–3,974 |
| 7 objects, same trajectory and new software | 299–336 events per second | 2.85–2.91 ms | 57–82 |
| 7 objects, distinct native pointer positions | 500 and 1,000 events per second | 2.88–3.04 ms | 59–82 |
| 512 objects, distinct native pointer positions | 500 and 1,000 events per second | 2.86–3.12 ms | 53–82 |

Each row covers `899×707` and `2019×1107` viewport clients, with the Inspector and Scene docks visible. The dense input benchmark verifies the 4.2 ms drag p95 and 0.15 ms mouse-dispatch p95 reference targets. For 100 exact alternating object/background picks per run, click dispatch p95 was 0.091–0.116 ms and click-to-Studio-selection p95 was 3.46–6.84 ms. Tests report CPU submission times and cannot measure physical display scanout. The 512-object stress scene was made by repeating a built-in block on a disposable validation copy. A scene with hundreds of unique assets has a different resource workload.

Local evidence includes `build/post-neon-v2-final.log`, `build/post-neon-v2-final-ctest.log`, `build/post-neon-v2-baseline-drag.log`, `build/post-neon-v2-comparable-drag.log`, `build/post-neon-v2-512-drag.log` and six captures in `build/post-neon-v2-evidence/`. The timing transcripts preserve p50, p95, p99 and maxima.

Early Studio qualification found that Qt could defer the first viewport repaint while a modal editing dialog opened. Preview now explicitly calls the shared viewport render path. The test captures that result directly and sets a deadline for dialog readiness.

| Test | Observed result |
|---|---|
| core-project | 29 scenarios. Native image bounds, budgets, v2/v6-to-v8 migration, exact pixels, shared/unique links, typed revisions, interrupted-write rollback and corrupt blob rejection. Previous geometry, project and GLB checks passed. |
| shader_toolchain | Deterministic Slang output, validated SPIR-V, uniform and texture layout reflection, include rebuild, and last-known-good preservation. |
| hardware_frame_readback | Project RGBA upload changed just the assigned slot, left object IDs intact, rendered opaque even at alpha zero and restored the underlying GLB texture on detach. Synchronization validation reported zero errors. |
| hardware_gpu_assisted | Same hardware checks with GPU-assisted validation. Zero errors. Existing Vulkan loader warnings about installed capture overlays remain. |
| validation_scene_profile | Seven objects, two base-color textures, 1280×720 SDR, hidden FIFO surface, 16 warm-up and 96 measured frames, validation off. CPU submit/present p50/p95 was 0.1932/8.4141 ms. GPU p50/p95 was 0.0205/0.0215 ms. |
| studio_edit_play_reopen | All 18 handle hit tests and transforms, signed snap math, local axes, bounds-centered scaling, Vulkan color overlay, Inspector previews and cancel, GLB overwrite, Undo, Play, and pixel-identical reopen. |
| studio_transform_highdpi and studio_transform_200dpi | Handle math and visible integration at synthetic Qt 150% and 200% scales, including right-click and Escape cancellation. |
| studio_visible_drag_profile | Visible native Windows input at 500 and 1,000 events per second, two client sizes, bounded Inspector paints and 100 asynchronous GPU clicks. The 512-object variant was also run separately. |
| studio_native_geometry | Live creation and cancellation, linked edit, asset rename, copy, detach, Locate Source, one-step Undo/Redo, Play, reimportable GLB export, dense library-only sphere and v7 reopen. |
| studio_native_images | Canvas input and stroke history, Vulkan preview, cancellation, an additional imported GLB slot, single-step project Undo/Redo, pixel-exact PNG export/reimport, geometry edits, Play, v8 reopen and editing after reopen. |
| studio_workspaces | Legacy layout migration, custom dock persistence, offscreen recovery, appearance preview/cancel, shortcut conflicts and remapping, searchable menu actions, reset, malformed-settings recovery, packaged P geometry and unchanged portable project bytes. |
| studio_workspaces_highdpi | Same workflow with 150% Qt display scaling and a 125% Studio-scale setting. Physical mixed-monitor transitions remain unqualified. |
| studio_process_restart | Windows icon-group size table and all seven icon loads, window small-icon handle, actual PLATZ.exe save, shutdown, restart and stable-ID recovery. Permanent validation project bytes remained intact during the tests. |

The permanent validation project is projects/PLATZ Validation.platz at revision 12. **Harbor signs** adds one object with two slots, **Signal flags** (PNG) and **Harbor mark** (JPEG). The prior six object IDs remain intact. SHA-256 is **D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894**.

Native image editing still uses a preview timer and per-scene texture uploads. Asset-heavy scenes, device loss, physical display latency and mixed-monitor DPI remain unqualified.

## Continuation

Independent review starts with the two dense drag logs and a hands-on pass on the owner's display. Measure the remaining click-to-selection delay and qualify the fallback on a FIFO-only surface. The next ordered implementation package establishes shared Vulkan device and resource lifetimes for multiple viewports, then makes mesh and texture uploads incremental. Its command and property descriptors, scrubbable fields and native-dialog test seam share the existing revision-bound editing path. Schema v9, the external asset store, changes to the permanent fixture, new dependencies, and default brand or key-profile changes retain their owner-approval gates.
