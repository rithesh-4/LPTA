# LPTA — Implementation Notes

Detailed notes on what was built, why, and how it works. Written for hackathon review.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Build System](#2-build-system)
3. [Core C++ Implementation](#3-core-cpp-implementation)
4. [Dashboard](#4-dashboard)
5. [Key Technical Decisions](#5-key-technical-decisions)
6. [What Each File Does](#6-what-each-file-does)
7. [How the Pass Pipeline Works](#7-how-the-pass-pipeline-works)
8. [How Instrumentation Works](#8-how-instrumentation-works)
9. [Metrics and Deltas](#9-metrics-and-deltas)
10. [Codegen Measurement](#10-codegen-measurement)
11. [Known Limitations](#11-known-limitations)

---

## 1. Project Overview

LPTA (LLVM Pass Transformation Analysis) answers the question: **"What did LLVM's optimization pipeline do to my code?"**

When you run `clang -O2`, LLVM applies ~100+ optimization passes. You see the final result, but not the step-by-step transformation history. LPTA hooks into LLVM's pass instrumentation system, observes every pass execution, measures the IR before and after each pass, and produces a structured history that powers an interactive dashboard.

### What we observe (not what we build)

LLVM does the actual optimization. LPTA is a **passive observer** — it watches LLVM's pipeline execute and records what happens. We don't modify LLVM or its passes.

---

## 2. Build System

### CMakeLists.txt

Uses `LLVMConfig.cmake` to discover LLVM's include paths, libraries, and definitions. The key pattern:

```cmake
find_package(LLVM REQUIRED CONFIG)
llvm_map_components_to_libnames(LLVM_LIBS core irreader passes analysis transformutils)
target_link_libraries(lpta_test PRIVATE ${LLVM_LIBS})
```

This avoids manually listing dozens of `.lib` files — CMake resolves transitive dependencies automatically.

### LLVM_DIR Resolution

The build system looks for LLVM in this order:
1. `-DLLVM_DIR=...` cmake variable
2. `LLVM_DIR` environment variable
3. Fails with a clear error message

### build script (`run_lpta.sh`)

A single command to build + run + generate dashboard. Accepts `LLVM_DIR` as an env var.

---

## 3. Core C++ Implementation

The entire implementation is in `lpta_test.cpp` (~790 lines). Here's what each section does:

### IRMetrics (lines 35-105)

A struct with 10 counters: instruction_count, basic_block_count, function_count, global_count, call_count, load_count, store_count, branch_count, phi_count, return_count.

Three capture functions:
- `captureModuleMetrics(Module)` — iterates all functions, all blocks, all instructions
- `captureFunctionMetrics(Function)` — iterates one function's blocks and instructions
- `captureLoopMetrics(Loop)` — iterates blocks within a loop

These are called BEFORE each pass to snapshot the IR's structural state.

### IR Detection (lines 107-145)

LLVM's instrumentation callbacks receive an `Any` parameter. This could be a `Module*`, `Function*`, or `Loop*` depending on which pass is running.

We use `any_cast` to detect the type:
```cpp
if (auto *Mod = any_cast<const Module *>(&IR)) { ... }
else if (auto *F = any_cast<const Function *>(&IR)) { ... }
else if (auto *L = any_cast<const Loop *>(&IR)) { ... }
```

### PassFrame Stack (lines 147-165)

The most important architectural decision. LLVM's pass pipeline is nested:

```
Module pass
  → ModuleToFunctionPassAdaptor
    → Function pass
      → FunctionToLoopPassAdaptor
        → Loop pass
```

A single "current pass" variable would be overwritten by inner passes. Instead, we maintain a **stack** of `PassFrame` objects:

- BEFORE callback: push a frame with the pass name, IR type, and captured metrics
- AFTER callback: pop the frame, compute delta, record event
- INVALIDATED callback: pop the frame, record invalidation (no after-state available)

This correctly handles arbitrarily deep nesting. Verified: 550 BEFORE events = 548 AFTER + 2 INVALIDATED → stack balanced at 0.

### Pass Classification (lines 167-185)

Every pass name is classified as one of:
- **transformation** — actual optimization (e.g., InstCombinePass, GVNPass)
- **adaptor** — structural bridge between pass manager levels (e.g., ModuleToFunctionPassAdaptor)
- **pipeline** — pass manager container (e.g., PassManager<Function, ...>)
- **analysis** — analysis pass (e.g., DominatorTreeAnalysis)

This distinction helps the dashboard filter out infrastructure noise and focus on real transformations.

### Event Recording (lines 230-350)

Three callbacks are registered with LLVM's `PassInstrumentationCallbacks`:

1. **BeforePass**: captures metrics, pushes PassFrame, records BEFORE event, optionally saves IR snapshot
2. **AfterPass**: pops PassFrame, captures after-metrics, computes delta, records AFTER event, optionally saves IR snapshot
3. **AfterPassInvalidated**: pops PassFrame, records INVALIDATED event (no after-state)

### JSON Serialization (lines 200-350)

Hand-written JSON output (no external library dependency). Writes:
- `events[]` — every pass event with metrics, deltas, and IR text for changed passes
- `summary` — aggregate statistics (total events, passes with changes, instruction/BB reduction, codegen data)

### Codegen Measurement (lines 450-530)

Uses `llc` to compile before/after IR to assembly, then counts lines and bytes. The `findLlcExe()` function auto-discovers `llc` by checking:
1. `LLVM_DIR` environment variable
2. Sibling directories next to the executable
3. PATH fallback

---

## 4. Dashboard

Self-contained HTML file (`dashboard.html`, ~575 lines). No external dependencies — everything is inline CSS + JS.

### Features

1. **Summary cards** — before/after/invalidated counts, instruction/codegen reduction percentages
2. **Pipeline story** — natural language summary of what happened
3. **Execution timeline** — canvas-based chart showing pass execution order vs nesting depth
4. **Per-function impact table** — which functions were most optimized
5. **Filterable timeline** — every pass event with name, type, IR unit, depth, deltas
6. **IR diff modal** — click any changed pass to see side-by-side before/after IR with red/green highlighting
7. **Top passes by impact** — aggregated view of which pass types mattered most

### Data Flow

```
history.json (from lpta_test)
  ↓
fetch() in browser
  ↓
JavaScript renders all sections
  ↓
Per-function impact computed client-side from Function-scoped AFTER events
```

---

## 5. Key Technical Decisions

### Why a stack, not a global variable?

LLVM's pass pipeline is hierarchical. An inner pass runs while an outer pass is still "in progress." A global variable would be overwritten:

```
BEFORE ModulePass A
  current_before = A's metrics
  BEFORE FunctionPass B
    current_before = B's metrics  ← A's metrics lost!
  AFTER B
    computes delta using B's metrics  ← correct for B
  ← but A's BEFORE metrics are gone
```

The stack preserves all nesting levels.

### Why not clone the Module for every pass?

Cloning is expensive and unnecessary for structural metrics. We only need instruction/BB counts, which are cheap to compute. Full IR text snapshots are reserved for passes that actually changed the IR.

### Why hand-written JSON instead of a library?

For a hackathon prototype, adding a JSON library dependency creates build complexity. Hand-written serialization is ~50 lines and works reliably for our schema.

### Why selective IR snapshots?

Capturing full IR text for 1000+ passes would create a massive JSON file (hundreds of MB). We only save IR text for passes where `has_changes == true`, which is typically ~20-50 passes out of 1000+.

---

## 6. What Each File Does

| File | Lines | Role |
|------|-------|------|
| `lpta_test.cpp` | ~790 | Core implementation: instrumentation, metrics, JSON output |
| `dashboard.html` | ~575 | Interactive web dashboard (self-contained HTML/CSS/JS) |
| `CMakeLists.txt` | ~58 | Build configuration with LLVM discovery |
| `run_lpta.sh` | ~55 | One-command build + run script |
| `test.ll` | ~56 | Simple test input (5 functions with loops, branches, dead code) |
| `real_test.c` | ~116 | Realistic C test input (bubble sort, matrix ops, recursion) |
| `real_test.ll` | ~543 | Compiled from real_test.c |

---

## 7. How the Pass Pipeline Works

```
LPTA creates PassBuilder
  ↓
PassBuilder builds ModulePassManager for -O2
  ↓
ModulePassManager contains:
  Module passes (e.g., GlobalOptPass)
  ModuleToFunctionPassAdaptor
    Function passes (e.g., InlinerPass, InstCombinePass)
    FunctionToLoopPassAdaptor
      Loop passes (e.g., LICMPass, LoopUnrollPass)
  ↓
MPM.run(Module, ModuleAnalysisManager)
  ↓
LLVM executes passes in order
  ↓
Each pass triggers BEFORE → execute → AFTER callbacks
```

### Nesting example

```
BEFORE: ModuleToFunctionPassAdaptor          depth=0
  BEFORE: SimplifyCFGPass                     depth=1
  AFTER:  SimplifyCFGPass                     depth=1  ← delta computed
  BEFORE: FunctionToLoopPassAdaptor           depth=1
    BEFORE: LICMPass                          depth=2
    AFTER:  LICMPass                          depth=2
  AFTER:  FunctionToLoopPassAdaptor           depth=1
AFTER:  ModuleToFunctionPassAdaptor           depth=0
```

---

## 8. How Instrumentation Works

LLVM's New Pass Manager provides `PassInstrumentationCallbacks` with three hooks:

- `registerBeforeNonSkippedPassCallback(StringRef, Any)` — called before each pass
- `registerAfterPassCallback(StringRef, Any, PreservedAnalyses)` — called after each pass
- `registerAfterPassInvalidatedCallback(StringRef, PreservedAnalyses)` — called when a pass invalidates the IR unit

Each callback receives:
- `StringRef PassID` — the pass name (e.g., "InstCombinePass")
- `Any IR` — the IR unit being optimized (Module*, Function*, or Loop*)
- `PreservedAnalyses PA` — which analyses survived the pass

LPTA uses these to:
1. Identify the pass name
2. Detect the IR unit type
3. Capture metrics before/after
4. Compute deltas

---

## 9. Metrics and Deltas

For each BEFORE/AFTER pair, we capture:
- instruction_count: total instructions in scope
- basic_block_count: total basic blocks
- function_count: total functions (usually 1 for Function scope)
- global_count: total globals (Module scope only)
- call_count, load_count, store_count, branch_count, phi_count, return_count

Delta = after - before for each metric.

A pass is flagged as `has_changes` if ANY metric changed.

The dashboard shows deltas as color-coded values: green for reductions (typically "improvements"), red for increases.

---

## 10. Codegen Measurement

After the full pipeline runs:
1. Save the initial IR (before optimization) to `ir_before_opt.ll`
2. Save the final IR (after optimization) to `ir_after_opt.ll`
3. Run `llc -filetype=asm` on both
4. Count assembly lines and bytes for each
5. Report the reduction

This answers: "How did the optimization pipeline affect the final generated code?"

---

## 11. Known Limitations

1. **CGSCC (Call Graph SCC) passes not detected** — `LazyCallGraph::SCC` is a fourth IR type that `any_cast` doesn't currently handle. These passes show as `Unknown` IR unit.

2. **Metrics are structural, not semantic** — fewer instructions doesn't always mean better performance. LPTA reports facts, not interpretations.

3. **IR snapshots can be large** — for passes with changes, the full IR text is embedded in history.json. Large inputs can produce 100MB+ JSON files.

4. **Codegen measurement is approximate** — assembly line counts and byte sizes are rough proxies for actual binary size. Linker effects, alignment, and section layout are not captured.

5. **No cross-run comparison yet** — LPTA analyzes a single pipeline run. Comparing O2 vs O3 requires running twice and comparing JSON files externally.

6. **Dashboard is single-page** — no URL routing, no bookmarking of specific passes. All state is in JavaScript memory.
