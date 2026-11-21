# PLATZ — COMPLETE RESUMPTION AND VULKAN ENGINEERING DIRECTIVE
## Authoritative steering instructions for the current `C:\PLATZ` project

**Product:** PLATZ — One Place. Zero Limits.

**Mission:** Continue building one enduring, professional creative platform with a high-end Vulkan-first game engine, deeply integrated Studio and Runtime, editable canonical project data, no-code and programming systems, and original PLATZ AI.

**Authority:** Read the current approved PLATZ Master, the developer directive, the independent snapshot audit, and this message. This message supersedes conflicting older *implementation* preferences. Do not discard the Master's long-term feature vision or security requirements. If repository source or tests contradict historical reports, verify and treat current source and reproducible results as implementation truth.

## 1. RESUME THE REAL PROJECT; DO NOT RESTART

Continue in `C:\PLATZ`. The previous AI ran out of usage mid-implementation. Inspect the **live** repository first: branch, status, modified/untracked files, source, dependencies, build scripts, and tests. The independent audit examined an older ZIP; do not assume it reflects the latest files. Preserve the C++ Core, working scene/project features, source history, and any useful graphics/editor work. Create a recoverable Git checkpoint before major refactoring; do not blindly add generated binaries, private files, or vendored bundles to Git. Never delete or replace unrelated work. Do not create `PLATZ2`, another game, or a throwaway editor. Small targeted test executables are permitted, but only one real PLATZ product and one growing internal validation project exist.

The earlier snapshot reportedly passed eight Core tests and a crash-recovery experiment, but did not establish a fully buildable Studio. Re-run applicable tests yourself and report exact results; do not adopt the audit as a substitute for current verification.

## 2. BINDING ENGINEERING DIRECTION

Use **native modern C++** for PLATZ Core, Engine and Runtime; prefer C++23 when current compiler/dependency support is sound, otherwise justify C++20. Use a repeatable CMake build and pinned, licensed dependencies. **Vulkan 1.4 is the primary graphics API.** This is a product preference, not a tentative optional feature. Direct3D 12 may be preserved as historical research or a future secondary backend, but stop expanding it as the primary renderer. Verify installed Vulkan loader, SDK, driver, RTX 5080 feature availability, and validation tool versions; use the newest *suitable stable* versions, never an older toolchain simply because it is already installed and never an unqualified prerelease just for its version number.

For windowing, native platform events, and input, **SDL3 is preferred** with direct Vulkan surface creation. Do not mistake SDL GPU's abstraction for a requirement to surrender PLATZ's own Vulkan renderer. For Studio, prefer a **PLATZ-owned, engine-integrated UI component and customization architecture** using reliable third-party foundations where justified. Qualify text editing, font shaping, IME, clipboard, focus, docking, high-DPI, accessibility, localization, and input before discarding working Qt code. Qt may remain a bounded transitional adapter if that is demonstrably safer; do not let it dictate the renderer or become permanent solely from sunk cost. Make one focused UI decision, not a month of toolkit exploration.

Use a **PLATZ-owned canonical project model**. Retain SQLite for appropriate transactional metadata, object identity and revision records; use a separately managed content-addressed or similarly robust store for large binary assets. Do not invent a new general-purpose database. Cross-store commits need recoverable ordering and crash tests. Core source data must remain independent of any rendering backend and UI framework.

Record the chosen architecture, versions, dependency licensing and key tradeoffs once in an architecture decision record. Reopen only when tests establish a material defect or significant advantage. Do not stop at the decision document: implement the application.

## 3. GPU MEMORY ALLOCATION — USE VMA, BUT PLATZ OWNS MEMORY POLICY

**Default decision: use the mature Vulkan Memory Allocator (VMA) library** at a pinned, compatible stable release. Do **not** build a custom replacement for Vulkan device-memory allocation, memory-type selection, suballocation, or fragmentation handling simply to claim ownership. Qualify VMA against Vulkan 1.4 and the actual chosen loader/driver. Keep VMA wrapped behind **PLATZ-owned typed GPU resource interfaces** so the library can evolve without redefining scenes or materials.

PLATZ owns the higher-level engineering that VMA does not solve on its own: buffers/images/samplers ownership; RAII handles and generation IDs; allocation intent and usage classes; CPU-visible upload/readback rings and staging; persistent and transient resource pools; per-frame lifetime and deferred destruction; alignment and memory coherence; sparse/streaming-resource strategy; asset residency and eviction; memory telemetry; out-of-memory recovery; project/viewport/AI workload budgets; device loss behavior; GPU-to-CPU synchronization before reuse/destruction. Avoid calling Vulkan's raw memory-allocation API per mesh, image, or draw.

Query real device heap budgets when available (for example via `VK_EXT_memory_budget`) and enforce PLATZ's own **conservative** limits. VMA reporting is not a guarantee of available physical VRAM, nor does it account for all competing applications or CUDA/model workloads. Differentiate reserved, committed, resident, peak, transient, and evictable bytes. Provide honest measurements and graceful degradation instead of assuming 16 GB is entirely free. Never defragment or move a live resource without updating GPU references and respecting in-flight use. Only introduce custom specialized suballocators above VMA when profiling proves benefits (e.g., ring buffers, transient heaps, tightly managed atlas/buddy pools).

## 4. SHADERS — PLATZ OWNS THE SHADERS, NOT THE COMPILER

**Do not write a custom shader programming language, SPIR-V compiler, or manual SPIR-V encoder at this stage.** PLATZ should author and own its rendering algorithms, shader modules, material graphs, shader parameter conventions, and pipeline behavior, using proven compilers to emit **validated SPIR-V**.

**Preferred high-level choice: Slang**, subject to a **single bounded qualification** on this exact platform. Test representative vertex/fragment, compute, material parameter/reflection, shader includes/modules, and one relevant advanced feature against Vulkan 1.4. Check reproducible outputs, compiler stability, debugging, generated SPIR-V validation, and license/redistribution obligations. Slang is attractive for modular HLSL-like shader code, reflection, specialization, and future multi-backend work.

**Fallback/reference: HLSL compiled with DXC to SPIR-V**, which is an established Vulkan route. GLSL/glslang may be supported for compatibility or imported assets, not become a second unmaintained first-party shading architecture. If Slang introduces a concrete blocker, select DXC, document it once, and continue. Do not spend several sessions maintaining Slang and DXC as equally elaborate production pathways before a need is proven.

Implement a coherent **shader toolchain**: versioned source, include/dependency graph, offline build or asset-pipeline compilation, explicit entry points and target environment, SPIR-V validation (`spirv-val`), optional optimization only if tested, reflection-derived descriptor/push-constant layouts, shader-to-material contracts, diagnostics with file/line, shader variant control, cache keys including relevant source/defines/compiler/target settings, and a safe last-known-good shader on hot-reload failure. Runtime hot reload belongs to authoring/development; packaged builds should not depend on compilers being installed unless an explicit supported user-shader workflow requires them. Pipeline cache data must be treated as driver/device-specific and safely invalidated.

Eventually, the visual Material Editor should compile editable material graphs into the same qualified shader pipeline. Preserve graph source and parameters rather than flattening everything to a compiled binary. Define clear 2D pixel-exact and 3D HDR/PBR color paths so temporal antialiasing or tone mapping does not blur pixel art.

## 5. VULKAN ARCHITECTURE — OWN THE RENDERER, KEEP IT TESTABLE

Build a narrow, well-structured Vulkan backend with **explicit device capability negotiation**, not unconditional RTX-only assumptions. Record queue families, Vulkan version, presentation formats, memory heaps, limits, supported extension/feature chains, ray-tracing and mesh-shader availability. Core requirements must be distinguished from optional features with lower-tier fallback; don't silently enable unsupported extensions.

Engine components should include, as justified by real working functionality:

- Vulkan instance/device/queues, debug naming, validation and error reporting.
- SDL3-created Vulkan surface and reliable swapchain/present lifecycle.
- Typed resource wrappers, VMA-backed allocations, staging/upload/readback and safe deferred destruction.
- Frame contexts, command pools/buffers, multi-frame-in-flight accounting, timeline semaphores where appropriate, and correct binary synchronization for WSI presentation.
- **Synchronization2** for explicit execution/memory dependencies and queue ownership transitions; **dynamic rendering** where suitable; no broad barriers just to silence validation.
- Resource-state tracking for image layouts and buffer hazards; per-frame/transient resource lifetime and aliasing only when safe.
- Descriptor layouts, sets/pools, binding lifetime, per-frame updates and reflection. Use descriptor indexing/bindless approaches only when justified and supported. Treat descriptor buffers and push descriptors as optional measured choices, not mandatory modernity labels.
- Pipeline state management, asynchronous/precompiled variants where safe, pipeline caching, meaningful shader diagnostics, and stutter measurement.
- The 3D renderer's scene passes (depth, opaque, lighting, transparent, UI, picking, post-processing) with a **small explicit pass schedule first**; introduce a validated render graph/frame graph when pass dependencies and resource transitions justify it, not as an oversized first-day framework.
- Viewports with resize, minimize, DPI, fullscreen/windowed, multi-monitor, swapchain recreation, error recovery, and honest device-loss handling.
- GPU timestamps, CPU frame timing, resource counts, transient allocation peaks, shader/pipeline compile timings and a debugging overlay.

Prefer **correctness first, then benchmark-directed optimization**. Measure before deciding forward vs deferred, bindless strategies, meshlet architectures, async compute schedules, transient aliasing, or expensive synchronization refactors. Vulkan alone does not make an engine fast. Avoid hidden device-idle waits per frame, unnecessary map/unmap, unnecessary pipeline creation during rendering, one allocation per tiny resource, and unsafe in-flight descriptor or buffer updates.

For future graphics, preserve credible paths toward PBR and material graphs, geometric LOD and streaming, GPU culling/indirect draws, virtual textures, large-world precision, compute effects, shadows, ray-traced acceleration structures and GI, path tracing, and HDR. **Do not claim these features exist until they are actually implemented and measured.** Do not rush ray tracing before correct basic scene rendering, memory, synchronization, materials, and profiling work.

## 6. GPU DEBUGGING, QUALIFICATION AND QUALITY GATES

Run **Khronos validation layers** on development builds, including synchronization and GPU-assisted modes on targeted checks when tool support and runtime cost make them appropriate. Use **RenderDoc** for frame inspection and **Nsight Graphics** for NVIDIA profiling/debugging as available and permitted. Provide a CPU/GPU timing path and detailed GPU-capability log. Enable debug object labels and reproducible GPU captures for defects where possible. Do not report "optimized" based only on code structure, a single FPS screenshot, or absence of validation warnings.

Introduce targeted negative/regression tests for: invalid/stale handles; resource destruction while in flight; swapchain resize/minimize/recreate; bad shader compilation and hot-reload recovery; invalid resource transitions; missing queue-family ownership; descriptor reuse while executing; rapid project open/close; incorrect sRGB/HDR and pixel-exact results; device/feature absence; GPU budget exhaustion and graceful fallback; interrupted asset writes; crash recovery; Play/Stop authoring isolation; stale AI mutations; and path/permission escapes. Not all tests can be fully automated immediately—track **not tested** distinctly from **passed**.

Keep meaningful benchmark scenes in the **one permanent internal validation project**, alongside focused test fixtures. Establish a stable benchmark capture including GPU, driver, build SHA, resolution, render scale, scene content, feature settings, CPU/GPU frame time distributions, VRAM use and loading/compilation stutter. Set numerical performance budgets after measuring representative workloads, rather than inventing a guaranteed 4K/120 FPS claim. Protect lower-tier hardware via selectable quality and explicit capability tiers.

## 7. RENDERING AND LOCAL AI MUST SHARE GPU RESOURCES SAFELY

PLATZ AI is a central goal, not a decorative chat panel. Begin with an authorized, typed, versioned project command interface; make edits reviewable, revision-aware, undoable, testable and appropriately staged. Continue toward **original PLATZ-trained local specialist models**, verified procedural knowledge and qualified coordinated agents; label any external pretrained model clearly as such.

The GPU/VRAM budget manager must coordinate **editor rendering, game runtime, textures/geometry, local inference, generation and idle-only training**. Define authoring-priority, bounded generation, and idle-training operating policies. Never allow a background model to consume all remaining VRAM and crash active rendering. Track actual usage/estimates, limit job concurrency, release or unload inactive model resources, and provide cancellation and failure recovery. A Vulkan graphics allocator alone cannot arbitrate allocations held by separate ML/CUDA processes; enforce process-level policy too.

Do not begin extensive model-training infrastructure at the expense of delivering a working Engine/Studio, but do not remove original specialist intelligence from the roadmap.

## 8. PLATZ-OWNED PROFESSIONAL STUDIO — NO DISCONNECTED UI

Maintain one integrated Studio with working hierarchy, viewport, Inspector, project browser, creation tools, workspaces and relevant visible status. Prefer a PLATZ-owned UI component/data-binding/customization model with SDL3 and qualified supporting libraries; reuse working Qt code if a bounded transition is necessary. Test real text input, IME, focus, clipboard, docking, resizing, accessibility and high-DPI rather than judging UI technology from screenshots. Safety-critical Stop/permission/review controls must remain protected from arbitrary customization.

Whenever a feature changes, implement the relevant **engine + Core + user interface + project persistence + undo/redo + runtime + testing** integration in the same coherent batch. Add no-code, scripting and AI tool integration as those real interfaces become operational. Do not mark a feature finished if its applicable editor/persistence/runtime surfaces are absent or broken. Do not manufacture placeholder subsystems just to check a box.

Keep the actual edited project as the authoritative source. Editing a real object must change what Vulkan renders; Play must operate from the same scene semantics without overwriting authoring state. No separate fake preview scene, game demonstration application or competing editor.

## 9. CANONICAL SOURCE, TRANSACTIONS AND PROJECT SAFETY

Preserve the working C++ project Core and its tests. Separate canonical editable scene/assets from GPU caches and compiled runtime projections. Keep stable identities, typed operations, revision preconditions, controlled undo/redo and atomic or recoverable edits. SQLite may store relational metadata and revision heads; immutable content-addressed assets may hold large payloads. Define and test crash-safe ordering between file writes/fsync and committed project references. Avoid automatically claiming that SQLite transactions protect external files. Use safe path handling, schema migration, lineage and recovery strategies; add complex project-format features as concrete workflows require them.

Maintain real create/edit/save/close/reopen/Play/Stop user workflows. Build a project using PLATZ; don't substitute manually edited fixture JSON for UI functionality. Use one internal living validation world and targeted automated tests.

## 10. REQUIRED FIRST ACCEPTANCE SLICE (NOT A DEMO)

The first meaningful fully verified PLATZ application capability must demonstrate:

1. Build and launch the **real PLATZ executable**, with exact tested path.
2. Display a **real Vulkan 3D viewport** with depth, perspective-capable camera and actual GPU-submitted scene geometry.
3. Create/open a normal editable PLATZ project with stable object identity.
4. Select through actual viewport picking; display the selected object in the actual hierarchy and Inspector.
5. Change a transform/property and see the engine-rendered result update.
6. Add/duplicate/delete a normal object, with correct undo/redo and saving where implemented.
7. Enter Play, exercise a genuine runtime interaction using the same engine, and stop without mutating the authoring project.
8. Save, close and reopen the project with content and identity preserved.
9. Pass applicable Core, graphics-lifetime, shader, UI, persistence and integration checks; document anything blocked by this environment.
10. Make the application usable and visually cohesive, not a debugging window or decorative mockup.

A floor, light and mesh inside the real, permanent PLATZ renderer are acceptable incremental assets. **A separate toy game, CPU isometric renderer, or screenshot-based mockup is not.** Do not declare the whole acceptance slice complete until all claimed steps are actually exercised. Build larger feature sets only on the confirmed foundation.

## 11. DEVELOPMENT EFFICIENCY, LOOPS AND CONTINUITY

Do not restart from scratch, add unnecessary new repos, invent new product masters, or spend entire sessions debating frameworks. Use a **single short bounded qualification** for unresolved Slang-vs-DXC and custom-UI-vs-Qt choices; select an implementation and proceed. Targeted current documentation and real experiments are appropriate; repeated broad research without new evidence is not. Pin dependencies and record the basis for decisions, versions and licensing so future agents do not repeat the research.

Choose substantial, connected work batches that deliver visible user value. A task is not complete until its applicable UI, data, runtime and tests are updated. If two comparable attempts fail, stop the loop, identify the failed assumption, create a minimal reproducer and switch approach. Avoid passing the same compile error between agents without a verified root-cause change. Preserve useful source and experiment results rather than treating churn as progress.

Maintain a canonical repository handoff containing current commit and working tree status; actual architecture; exact build/run commands; passing/failing/unrun tests; what works today; known defects; next highest-value integrated task; and any owner approval needed. Make coherent Git commits only after appropriate local verification. Do not self-certify independently accepted milestones. Preserve owner control and privacy: no personal accounts, credentials, subscriptions, purchases or unapproved risky downloads and execution.

## 12. IMPLEMENTATION ORDER FOR THE NEXT ACTIVATION

**A. Source truth:** Inspect `C:\PLATZ`; compare with the independent audit; checkpoint uncommitted work; rerun Core tests; identify missing Studio build pieces. Report a concise inventory.

**B. Architecture commitment:** Confirm Vulkan 1.4/driver support and SDL3 integration; choose VMA as allocator; qualify Slang as preferred shader toolchain with DXC fallback; make one bounded professional UI technology decision, preserving reusable Qt code. Write a concise ADR, then stop general research.

**C. Real backend:** Build the actual Vulkan backend in PLATZ—device, presentation, resource wrappers/VMA, shader pipeline/SPIR-V, depth/perspective, frame sync, simple passes and GPU validation. Do not hide failures behind a separate throwaway executable.

**D. Real Studio integration:** Connect the Vulkan viewport to actual scene Core, selection, hierarchy, Inspector and edits, with usable professional UI.

**E. Durable authoring/runtime:** Add verified save/reopen and Play/Stop integration without breaking the Core regression suite; establish the permanent internal validation project and automated checks.

**F. Quality and delivery:** Run all accessible builds/tests and at least one actual GUI launch where possible; record GPU validation results, build/runtime limits, relevant performance measurements; fix the highest-impact known defects; update handoff and commit coherent verified changes.

Keep progression practical: not every future graphics feature belongs in this first activation. Make the foundation real and extensible, then keep shipping substantial, complete improvements rather than reopening architecture debates.

## 13. PROGRESS REPORTS AND NON-NEGOTIABLE FINAL DELIVERY

During significant work provide approximately five **substantive** progress updates when practical, not trivial narration. At the end report: selected Vulkan/SDL3/UI/VMA/shader stack and **why**; source files and commits; actual functioning user workflow; core and GPU tests executed; validation errors; performance measurements with their conditions; outstanding limitations; next high-value work; and the exact **verified** `.exe` location plus launch command if an application actually runs. Explicitly distinguish local `C:\PLATZ` outputs from inaccessible remote artifacts. If no executable was built, state the blocker; never invent the path or claim launch success.

**The permanent rule:** Improve the one existing PLATZ application for greatest verified long-term value. Build a correct, modern Vulkan engine with mature low-level tools and PLATZ-owned high-level architecture. Ship genuinely integrated user capabilities, not isolated technical experiments or unverified demonstrations. Preserve the original PLATZ AI goal, protect user data, use development usage efficiently, and make each subsequent session easier to continue.

**Continue implementation now. PLATZ — ONE PLACE. ZERO LIMITS.**
