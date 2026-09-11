# LPTA — LLVM Pass Transformation Analysis

Instrumentation tool (**not** upstream LLVM source): runs the `opt` pipeline on a `.ll` file via PassInstrumentationCallbacks, records IR metrics before/after every pass, emits `history.json` for `dashboard.html`. `lpta_test` takes LLVM IR (`.ll`) only; `run_lpta.sh` also accepts C/C++ (compiles to `build/lpta_input_<name>.ll` via `$CLANG` + `$LPTA_CFLAGS`, default `-O0`). **Windows-only**: `clang-cl` + Ninja + `.exe`; tests require Git Bash/MSYS (`dd`, `timeout`, `sed`).

## Layout (non-obvious wiring)

- Entrypoint `src/main.cpp`; headers in `inc/` — shared globals in `inc/Config.h` + `src/globals.cpp` (`g_target_triples`, `g_snapshots`, etc.)
- Two executables in root `CMakeLists.txt`: `lpta_test` (tool) and `test_utilities` (unit tests). **No globbing** — new `.cpp` must be added to the relevant `add_executable()` manually
- `clang+llvm-22.1.8-x86_64-pc-windows-msvc/` is bundled LLVM 22 **dependency** (gitignored), not project source
- `dashboard.html` is source; copied to report dir as `index.html` — re-copy after editing (`run_lpta.sh` does it; otherwise `cp dashboard.html report/index.html`)
- `serve_dashboard.py` — static server + optional NVIDIA NIM proxy for dashboard "Ask AI" panel; stdlib-only; loads `.lpta_config.json` from `cwd` then `home` (cwd wins), env vars override file
- `docs/architecture/` — Mermaid DATA_FLOW.md / PROJECT_MAP.md; Doxyfile in `docs/` — regenerate with `doxygen docs/Doxyfile` (output `docs/doxygen/` gitignored)
- Gitignored outputs: `build/`, `report*/` (covers `report/`, `report_final/`, `report_multi/`, `report_debug/`, `report_edge_*`), `tests/*_report.txt`, `tests/fuzz_results.log`, `.lpta_config.json`, `.env`/`*.key`. Root `package.json`/`package-lock.json`, `node_modules/` (Playwright dev dep), and `.opencode/` are also gitignored — don't try to commit them

## Build & run (Windows)

- `build/` is pre-configured (Ninja + clang-cl, `LLVM_DIR` → bundled distro) — just `ninja` inside `build/`
- Fresh configure:
  ```
  cmake -G Ninja -DLLVM_DIR=C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/lib/cmake/llvm -DCMAKE_CXX_COMPILER=C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/clang-cl.exe ..
  ```
- One-command: `bash run_lpta.sh input.ll` (env overrides: `BUILD_DIR`, `REPORT_DIR`; auto-detects `LLVM_DIR` via `llvm-config`). Always reconfigures + rebuilds (failure logs: `build/cmake.log`, `build/ninja.log`); C/C++ inputs are sanitized to `build/lpta_input_<name>.ll`
- Direct (run from repo root — binary/scripts use relative `test.ll` paths; ctest sets `WORKING_DIRECTORY` to root):
  ```
  build/lpta_test.exe input.ll [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--no-ir-hash] [--targets=common|triple,...|@file] [--]
  build/lpta_test.exe --compare <base.json> <curr.json> [--json] [--allow-different-input]
  build/lpta_test.exe --version
  ```
  One run per report dir at a time — concurrent runs into the same dir corrupt each other's `history.json`.
  `--no-ir-hash` skips IR serialization/hashing (perf mode: `ir_changed` stays false; counters/pairing/snapshots unaffected).
  Keep all four usage strings in `src/main.cpp` identical (plus `--compare` line). `--targets=@file` is line-delimited with `#` comments; comma list is trimmed. `--targets=common` expands to `x86_64`/`aarch64`/`riscv64` presets (see `COMMON_TARGETS` in `inc/Codegen.h`).
- Dashboard needs HTTP — `file://` fails (`fetch(history.json)`): `python serve_dashboard.py report -p 8080` or `cd report && python -m http.server 8080`
- AI Insights (optional): `NVIDIA_API_KEY` must live in env or `.lpta_config.json` only — **never committed or embedded client-side**. Model/endpoint via `LPTA_AI_MODEL` / `LPTA_AI_BASE_URL` (any OpenAI-compatible endpoint). No key → panel shows setup hint, rest unaffected
- `llc` discovery at runtime: `LLVM_DIR` env → exe dir → parent → sibling bundled dirs → common paths → `PATH` (`findLlcExe`, `src/Codegen.cpp`); no `PATH` setup needed
- `real_test.ll` is generated from `real_test.c` with bundled clang — regenerate if `.c` changes

## Tests — bash only (Git Bash/MSYS)

- Fast gate: `ctest --test-dir build` (unit + judge_proof + validate_correctness; needs Git Bash — CMake prefers it over WSL bash on Windows). CI (`.github/workflows/ci.yml`) runs only this gate on choco-installed LLVM; 300-iteration fuzz stays manual/scheduled
- Full suite (minutes, 300-iteration fuzz): `bash tests/run_all_tests.sh build/`
- Fast correctness: `bash tests/judge_proof.sh build/` (hand-verified `tests/tiny_proof.ll`)
- Independent cross-validation: `bash tests/validate_correctness.sh build/ test.ll` (grep counting + `opt -stats`)
- Compare gates (both must pass before merging any compare change): `bash tests/compare_golden.sh build/` (12 fixtures, byte-equivalent normalized output) + `bash tests/compare_parity.sh build/` (CLI vs `serve_dashboard.py /api/compare`)
- Node-gated checks (`tests/dashboard_smoke.js`, Playwright `dashboard_browser.js`) SKIP without node — not failures
- Unit tests only: `build/test_utilities.exe`
- Scripts resolve binaries with `.exe`-first fallback (`[ -x "$EXE" ] || EXE="${EXE%.exe}"`) — Windows behavior unchanged, Linux builds use the bare name. Keep binary names `lpta_test` / `test_utilities` (+ `.exe` on Windows)

## Invariants (don't regress — edge-case tests enforce these)

- Exit `1` on missing/bad input or unknown flag; `0` on success (empty `.ll` = success)
- Must print `Stack remaining: 0` — PassFrame stack balances before/after/invalidated (FIX #7)
- Same input → byte-identical `history.json` (determinism), even across output dirs (`normalizeAsmPaths` rewrites llc-embedded absolute paths to basenames)
- Prefer extending `README.md`'s 7-layer validation strategy over ad-hoc checks

## Gotchas / conventions

- **Keep in sync:** `COMMON_TARGETS` (`inc/Codegen.h`) ↔ `LLVMInitialize*` calls (`src/main.cpp`: native + AArch64 + RISCV) ↔ `llvm_map_components_to_libnames` (`CMakeLists.txt`, which also links ARM — no ARM init call yet). Add a target in all three or link/lookup fails
- **Keep in sync:** compare contract in `src/main.cpp` (`--compare`, `--json` canonical output, `--allow-different-input` cross-input override) ↔ `serve_dashboard.py` `compare_histories`: score components/verdict/coverage, presence-vs-effect maps, target states, finding shapes/messages, one-decimal pcts. JSON keys (`regression_score`, `verdict`, `score_components`, `coverage`, `compat`) are API — don't rename. Exits: `1` regressed, `2` incomparable pair. C++ `round1()` must match Python `round1()` exactly or boundary-pct parity fixtures fail. Gates: `compare_golden.sh` + `compare_parity.sh` (see Tests)
- **Change semantics:** after-events carry both `has_changes` (counters moved) and `ir_changed` (IR hash moved). `passes_with_changes` stays counter-based; dashboard/compare treat either flag as "changed". `--compare` parser must handle old JSON lacking `ir_changed` (defaults false)
- **`--compare` input contract:** pretty-printed `history.json` only (as `JsonWriter` emits). Minified single-line arrays, truncated files, and files without an events array are hard errors — never silent partial results. Full `llvm::json` rewrite is the documented follow-up if this ever needs relaxing.
- **Shell quoting (`shellQuoteBare`/`runLlc`, `src/Codegen.cpp`):** POSIX quotes every token (`triple`/`abs_output`/`abs_input`/`abs_llc`) with single-quote wrapping — never raw double-quote concatenation. Windows writes a PID-suffixed temp `.bat` (`%` escaped, delayed expansion off). A bare `llc` fallback must bypass `fs::absolute()` or `PATH` lookup silently breaks.
- **Determinism (`normalizeAsmPaths`, `src/Codegen.cpp`):** llc embeds absolute host paths in debug directives — they are rewritten to basenames in the saved `.s` (raw, backslash-escaped, and slashified forms) before line/byte counting. Don't count raw llc output or cross-dir determinism breaks. Stale `codegen_*.s` are deleted before each run so a failed llc can't inherit old counts.
- **Trim/parsing (`trimWS` + `--targets` parsing, `src/main.cpp`):** `find_first_not_of == npos` must return `""` (no `npos+1` wrap); `@file` open failure must error `return 1`; keep trim charset unified as `" \t\r\n"` across comma/file branches
- **Snapshots (`g_snapshot_allowlist`, `src/Snapshots.cpp`; cap `kMaxSnapshotBytes` = 4 MiB in `inc/Snapshots.h`):** allowlist-gated, pair atomically dropped if either side exceeds; expanding allowlist increases volume — keep `dashboard.html` in sync. Grep-checks to reuse: `--snapshots` must yield `"ir_before": "` in JSON; `--no-ir-hash` must yield zero `"ir_changed": true`
- **Optnone strings are API:** stripped functions record `"optnone_stripped": true` + `had optnone stripped` — never `were not optimized` (`run_all_tests.sh` greps all three)
- Changes to behavior/architecture/validation must keep `README.md` "Architecture" and "How to Prove Correctness" in sync, and update this file if build/test/run workflows change
