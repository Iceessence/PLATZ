# PLATZ development entry point

The permanent source root is C:\PLATZ. Start with CURRENT_HANDOFF.md for the tested revision and feature ledger. Product authority is docs/authority/PLATZ_MASTER_ENGINE_FIRST_V4.md. The V4 developer directive and Vulkan steering are alongside it. ADR-001 is the active renderer decision, retaining the Core and Studio decisions in ADR-000.

## Launch on the development workstation
Double-click out\Release\PLATZ.exe for the project hub, or run the following in PowerShell.

    & 'C:\PLATZ\out\Release\PLATZ.exe' 'C:\PLATZ\projects\PLATZ Validation.platz'

Use New project in the hub to create an ordinary editable .platz file. Select an object in the viewport or Scene panel. Edit name, position, rotation, scale or color in the Inspector. Right-drag to orbit, middle-drag to pan, scroll to zoom, and press F in the viewport to frame the selection. Add object creates primitives. Edit > Repeat objects previews copies with world-space spacing. Create copies accepts the entire operation, and Ctrl+Z undoes it. F5 starts Play. Click a block or column to turn it and use WASD to move. Escape restores the authored scene and camera. Ctrl+S saves. The Project menu can restore the previous saved revision as an undoable edit.

## Build and verify
On Windows x64 with VS 2026 Desktop development with C++, Windows SDK 10.0.26100, VS CMake tools, Python 3.9+ with the py launcher, Windows tar and curl:

    Set-Location C:\PLATZ
    .\tools\build.ps1 -Test

Initial setup requires network to retrieve the hash-checked Qt Base, SDL, Vulkan SDK components, Slang and VMA. Setup caches them in .deps. The development executable uses the adjacent Qt and SDL DLLs, platforms\qwindows.dll, the x64 MSVC runtime, Windows SQLite, and a Vulkan 1.4 driver with the engine-required device capabilities. Keep the deployment directory intact. This checkout-relative launch includes the validation project at projects\PLATZ Validation.platz. No external account or shader compiler is required to run the built editor.
