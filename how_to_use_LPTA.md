# How to Use LPTA — Complete CLI Reference

This guide documents every way to run LPTA from the command line so you never need to guess.

---

## Prerequisites

- LLVM 17+ (bundled: `clang+llvm-22.1.8-x86_64-pc-windows-msvc/` in repo)
- CMake 3.20+, Ninja
- Windows: Git Bash / MSYS2 (for `bash run_lpta.sh`, tests)

> **Input must be LLVM IR (`.ll`), not C.** Compile C first if needed:
> ```bash
> clang -O2 -S -emit-llvm real_test.c -o input.ll   # or use bundled clang
> # bundled: clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/clang.exe
> # real_test.ll is already compiled from real_test.c in this repo
> ```
> **Windows-only:** uses `clang-cl` + Ninja + `.exe` (see `CMakeLists.txt`). Tests require Git Bash/MSYS (`dd`, `timeout`, `sed`).

---

## 1. One-Command Build & Run (Recommended)

```bash
# From repo root
bash run_lpta.sh input.ll [--snapshots]
```

**Environment variables (optional):**
```bash
export LLVM_DIR=/path/to/llvm          # if not auto-detected (bundled auto-found via llvm-config)
export BUILD_DIR=./build               # default
export REPORT_DIR=./report             # default
```

**What it does:**
1. Configures CMake with bundled LLVM (or your `LLVM_DIR`)
2. Builds `lpta_test.exe` via Ninja
3. Runs LPTA on `input.ll` → `./report/`
4. Copies `dashboard.html` → `./report/index.html`

> **Note:** `build/` is pre-configured (Ninja + clang-cl, `LLVM_DIR` → bundled distro) — just `ninja` inside `build/` if you already built once. Fresh configure only needed after `rm -rf build/`.

**Output:**
- `./report/history.json` — full trace
- `./report/index.html` — open in browser (needs HTTP server)

---

## 2. Direct Binary Usage (After Build)

```bash
# From repo root (after building)
./build/lpta_test.exe input.ll [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=...]
```

### Arguments

| Argument | Description |
|----------|-------------|
| `input.ll` | **Required.** Path to LLVM IR file (`.ll`). Not C source. |
| `report_dir` | Optional. Output directory (default: `./report`). |
| `-O0`..`-Oz` | Optimization level (default: `-O2`). Must be exactly one of: `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, `-Oz`. |
| `--snapshots` | Enable IR text snapshots for passes that change IR. Increases `history.json` size. |
| `--targets=common` | Cross-target codegen: x86_64, aarch64, riscv64 presets. |
| `--targets=triple1,triple2` | Comma-separated LLVM target triples. |
| `--targets=@file.txt` | Read triples from file (one per line, `#` comments allowed). |

### Examples

```bash
# Basic -O2 analysis
./build/lpta_test.exe test.ll report -O2

# With IR snapshots (for diff view in dashboard)
./build/lpta_test.exe test.ll report -O2 --snapshots

# Cross-target codegen comparison (3 presets)
./build/lpta_test.exe test.ll report -O2 --targets=common

# Custom targets
./build/lpta_test.exe test.ll report -O2 --targets=x86_64-pc-windows-msvc,aarch64-unknown-linux-gnu

# From file (targets.txt contains triples, one per line)
./build/lpta_test.exe test.ll report -O2 --targets=@targets.txt
```

---

## 3. View the Dashboard

The dashboard is static HTML — **must be served via HTTP** (fetch fails on `file://`).

### Option A: Bundled server (recommended, enables AI)
```bash
cd report
python ../serve_dashboard.py . -p 8080
# Open http://localhost:8080
```

### Option B: Python stdlib (no AI)
```bash
cd report
python -m http.server 8080
# Open http://localhost:8080
```

### Dashboard Navigation
| Key | Page |
|-----|------|
| `1` | Overview (summary, charts, story) |
| `2` | Pipeline Timeline |
| `3` | Function Impact |
| `4` | Pass Explorer (filterable, click for IR diff) |
| `5` | Compare Runs (load two `history.json` files) |
| `Esc` | Close modals / AI drawer |

---

## 4. Cross-Run Comparison (Regression Investigation)

The **Compare** page (key `5`) loads two `history.json` files and computes:
- Summary delta (instructions, BBs, codegen, invalidated)
- Per-target codegen regression
- Per-pass impact delta
- Auto-detected regressions (>5% instruction/codegen increase)

### Workflow
```bash
# 1. Baseline run (e.g., before change)
./build/lpta_test.exe input.ll report_baseline -O2 --snapshots

# 2. Current run (e.g., after change)
./build/lpta_test.exe input.ll report_current -O2 --snapshots

# 3. Open dashboard, go to Compare tab
cd report_current
python ../serve_dashboard.py . -p 8080
# Load report_baseline/history.json as Baseline
# Load report_current/history.json as Current
# Click "Run Comparison"
```

---

## 5. Build from Scratch (Manual)

```bash
# From repo root — if build/ already exists, just run:
cd build && ninja

# Fresh configure (after rm -rf build or first clone):
mkdir build
cd build
cmake -G Ninja \
  -DLLVM_DIR=../clang+llvm-22.1.8-x86_64-pc-windows-msvc/lib/cmake/llvm \
  -DCMAKE_CXX_COMPILER=../clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/clang-cl.exe \
  ..
ninja
```

> **`llc` discovery at runtime:** `LLVM_DIR` env → exe dir → parent → sibling bundled dirs → common paths → `PATH` (`src/Codegen.cpp:58-129`); no `PATH` setup needed.

Outputs:
- `build/lpta_test.exe` — main tool
- `build/test_utilities.exe` — unit tests

---

## 6. Run Tests

```bash
# Full test suite (slow, includes 300-iteration fuzz)
bash tests/run_all_tests.sh build/

# Fast correctness check (hand-verified tiny_proof.ll)
bash tests/judge_proof.sh build/

# Cross-validation against opt -stats + grep counting
bash tests/validate_correctness.sh build/ test.ll

# Unit tests only
./build/test_utilities.exe
```

---

## 7. Common Input Files in Repo

| File | Description |
|------|-------------|
| `test.ll` | 5 tiny functions, minimal IR |
| `real_test.c` | 9 realistic functions (loops, calls, math) |
| `real_test.ll` | Pre-compiled from `real_test.c` with bundled clang |
| `tests/tiny_proof.ll` | 2 functions, 12 instructions — hand-countable ground truth |

---

## 8. Key Invariants (Don't Regress)

- Exit code `1` on missing/bad input or unknown flag; `0` on success (empty `.ll` = success)
- Must print `Stack remaining: 0` — PassFrame stack balances
- Same input → byte-identical `history.json` (determinism)

---

## 9. Environment Variables & Config File Reference

| Variable | Purpose |
|----------|---------|
| `LLVM_DIR` | Path to LLVM install (for CMake config + `llc` discovery) |
| `NVIDIA_API_KEY` | Enables AI Insights panel (**never committed** — keeps repo private) |
| `LPTA_AI_MODEL` | Override model (default: `nvidia/nemotron-3-ultra-550b-a55b`) |
| `LPTA_AI_BASE_URL` | Override endpoint (any OpenAI-compatible) |

**AI config file (std-lib only, no deps):** `serve_dashboard.py` loads `.lpta_config.json` from `cwd` then `home` (cwd wins), env vars override file. Example:
```json
{
  "NVIDIA_API_KEY": "nvapi-...",
  "LPTA_AI_MODEL": "nvidia/nemotron-3-ultra-550b-a55b",
  "LPTA_AI_BASE_URL": "https://integrate.api.nvidia.com/v1"
}
```
No key → panel shows setup hint (`fetch /api/health`), rest of dashboard unaffected. Key lives only in server process env — never sent to browser.

---

## 10. Quick Reference Card

```bash
# Typical daily workflow (input MUST be .ll)
bash run_lpta.sh my_input.ll --snapshots
# → wait for build + run (Stack remaining: 0 must appear)
cd report
python ../serve_dashboard.py . -p 8080
# → open http://localhost:8080 (file:// fails — must be http)
```

```bash
# Compare two runs
./build/lpta_test.exe input.ll baseline -O2 --snapshots
./build/lpta_test.exe input.ll current -O2 --snapshots
# → open dashboard, use Compare tab
```

```bash
# Debug a specific pass
# 1. Run with --snapshots
# 2. Dashboard → Passes tab → click any changed pass
# 3. IR diff modal shows before/after with line highlights
```

---

## Troubleshooting

| Issue | Fix |
|-------|-----|
| `LLVM_DIR not set` | `export LLVM_DIR=/path/to/llvm` or use bundled |
| `clang-cl.exe not found` | Use bundled: `../clang+llvm-22.1.8-.../bin/clang-cl.exe` |
| Dashboard shows "Could not load history.json" | Serve via HTTP (`python -m http.server`), not `file://` |
| "Stack remaining: N (WARNING)" | PassFrame stack imbalance — bug in pass matching |
| Cross-target fails | Ensure `llc` is in PATH or LLVM_DIR (bundled works) |
| AI panel shows "no backend" | `export NVIDIA_API_KEY=nvapi-...` then restart server |

---

## Files You'll Generate

After running LPTA on `input.ll` with output dir `report/`:
```
report/
├── history.json          # Main output — all events, metrics, IR diffs
├── index.html            # Dashboard (copy of dashboard.html)
├── ir_before_opt.ll      # Module IR before pipeline
├── ir_after_opt.ll       # Module IR after pipeline
├── codegen_before/       # Assembly before (per target)
│   └── *.s
├── codegen_after/        # Assembly after (per target)
│   └── *.s
└── ir/                   # Per-pass IR snapshots (if --snapshots)
    ├── pass_1_before.ll
    ├── pass_1_after.ll
    └── ...
```

---

## TL;DR — Minimal Commands

```bash
# 1. Analyze
bash run_lpta.sh input.ll --snapshots

# 2. View
cd report && python ../serve_dashboard.py . -p 8080
# http://localhost:8080

# 3. Compare (repeat step 1 with different inputs, then Compare tab)
```