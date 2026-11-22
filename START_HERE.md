# PLATZ development entry point

The permanent source root is C:\PLATZ. Start with CURRENT_HANDOFF.md for the tested revision and feature ledger. Product authority is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. The V4 developer directive and Vulkan steering are alongside it. ADR-001 is the active renderer decision, retaining the Core and Studio decisions in ADR-000.

## Launch on the development workstation
Double-click out\Release\PLATZ.exe for the project hub, or run the following in PowerShell.

    & 'C:\PLATZ\out\Release\PLATZ.exe' 'C:\PLATZ\projects\PLATZ Validation.platz'

Use New project in the hub to create an ordinary editable .platz file. Select an object in the viewport or Scene panel. Use **Select**, **Move**, **Rotate**, or **Scale** above the viewport, or press **1–4** while the viewport has focus. Drag a colored axis or ring and release to apply a single undoable change. **World** and **Local** apply to Move and Rotate. Scale follows the object's axes. **Snap** uses the neighboring increment, measured in metres, degrees or scale units for the active tool. In the viewport, right-drag to orbit, middle-drag to pan, scroll to zoom, and press **F** to frame the selection.

Type a transform value or a six-digit sRGB color in the Inspector to preview it. Drag an axis label to scrub a number, or hold **Shift** for finer scrubbing. Enter or leaving the field applies valid text, and releasing applies a scrub. The color picker previews as you adjust it. Use **Escape** to cancel a text edit or drag, or **Cancel** in the color picker. Use **Ctrl+Z** and **Ctrl+Y** after committing changes.

Add object creates primitives. Edit > Repeat objects previews copies with world-space spacing. Create copies accepts the entire operation, and Ctrl+Z undoes it. F5 starts Play. Click a block or column to turn it and use WASD to move. Escape restores the authored scene and camera. Ctrl+S saves. The Project menu can restore the previous saved revision as an undoable edit.

The **Assets** dock lists embedded GLB meshes and their instance counts. Use **Import…** to keep a source in the library, **Add instance** to place it, or **Assign to selection** to change the geometry of the selected object while preserving its identity, placement, and color. In the validation project you can select **Low spire** in Assets and assign it to **Oblique spire** in the Scene panel. The **Manage** menu can replace every instance from a different GLB, save the original GLB, or remove a mesh with zero uses. These project changes support Undo and Redo.

Use **Add object → Import mesh from GLB…** to import and place in one step. The importer accepts one static mesh with up to 32 indexed triangle primitives and an embedded buffer, up to 2 MiB per file and 8 MiB of unique sources across the project library. Saving embeds the exact source bytes and keeps the .platz project portable. The current build opens older projects and upgrades asset projects to schema v6 when saving changes.

Select **Color study** in the validation project. Under **Material** in the Inspector, choose **Copper** or **Lagoon** and edit the sRGB hex value or use the swatch to pick a color. Parts that share a GLB material share a slot. A slot edit applies to the selected object. **Reset** restores the imported color, and Undo restores the previous edit. The importer reads RGB base-color factors. The preview renders opaque colors.

Select **Harbor signs** to try textures. Under **Material**, choose **Signal flags** (PNG) or **Harbor mark** (JPEG). **Use imported texture** switches the selected slot between its embedded image and flat base color. While the texture is enabled, the base color tints it. The checkbox setting is saved per object and supports Undo and Redo. The current slice uses embedded PNG/JPEG images up to 1024 × 1024, UV0, and single-level sampling. Older asset projects keep their flat appearance until you enable a supported imported texture.

## Build and verify
On Windows x64 with VS 2026 Desktop development with C++, Windows SDK 10.0.26100, VS CMake tools, Python 3.9+ with the py launcher, Windows tar and curl:

    Set-Location C:\PLATZ
    .\tools\build.ps1 -Test

Initial setup requires network to retrieve the hash-checked Qt Base, SDL, Vulkan SDK components, Slang and VMA. Setup caches them in .deps. The development executable uses the adjacent Qt and SDL DLLs, platforms\qwindows.dll, the x64 MSVC runtime, Windows SQLite, and a Vulkan 1.4 driver with the engine-required device capabilities. Keep the deployment directory intact. This checkout-relative launch includes the validation project at projects\PLATZ Validation.platz. No external account or shader compiler is required to run the built editor.
