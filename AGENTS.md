# LPTA — LLVM Pass Transformation Analysis

Instrumentation tool (**not** upstream LLVM source): runs the `opt` pipeline on a `.ll` file via PassInstrumentationCallbacks, records IR metrics before/after every pass, emits `history.json` for `dashboard.html`. `lpta_test` takes LLVM IR (`.ll`) only; `run_lpta.sh` also accepts C/C++ (compiles to `build/lpta_input_<name>.ll` via `$CLANG` + `$LPTA_CFLAGS`, default `-O0`). **Windows-only**: `clang-cl` + Ninja + `.exe`; tests require Git Bash/MSYS (`dd`, `timeout`, `sed`).

## Layout (non-obvious wiring)

- Entrypoint `src/main.cpp`; headers in `inc/` — shared globals in `inc/Config.h` + `src/globals.cpp` (`g_target_triples`, `g_snapshots`, etc.)
- Two executables in root `CMakeLists.txt`: `lpta_test` (tool) and `test_utilities` (unit tests). **No globbing** — new `.cpp` must be added to the relevant `add_executable()` manually
- `clang+llvm-22.1.8-x86_64-pc-windows-msvc/` is bundled LLVM 22 **dependency** (gitignored), not project source
- `dashboard.html` is source; copied to report dir as `index.html` — re-copy after editing (`run_lpta.sh` does it; otherwise `cp dashboard.html report/index.html`)
- `serve_dashboard.py` — static server + optional NVIDIA NIM proxy for dashboard "Ask AI" panel; stdlib-only; loads `.lpta_config.json` from `cwd` then `home` (cwd wins), env vars override file
- `docs/architecture/` — Mermaid DATA_FLOW.md / PROJECT_MAP.md; Doxyfile in `docs/` — regenerate with `doxygen docs/Doxyfile` (output `docs/doxygen/` gitignored)
- Gitignored outputs: `build/`, `report*/` (covers `report/`, `report_final/`, `report_multi/`, `report_debug/`, `report_edge_*`), `tests/*_report.txt`, `tests/fuzz_results.log`, `.lpta_config.json`, `.env`/`*.key`

## Build & run (Windows)

- `build/` is pre-configured (Ninja + clang-cl, `LLVM_DIR` → bundled distro) — just `ninja` inside `build/`
- Fresh configure:
  ```
  cmake -G Ninja -DLLVM_DIR=C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/lib/cmake/llvm -DCMAKE_CXX_COMPILER=C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/clang-cl.exe ..
  ```
- One-command: `bash run_lpta.sh input.ll` (env overrides: `BUILD_DIR`, `REPORT_DIR`; auto-detects `LLVM_DIR` via `llvm-config`)
- Direct:
  ```
  build/lpta_test.exe input.ll [report_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file]
  ```
  Keep all four usage strings in `src/main.cpp` identical (plus `--compare` line). `--targets=@file` is line-delimited with `#` comments; comma list is trimmed. `--targets=common` expands to `x86_64`/`aarch64`/`riscv64` presets (see `inc/Codegen.h:41`).
- Dashboard needs HTTP — `file://` fails (`fetch(history.json)`): `python serve_dashboard.py report -p 8080` or `cd report && python -m http.server 8080`
- AI Insights (optional): `NVIDIA_API_KEY` must live in env or `.lpta_config.json` only — **never committed or embedded client-side**. Model/endpoint via `LPTA_AI_MODEL` / `LPTA_AI_BASE_URL` (any OpenAI-compatible endpoint). No key → panel shows setup hint, rest unaffected
- `llc` discovery at runtime: `LLVM_DIR` env → exe dir → parent → sibling bundled dirs → common paths → `PATH` (`src/Codegen.cpp:58-129`); no `PATH` setup needed
- `real_test.ll` is generated from `real_test.c` with bundled clang — regenerate if `.c` changes

## Tests — bash only (Git Bash/MSYS)

- Full suite (minutes, 300-iteration fuzz): `bash tests/run_all_tests.sh build/`
- Fast correctness: `bash tests/judge_proof.sh build/` (hand-verified `tests/tiny_proof.ll`)
- Independent cross-validation: `bash tests/validate_correctness.sh build/ test.ll` (grep counting + `opt -stats`)
- Unit tests only: `build/test_utilities.exe`
- Scripts hardcode `.exe` — keep binary names `lpta_test.exe` / `test_utilities.exe`

## Invariants (don't regress — edge-case tests enforce these)

- Exit `1` on missing/bad input or unknown flag; `0` on success (empty `.ll` = success)
- Must print `Stack remaining: 0` — PassFrame stack balances before/after/invalidated (FIX #7)
- Same input → byte-identical `history.json` (determinism)
- Prefer extending `README.md`'s 7-layer validation strategy over ad-hoc checks

## Gotchas / conventions

- **Keep in sync:** `inc/Codegen.h:41` `COMMON_TARGETS` ↔ `src/main.cpp:1162-1170` `LLVMInitialize*` ↔ `CMakeLists.txt:62-88` `llvm_map_components_to_libnames` (add target in all three or link fails)
- **Keep in sync:** compare heuristic formula in `src/main.cpp` `--compare` ↔ `serve_dashboard.py` `compare_histories` (same weights/bands; JSON key `regression_score` is API — don't rename)
- **Change semantics:** after-events carry both `has_changes` (counters moved) and `ir_changed` (IR hash moved). `passes_with_changes` stays counter-based; dashboard/compare treat either flag as "changed". `--compare` parser must handle old JSON lacking `ir_changed` (defaults false)
- **Shell quoting (`src/Codegen.cpp:190-214`):** never concatenate `triple`/`abs_output`/`abs_input` with raw double-quotes into `cmd_args`; use `shellQuoteBare()` per-token on Linux (`shellQuoteBare(abs_llc) + " " + cmd_args` where `cmd_args` already quoted is injection). Windows writes a temp `.bat`.
- **Determinism (`src/Codegen.cpp` `normalizeAsmPaths`):** llc embeds absolute host paths in debug directives — they are rewritten to basenames in the saved `.s` before line/byte counting. Don't count raw llc output or cross-dir determinism breaks.
- **Trim/parsing (`src/main.cpp:69-91`):** handle `find_first_not_of == npos` without `npos+1` wrap; `ifstream` for `@file` must error `return 1` on open failure; unify trim charset to `" \t\r\n"` (comma vs file branch diverged)
- **Snapshots (`src/Snapshots.cpp:17`):** allowlist-gated + 4 MiB cap per snapshot (pair atomically dropped if either side exceeds); expanding allowlist increases volume — keep `dashboard.html` in sync
- Changes to behavior/architecture/validation must keep `README.md` "Architecture" and "How to Prove Correctness" in sync, and update this file if build/test/run workflows change
