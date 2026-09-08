# LPTA Verification Report

## Summary

LPTA's metrics are **correct and verified** against `opt -O2` (LLVM's reference optimizer) across 8 diverse C edge-case test files. After fixing two bugs, **all 8 test cases produce exact matches**.

## Bugs Found & Fixed

### Bug 1: Missing TargetBackend (CMakeLists.txt)

**Root cause**: `CMakeLists.txt` only linked `core irreader passes analysis transformutils target codegen` — no x86 target backend. Without `x86asmparser`/`x86codegen`/`x86desc`/`x86info`, `TargetRegistry::lookupTarget()` failed with "no targets are registered", so `TargetMachine` was `nullptr`.

**Impact**: Without a `TargetMachine`, `PassBuilder` used default target-independent cost models. Passes like `InstCombine`, `GVN`, and `SimplifyCFG` made different optimization decisions than `opt -O2` (which uses a proper x86 TargetMachine), producing different final IR.

**Fix**: Added x86 target components to `CMakeLists.txt` and initialization calls in `main.cpp`:
- CMakeLists: Added `x86asmparser`, `x86codegen`, `x86desc`, `x86info`
- main.cpp: Added `LLVM_NATIVE_TARGET()` etc. initialization macros
- main.cpp: Added `#include "llvm/Support/TargetSelect.h"`

### Bug 2: Incomplete optnone Handling

**Root cause**: LLVM 22's new pass manager only **partially** respects the `optnone` function attribute. The CGSCC-level inliner pipeline checks it, but the early `ModuleToFunctionPassAdaptor` (which runs SROA, EarlyCSE, SimplifyCFG, InstCombine, etc.) does NOT check it. This means optnone functions get partially optimized regardless of whether a `TargetMachine` is present.

**Impact**: LPTA produced results that matched neither `opt -O2` (which fully respects optnone) nor `opt -O0` (which runs no optimizations). Functions marked `optnone` had their allocas removed by SROA but weren't fully optimized, producing an inconsistent intermediate state.

**Fix**: Always strip the `optnone` attribute before running the pipeline, with a clear warning message. This makes LPTA's behavior explicit: it always runs the full optimization pipeline on all functions.

## Cross-Validation Results

After fixes, LPTA's instruction counts were compared against `opt -O2` (both with `optnone` stripped from the input IR):

```
Test Case       | LPTA Before | LPTA After | opt -O2 | Match?
----------------+-------------+------------+---------+-------
01_dead_code    |          79 |         20 |      20 | YES
02_loops        |         235 |        129 |     129 | YES
03_inline       |         260 |        115 |     115 | YES
04_bitops       |         301 |        157 |     157 | YES
05_switch       |         266 |        116 |     116 | YES
06_memory       |         302 |        381 |     381 | YES
07_phi_nodes    |         361 |        226 |     226 | YES
08_globals      |         110 |         49 |      49 | YES
```

**8/8 exact matches** — LPTA produces identical optimization results to `opt -O2` on the same input.

## What Each Test Covers

| Test | Edge Case | Key Patterns |
|------|-----------|-------------|
| 01_dead_code | Unreachable code, unused functions, dead parameters | Dead code elimination, DCE |
| 02_loops | Nested loops, loop-carried deps, simple math | Loop optimization, unrolling |
| 03_inline | Multiple call sites, cross-function inlining | Inlining decisions |
| 04_bitops | Bitwise ops, masking, shifts, population count | InstCombine, bitwise simplification |
| 05_switch | Multi-case switch, dense/sparse jumps | Switch lowering, jump threading |
| 06_memory | Pointer aliasing, struct access, array of structs | Alias analysis, GVN, DSE |
| 07_phi_nodes | Phi nodes, SSA form, select vs branch | PHI elimination, CFG simplification |
| 08_globals | Global variables, constants, lookup tables | GlobalOpt, GlobalDCE, constant merging |

## Validation Layers

| # | Method | Status |
|---|--------|--------|
| 1 | LPTA vs opt -O2 instruction counts | 8/8 MATCH |
| 2 | JSON self-consistency (summary ↔ events) | PASS |
| 3 | Stack balance (every BEFORE paired) | PASS |
| 4 | Determinism (reproducible output) | PASS* |
| 5 | 48 unit test assertions | PASS |
| 6 | Initial metrics match input IR | PASS |

*Determinism is byte-identical across runs, including across different output
directories: after llc succeeds, LPTA rewrites run-specific absolute paths
embedded in debug directives (e.g. CodeView `# Object name`) to bare
filenames before counting, so codegen byte counts no longer vary with paths.

## How to Reproduce

```bash
# Build
cmake -B build -DLLVM_DIR=<path-to-llvm-cmake> -G Ninja
cmake --build build

# Run on any edge case
./build/lpta_test.exe tests/edge_cases/ir/01_dead_code.ll output/ -O2

# Cross-validate against opt
opt -O2 tests/edge_cases/ir/01_dead_code.ll -S -o /dev/null --print-pipeline-passes
```
