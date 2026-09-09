#!/bin/bash
# ============================================================
# LPTA Comprehensive Test Suite
#
# Runs all testing techniques:
#   1. Static analysis (clang-tidy, optional)
#   2. Unit tests (test_utilities)
#   3. Edge case / boundary tests
#   4. Mutation fuzz testing
#   5. Formal verification (assertions in code)
#   6. Ground-truth proof (judge_proof.sh on tiny_proof.ll)
#   7. Cross-validation (validate_correctness.sh: independent recount + opt -stats + determinism)
#
# Usage: bash tests/run_all_tests.sh [build_dir]
# ============================================================

set -u

BUILD_DIR="${1:-./build}"
TMP_DIR="$BUILD_DIR/test_tmp"
mkdir -p "$TMP_DIR"
EXE="$BUILD_DIR/lpta_test.exe"
TEST_EXE="$BUILD_DIR/test_utilities.exe"
REPORT="tests/test_report.txt"
PASS_COUNT=0
FAIL_COUNT=0
TOTAL=0

pass() { PASS_COUNT=$((PASS_COUNT + 1)); TOTAL=$((TOTAL + 1)); echo "  [PASS] $1" | tee -a "$REPORT"; }
fail() { FAIL_COUNT=$((FAIL_COUNT + 1)); TOTAL=$((TOTAL + 1)); echo "  [FAIL] $1" | tee -a "$REPORT"; }

echo "=== LPTA Comprehensive Test Suite ===" | tee "$REPORT"
echo "Date: $(date)" | tee -a "$REPORT"
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 1: Unit Tests
# -------------------------------------------------------
echo "Phase 1: Unit Tests" | tee -a "$REPORT"
if [ -f "$TEST_EXE" ]; then
    OUT=$("$TEST_EXE" 2>/dev/null)
    RC=$?
    PASSES=$(echo "$OUT" | grep -c "\[PASS\]")
    FAILS=$(echo "$OUT" | grep -c "\[FAIL\]")
    if [ $RC -eq 0 ]; then
        pass "Unit tests: $PASSES/$((PASSES + FAILS)) passed"
    else
        fail "Unit tests: $FAILS failures out of $((PASSES + FAILS))"
    fi
else
    fail "Unit test binary not found at $TEST_EXE"
fi
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 2: Edge Case Tests
# -------------------------------------------------------
echo "Phase 2: Edge Case Tests" | tee -a "$REPORT"

# Test: no arguments
"$EXE" >/dev/null 2>&1
[ $? -eq 1 ] && pass "No arguments -> RC=1" || fail "No arguments -> RC=$? (expected 1)"

# Test: nonexistent file
"$EXE" nonexistent.ll "$TMP_DIR/t_nonexist" >/dev/null 2>&1
[ $? -eq 1 ] && pass "Nonexistent file -> RC=1" || fail "Nonexistent file -> RC=$?"

# Test: empty file
touch "$TMP_DIR/t_empty.ll"
"$EXE" "$TMP_DIR/t_empty.ll" "$TMP_DIR/t_empty_out" >/dev/null 2>&1
[ $? -eq 0 ] && pass "Empty file -> RC=0" || fail "Empty file -> RC=$?"

# Test: binary garbage
dd if=/dev/urandom bs=256 count=1 2>/dev/null > "$TMP_DIR/t_garbage.ll"
"$EXE" "$TMP_DIR/t_garbage.ll" "$TMP_DIR/t_garbage_out" >/dev/null 2>&1
[ $? -eq 1 ] && pass "Binary garbage -> RC=1" || fail "Binary garbage -> RC=$?"

# Test: truncated IR
head -5 test.ll > "$TMP_DIR/t_truncated.ll"
"$EXE" "$TMP_DIR/t_truncated.ll" "$TMP_DIR/t_trunc_out" >/dev/null 2>&1
[ $? -eq 1 ] && pass "Truncated IR -> RC=1" || fail "Truncated IR -> RC=$?"

# Test: unknown flag (FIX #9)
"$EXE" test.ll "$TMP_DIR/t_flag" --bogus-flag >/dev/null 2>&1
[ $? -eq 1 ] && pass "Unknown flag -> RC=1 (FIX #9 verified)" || fail "Unknown flag -> RC=$?"

# Test: valid -O0
"$EXE" test.ll "$TMP_DIR/t_o0" -O0 >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -O0 -> RC=0" || fail "Valid -O0 -> RC=$?"

# Test: valid -O1
"$EXE" test.ll "$TMP_DIR/t_o1" -O1 >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -O1 -> RC=0" || fail "Valid -O1 -> RC=$?"

# Test: valid -O3
"$EXE" test.ll "$TMP_DIR/t_o3" -O3 >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -O3 -> RC=0" || fail "Valid -O3 -> RC=$?"

# Test: valid -Os / -Oz
"$EXE" test.ll "$TMP_DIR/t_os" -Os >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -Os -> RC=0" || fail "Valid -Os -> RC=$?"
"$EXE" test.ll "$TMP_DIR/t_oz" -Oz >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -Oz -> RC=0" || fail "Valid -Oz -> RC=$?"

# Test: bad --targets values
"$EXE" test.ll "$TMP_DIR/t_badt" --targets= >/dev/null 2>&1
[ $? -eq 1 ] && pass "Empty --targets -> RC=1" || fail "Empty --targets -> RC=$?"
"$EXE" test.ll "$TMP_DIR/t_badt2" --targets=@nonexistent_targets_file.txt >/dev/null 2>&1
[ $? -eq 1 ] && pass "Missing @targets file -> RC=1" || fail "Missing @targets file -> RC=$?"

# Test: --compare needs two args
"$EXE" --compare >/dev/null 2>&1
[ $? -eq 1 ] && pass "--compare without args -> RC=1" || fail "--compare without args -> RC=$?"

# Test: --version prints LLVM version
if "$EXE" --version 2>/dev/null | grep -qE "[0-9]+\.[0-9]+"; then
    pass "--version reports LLVM version"
else
    fail "--version missing version number"
fi

# Test: valid with snapshots
"$EXE" test.ll "$TMP_DIR/t_snap" -O2 --snapshots >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid with --snapshots -> RC=0" || fail "Valid with --snapshots -> RC=$?"
# Snapshots must actually retain IR text (not just run clean)
if grep -q '"ir_before": "' "$TMP_DIR/t_snap/history.json" 2>/dev/null; then
    pass "--snapshots retains IR text in history.json"
else
    fail "--snapshots produced no IR text (diffs would be empty)"
fi

# Test: real test file
"$EXE" real_test.ll "$TMP_DIR/t_real" -O2 >/dev/null 2>&1
[ $? -eq 0 ] && pass "real_test.ll -O2 -> RC=0" || fail "real_test.ll -O2 -> RC=$?"

# Test: stack balance
OUT=$("$EXE" test.ll "$TMP_DIR/t_stack" -O2 2>&1)
echo "$OUT" | grep -q "Stack remaining: 0" && pass "Stack balanced (FIX #7 verified)" || fail "Stack not balanced"

# Test: JSON valid
"$EXE" test.ll "$TMP_DIR/t_json" -O2 >/dev/null 2>&1
if [ -f "$TMP_DIR/t_json/history.json" ]; then
    grep -q '"events"' "$TMP_DIR/t_json/history.json" && pass "JSON output has events array" || fail "JSON missing events"
    grep -q '"summary"' "$TMP_DIR/t_json/history.json" && pass "JSON output has summary" || fail "JSON missing summary"
else
    fail "JSON file not created"
fi

# Test: --compare parses the final event (regression test for last-event drop)
# Single-event fixtures where the ONLY event is also the last one: old parser
# dropped it, so BarPass would never appear in the output.
COMPARE_BASE="$TMP_DIR/cmp_base.json"
COMPARE_CURR="$TMP_DIR/cmp_curr.json"
cat > "$COMPARE_BASE" <<'JSONEOF'
{
  "module_name": "test.ll",
  "pipeline": "O2",
  "events": [
    {
      "id": 1,
      "event_type": "after",
      "pass_name": "BarPass",
      "pass_type": "transformation",
      "ir_kind": "Module",
      "ir_name": "test",
      "depth": 0,
      "metrics_before": {
        "instruction_count": 50,
        "basic_block_count": 5,
        "function_count": 1,
        "global_count": 0,
        "call_count": 0,
        "load_count": 0,
        "store_count": 0,
        "branch_count": 1,
        "phi_count": 0,
        "return_count": 1
      },
      "metrics_after": {
        "instruction_count": 40,
        "basic_block_count": 5,
        "function_count": 1,
        "global_count": 0,
        "call_count": 0,
        "load_count": 0,
        "store_count": 0,
        "branch_count": 1,
        "phi_count": 0,
        "return_count": 1
      },
      "has_changes": true,
      "ir_before": null,
      "ir_after": null
    }
  ],
  "summary": {
    "total_events": 1,
    "total_before": 0,
    "total_after": 1,
    "total_invalidated": 0,
    "passes_with_changes": 1,
    "unique_pass_names": 1,
    "total_instructions_before": 150,
    "total_instructions_after": 100,
    "total_bbs_before": 10,
    "total_bbs_after": 10,
    "codegen_asm_lines_before": 50,
    "codegen_asm_lines_after": 50,
    "codegen_asm_bytes_before": 500,
    "codegen_asm_bytes_after": 500,
    "codegen_error_before": null,
    "codegen_error_after": null,
    "codegen_targets": {},
    "optnone_detected": false,
    "optnone_function_count": 0,
    "optnone_functions": [],
    "optnone_warning": ""
  }
}
JSONEOF
cat > "$COMPARE_CURR" <<'JSONEOF'
{
  "module_name": "test.ll",
  "pipeline": "O2",
  "events": [
    {
      "id": 1,
      "event_type": "after",
      "pass_name": "BarPass",
      "pass_type": "transformation",
      "ir_kind": "Module",
      "ir_name": "test",
      "depth": 0,
      "metrics_before": {
        "instruction_count": 50,
        "basic_block_count": 5,
        "function_count": 1,
        "global_count": 0,
        "call_count": 0,
        "load_count": 0,
        "store_count": 0,
        "branch_count": 1,
        "phi_count": 0,
        "return_count": 1
      },
      "metrics_after": {
        "instruction_count": 80,
        "basic_block_count": 5,
        "function_count": 1,
        "global_count": 0,
        "call_count": 0,
        "load_count": 0,
        "store_count": 0,
        "branch_count": 1,
        "phi_count": 0,
        "return_count": 1
      },
      "has_changes": true,
      "ir_before": null,
      "ir_after": null
    }
  ],
  "summary": {
    "total_events": 1,
    "total_before": 0,
    "total_after": 1,
    "total_invalidated": 0,
    "passes_with_changes": 1,
    "unique_pass_names": 1,
    "total_instructions_before": 150,
    "total_instructions_after": 100,
    "total_bbs_before": 10,
    "total_bbs_after": 10,
    "codegen_asm_lines_before": 50,
    "codegen_asm_lines_after": 50,
    "codegen_asm_bytes_before": 500,
    "codegen_asm_bytes_after": 500,
    "codegen_error_before": null,
    "codegen_error_after": null,
    "codegen_targets": {},
    "optnone_detected": false,
    "optnone_function_count": 0,
    "optnone_functions": [],
    "optnone_warning": ""
  }
}
JSONEOF
COMPARE_OUT=$("$EXE" --compare "$COMPARE_BASE" "$COMPARE_CURR" 2>&1)
if echo "$COMPARE_OUT" | grep -q "BarPass"; then
    pass "--compare parses final event (last-event fix verified)"
else
    fail "--compare missed final event (last-event parse bug)"
fi
rm -f "$COMPARE_BASE" "$COMPARE_CURR"

# Test: --compare with empty events array + per-target codegen
# Covers single-line "events": [] (must not swallow the summary) and
# per-target findings (regression + new/removed targets).
COMPARE_TBASE="$TMP_DIR/cmp_t_base.json"
COMPARE_TCURR="$TMP_DIR/cmp_t_curr.json"
write_target_fixture() {
    local file="$1" aarch_after="$2" extra_target_json="$3"
    cat > "$file" <<JSONEOF
{
  "module_name": "t.ll",
  "pipeline": "O2",
  "events": [],
  "summary": {
    "total_events": 0,
    "total_before": 0,
    "total_after": 0,
    "total_invalidated": 0,
    "passes_with_changes": 0,
    "unique_pass_names": 0,
    "total_instructions_before": 100,
    "total_instructions_after": 100,
    "total_bbs_before": 10,
    "total_bbs_after": 10,
    "codegen_asm_lines_before": 100,
    "codegen_asm_lines_after": 100,
    "codegen_asm_bytes_before": 1000,
    "codegen_asm_bytes_after": 1000,
    "codegen_error_before": null,
    "codegen_error_after": null,
    "codegen_targets": {
      "x86_64-pc-windows-msvc": {
        "asm_lines_before": 100,
        "asm_lines_after": 90,
        "asm_bytes_before": 1000,
        "asm_bytes_after": 900,
        "error": null
      },
      "aarch64-unknown-linux-gnu": {
        "asm_lines_before": 100,
        "asm_lines_after": $aarch_after,
        "asm_bytes_before": 1000,
        "asm_bytes_after": 900,
        "error": null
      }$extra_target_json
    },
    "optnone_detected": false,
    "optnone_function_count": 0,
    "optnone_functions": [],
    "optnone_warning": ""
  }
}
JSONEOF
}
write_target_fixture "$COMPARE_TBASE" 90 ""
write_target_fixture "$COMPARE_TCURR" 99 ",
      \"new-target\": {
        \"asm_lines_before\": 50,
        \"asm_lines_after\": 40,
        \"asm_bytes_before\": 500,
        \"asm_bytes_after\": 400,
        \"error\": null
      }"
COMPARE_TOUT=$("$EXE" --compare "$COMPARE_TBASE" "$COMPARE_TCURR" 2>&1)
if echo "$COMPARE_TOUT" | grep -q "aarch64-unknown-linux-gnu codegen regression"; then
    pass "--compare per-target regression detected"
else
    fail "--compare missed per-target regression"
fi
if echo "$COMPARE_TOUT" | grep -q "new-target"; then
    pass "--compare new target detected"
else
    fail "--compare missed new target"
fi
if echo "$COMPARE_TOUT" | grep -q "Instructions (after):  100 -> 100"; then
    pass "--compare empty events array leaves summary intact"
else
    fail "--compare summary lost with empty events array"
fi
rm -f "$COMPARE_TBASE" "$COMPARE_TCURR"
# Test: independent recount matches LPTA initial metrics on every shipped input
if command -v python3 &>/dev/null && [ -f tests/count_ir.py ]; then
    SWEEP_PASS=0
    SWEEP_FAIL=0
    for _f in test.ll real_test.ll tests/tiny_proof.ll tests/edge_cases/ir/0*.ll tests/edge_cases/ir/1*.ll tests/judge_demo/demo.ll; do
        [ -f "$_f" ] || continue
        _n=$(basename "$_f" .ll)
        "$EXE" "$_f" "$TMP_DIR/sweep_$_n" -O0 >/dev/null 2>&1
        if [ $? -ne 0 ]; then
            echo "  [SWEEP-FAIL] $_f: LPTA run failed" | tee -a "$REPORT"
            SWEEP_FAIL=$((SWEEP_FAIL + 1))
            continue
        fi
        if python3 -c "
import json, sys
sys.path.insert(0, 'tests')
from count_ir import count_file
gt = count_file('$_f')
d = json.load(open('$TMP_DIR/sweep_$_n/history.json'))
first = next(e for e in d['events'] if e['event_type'] == 'before' and e.get('ir_kind') == 'Module')
m = first['metrics']
keys = {'function_count': 'GT_FUNCTIONS', 'basic_block_count': 'GT_BBS',
        'instruction_count': 'GT_INSTRUCTIONS', 'call_count': 'GT_CALLS',
        'load_count': 'GT_LOADS', 'store_count': 'GT_STORES',
        'branch_count': 'GT_BRANCHES', 'phi_count': 'GT_PHIS',
        'return_count': 'GT_RETURNS', 'global_count': 'GT_GLOBALS'}
bad = [k for k, g in keys.items() if m[k] != gt[g]]
sys.exit(1 if bad else 0)
" >/dev/null 2>&1; then
            SWEEP_PASS=$((SWEEP_PASS + 1))
        else
            echo "  [SWEEP-FAIL] $_f: recount mismatch" | tee -a "$REPORT"
            SWEEP_FAIL=$((SWEEP_FAIL + 1))
        fi
        rm -rf "$TMP_DIR/sweep_$_n"
    done
    [ $SWEEP_FAIL -eq 0 ] && pass "Input sweep: $SWEEP_PASS shipped inputs match recount" || fail "Input sweep: $SWEEP_FAIL mismatches"
else
    echo "  [SKIP] input sweep (needs python3 + tests/count_ir.py)" | tee -a "$REPORT"
fi

# Test: headless dashboard smoke (node) against the snapshot report above
# (node.exe probe covers Git Bash, where Windows node is on PATH)
HAVE_NODE=0
command -v node &>/dev/null && HAVE_NODE=1
if [ $HAVE_NODE -eq 0 ]; then command -v node.exe &>/dev/null && HAVE_NODE=1; fi
if [ $HAVE_NODE -eq 1 ] && [ -f tests/dashboard_smoke.js ] && [ -f "$TMP_DIR/t_snap/history.json" ]; then
    _node_bin=node
    command -v node &>/dev/null || _node_bin=node.exe
    if $_node_bin tests/dashboard_smoke.js "$TMP_DIR/t_snap/history.json" >>"$REPORT" 2>&1; then
        pass "Dashboard smoke: render paths, diffs, copy, compare labels"
    else
        fail "Dashboard smoke: see $REPORT for failing check"
    fi
else
    echo "  [SKIP] dashboard smoke (needs node + snapshot report)" | tee -a "$REPORT"
fi
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 3: Fuzz Testing
# -------------------------------------------------------
echo "Phase 3: Fuzz Testing (300 iterations)" | tee -a "$REPORT"

CRASH_COUNT=0
TIMEOUT_COUNT=0
CORRUPT_COUNT=0

HAVE_PY=0
command -v python3 &>/dev/null && HAVE_PY=1

for i in $(seq 1 300); do
    MUTANT="$TMP_DIR/fuzz_$i.ll"
    
    case $((RANDOM % 6)) in
        0) head -c $((RANDOM % 1069 + 1)) test.ll > "$MUTANT" ;;
        1) cp test.ll "$MUTANT"; dd if=/dev/urandom bs=1 count=$((RANDOM % 32 + 1)) 2>/dev/null | tee -a "$MUTANT" > /dev/null ;;
        2) dd if=/dev/urandom bs=1 count=$((RANDOM % 2048 + 1)) 2>/dev/null > "$MUTANT" ;;
        3) printf 'A%.0s' $(seq 1 $((RANDOM % 500 + 50))) > "$MUTANT" ;;
        4) : > "$MUTANT" ;;
        5) cp test.ll "$MUTANT"; sed -i "${RANDOM}d" "$MUTANT" 2>/dev/null ;;
    esac
    
    OUT="$TMP_DIR/fuzz_out_$i"
    mkdir -p "$OUT"
    # Vary flags so snapshot/cap, size-opt, and multi-target paths get
    # fuzzed too — not just the default -O2 run.
    case $((i % 10)) in
        0) FUZZ_FLAGS="--snapshots" ;;
        1) FUZZ_FLAGS="-Os" ;;
        2) FUZZ_FLAGS="--targets=common" ;;
        *) FUZZ_FLAGS="" ;;
    esac
    # shellcheck disable=SC2086: intentional word splitting of FUZZ_FLAGS
    timeout 20 "$EXE" "$MUTANT" "$OUT" -O2 $FUZZ_FLAGS >/dev/null 2>&1
    RC=$?
    if [ $RC -eq 124 ]; then
        TIMEOUT_COUNT=$((TIMEOUT_COUNT + 1))
    elif [ $RC -ne 0 ] && [ $RC -ne 1 ]; then
        # Only RC=0 (success) and RC=1 (bad input) are expected;
        # anything else (segfault/abort/...) is a crash.
        CRASH_COUNT=$((CRASH_COUNT + 1))
        echo "  [FUZZ-CRASH] iter $i RC=$RC" | tee -a "$REPORT"
    elif [ $RC -eq 0 ]; then
        # Success must produce valid JSON
        if [ ! -f "$OUT/history.json" ]; then
            CORRUPT_COUNT=$((CORRUPT_COUNT + 1))
            echo "  [FUZZ-CORRUPT] iter $i missing history.json" | tee -a "$REPORT"
        elif [ $HAVE_PY -eq 1 ]; then
            if ! python3 -c "import json; json.load(open(\"$OUT/history.json\"))" >/dev/null 2>&1; then
                CORRUPT_COUNT=$((CORRUPT_COUNT + 1))
                echo "  [FUZZ-CORRUPT] iter $i invalid history.json" | tee -a "$REPORT"
            fi
        fi
    fi
    rm -f "$MUTANT"
    rm -rf "$OUT"
done

[ $CRASH_COUNT -eq 0 ] && pass "Fuzz: 0 crashes in 300 iterations" || fail "Fuzz: $CRASH_COUNT crashes detected"
[ $TIMEOUT_COUNT -eq 0 ] && pass "Fuzz: 0 timeouts in 300 iterations" || fail "Fuzz: $TIMEOUT_COUNT timeouts detected"
[ $CORRUPT_COUNT -eq 0 ] && pass "Fuzz: 0 corrupt outputs in 300 iterations" || fail "Fuzz: $CORRUPT_COUNT corrupt outputs detected"
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 4: Static Analysis Summary
# -------------------------------------------------------
echo "Phase 4: Static Analysis (clang-tidy)" | tee -a "$REPORT"
echo "  (Run 'bash tests/run_clang_tidy.sh' for full report)" | tee -a "$REPORT"
echo "  Skipping in automated run (requires LLVM headers)" | tee -a "$REPORT"
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 5: Ground-truth proof + cross-validation
# -------------------------------------------------------
echo "Phase 5: Ground-truth proof (judge_proof.sh)" | tee -a "$REPORT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -x "$SCRIPT_DIR/judge_proof.sh" ] || [ -f "$SCRIPT_DIR/judge_proof.sh" ]; then
    if bash "$SCRIPT_DIR/judge_proof.sh" "$BUILD_DIR" >/dev/null 2>&1; then
        pass "judge_proof.sh: tiny_proof.ll ground truth matches"
    else
        fail "judge_proof.sh: ground truth mismatch (see judge output)"
    fi
else
    echo "  [SKIP] judge_proof.sh not found" | tee -a "$REPORT"
fi
echo "" | tee -a "$REPORT"

echo "Phase 6: Cross-validation (validate_correctness.sh)" | tee -a "$REPORT"
if [ -f "$SCRIPT_DIR/validate_correctness.sh" ]; then
    if bash "$SCRIPT_DIR/validate_correctness.sh" "$BUILD_DIR" test.ll >/dev/null 2>&1; then
        pass "validate_correctness.sh: independent recount + determinism"
    else
        fail "validate_correctness.sh failed (see tests/correctness_report.txt)"
    fi
else
    echo "  [SKIP] validate_correctness.sh not found" | tee -a "$REPORT"
fi
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Summary
# -------------------------------------------------------
echo "=== Summary ===" | tee -a "$REPORT"
echo "  Passed: $PASS_COUNT" | tee -a "$REPORT"
echo "  Failed: $FAIL_COUNT" | tee -a "$REPORT"
echo "  Total:  $TOTAL" | tee -a "$REPORT"
if [ $FAIL_COUNT -eq 0 ]; then
    echo "  Status: ALL TESTS PASSED ✓" | tee -a "$REPORT"
else
    echo "  Status: $FAIL_COUNT FAILURES ✗" | tee -a "$REPORT"
fi
echo "" | tee -a "$REPORT"

exit $FAIL_COUNT
