# LPTA Demo Script — 10-Minute Hackathon Video

> **Before recording:** Open two terminal windows, a browser, and have this script visible on a second screen or printed.

---

## Pre-Record Checklist

- [ ] Server running: `http://localhost:8080/` (already up with cross-target data)
- [ ] Browser open to `http://localhost:8080/`
- [ ] Terminal 1: in project root (`LPTA/`)
- [ ] Terminal 2: in project root (for CLI demos)
- [ ] Screen recording software ready (OBS, Win+G, etc.)
- [ ] Close notifications / silence phone
- [ ] Demo data: `demo_cross/` (O2 + 3 targets, 1756 events, 102 changed passes)

---

## Act 1: The Problem (0:00 – 1:00)

**[0:00] Show terminal — run a plain clang command**
```bash
clang -O2 real_test.c -o output
```
> "When you compile code with LLVM, dozens of optimization passes run internally. You see the input and the output, but you have no idea what happened in between."

**[0:20] Show what LLVM gives you natively**
```bash
opt -O2 -print-before-all -print-after-all demo_input.ll 2>&1 | head -60
```
> "LLVM provides individual debugging flags, but the output is massive, unstructured, and impossible to correlate. You get raw IR dumps but no metrics, no deltas, no explanation of significance."

**[0:40] State the problem clearly**
> "LLVM developers and performance engineers lack a unified view that answers: which passes ran, what changed, how significant was it, and how did it affect final codegen. Today I'll show you LPTA — the LLVM Pass Transformation Analyzer."

---

## Act 2: Build & Run (1:00 – 2:30)

**[1:00] Show the one-command build**
```bash
bash run_lpta.sh demo_input.ll --snapshots --targets=common
```
> "LPTA is a single command. It instruments LLVM's optimization pipeline using PassInstrumentationCallbacks, records before/after metrics for every pass, hashes IR to catch changes counters miss, measures final codegen size across three architectures, and generates an interactive dashboard."

**[1:30] Watch it run — point out key output**
> "Notice: 1,756 events recorded across 102 passes that made changes. Stack remaining: 0 — the instrumented pass stack is balanced. Cross-target codegen: x86_64, aarch64, and riscv64 — we'll see how the same optimization pipeline produces different assembly for each architecture."

**[2:00] Show the output files**
```bash
ls -la report/
```
> "We get a structured history.json — the full trace — plus before/after IR, codegen assembly for each target, and IR snapshots for allowlisted passes whose IR actually changed."

---

## Act 3: Dashboard Overview (2:30 – 4:00)

**[2:30] Open browser to `http://localhost:8080/`**
> "Now let's look at the dashboard. This is the Overview page."

**[2:40] Walk through Overview cards**
- Point to **Instructions** card: "109 to 112 — net +3: cleanup passes removed code, the vectorizer added more back"
- Point to **Codegen** card: "15% code size increase — 190 to 219 assembly lines"
- Point to **Basic Blocks**: "21% reduction — 29 to 23"
- Point to **Passes**: "102 passes with changes out of 878 executions"

> "The overview gives you the big picture: what the optimization pipeline achieved overall. Notice the tradeoff — basic blocks went down 21% but instructions went slightly up and code size grew 15%. That's the kind of insight LPTA surfaces."

**[3:00] Show the Pipeline Story**
- Scroll to the narrative section
> "The story tells you what happened: SimplifyCFG was the biggest reducer at minus 42 instructions, EarlyCSE and InstCombine cleaned up further — but LoopVectorize added 58 and LoopUnroll added 14 by vectorizing and unrolling the loops. The optimizer traded IR size for vectorized loops, and code size grew on all three targets — a tradeoff made visible."

**[3:20] Show Overview Charts (mini sparklines)**
- Point to the instruction trend chart
> "You can see the instruction count trend across the entire pipeline — it dips early as cleanup passes run, then jumps where the loop vectorizer fires. This is the optimization curve visible in a single chart."

**[3:30] Navigate to Pipeline page (press `2`)**
> "The Pipeline page shows a timeline of every pass execution. Each dot is a pass — red means it changed the IR, gray means no change."

**[3:45] Hover over timeline dots**
> "Hovering shows you the exact pass, the IR it processed, and the instruction delta. 878 passes visualized in one view."

---

## Act 4: Pass Explorer Deep Dive (4:00 – 6:00)

**[4:00] Navigate to Passes page (press `4`)**
> "The Pass Explorer is where you dig into individual passes."

**[4:10] Show the filter bar**
- Click "Changed" filter
> "Filter to only passes that made changes — 102 out of 878. You can also filter by pass name, search for specific patterns."

**[4:30] Click on a pass to see the IR diff**
- Click on a `InstCombinePass` or `SimplifyCFGPass` that has changes
> "Click any pass and you get the full IR diff — what was before, what was after, with line-by-line highlighting. Green lines were added, red lines were removed."

**[5:00] Walk through the IR diff**
- Point to specific changes
> "Here InstCombinePass simplified this arithmetic chain. It folded constant expressions, eliminated redundant operations, and reduced the instruction count — all in one pass."

**[5:20] Show the metrics panel in the diff modal**
> "The modal also shows the metric deltas — instruction count before/after, basic blocks, loads, stores. You can quantify exactly how much this pass changed."

**[5:40] Click the ✨ Explain button**
> "And with AI Insights enabled, you can click Explain to get an AI-generated analysis of what this pass did and why — grounded in the actual IR data."

---

## Act 5: AI Insights (6:00 – 7:00)

**[6:00] Click "✨ Ask AI" in bottom-right corner**
> "The AI panel gives you a chat interface to ask questions about your optimization run."

**[6:10] Use a preset — click "Summarize pipeline"**
> "The AI gets the full report context — metrics, pass sequence, IR snapshots — and follows an evidence-first ruleset. It won't invent data or make unsupported claims."

**[6:30] Show the AI response**
> "It correctly identifies the key transformations, quantifies the impact, and explains the compiler concepts involved."

**[6:40] Type a custom question**
- Type: "Why did codegen size increase despite instruction reduction?"
> "You can ask follow-up questions about anything in the data."

**[6:50] Show per-pass explain**
- Close AI, go to Passes, click ✨ on a specific pass
> "Or get targeted explanations for individual passes."

---

## Act 6: Cross-Target Codegen (7:00 – 8:30)

**[7:00] Navigate to Overview, scroll to Codegen section**
> "One of LPTA's unique features: cross-target codegen comparison. We compiled the same optimized IR to three architectures — x86_64, aarch64, and riscv64."

**[7:15] Show the per-target assembly comparison**
- Point to the codegen table in the Overview
> "x86_64: 195 → 232 lines. aarch64: 155 → 203 lines. riscv64: 221 → 267 lines. The same optimization pipeline produces different code size impacts depending on the target architecture."

**[7:30] Explain the insight**
> "This is something you can't get from LLVM's built-in tools. You can see how architecture-specific instruction selection interacts with target-independent optimizations. aarch64 had the biggest relative increase — 31% — because its instruction set has different cost modeling for the operations this code uses."

**[7:50] Show the pipeline chart with codegen overlay**
- Navigate to Pipeline page (press `2`)
> "The pipeline chart shows codegen before/after alongside the pass timeline. You can see exactly when the code size changed and which passes were responsible."

**[8:10] Highlight the before/after codegen assembly**
> "And you can compare the actual assembly output — before optimization vs after — for each target. This turns codegen regression investigation from hours of manual comparison into a single dashboard view."

---

## Act 7: Cross-Run Comparison (8:30 – 9:30)

**[8:30] Navigate to Compare page (press `5`)**
> "The Compare page lets you detect regressions between two LPTA runs."

**[8:40] Load the two history files**
- Click "Load Baseline" → select `demo_current/history.json` (the -O0 run)
- Click "Load Current" → select `demo_cross/history.json` (the -O2 run)
- Click "Run Comparison"

> "I'm comparing an unoptimized run against an optimized run. Let's see what the optimizer actually did."

**[8:55] Show the regression score gauge**
> "Regression score: shows the tradeoff. The optimizer improved instructions but increased codegen. This is the tradeoff visible in a single number."

**[9:05] Show the findings**
- Scroll through regressions and improvements
> "Findings detected: instruction reduction is an improvement, codegen increase is a regression. The pass-level comparison shows which specific passes contributed most to the delta."

**[9:20] Show CLI comparison**
```bash
# In terminal 2:
build/lpta_test.exe --compare demo_current/history.json demo_cross/history.json
```
> "There's also a CLI mode for scripting and CI pipelines — same analysis, human-readable output. You can pipe this into your CI to automatically flag regressions."

---

## Act 8: Closing (9:30 – 10:00)

**[9:30] Show the test suite**
```bash
build/test_utilities.exe 2>&1 | tail -5
```
> "75 unit tests pass. The tool is deterministic — same input produces byte-identical output. The test suite includes hand-verified ground truth, fuzz testing, and cross-validation against LLVM's own opt -stats."

**[9:40] Summary**
> "LPTA gives compiler developers and performance engineers an explainable, evidence-backed view of LLVM's optimization pipeline: which passes ran, what they changed, how significant each change was, cross-target codegen impact, and regression detection between runs. It turns manual, multi-stage IR comparison into a repeatable workflow."

**[9:50] Call to action**
> "Built with LLVM 22, C++17, and a pure HTML/JS dashboard — no npm, no frameworks, no dependencies. The AI insights use NVIDIA's free NIM API. Everything is open source."

**[10:00] End recording**

---

## Quick Reference — Keyboard Shortcuts for Demo

| Key | Page | Use in Demo |
|-----|------|-------------|
| `1` | Overview | Act 3 opening |
| `2` | Pipeline | Act 3 timeline + Act 6 codegen overlay |
| `3` | Functions | (skip in 10-min version) |
| `4` | Passes | Act 4 deep dive |
| `5` | Compare | Act 7 comparison |
| `Esc` | Close modals | Between sections |
| `/` | Focus search | Act 4 filtering |

---

## Demo Data Files

| File | Purpose | When to Use |
|------|---------|-------------|
| `demo_cross/history.json` | Main demo — O2 + 3 targets, 1756 events, 102 changed | Acts 3-6 (dashboard walkthrough) |
| `demo_current/history.json` | O0 run — 42 events, no changes | Act 7 (comparison baseline) |

> Regenerate both any time (gitignored, never committed):
> `build/lpta_test.exe demo_input.ll demo_cross -O2 --snapshots --targets=common`
> `build/lpta_test.exe demo_input.ll demo_current -O0`

**To switch demo data during recording:**
```bash
# For dashboard walkthrough (cross-target):
cp demo_cross/history.json report/history.json
cp dashboard.html report/index.html

# For comparison demo (need both files):
# Keep demo_cross in report/, load demo_current as baseline in Compare tab
```

---

## Backup: If Something Goes Wrong

| Issue | Recovery |
|-------|----------|
| Dashboard blank | Hard refresh (`Ctrl+Shift+R`), check server is running |
| AI returns error | "AI is optional — the dashboard works fully without it" |
| Slow loading | "This is a full optimization pipeline trace with 1,756 events" |
| Compare fails | Ensure both files loaded, click "Run Comparison" again |
| Terminal error | "Let me show you the output files directly" → `ls report/` |
| Codegen shows 0 lines | Say "The before-state represents unoptimized IR" and move on |
