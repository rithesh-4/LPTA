# LPTA Hackathon Roadmap

## 1. PROBLEM → REQUIRED CAPABILITIES

The problem statement asks for:
- "records IR state before and after each optimization pass" → **HAVE**
- "presents the resulting transformation history as a single, explainable view" → **HAVE (dashboard)**
- "connects individual passes to measurable, quantified changes" → **HAVE (metrics+deltas)**
- "which passes ran" → **HAVE**
- "what they changed" → **PARTIAL** (we show numbers, not actual IR changes)
- "how significant each change was" → **HAVE** (impact ranking)
- "how it affected final codegen" → **MISSING**
- "explainable view" → **PARTIAL** (no IR diffs, no pass explanations)
- "evidence-backed" → **PARTIAL** (no IR snapshots in dashboard)

## 2. CURRENT STATE → GAP ANALYSIS

### HAVE:
- Pass execution tracking (1100 events)
- Nested pass tracking (PassFrame stack)
- IR structural metrics (10 counters)
- Delta computation
- Pass classification (adaptor/pipeline/transformation/analysis)
- Dashboard with filtering
- JSON output
- CLI with optimization level
- IR snapshot infrastructure (--snapshots flag)

### MISSING:
- **IR text diffs** — what SPECIFICALLY changed in the code
- **Per-function impact summary** — which function was most optimized
- **Pipeline-level summary** — total reduction across all passes
- **IR view in dashboard** — click a pass, see before/after IR
- **Codegen impact** — object size, code size

## 3. P0 / P1 / P2 / P3

### P0 — MUST BUILD (directly required by problem statement)
| Feature | Why Required | Effort |
|---------|-------------|--------|
| IR text snapshots in history.json | "records IR state before and after" | 2hrs |
| IR diff view in dashboard | "explainable view" = see what changed | 3hrs |
| Per-function impact table | "which passes changed what" | 1hr |
| Pipeline summary (total reduction) | "how significant" = cumulative effect | 1hr |

### P1 — HIGH VALUE (strengthens the narrative)
| Feature | Why | Effort |
|---------|-----|--------|
| Pass aggregation (total impact per pass type) | Shows which passes matter most | 1hr |
| "Before/After" IR toggle per pass | Developer drill-down | 2hrs |
| Codegen size measurement | "how it affected final codegen" | 2hrs |

### P2 — NICE TO HAVE
- Cross-run comparison (O2 vs O3)
- Pass dependency visualization
- Source-level mapping

### P3 — AVOID
- AI/LLM explanations (unreliable, complex)
- Runtime benchmarking (out of scope)
- ML-based scoring (overkill)
- Cloud deployment (unnecessary)
- Assembly comparison (complex)

## 4. ORDERED IMPLEMENTATION ROADMAP

### Stage 1: IR Text Snapshots (P0)
**Goal:** Save before/after IR text for passes with changes
**Why:** Problem statement says "records IR state before and after"
**Builds on:** Existing --snapshots infrastructure
**Difficulty:** Easy — we already have the plumbing
**Done:** history.json contains ir_before/ir_after for changed passes

### Stage 2: Dashboard IR Diff View (P0)
**Goal:** Click any changed pass → see before/after IR with diff highlighting
**Why:** This is what makes the tool "explainable" — not just numbers, but actual code
**Builds on:** IR text from Stage 1
**Difficulty:** Medium — JS diff algorithm + code viewer
**Done:** User clicks pass → sees IR diff → understands what changed

### Stage 3: Per-Function Impact + Pipeline Summary (P0)
**Goal:** Show which functions were most optimized, total pipeline reduction
**Why:** "how significant each change was" at both function and pipeline level
**Builds on:** Existing Event data
**Difficulty:** Easy — aggregation in JS
**Done:** Dashboard shows function impact table + pipeline summary card

### Stage 4: Dashboard Upgrade (P1)
**Goal:** Click-to-inspect IR, pass aggregation, cleaner layout
**Why:** Makes the tool developer-friendly
**Builds on:** Stages 1-3
**Difficulty:** Medium
**Done:** Developer can drill down from pipeline → function → pass → IR

## 5. FINAL PRODUCT ARCHITECTURE

```
Input: LLVM IR or C source
  ↓
LPTA CLI (lpta_test input.ll [-O0..-Oz] [--snapshots])
  ↓
LLVM Pipeline (configurable)
  ↓
Instrumentation (BEFORE/AFTER/INVALIDATED)
  ↓
Metrics + Deltas + IR Snapshots
  ↓
history.json (events + IR text + summary)
  ↓
Dashboard (HTML)
  ↓
Developer sees:
  ├── Pipeline summary (total reduction, top passes)
  ├── Per-function impact table
  ├── Filterable pass timeline
  ├── Click → IR diff view
  └── Pass aggregation
```

## 6. HACKATHON DEMO FLOW

1. **Show the problem:** "LLVM optimization is opaque. You see input, you see output, but what happened in between?"
2. **Run LPTA:** `lpta_test real_test.ll -O2`
3. **Open dashboard:**
   - "500 instructions reduced to 120 (76% reduction)"
   - "237 passes made changes across 9 functions"
4. **Drill into bubble_sort:**
   - "72 → 27 instructions (62% reduction)"
   - Click SROA: "This pass replaced 30 stack allocations with registers"
5. **Show IR diff:** Actual code changes highlighted
6. **Show top passes:** "SROA, InstCombine, SimplifyCFG did 80% of the work"
7. **Compare levels:** "O0: 0 changes. O2: 22 passes. O3: 22 passes + 30 more events."

## 7. RISKS / THINGS WE SHOULD NOT BUILD

**DO NOT BUILD:**
- AI/LLM explanations
- Runtime benchmarking
- ML-based scoring
- Cloud deployment
- Source-level mapping
- Assembly comparison
- Pass dependency graphs
- Fancy animations

**RISKS:**
- IR snapshots can be large → mitigate: only save for changed passes
- Dashboard may be slow → mitigate: lazy loading
- IR diffs may be noisy → mitigate: show changed lines only

## 8. RECOMMENDED NEXT STEP

**Stage 1: IR Text Snapshots**

This is the single most impactful addition. It directly answers "what changed?" which is the core of the problem statement.

Implementation:
1. Add `std::string ir_before` and `std::string ir_after` to Event struct
2. In BEFORE callback: serialize IR to string via raw_string_ostream
3. In AFTER callback: serialize IR to string
4. Write to history.json
5. Update dashboard to show IR diff

Estimated effort: 2-3 hours
Impact: HIGH — this is the difference between "we observed" and "we explain"
