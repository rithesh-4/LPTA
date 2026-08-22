# LPTA — LLVM Pass Transformation Analysis

A tool that makes LLVM's optimization pipeline transparent by recording IR state before and after every pass, computing measurable changes, and presenting the results as an interactive dashboard.

## The Problem

When you run `clang -O2`, LLVM applies dozens of optimization passes internally. You see the input and the output, but you have no idea what happened in between. Which passes ran? What did each one change? How significant was it? How did it affect final codegen?

LLVM provides individual debugging options (`-print-before-all`, `-debug-pass-manager`), but developers still lack a **unified view** that correlates which pass changed the IR, what changed, and how significant that change was.

## What LPTA Does

```
Input: C source or LLVM IR
  ↓
LPTA runs the LLVM optimization pipeline (configurable: -O0 to -Oz)
  ↓
Instruments every pass execution via LLVM's PassInstrumentationCallbacks
  ↓
Records IR state before and after each pass
  ↓
Computes structural metrics and deltas
  ↓
Saves before/after IR text for passes that made changes
  ↓
Measures final codegen impact via llc
  ↓
Generates structured JSON + interactive HTML dashboard
```

## Quick Start

### Prerequisites

- LLVM 17+ (tested with 22.1.8) installed
- CMake 3.20+
- Ninja (recommended) or Make
- A C++17 compiler (Clang recommended)

### Build

```bash
# Set LLVM_DIR to your LLVM installation
export LLVM_DIR=/path/to/llvm

# Configure and build
mkdir build && cd build
cmake -G Ninja \
  -DLLVM_DIR=$LLVM_DIR/lib/cmake/llvm \
  -DCMAKE_CXX_COMPILER=$LLVM_DIR/bin/clang++ \
  ..
ninja
```

Or use the build script:
```bash
export LLVM_DIR=/path/to/llvm
bash run_lpta.sh input.ll
```

### Run

```bash
# Analyze with -O2 (default)
./lpta_test input.ll [output_dir]

# Analyze with different optimization levels
./lpta_test input.ll report -O0
./lpta_test input.ll report -O2
./lpta_test input.ll report -O3

# With IR snapshots for selected passes
./lpta_test input.ll report -O2 --snapshots
```

### View Dashboard

```bash
cd report
python -m http.server 8080
# Open http://localhost:8080
```

## What the Dashboard Shows

### Summary Cards
- Total before/after/invalidated events
- Passes that produced measurable changes
- **IR instruction reduction** (e.g., 368 → 155, -58%)
- **Codegen size reduction** (e.g., 542 → 444 assembly lines, -18%)

### Pipeline Story
Natural language summary of what the optimization pipeline did.

### Per-Function Impact
Which functions were most optimized, sorted by instruction reduction.

### Execution Timeline
Visual chart showing pass execution over time with nesting depth.

### Filterable Pass Timeline
Every pass execution with:
- Pass name and type (transformation/adaptor/pipeline/analysis)
- IR unit (Module/Function/Loop)
- Nesting depth
- Metric deltas (instructions, BBs, branches, PHIs, etc.)
- Click any changed pass → **IR diff view**

### IR Diff View
Side-by-side before/after IR with:
- Removed lines highlighted in red
- Added lines highlighted in green
- Line numbers
- Per-metric breakdown

### Top Passes by Impact
Aggregated view showing which pass types had the most effect.

## Architecture

```
inc/ + src/             ← Modular C++ implementation
  Config.h              ← Shared globals (output dir, opt level, snapshots flag)
  Metrics.h/.cpp        ← 10 structural counters (instructions, BBs, calls, etc.)
  Detection.h/.cpp      ← Detects IR unit type from Any (Module/Function/Loop)
  Tracker.h/.cpp        ← PassFrame stack + Event records; tracks nested pass execution
  Snapshots.h/.cpp      ← Selective IR snapshot saving for changed passes
  Codegen.h/.cpp        ← Uses llc to measure assembly size
  JsonWriter.h/.cpp     ← Writes structured history.json
  main.cpp              ← CLI parsing (-O0..-Oz, --snapshots) + pipeline wiring

dashboard.html         ← Self-contained HTML/CSS/JS dashboard
  ├── Summary cards
  ├── Pipeline story
  ├── Per-function impact table
  ├── Execution timeline chart (canvas-based)
  ├── Filterable pass timeline
  ├── IR diff modal (click-to-inspect)
  └── Top passes aggregation
```

## Key Technical Decisions

### Stack-based Pass Tracking
LLVM's pass pipeline is hierarchical (Module → Function → Loop). Passes nest inside adaptors. A single "current pass" variable would be overwritten by inner passes. LPTA uses a **stack** of `PassFrame` objects to correctly match BEFORE/AFTER events across nesting.

### Metrics, Not Interpretation
LPTA counts structural properties (instructions, blocks, calls, etc.) — it does **not** claim fewer instructions = better performance. The significance score is a heuristic, not ground truth.

### Selective IR Snapshots
Full IR text is expensive to capture for every pass. LPTA only saves IR text for passes that actually changed the IR, keeping the JSON file manageable.

### Codegen via llc
Object size measurement uses `llc` to compile before/after IR to assembly, then counts lines and bytes. This gives a concrete measure of how optimization affected final codegen.

## Files

| File | Purpose |
|------|---------|
| `inc/`, `src/` | Modular C++ implementation (see Architecture above) |
| `dashboard.html` | Interactive HTML dashboard |
| `CMakeLists.txt` | Build configuration |
| `run_lpta.sh` | One-command build + run script |
| `test.ll` | Simple test input (5 functions) |
| `real_test.c` | Realistic test input (9 functions) |
| `real_test.ll` | Compiled from real_test.c |

## Example Output

```
real_test.ll with -O2:
  2,196 pass events tracked
  237 passes produced measurable changes
  91 unique pass names observed

  IR Instructions:  368 → 155  (58% reduction)
  Codegen Assembly: 542 → 444 lines  (18% reduction)

  Top passes by impact:
    SROAPass:         -89 instructions (15x)
    EarlyCSEPass:     -42 instructions (9x)
    SimplifyCFGPass:  -28 instructions (12x)
    InstCombinePass:  -18 instructions (8x)
```

## How to Prove Correctness

The most common question: **"How do you know the numbers are right?"** LPTA provides a 7-layer validation strategy:

### 1. Hand-Countable Ground Truth

`tests/tiny_proof.ll` is a tiny IR file (2 functions, 12 instructions) where every element can be counted by hand. Run:

```bash
bash tests/judge_proof.sh build/
```

This produces a side-by-side comparison of LPTA's metrics against manually verified ground truth.

### 2. Independent IR Parsing

`tests/validate_correctness.sh` uses `grep` to count IR elements independently (separate from LPTA's C++ counting code), then compares against LPTA's output:

```bash
bash tests/validate_correctness.sh build/ test.ll
```

### 3. LLVM Cross-Validation

If `opt` is available, the validation script compares LPTA's metrics against LLVM's own `opt -stats` output — two independent counting implementations.

### 4. Determinism

Running LPTA twice on the same input produces byte-identical `history.json` output.

### 5. Formal Invariants

The C++ source code contains `assert()` statements that verify counting invariants at runtime (e.g., `instruction_count >= call_count + load_count + store_count`).

### 6. Unit Tests

48 unit tests (`tests/test_utilities.cpp`) verify utility functions, delta calculations, JSON escaping, pass classification, and edge cases.

### 7. Fuzz Testing

300+ mutated inputs (`tests/fuzz_runner.sh`) verify LPTA never crashes, hangs, or produces corrupt output.

```bash
bash tests/run_all_tests.sh build/   # Run full test suite
```

## License

MIT License. See [LICENSE](LICENSE) for details. LLVM components follow the [Apache 2.0 License](https://llvm.org/LICENSE.txt).
