# LPTA — How Data Moves Through the Tool

> Render this file on GitHub, in VS Code (with a Mermaid preview extension),
> or open `index.html` in this folder for a styled browser view.

Three views, zooming inward:

1. **End-to-end pipeline** — disk to dashboard
2. **Anatomy of one pass execution** — callbacks and the frame stack
3. **File-level call map** — the actual source files

---

## 1. End-to-end pipeline

The whole tool in one picture. Blue = build/run steps, green = data artifacts,
orange = the instrumentation layer that makes LPTA what it is.

```mermaid
flowchart TD
    subgraph INPUT["Input preparation"]
        C["real_test.c"] -->|"clang -emit-llvm -S"| LL[("real_test.ll")]
    end

    subgraph LPTA["build/lpta_test.exe"]
        PARSE["parseIRFile<br/>(main.cpp)"] --> MOD["llvm::Module"]
        MOD --> PB["PassBuilder.buildPerModuleDefaultPipeline(-O2)<br/>+ PassInstrumentationCallbacks"]

        subgraph INSTR["Instrumentation (the core idea)"]
            direction LR
            DET["Detection.cpp<br/>detectIR(Any) + FNV-1a IR hash<br/>→ kind + name + metrics"] --> MET["Metrics.cpp<br/>capture*Metrics()<br/>10 counters + 8 opcode groups"]
        end

        PB -->|"BEFORE callback"| PUSH["push PassFrame onto pass_stack<br/>+ 'before' Event → g_events"]
        PUSH --> RUN["the pass actually runs"]
        RUN -->|"AFTER callback"| POP["match + pop frame by name & IR pointer<br/>compute delta → 'after' Event"]
        RUN -.->|"loop deleted"| INV["INVALIDATED callback<br/>(fires instead of AFTER)"]
        DET -.-> PUSH
        DET -.-> POP
    end

    LL --> PARSE

    subgraph OUTPUT["Artifacts in report dir"]
        JSON[("history.json")]
        IRO[("ir_before_opt.ll / ir_after_opt.ll")]
        SNAP[("ir/pass_&lt;id&gt;_&lt;pass&gt;_before.ll ...")]
        DASH["dashboard.html → index.html"]
    end

    MOD -->|"print()"| IRO
    POP -->|"g_events"| JSON
    PUSH -->|"if shouldSnapshot + under kMaxSnapshotBytes"| SNAP
    RUN -->|"if changed + allowlisted + pair fits cap"| SNAP

    IRO -->|"llc ×2 (Codegen.cpp)"| ASM["asm line/byte counts<br/>(CodegenResult)"]
    ASM --> JSON

    JSON --> SERVE["python -m http.server 8080"]
    DASH --> SERVE
    SERVE --> BROWSER["Browser dashboard<br/>fetch()es history.json<br/>timeline · diffs · impact tables"]
```

Key point: `pass_stack` is **transient** (alive only during `MPM.run`),
`g_events` is the **accumulated record**, and `history.json` is its
**serialization**. The dashboard never sees the stack — only events.

---

## 2. Anatomy of one pass execution

Why a *stack*, not a "current pass" variable: pipelines nest.
`ModuleToFunctionPassAdaptor` sits at depth 0 while real passes run deeper;
every BEFORE must pair with exactly one AFTER across that nesting.

```mermaid
sequenceDiagram
    participant PM as LLVM PassManager
    participant CB as LPTA callbacks (main.cpp)
    participant ST as pass_stack
    participant EV as g_events

    PM->>CB: BEFORE "ModuleToFunctionPassAdaptor" (Module)
    CB->>CB: detectIR → metrics snapshot
    CB->>ST: push frame {depth=0}
    CB->>EV: push "before" event

    PM->>CB: BEFORE "SimplifyCFGPass" (Function entry)
    CB->>ST: push frame {depth=1}
    CB->>EV: push "before" event

    Note over PM: SimplifyCFG mutates the function

    PM->>CB: AFTER "SimplifyCFGPass"
    CB->>ST: find topmost frame matching name + ir_ptr, pop
    CB->>CB: delta = before vs after metrics
    CB->>EV: push "after" event (+ IR diff if changed)

    PM->>CB: AFTER "ModuleToFunctionPassAdaptor"
    CB->>ST: pop outer frame (depth back to 0)
    CB->>EV: push "after" event

    Note over CB,EV: Invariant: when MPM.run() returns,<br/>stack must be empty → "Stack remaining: 0"
```

The special case — a loop pass **deletes its own loop**: LLVM has no valid IR
object left to hand back, so it fires `AfterPassInvalidated` *instead of*
`AfterPass`. That's why INVALIDATED events carry metrics-before but no
after-state, and are matched by name alone (topmost, LIFO).

```
BEFORE(LICMPass on loop L)   → push
   ... loop L gets deleted ...
INVALIDATED(LICMPass)         → pop by name, emit "invalidated" event
```

---

## 3. File-level call map

Solid arrows = direct calls, dashed arrows = shared global state
(`inc/Config.h` + `src/globals.cpp`: `g_output_dir`, `g_snapshots`,
`g_opt_level`, optnone tracking).

```mermaid
flowchart LR
    MAIN["main.cpp<br/>CLI · wiring · 3 callbacks"]

    subgraph COMPUTE["pure computation"]
        DETC["Detection.cpp<br/>Any → which IR unit?"]
        METC["Metrics.cpp<br/>count instructions/blocks/..."]
    end

    subgraph STATE["shared state"]
        TRK["Tracker.cpp<br/>pass_stack · g_events<br/>classifyPass · printDeltaLine"]
        GLB["globals.cpp + Config.h<br/>output_dir · snapshots · opt level"]
    end

    subgraph SIDE["side effects"]
        SNAP["Snapshots.cpp<br/>allowlist check → .ll files"]
        CODE["Codegen.cpp<br/>find llc → spawn → count lines"]
        JW["JsonWriter.cpp<br/>escape → history.json"]
    end

    TEST["tests/test_utilities.cpp<br/>75 unit tests"]

    MAIN -->|"detectIR(IR)"| DETC
    DETC --> METC
    MAIN -->|"classifyPass, frames, events"| TRK
    MAIN -->|"shouldSnapshot, saveIRSnapshot"| SNAP
    MAIN -->|"measureCodegen(before.ll, after.ll)"| CODE
    MAIN -->|"writeHistoryJSON(dir/history.json)"| JW
    JW -->|"reads g_events"| TRK
    SNAP -.-> GLB
    CODE -.-> GLB
    JW -.-> GLB
    MAIN -.-> GLB
    TEST --> METC
    TEST --> TRK
    TEST --> JW
    TEST --> SNAP
    TEST --> DETC
```

Reading guide:

| Question you're asking | Follow the arrow into |
|---|---|
| Where do the numbers come from? | `Detection.cpp` → `Metrics.cpp` |
| Where is pass pairing enforced? | `main.cpp` callbacks + `Tracker.cpp` stack |
| Why does the dashboard show diffs? | `Snapshots.cpp` allowlist → `ir_before`/`ir_after` strings → `JsonWriter.cpp` |
| Where does asm size come from? | `Codegen.cpp` (`llc`, temp `.bat` on Windows, PATH fallback logic) |

---

## Cross-check these diagrams yourself

- Console run prints `[N] BEFORE/AFTER` lines with `depth=` — that's diagram 2 live.
- `report/history.json` `"summary"."total_before"/"total_after"` — diagram 1's event ledger.
- Doxygen call graphs (`docs/doxygen/html`) — diagram 3, machine-generated per function.
