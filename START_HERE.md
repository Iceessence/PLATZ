# PLATZ development entry point

The permanent source root is C:\PLATZ. Start with CURRENT_HANDOFF.md for the tested revision and feature ledger. Product authority is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. The V4 developer directive and Vulkan steering are alongside it. ADR-001 is the active renderer decision, retaining the Core and Studio decisions in ADR-000.

## Launch on the development workstation
Double-click `C:\PLATZ\out\Release\PLATZ.exe` or run it in PowerShell to open the project hub. The hub, status bar, **Help → About PLATZ Studio**, and Windows executable properties identify the installed Studio version. The viewport displays the open project's revision separately. Update the top-level CMake project version when publishing a new Studio development build.

    & 'C:\PLATZ\out\Release\PLATZ.exe'

For an editable copy of the permanent validation scene, run the following.

    $platzReview = Join-Path $env:TEMP "PLATZ Review $([guid]::NewGuid()).platz"
    Copy-Item -LiteralPath 'C:\PLATZ\projects\PLATZ Validation.platz' -Destination $platzReview
    & 'C:\PLATZ\out\Release\PLATZ.exe' $platzReview

Use **New project** in the hub to create an editable `.platz` file. Select an object in the viewport or Scene panel. Use **Select**, **Move**, **Rotate**, or **Scale** above the viewport, or press **1–4** while the viewport has focus. Drag a colored axis or plane handle. The center moves in the screen plane or scales uniformly, depending on the tool. The outer rotation ring turns around the view axis. Release to apply one Undo step. Right-click or press **Escape** during a drag to restore the previous transform.

**World** and **Local** apply to Move and Rotate. Scale follows the object's axes. Choose **Object pivot** or **Bounds center** to set the pivot. **Snap** uses the adjacent increment in metres, degrees or scale units. Hold **Ctrl** to temporarily toggle snap or **Shift** to move at one-tenth speed during the drag. The strip beneath the viewport shows the active transform delta. Outside a drag, right-drag to orbit, middle-drag to pan, scroll to zoom, and press **F** to frame the selection.

Type a transform value or a six-digit sRGB color in the Inspector to preview it. Drag an axis label to scrub a number, or hold **Shift** for finer scrubbing. Enter or leaving the field applies valid text, and releasing applies a scrub. The color picker previews as you adjust it. Use **Escape** to cancel a text edit or drag, or **Cancel** in the color picker. Use **Ctrl+Z** and **Ctrl+Y** after committing changes.

Add object creates primitives. Edit > Repeat objects previews copies with world-space spacing. Create copies accepts the entire operation, and Ctrl+Z undoes it. F5 starts Play. Click a block or column to turn it and use WASD to move. Escape restores the authored scene and camera. Ctrl+S saves. The Project menu can restore the previous saved revision as an undoable edit.

## Personalize Studio

Use the toolbar workspace picker or **Workspace → Switch workspace**. **Modeling** shows Scene, Inspector and the mesh library. **Materials** opens a wider Inspector near the material controls. **Texture** brings the image library forward. **Playtest** gives the viewport more space and keeps Play/Stop in the toolbar. Drag dock titles to rearrange or float panels, resize dividers, and use the View menu to show panels again.

Choose **Workspace → Save workspace as** to create a named copy. **Reset workspace** restores the active preset's default or the original arrangement of a saved copy. Studio remembers dock placement, sizes, floating positions, visibility and the Assets tab for each workspace. Earlier single-layout preferences appear as **Custom**.

In **View → Customize Studio**, choose Slate, Light or High contrast, select an accent, and adjust interface scale from 85% to 140%. Configure Save, Play/Stop, Frame Selection and Create Image in the Shortcuts tab. Studio checks conflicts before applying any changes. Escape stays available for stopping Play or canceling an edit. Use **Restore Defaults** to reset appearance and shortcuts. Press **Ctrl+K** or click **Search Studio** to find commands, scene objects, meshes, and images. Use the arrow keys and Enter to open a result. Workspaces and preferences belong to the local user profile.

## Organize the project in Studio

In **Scene**, **Assets → Meshes**, or **Assets → Images**, open **Organize → New folder**. Drag a row onto a folder to move it, or use **Move to folder** for a keyboard-accessible picker that includes the top level. Select a folder and use **New folder** to create a nested folder. Right-click a row for context actions. Each panel has a search field. Meshes and Images also offer **Organize → Filter by use**.

Studio stores folders in the local user profile for the project's stable ID. They persist across launches on this profile and organize the editor view. Project files carry the objects and assets. The Scene folders have no effect on transforms, rendering, or Play.

Select an object in Scene or the viewport to open its Inspector. Expand **Transform**, **Mesh**, or **Material overrides** to edit the corresponding settings. Use **Mesh source** to choose a project mesh or **Make mesh editable** to create a UV-mapped source from a built-in primitive. For a native mesh, **Editable mesh** offers **Edit shared mesh** and **Make mesh unique**.

Use **Copy appearance from** to copy a compatible slot's base color and texture settings onto the current object. The copied settings are per instance. A project-image assignment references the existing asset. Use **Make image unique** before editing the pixels independently. Controls appear where the selected object and material slot support them.

## Create a reusable mesh

Open **Create → 3D Mesh** and choose **Cube**, **Sphere**, or **Cylinder**. Set its dimensions and tessellation while the viewport previews the result. Cylinders also support taper through the top diameter multiplier. Turn off **Place an instance in the scene** to save only the reusable asset. Select the asset in **Assets** and choose **Place in scene** or **Assign to selection**.

Double-click a native asset or choose **Manage → Edit mesh parameters** to preview a change on every linked instance. Apply commits one Undo step and preserves instance transforms and material overrides. **Manage** also offers rename, duplicate, and **Make selected instance unique**. From an instance, **Locate editable source** selects the asset in the library.

Save the project to retain the editable recipe and references in schema v7. **Export mesh GLB** writes derived positions, normals, UV0 and indices. The exported GLB imports as a static mesh. Editable parameters, instance transforms and instance tint stay in the .platz project. The existing **Save original GLB** command still extracts the exact bytes of imported assets.

## Paint and assign a texture

Select a native mesh or an imported mesh material slot with UV0. Open **Create → Image / Texture** to paint a project image. The canvas has a pencil, eraser, connected fill, eyedropper, RGBA color picker, zoom and grid. Change the width and height to crop or extend the canvas at its top-left origin. The hex field uses `#RRGGBBAA`. Canvas Undo/Redo works on strokes, fills and dimension changes. Enable **Assign to selected material slot** to see the draft on your mesh. **Create image** or **Apply to image** commits the draft as one project edit. Cancel restores the previous scene.

The **Images** tab in Assets lists editable sources and usage counts. **New image** opens the project image editor directly. Double-click a source to edit it across its linked material slots. Use **Default texture** in the Inspector to detach a project image and restore the slot’s prior embedded-texture or flat-color setting. Use **Locate image source** to select the source in Assets. Manage offers assignment, rename, duplication, **Make selected slot unique**, and removal of unused images.

Save the project to store exact RGBA pixels, source IDs, and material assignments in schema v8. Use **Export PNG** to write those pixels losslessly and **Import** on the Images tab to bring a PNG back as an independent editable source. Alpha is available in the canvas and PNG. Studio’s Vulkan material samples RGB and remains opaque. Project image assignments stay in the .platz project when exporting geometry to GLB.

PNG and GLB exports ask before replacing an existing file and publish the new file after the write succeeds.

## Import and use existing assets

The **Assets** dock lists embedded GLB meshes and their instance counts. Use **Import…** to keep a source in the library, **Place in scene** to place it, or **Assign to selection** to change the geometry of the selected object while preserving its identity, placement, and color. In the validation project you can select **Low spire** in Assets and assign it to **Oblique spire** in the Scene panel. The **Manage** menu can replace every instance from a different GLB, save the original GLB, or remove a mesh with zero uses. These project changes support Undo and Redo.

Use **Add object → Import mesh from GLB…** to import and place in one step. The importer accepts one static mesh with up to 32 indexed triangle primitives and an embedded buffer, up to 2 MiB per file. Imported GLB bytes and derived native GLB sizes share the 8 MiB mesh-library budget. Saving embeds the exact imported source bytes and keeps the .platz project portable. Project images have a separate budget of 32 sources and 8 MiB of RGBA pixels.

Select **Color study** in the validation project. Under **Material overrides** in the Inspector, choose **Copper** or **Lagoon** and edit the sRGB hex value or use the swatch to pick a color. Parts that share a GLB material share a slot. A slot edit applies to the selected object. **Reset** restores the imported color, and Undo restores the previous edit. The importer reads RGB base-color factors. The preview renders opaque colors.

Select **Harbor signs** to try textures. Under **Material overrides**, choose **Signal flags** (PNG) or **Harbor mark** (JPEG). **Use imported texture** switches the selected slot between its embedded image and flat base color. While the texture is enabled, the base color tints it. The checkbox setting is saved per object and supports Undo and Redo. The current slice uses embedded PNG/JPEG images up to 1024 × 1024, and UV0. Imported textures can use GPU-generated mipmaps when the sampler calls for them. Older asset projects keep their flat appearance until you enable a supported imported texture.

## Build and verify
On Windows x64 with VS 2026 Desktop development with C++, Windows SDK 10.0.26100, VS CMake tools, Python 3.9+ with the py launcher, Windows tar and curl:

    Set-Location C:\PLATZ
    .\tools\build.ps1 -Test

Initial setup requires network to retrieve the hash-checked Qt Base, SDL, Vulkan SDK components, Slang and VMA. Setup caches them in .deps. The development executable uses the adjacent Qt and SDL DLLs, platforms\qwindows.dll, the x64 MSVC runtime, Windows SQLite, and a Vulkan 1.4 driver with the engine-required device capabilities. Keep the deployment directory intact. This checkout-relative launch includes the validation project at projects\PLATZ Validation.platz. No external account or shader compiler is required to run the built editor.

Regenerate the checked-in Windows and Qt icon assets with `py tools/generate_brand.py` after changing the capital P artwork.
