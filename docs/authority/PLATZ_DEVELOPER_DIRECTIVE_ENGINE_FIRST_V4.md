# PLATZ — FINAL ENGINE-FIRST AI DEVELOPER EXECUTION DIRECTIVE

**Edition 4.0 — Corrected architecture selection, latest stable technology, no demos, and genuine production development**  
**Product:** PLATZ — One Place. Zero Limits.  
**Authoritative product vision:** `PLATZ_MASTER_ENGINE_FIRST_V4.md`  
**Status:** Development instructions for one new enduring PLATZ application. Not a claim that any capability has already been implemented.

## 0. ABSOLUTE PRIORITY

You are creating **a professional, fully integrated, AAA-and-beyond-ambition creative platform centered on a real game engine**, not a quick desktop application, an isometric demo, or a UI-only prototype. The master specifies the full long-term ambition; this directive governs all implementation decisions. Read the master in full for the first activation and honor the 401 stable capability contracts, with active release scope determined by actual dependencies and evidence.

> **Improve the existing PLATZ application in the way that delivers the greatest verified long-term value. Research the best currently available approaches, preserve existing functionality, and prioritize integrated product quality over the number of features or demonstration projects created.**

**Do not interpret “first build” or “minimum integrated slice” as permission to choose an inadequate architecture.** Begin an authentic permanent engine immediately. Deliver a bounded but real production-capable capability, not a pretend version of a future tool.

## 1. STOP AND CORRECT ANY CONVENIENCE-BASED START

If work has already begun with **.NET 10 chosen primarily because it was installed**, WPF isometric or CPU-side pseudo-3D rendering, a separately titled throwaway game, or an empty Hub instead of a true engine: **pause expansion of those choices immediately**. Preserve the current repository and all code and data on a named preservation branch or commit. Do not delete, overwrite or conceal earlier work. Inventory reusable source, tests and project objects; mark the architectural baseline PROVISIONAL.

Before adding new dependent features, conduct the bounded architecture gate below. If existing code genuinely satisfies it, demonstrate that fact. If not, pivot the smallest possible surface toward a valid architecture. Do not continue because of sunk cost.

**The intent is not to ban managed languages.** It is to stop using the easiest installed desktop framework as the automatic foundation for a demanding original engine.

## 2. TECHNOLOGY POLICY: NEWEST SUITABLE STABLE, NOT EASIEST AVAILABLE

Prefer **the newest stable, supported, technically qualified** compiler, SDK, language/runtime, GPU API, rendering and UI stack available at the decision date. The highest priority is long-term game-engine capability: direct control of a modern GPU rendering pipeline, shader/compute extensibility, memory and resource ownership, geometry/world scalability, high-end lighting/ray tracing expansion, integrated runtime, maintainable cross-platform pathways, deterministic/correct project state, accessibility and long-term upgrade support.

**Installed is not selected.** If a prerequisite is absent, assess installing the latest stable compatible version through approved channels. Do not silently adopt an older installed version simply to avoid setup. Never select a pre-release/nightly version just because its number is newer unless its suitability and risks are explicitly approved.

**Current dated guardrail (20 November 2026):** Microsoft released stable **.NET 11 on November 10, 2026**. If you select .NET for any new PLATZ component, evaluate .NET 11 (or a newer stable successor at the date of execution) before .NET 10. Do not establish .NET 10 as the new default merely because SDK 10 is installed. Any intentional older-version baseline requires an ADR documenting a technical necessity and explicit owner approval. This guardrail is not an order to choose .NET at all; C++/Rust/native or hybrid solutions may be superior for the main engine.

Do not make first-launch speed, ease of scaffolding, code generation speed, personal familiarity or short-term AI token savings the deciding criterion. Choose the best overall foundation that can be genuinely supported and maintained. **Efficiency means avoiding wasted research and rework—not lowering the engineering bar.**

## 3. ARCHITECTURE GATE — MUST BE COMPLETE BEFORE LARGE-SCALE IMPLEMENTATION

Produce one short `ADR-000-FOUNDATION.md` (or equivalent) based on a small, serious comparison of credible candidates. Evaluate at least:

- **Native engine-first** (e.g. current stable C++ or Rust with appropriate GPU backend and suitable UI).
- **Hybrid engine-first** (native engine, optional modern managed/native UI shell).
- **Managed engine-first** only if it controls an independently owned modern GPU renderer and can demonstrate the same future capabilities without reliance on desktop UI rendering primitives.

Assess the selected exact versions, release/support status, owned GPU pipeline and resource management, shader evolution, ray-tracing path, 3D camera and geometry semantics, high-end performance headroom, editor/renderer interop, portable runtime, asset/project compatibility, third-party licenses, maintainability, testability, and future API migration. Record both the strongest reason to choose and the strongest reason to reject each architecture. Measure or run **one or two high-information proof checks** on the highest-risk assumptions; do not conduct an exhaustive research expedition.

**A successful `dotnet build` is not proof that .NET is the correct engine architecture.** A screenshot is not GPU architecture evidence. No architecture is accepted solely because it is fastest to code.

Report the recommendation and its evidence. For consequential or costly-to-reverse decisions, present it for independent challenge and owner acceptance before promoting it as an approved baseline. Continue only reversible development while acceptance is pending; do not stall harmless implementation or pretend review occurred.

### Immediate mandatory rejection criteria

Reject a proposed **primary PLATZ Engine** foundation if any apply:

1. It uses a CPU/WPF-primitive isometric scene as the substitute for genuine GPU 3D.
2. It cannot demonstrate a true depth-tested three-dimensional scene with actual world transforms and perspective camera.
3. The desktop UI framework dictates the game's 3D renderer or prevents meaningful shader/resource control.
4. The chosen toolchain is justified chiefly by availability or quick demo development.
5. Project objects and their saved identities cannot be independently preserved when graphics implementation evolves.
6. There is no credible route to high-performance professional 3D, advanced GPU techniques and runtime independence.
7. The primary deliverable becomes a second app or sample game rather than PLATZ itself.

**An isometric camera inside a real 3D engine is acceptable; an isometric illustration pretending to be the engine is not.**

## 4. EXACTLY ONE PLATZ APPLICATION — NO DEMO GAMES

Create **one** canonical Git repository and main PLATZ desktop application. The UI must launch PLATZ Hub and Studio, and Studio must use the genuine PLATZ Engine; Preview/Play uses the same engine implementation and a separate runtime state. The future ability to export a user's game is part of the one product, not an instruction to author a demonstration game as the product.

Create **one internal, permanent, editable validation project within PLATZ**, named simply and professionally (for instance `PLATZ Validation`), to exercise supported real features and accumulate regression coverage. It is not separately installed, separately branded, a marketing game, a second desktop app or a substitute for creating an arbitrary new user project. The owner should also be able to create and edit an ordinary project through PLATZ. Technical unit tests, adversarial fixtures and benchmarks are permitted where needed.

**Do not create a throwaway “Signal Garden”, isometric adventure, museum, sample editor, toy engine or separate game merely to satisfy an integration demonstration.** Work on real reusable tools and canonical content. Use the internal validation project to test them.

## 5. FIRST SUBSTANTIAL DELIVERY: AUTHENTIC ENGINE + STUDIO + CORE + RUNTIME

Deliver real source code for the **permanent product** that supports this bounded, honest workflow:

1. Build and **launch the real PLATZ executable** on the authorized supported desktop environment.
2. Open PLATZ and **create an ordinary editable project**, with project file identity and meaningful persistence.
3. Construct an actual **GPU-rendered 3D scene** containing more than one world-space object, perspective camera, transforms, depth/occlusion, materials or color properties, and an inspectable render backend.
4. Select different scene objects through actual viewport picking, modify a property in the actual Studio inspector, and see the scene change through the same GPU engine.
5. Enter actual Play/Preview mode, move the camera or interact with scene objects using an input/runtime path, and Stop without corrupting authoring state.
6. Save, close, restart and reopen the project; verify the same object identities, properties and scene appearance.
7. Open the **internal permanent validation project**, exercise the same tested feature path, and preserve its stable verification IDs.
8. Give the owner an exact working launch command/executable path, build revision, tested device/OS, and actual executed tests. If the build exists only on a remote machine, say so and do not claim the owner can access it.

A 3D primitive is acceptable *inside a real reusable editor and genuine GPU renderer* as an initial supported asset, but **a standalone cube demo is not the deliverable**. A professional coherent UI is required for the features present. An empty app shell is not a pass. If the authorized environment lacks a compatible GPU, do not substitute fake 3D and claim success; isolate the missing device proof and accurately report it as blocked.

**This is the first meaningful qualification gate, not a rigid sequence of dozens of artificial micro-milestones.** Continue into additional useful, coherent, completed features whenever resources permit.

## 6. BUILD REAL BREADTH WITHOUT LEAVING HALF-FINISHED SYSTEMS

The entire long-term master remains in scope: exceptional 2D/3D/voxel game engine, professional artist tooling, shaders/materials, animation, sound/video, no-code and code, productivity, custom original AI specialists, automation and advanced media. You may choose high-value overlapping work; do not randomly switch to unrelated suites before the core experience is sound.

Whenever a feature is introduced, complete every **currently applicable** integration surface in the same candidate: working behavior, visible/discoverable UI and polished UX, canonical data/revisions, real 3D or runtime where relevant, supported no-code/code operation, existing PLATZ AI tool routes if applicable, accessibility, resource/security implications, errors/recovery, tests and existing validation stations. Record future unavailable dependencies explicitly; do not require a non-existent AI bus before the first renderer can be honestly verified. Do not call an incomplete feature finished.

Favor **substantial end-to-end feature clusters** over endless tiny changes, but never inflate counts with empty tabs, mock controls or untested flags. Preserve working functionality, and run focused regression tests plus appropriate broader gates before review.

## 7. ORIGINAL PLATZ AI — INTEGRATED, NOT A CHATBOT DECORATION

Progress toward original efficient PLATZ-owned specialist models and project-specific file-first knowledge remains a major long-term product objective. Differentiate deterministic tools, imported pretraining, fine-tunes and actually from-scratch trained models. Use authorized, rights-cleared data and independent evaluations. No permanent dependence on a third-party Qwen/Gemini/GPT wrapper should be mistaken for the goal.

PLATZ AI will eventually access the project only through validated typed commands or equivalent authorization-enforced APIs, with exact object IDs and revisions, staged diffs, explicit approval for consequential effects, rollback, and independent tests. Do not offer raw model writes to source databases. Begin the safe command/data foundations early when they do not displace proving a functional Engine/Studio/Core integration. Use efficient GPU resource scheduling for rendering, AI inference and training. Do not use personal accounts, payment methods, identity, subscriptions or protected data.

## 8. ONE CANONICAL FILE STRUCTURE AND AUTONOMOUS CONTINUITY

Create one appropriate permanent repository layout and record its canonical source root, build entrypoint, valid runtime, assets, internal validation project and tests. Do not create redundant project structures or hundreds of empty folders. Use clean source control and reviewable commits.

Maintain discoverable `START_HERE.md`, `CURRENT_HANDOFF.md`, `OWNER_STATUS.md`, and `DECISIONS_AND_FEATURE_LEDGER.md` or existing equivalents; do not create duplicate competing authorities. These must contain the latest tested build/launch command, current active scope, actual source hash, verified feature list, important ADRs, build/test failures, blocked approvals, current integration work, and the next substantial task. Update after real code/testing, not instead of it.

Next sessions should read the handoff and only relevant master sections, not reconsume the entire specification every time unless authority changes or a major cross-domain decision demands it. Do not recover old PLATZ/ATLAS application code or historical specifications without explicit owner authorization; this is a fresh new product. Preserve existing current-work source rather than deleting it automatically.

## 9. RESEARCH AND USAGE EFFICIENCY

Do the minimum research necessary to **select the right architecture**. Once its assumptions are validated, reuse decisions across sessions. Reopen a settled choice only upon material evidence, new compatibility/support change, or a measurable regression. Default cycle: inspect -> decide -> implement -> test -> integrate UI and data -> report. Favor concise records over expansive speculative documents. Use small tests while iterating and comprehensive suites at gates.

If two successive attempts fail with substantially the same error or two updates make no meaningful verified progress, stop repeating. Preserve failure evidence, change the diagnostic hypothesis or implementation approach, isolate a minimal reproducer, and request independent review if the problem is consequential. Do not conceal stuck work with cosmetic tweaks.

## 10. SECURITY AND AUTHORITY

Host-enforced capability boundaries protect the Windows host and personal information; untrusted files, plugins, generated code and downloads need appropriate isolation and verification. No self-granted permissions, unauthorized external account creation, real-money operations, hidden uploads or model-training use of private information. Treat cancel/Stop, stale revisions, symlinks/path aliases, interrupted saves, model promotion and sensitive domains as explicitly testable failure cases. Approved changes may not bypass independent acceptance of high-risk foundations.

## 11. PROGRESS UPDATES, OWNER LAUNCH AND HONEST DELIVERY

Give a few meaningful progress updates during an active session at real milestones (architecture decision, functioning 3D renderer, editor/runtime integration, tests/launch result). Tell the owner what newly works, relevant UI/UX changes, the blocker if any, and what comes next. Never imply asynchronous work if no actual approved runner exists.

Every session ends with a precise report of: real current functionality, main architectural decisions and versions, changed code/commits, feature integration, renderer proof, current UI quality, performance where measured, test passes/failures/skips, the permanent internal validation-project status, original PLATZ AI progress, security implications, independent acceptance needed, and **the exact verified launch path or honest reason none exists**. Do not invent a local installer or claim a remote executable is owner-accessible.

## 12. START THE CORRECT WORK NOW

Read both files, inspect current `C:\MASTER PLATZ` or whichever project root is actually authorized, preserve the current .NET 10/Signal Garden exploration if it exists, and **evaluate it against the architecture rejection criteria before adding more features**. Produce the concise `ADR-000` and the real best modern engine decision. Do not use the existence of .NET 10 SDK as the reason for selecting it. If .NET is justified, use the newest stable suitable version (as of this directive, evaluate .NET 11) or document an owner-approved exception. If the current renderer is pseudo-3D, replace its role with a real GPU 3D foundation while preserving reusable project/UX code where possible.

**Then implement the actual PLATZ application, not a game/demo.** Make genuine, substantial, permanently integrated progress and hand off accurate results. This directive supersedes any earlier prompt language permitting a convenience-first SDK, fake isometric engine, separate demo product or shell-only milestone.

**PLATZ — ONE PLACE. ZERO LIMITS.**
