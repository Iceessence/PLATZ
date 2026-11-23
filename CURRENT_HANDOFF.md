# Current handoff

**Candidate:** PLATZ Studio 0.3.0, Windows x64 Release, pending independent review. [Owner status](OWNER_STATUS.md) · [Launch and tools](START_HERE.md) · [Master and feature decisions](DECISIONS_AND_FEATURE_LEDGER.md)

## Current integration

The paint surface now lives in the central Studio workspace beside the actual Vulkan viewport. Texture exposes image commands and the image library. Painting brings the canvas forward and temporarily removes the Scene and Inspector docks. Apply uses the same Core image and material-slot references already used by ordinary Studio, GPU preview and Play. The previous layout is restored when the draft closes. Play and Save are gated during the draft, and closing or switching projects asks before discarding it. Brush navigation supports arrows and Space, with stroke Undo/Redo isolated from project history. Source RGBA remains exact and exports to PNG. The 3D viewport still displays opaque base color.

Changing a base-color image now reuses unchanged GPU images and samplers when the texture set changes. Each batch still owns its descriptor pool. Earlier draws retire through the existing graphics queue fence before the old descriptors and unreferenced images are freed. Studio requests a coalesced texture preview every 16 ms during continuous edits. Fill uses connected scanline runs over the source RGBA buffer.

**Open validation workspace** reuses `.local/validation/PLATZ Validation.platz` after the initial seed from `projects/PLATZ Validation.platz`. The command is available in the hub and Project menu. The tracked reference is the permanent station source. The local file carries the same project identity and stays intact across builds. Tests use copies of the reference where mutations or restart checks require isolation.

## Validate the same working project

| Station | How to exercise | What to observe |
|---|---|---|
| Scene and transforms | Open the validation workspace and choose Silver block. Move or rotate it, cancel a drag with Escape, and apply an Inspector edit. | Correct rendered selection and one project undo step for a committed edit. |
| Paint and shared source | Select Harbor signs, choose the Texture workspace, then New image. Keep Assign to selected material slot checked, fill or draw on the canvas, and inspect both signs beside it. | Only the linked material slot previews the draft. Discard restores the scene. Apply creates the source and links it in one Undo step. |
| Save and Play | Apply an image, save, enter Play, and click an object. Stop, undo, redo, and inspect the saved project revision. | Play leaves authoring and its source pixels intact. A build updates the Studio version independently of the project revision. |
| Existing asset checks | Inspect the native/built-in objects, Color study's slots, Harbor signs' two embedded sources, and the folder tree. | Stable object identity, unchanged source references, context-specific controls, and usable search. |

## Build and qualifications

Run `& .\tools\build.ps1 -Test` from `C:\PLATZ`. The installed artifact and runtime dependencies are in `C:\PLATZ\out\Release`. See [START_HERE.md](START_HERE.md) for prerequisites. Tested software commit `22a2d63762ba429ae8beee0d409c0020e449bb86`. The Windows executable identifies as Studio **0.3.0** and file version **0.3.0.0**. Its SHA-256 is `1F60370CDD06B2AD4C175FC4FD6BFBFA03F6CCCC88E19BA311A884EBFD6F57CB`.

`tools/build.ps1 -Test` passed **16/16** suites in 42.68 seconds, with zero failures, skips or Vulkan synchronization and GPU-assisted validation errors. SDK-pinned Windows x64 Release on the RTX 5080 with Vulkan 1.4. The image checks measured 16.15–25.36 ms from finishing an edit to synchronous test readback in the small validation scene. This includes the test capture and is not glass-to-glass input latency. The GPU check verified exactly one uploaded image when its pixels changed with the neighboring GLB texture resident. The foreground verification transcript is `build/studio-0.3.0-final.log`. Detailed tests are in `build/Testing/Temporary/LastTest.log`.

The live executable was launched once for the walkthrough and remains open in Texture at local saved project revision **13**. The live route selected Harbor signs, filled **Harbor paint validation**, applied it to the Signal flags slot, saved, turned the object during Play, stopped, and selected **Open validation workspace** again. Studio reported the workspace was already open and kept the selection. The tracked project remains at SHA-256 `D13C5C1ED4A7726DA4D758A30EB6DA6B96161CCB937156F45D7A8CBDCFA5F894`. The restart suite uses its isolated copy and two process launches because it specifically exercises restart durability.

Qualification corrected two test assumptions. Switching away from a deliberately corrupted GLB also changes the embedded texture's immutable key, so the reuse check now measures a change of pixels on the same source and neighboring slot. Qt dock restoration can differ by two client pixels, so the image test compares Vulkan captures at a common pixel extent.

The previous 0.2.0 software commit was `3cbd591`. Its verification is available through `git show 8c978fa:CURRENT_HANDOFF.md`. The earlier NEON V2 audit targets `94acce51` and `43c3c410`.

## Next integrated batch

Prioritize the Modeling authoring path. Bring existing parametric source editing into the Studio split without duplicating the renderer, show shared instance use, and carry the same selected source through the outliner, Inspector, validation project, save/reopen and Play. Keep topology editing, reusable Material assets and transform parenting visible as separate scope with their schema gate. High-end lighting and antialiasing, scalable atlas lifetime, and dedicated device-loss qualification remain open. The active envelope is A. No promotion or external certification is recorded.
