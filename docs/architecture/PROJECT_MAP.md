# LPTA — Annotated Project Map

> What every directory and file *is*, and where to look when you want to
> understand or change something. Pairs with `DATA_FLOW.md` (the arrows).

## The tree

```
C:\LLVM-full\
│
├── src/                        ← implementation (8 files, ~1.5k lines total)
│   ├── main.cpp                ← ENTRYPOINT: CLI parsing, callback wiring,
│   │                              pipeline build & run, summary printing
│   ├── globals.cpp             ← definitions of shared globals (Config.h)
│   ├── Detection.cpp           ← llvm::Any → {Module|Function|Loop|Unknown},
│   │                              IR unit naming, serializeIR()
│   ├── Metrics.cpp             ← the 10 counters + counting invariants (asserts)
│   ├── Tracker.cpp             ← pass_stack, g_events, classifyPass(), deltas
│   ├── Snapshots.cpp           ← snapshot allowlist, filename sanitizing,
│   │                              writing per-pass .ll files
│   ├── Codegen.cpp             ← llc discovery (4 fallback tiers), Windows .bat
│   │                              spawning, asm line/byte counting
│   └── JsonWriter.cpp          ← jsonEscape(), writeHistoryJSON() schema
│
├── inc/                        ← one header per source file
│   ├── Config.h                ← SHARED GLOBALS: g_output_dir, g_snapshots,
│   │                              g_opt_level, g_opt, optnone tracking
│   ├── Detection.h  Metrics.h  Tracker.h  Snapshots.h  Codegen.h  JsonWriter.h
│
├── tests/                      ← bash-only suite (Git Bash / MSYS)
│   ├── run_all_tests.sh        ← everything: units + edge cases + fuzz (slow)
│   ├── judge_proof.sh          ← fast ground-truth proof (tiny_proof.ll)
│   ├── validate_correctness.sh ← grep recount + opt -stats cross-validation
│   ├── fuzz_runner.sh          ← 300 mutated inputs, no crash/hang allowed
│   ├── test_utilities.cpp      ← 57 unit tests → build/test_utilities.exe
│   ├── tiny_proof.ll           ← hand-countable IR: 2 fns, 5 BBs, 12 instrs
│   └── edge_cases/             ← per-scenario IR inputs + python verifiers
│
├── dashboard.html              ← self-contained dashboard (HTML/CSS/canvas JS);
│   │                              copied into report dir as index.html
├── run_lpta.sh                 ← one-command build + run + dashboard copy
├── CMakeLists.txt              ← two targets: lpta_test, test_utilities
│   │                              NO file globbing — add new .cpp manually!
├── AGENTS.md                   ← instructions for AI coding sessions
├── README.md                   ← human-facing overview + validation strategy
│
├── clang+llvm-22.1.8-*/        ← BUNDLED LLVM DEPENDENCY (gitignored) —
│   │                              compiler + headers + libs this repo links
│   │                              against; NOT project source
│   └── lib/cmake/llvm          ← what LLVM_DIR must point at
│
├── build/                      ← generated (gitignored). Pre-configured:
│   │                              Ninja + clang-cl. Just run `ninja` inside.
│   ├── lpta_test.exe           ← the tool
│   └── test_utilities.exe      ← unit tests
│
├── report*/                    ← generated output dirs (gitignored)
│   ├── history.json            ← THE artifact: all events + summary + codegen
│   ├── index.html              ← copy of dashboard.html
│   ├── ir_before_opt.ll / ir_after_opt.ll
│   ├── codegen_before.s / codegen_after.s
│   └── ir/pass_<id>_<Pass>_<Kind>_<name>_{before|after}.ll
│
├── real_test.c / real_test.ll  ← realistic sample input (regenerate .ll from .c)
├── test.ll                     ← tiny sample input
│
└── docs/                       ← YOU ARE HERE — visual documentation
    ├── Doxyfile                ← doxygen config → docs/doxygen/html/
    ├── mainpage.dox            ← doxygen landing page content
    └── architecture/
        ├── DATA_FLOW.md        ← mermaid: pipeline · pass lifecycle · call map
        ├── PROJECT_MAP.md      ← this file
        └── index.html          ← styled browser view of the diagrams
```

## "Where do I find...?" index

| I want to change/understand... | Go to | Landmark |
|---|---|---|
| Which optimization level runs | `src/main.cpp` | `-O0..-Oz` CLI parsing |
| How a metric is counted | `src/Metrics.cpp` | three `capture*Metrics()` switches |
| Why counts agree across scopes | `src/Metrics.cpp` | invoke/callbr in all three switches |
| Pass pairing / stack discipline | `src/main.cpp` callbacks + `src/Tracker.cpp` | `Stack remaining: 0` invariant |
| Which passes get snapshots | `src/Snapshots.cpp` | `g_snapshot_allowlist` (12 passes) |
| Snapshot size policy | `src/main.cpp` | joint 4 MB pair cap (`kMaxSnapshotBytes`) |
| history.json schema | `src/JsonWriter.cpp` | `writeHistoryJSON()` |
| JSON string escaping rules | `src/JsonWriter.cpp` | `jsonEscape()` |
| How llc is located | `src/Codegen.cpp` | `findLlcExe()` fallback chain |
| Exit-code contract | `src/main.cpp` | `1` bad input/flag · `0` success (empty file OK) |
| Dashboard rendering | `dashboard.html` | fetch of `history.json`, diff modal |
| Test entry points | `tests/*.sh`, `build/test_utilities.exe` | see tree above |

## Two rules that bite if forgotten

1. **New `.cpp` file** → must be added to the matching `add_executable()`
   list(s) in `CMakeLists.txt`. There is no globbing.
2. **Edited `dashboard.html`** → re-copy it into the report dir as
   `index.html` (or rerun `run_lpta.sh`) before refreshing the browser.
