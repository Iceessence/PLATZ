# PLATZ development entry point

The permanent source root is C:\PLATZ. Start with CURRENT_HANDOFF.md for the tested revision and feature ledger. Product authority is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. The V4 developer directive and Vulkan steering are alongside it. ADR-001 is the active renderer decision, retaining the Core and Studio decisions in ADR-000.

## Launch on the development workstation
Double-click out\Release\PLATZ.exe for the project hub, or run the following in PowerShell.

    & 'C:\PLATZ\out\Release\PLATZ.exe' 'C:\PLATZ\projects\PLATZ Validation.platz'

Use New project in the hub to create an ordinary editable .platz file. Select an object in the viewport or Scene panel. Use **Select**, **Move**, **Rotate**, or **Scale** above the viewport, or press **1–4** while the viewport has focus. Drag a colored axis or ring and release to apply a single undoable change. **World** and **Local** apply to Move and Rotate. Scale follows the object's axes. **Snap** uses the neighboring increment, measured in metres, degrees or scale units for the active tool. In the viewport, right-drag to orbit, middle-drag to pan, scroll to zoom, and press **F** to frame the selection.

Type a transform value or a six-digit sRGB color in the Inspector to preview it. Drag an axis label to scrub a number, or hold **Shift** for finer scrubbing. Enter or leaving the field applies valid text, and releasing applies a scrub. The color picker previews as you adjust it. Use **Escape** to cancel a text edit or drag, or **Cancel** in the color picker. Use **Ctrl+Z** and **Ctrl+Y** after committing changes.

Add object creates primitives. Edit > Repeat objects previews copies with world-space spacing. Create copies accepts the entire operation, and Ctrl+Z undoes it. F5 starts Play. Click a block or column to turn it and use WASD to move. Escape restores the authored scene and camera. Ctrl+S saves. The Project menu can restore the previous saved revision as an undoable edit.

## Personalize Studio

Use the toolbar workspace picker or **Workspace → Switch workspace**. **Modeling** shows Scene, Inspector and the mesh library. **Materials** opens a wider Inspector near the material controls. **Texture** brings the image library forward. **Playtest** gives the viewport more space and keeps Play/Stop in the toolbar. Drag dock titles to rearrange or float panels, resize dividers, and use the View menu to show panels again.

Choose **Workspace → Save workspace as** to create a named copy. **Reset workspace** restores the active preset's default or the original arrangement of a saved copy. Studio remembers dock placement, sizes, floating positions, visibility and the Assets tab for each workspace. Earlier single-layout preferences appear as **Custom**.

In **View → Customize Studio**, choose Slate, Light or High contrast, select an accent, and adjust interface scale from 85% to 140%. Configure Save, Play/Stop, Frame Selection and Create Image in the Shortcuts tab. Studio checks conflicts before applying any changes. Escape stays available for stopping Play or canceling an edit. Use **Restore Defaults** to reset appearance and shortcuts. Press **Ctrl+K** to find and run available menu commands. Workspaces and preferences belong to the local user profile.

## Create a reusable mesh

Open **Create → 3D Mesh** and choose **Cube**, **Sphere**, or **Cylinder**. Set its dimensions and tessellation while the viewport previews the result. Cylinders also support taper through the top diameter multiplier. Turn off **Place an instance in the scene** to save only the reusable asset. Select the asset in **Assets** and choose **Add instance** or **Assign to selection**.

Double-click a native asset or choose **Manage → Edit mesh parameters** to preview a change on every linked instance. Apply commits one Undo step and preserves instance transforms and material overrides. **Manage** also offers rename, duplicate, and **Make selected instance unique**. From an instance, **Locate editable source** selects the asset in the library.

Save the project to retain the editable recipe and references in schema v7. **Export mesh GLB** writes derived positions, normals, UV0 and indices. The exported GLB imports as a static mesh. Editable parameters, instance transforms and instance tint stay in the .platz project. The existing **Save original GLB** command still extracts the exact bytes of imported assets.

## Paint and assign a texture

Select a native mesh or an imported mesh material slot with UV0. Open **Create → Image / Texture** to paint a project image. The canvas has a pencil, eraser, connected fill, eyedropper, RGBA color picker, zoom and grid. Change the width and height to crop or extend the canvas at its top-left origin. The hex field uses `#RRGGBBAA`. Canvas Undo/Redo works on strokes, fills and dimension changes. Enable **Assign to selected material slot** to see the draft on your mesh. **Create image** or **Apply to image** commits the draft as one project edit. Cancel restores the previous scene.

The **Images** tab in Assets lists editable sources and usage counts. Double-click a source to edit it across its linked material slots. Use **Default texture** in the Inspector to detach a project image and restore the slot’s prior embedded-texture or flat-color setting. Use **Locate image source** to select the source in Assets. Manage offers assignment, rename, duplication, **Make selected slot unique**, and removal of unused images.

Save the project to store exact RGBA pixels, source IDs, and material assignments in schema v8. Use **Export PNG** to write those pixels losslessly and **Import** on the Images tab to bring a PNG back as an independent editable source. Alpha is available in the canvas and PNG. Studio’s Vulkan material samples RGB and remains opaque. Project image assignments stay in the .platz project when exporting geometry to GLB.

## Import and use existing assets

The **Assets** dock lists embedded GLB meshes and their instance counts. Use **Import…** to keep a source in the library, **Add instance** to place it, or **Assign to selection** to change the geometry of the selected object while preserving its identity, placement, and color. In the validation project you can select **Low spire** in Assets and assign it to **Oblique spire** in the Scene panel. The **Manage** menu can replace every instance from a different GLB, save the original GLB, or remove a mesh with zero uses. These project changes support Undo and Redo.

Use **Add object → Import mesh from GLB…** to import and place in one step. The importer accepts one static mesh with up to 32 indexed triangle primitives and an embedded buffer, up to 2 MiB per file. Imported GLB bytes and derived native GLB sizes share the 8 MiB mesh-library budget. Saving embeds the exact imported source bytes and keeps the .platz project portable. Project images have a separate budget of 32 sources and 8 MiB of RGBA pixels.

Select **Color study** in the validation project. Under **Material** in the Inspector, choose **Copper** or **Lagoon** and edit the sRGB hex value or use the swatch to pick a color. Parts that share a GLB material share a slot. A slot edit applies to the selected object. **Reset** restores the imported color, and Undo restores the previous edit. The importer reads RGB base-color factors. The preview renders opaque colors.

Select **Harbor signs** to try textures. Under **Material**, choose **Signal flags** (PNG) or **Harbor mark** (JPEG). **Use imported texture** switches the selected slot between its embedded image and flat base color. While the texture is enabled, the base color tints it. The checkbox setting is saved per object and supports Undo and Redo. The current slice uses embedded PNG/JPEG images up to 1024 × 1024, UV0, and single-level sampling. Older asset projects keep their flat appearance until you enable a supported imported texture.

## Build and verify
On Windows x64 with VS 2026 Desktop development with C++, Windows SDK 10.0.26100, VS CMake tools, Python 3.9+ with the py launcher, Windows tar and curl:

    Set-Location C:\PLATZ
    .\tools\build.ps1 -Test

Initial setup requires network to retrieve the hash-checked Qt Base, SDL, Vulkan SDK components, Slang and VMA. Setup caches them in .deps. The development executable uses the adjacent Qt and SDL DLLs, platforms\qwindows.dll, the x64 MSVC runtime, Windows SQLite, and a Vulkan 1.4 driver with the engine-required device capabilities. Keep the deployment directory intact. This checkout-relative launch includes the validation project at projects\PLATZ Validation.platz. No external account or shader compiler is required to run the built editor.

Regenerate the checked-in Windows and Qt icon assets with `py tools/generate_brand.py` after changing the capital P artwork.
