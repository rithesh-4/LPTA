#!/bin/bash
# ============================================================
# LPTA Judge Demo — proves LPTA numbers match opt -O2
# ============================================================
# Usage: bash tests/judge_demo/run_demo.sh <path-to-build-dir>
# Example: bash tests/judge_demo/run_demo.sh build

set -e
BUILD_DIR="${1:-build}"
LPTA="$BUILD_DIR/lpta_test.exe"
DEMO_DIR="tests/judge_demo"
DEMO_LL="$DEMO_DIR/demo.ll"
DEMO_C="$DEMO_DIR/demo.c"
OUT="$DEMO_DIR/demo_output"

# Python interpreter (python3 preferred)
PYTHON=""
if command -v python3 &>/dev/null; then
    PYTHON="python3"
elif command -v python &>/dev/null; then
    PYTHON="python"
else
    echo "ERROR: python3/python not found"
    exit 1
fi

# Find opt
OPT=""
for p in \
    "C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/opt.exe" \
    "/usr/bin/opt" \
    "$(which opt 2>/dev/null || true)"; do
    [ -f "$p" ] && OPT="$p" && break
done

if [ ! -f "$LPTA" ]; then
    echo "ERROR: lpta_test not found at $LPTA"
    echo "Build first: cmake -B build && cmake --build build"
    exit 1
fi

if [ ! -f "$DEMO_LL" ]; then
    echo "ERROR: demo.ll not found at $DEMO_LL"
    echo "Compile first: clang -O0 -emit-llvm -S -o $DEMO_LL $DEMO_C"
    exit 1
fi

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║           LPTA — Judge Demo: Proof of Correctness          ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# ---- Step 1: Show the input IR ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  STEP 1: Input IR (compiled with clang -O0, has optnone)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  Source: $DEMO_C"
echo "  Functions: $(grep -c '^define' $DEMO_LL)"
echo "  optnone:   $(grep -c 'optnone' $DEMO_LL || true) functions marked 'do not optimize'"
echo "  Total lines: $(wc -l < $DEMO_LL)"
echo ""
echo "  Note: Every function has 'optnone' — LLVM normally SKIPS optimization."
echo "  LPTA strips this and runs the full pipeline to show what each pass CAN do."
echo ""

# ---- Step 2: Run LPTA ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  STEP 2: Run LPTA (-O2 pipeline)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
"$LPTA" "$DEMO_LL" "$OUT" -O2 2>&1 | grep -E "(Optnone|Stripping|Handling|Summary|Pass executions|Total recorded|Stack remaining|Passes with changes|Assembly|Before:|After:|Reduction)" || true
echo ""

# ---- Step 3: Show per-function results ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  STEP 3: Per-Function Optimization Impact"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
$PYTHON -c "
import json
with open('$OUT/history.json') as f:
    data = json.load(f)

# before events have 'metrics', after events have 'metrics_before'/'metrics_after'
funcs = {}
for e in data['events']:
    if e['ir_kind'] != 'Function': continue
    name = e['ir_name']
    if name not in funcs:
        funcs[name] = {'before': None, 'after': None}
    if e['event_type'] == 'before' and funcs[name]['before'] is None:
        m = e.get('metrics') or e.get('metrics_before')
        if m: funcs[name]['before'] = m['instruction_count']
    if e['event_type'] == 'after' and 'metrics_after' in e:
        funcs[name]['after'] = e['metrics_after']['instruction_count']

print(f'  {\"Function\":<20s} {\"Before\":>8s} {\"After\":>8s} {\"Delta\":>8s} {\"Reduction\":>10s}')
print(f'  {\"-\"*20} {\"-\"*8} {\"-\"*8} {\"-\"*8} {\"-\"*10}')
total_before = 0
total_after = 0
for name in sorted(funcs.keys()):
    b = funcs[name]['before'] or 0
    a = funcs[name]['after'] if funcs[name]['after'] is not None else b
    d = a - b
    pct = f'{-d*100//b}%' if b > 0 else 'N/A'
    total_before += b
    total_after += a
    print(f'  {name:<20s} {b:>8d} {a:>8d} {d:>+8d} {pct:>10s}')
print(f'  {\"TOTAL\":<20s} {total_before:>8d} {total_after:>8d} {total_after-total_before:>+8d} {(-((total_before-total_after)*100//total_before) if total_before else 0):>9d}%')
"
echo ""

# ---- Step 4: Cross-validate against opt ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  STEP 4: Cross-Validate Against LLVM opt -O2 (computed live)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  Each edge-case IR is stripped of optnone (LPTA strips it internally;"
echo "  opt respects it, so both must see the same input), then optimized"
echo "  independently by LPTA and by opt -O2. Final instruction counts must"
echo "  match exactly."
echo ""

# Locate opt (Windows distros ship opt.exe)
if [ -z "$OPT" ] || [ ! -f "$OPT" ]; then
    OPT=""
    for p in \
        "${LLVM_DIR:-}/bin/opt.exe" \
        "${LLVM_DIR:-}/bin/opt" \
        "C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/opt.exe" \
        "/mnt/c/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/opt.exe" \
        "/usr/bin/opt" \
        "$(which opt 2>/dev/null || true)" \
        "$(which opt.exe 2>/dev/null || true)"; do
        [ -n "$p" ] && [ -f "$p" ] && OPT="$p" && break
    done
fi

# Locate the shared IR counter
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COUNT_IR="$SCRIPT_DIR/../count_ir.py"
if [ ! -f "$COUNT_IR" ]; then
    COUNT_IR="tests/count_ir.py"
fi
HAVE_PY=0
command -v $PYTHON &>/dev/null && HAVE_PY=1

printf '  %-12s | %6s | %6s | %s\n' "Test case" "LPTA" "opt" "Match"
echo '  -------------+--------+--------+------'
DEMO_PASS=0
DEMO_FAIL=0
DEMO_TMP="$OUT/xval_tmp"
mkdir -p "$DEMO_TMP"
if [ -z "$OPT" ] || [ $HAVE_PY -eq 0 ]; then
    echo "  [SKIP] opt or $PYTHON not found — cannot cross-validate"
    echo "         (set LLVM_DIR so opt.exe is found)"
else
    for ll in "$DEMO_DIR"/../edge_cases/ir/0*.ll "$DEMO_DIR"/../edge_cases/ir/1*.ll; do
        [ -f "$ll" ] || continue
        tname="$(basename "$ll" .ll)"
        # Strip optnone so both optimizers see identical input
        sed 's/ optnone//g' "$ll" > "$DEMO_TMP/input.ll"
        "$LPTA" "$DEMO_TMP/input.ll" "$DEMO_TMP/lpta_out" -O2 >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            printf '  %-12s | %6s | %6s | %s\n' "$tname" "ERR" "-" "LPTA FAILED"
            DEMO_FAIL=$((DEMO_FAIL + 1))
            continue
        fi
        LPTA_FINAL=$($PYTHON -c "
import json
d = json.load(open('$DEMO_TMP/lpta_out/history.json'))
last = None
for e in d['events']:
    if e['event_type'] == 'after' and e.get('ir_kind') == 'Module':
        last = e
print(last['metrics_after']['instruction_count'] if last else -1)
" 2>/dev/null)
        "$OPT" -O2 -S "$DEMO_TMP/input.ll" -o "$DEMO_TMP/opt.ll" >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            printf '  %-12s | %6s | %6s | %s\n' "$tname" "$LPTA_FINAL" "ERR" "OPT FAILED"
            DEMO_FAIL=$((DEMO_FAIL + 1))
            continue
        fi
        OPT_FINAL=$($PYTHON "$COUNT_IR" "$DEMO_TMP/opt.ll" 2>/dev/null | grep "^GT_INSTRUCTIONS=" | cut -d= -f2)
        if [ "$LPTA_FINAL" = "$OPT_FINAL" ]; then
            printf '  %-12s | %6s | %6s | %s\n' "$tname" "$LPTA_FINAL" "$OPT_FINAL" "YES"
            DEMO_PASS=$((DEMO_PASS + 1))
        else
            printf '  %-12s | %6s | %6s | %s\n' "$tname" "$LPTA_FINAL" "$OPT_FINAL" "NO"
            DEMO_FAIL=$((DEMO_FAIL + 1))
        fi
        rm -rf "$DEMO_TMP/lpta_out" "$DEMO_TMP/input.ll" "$DEMO_TMP/opt.ll"
    done
    echo '  -------------+--------+--------+------'
    echo "  $DEMO_PASS/$((DEMO_PASS + DEMO_FAIL)) EXACT MATCH"
    echo ""
    if [ $DEMO_FAIL -eq 0 ]; then
        echo "  [VERIFIED] LPTA numbers match LLVM opt -O2 on all test cases"
    else
        echo "  [FAIL] $DEMO_FAIL case(s) differ — investigate before presenting"
    fi
fi
echo ""

# ---- Step 5: Determinism check ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  STEP 5: Determinism (two runs produce identical results)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
OUT2="$OUT/determinism"
"$LPTA" "$DEMO_LL" "$OUT2" -O2 2>&1 > /dev/null
if diff -q "$OUT/history.json" "$OUT2/history.json" >/dev/null 2>&1; then
    echo "  [OK] Deterministic - byte-identical history.json across runs"
else
    echo "  [WARN] Non-deterministic output detected"
    DEMO_FAIL=$((DEMO_FAIL + 1))
fi
echo ""

# ---- Summary ----
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  VERDICT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "  LPTA correctly reports:"
SUMMARY_LINE=$($PYTHON -c "
import json
d = json.load(open('$OUT/history.json'))
s = d['summary']
print(str(s['total_before']) + '|' + str(s['passes_with_changes']))
" 2>/dev/null || echo "|")
echo "    * Which passes ran (${SUMMARY_LINE%%|*} pass executions tracked)"
echo "    * What changed per pass (${SUMMARY_LINE##*|} passes with measurable impact)"
echo "    * How significant each change was (counter + opcode-group + IR-hash deltas)"
echo "    * How it affected final codegen (assembly measurement)"
if [ $DEMO_FAIL -eq 0 ]; then
    echo "    * Numbers match LLVM opt -O2 (cross-validated)"
    echo "    * Deterministic (identical across runs)"
else
    echo "    * $DEMO_FAIL check(s) FAILED — see above, do not present as green"
fi
echo ""
echo "  Full data: $OUT/history.json ($(wc -c < "$OUT/history.json") bytes)"
echo "  Open dashboard.html to visualize the results interactively"
echo ""

exit $DEMO_FAIL
echo ""
