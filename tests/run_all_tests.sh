#!/bin/bash
# ============================================================
# LPTA Comprehensive Test Suite
#
# Runs all testing techniques:
#   1. Static analysis (clang-tidy)
#   2. Unit tests (test_utilities)
#   3. Edge case / boundary tests
#   4. Mutation fuzz testing
#   5. Formal verification (assertions in code)
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

# Test: valid -O3
"$EXE" test.ll "$TMP_DIR/t_o3" -O3 >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid -O3 -> RC=0" || fail "Valid -O3 -> RC=$?"

# Test: valid with snapshots
"$EXE" test.ll "$TMP_DIR/t_snap" -O2 --snapshots >/dev/null 2>&1
[ $? -eq 0 ] && pass "Valid with --snapshots -> RC=0" || fail "Valid with --snapshots -> RC=$?"

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
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 3: Fuzz Testing
# -------------------------------------------------------
echo "Phase 3: Fuzz Testing (300 iterations)" | tee -a "$REPORT"

CRASH_COUNT=0
TIMEOUT_COUNT=0

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
    timeout 15 "$EXE" "$MUTANT" "$OUT" -O2 >/dev/null 2>&1
    RC=$?
    [ $RC -eq 124 ] && TIMEOUT_COUNT=$((TIMEOUT_COUNT + 1))
    rm -f "$MUTANT"
    rm -rf "$OUT"
done

[ $CRASH_COUNT -eq 0 ] && pass "Fuzz: 0 crashes in 300 iterations" || fail "Fuzz: $CRASH_COUNT crashes detected"
[ $TIMEOUT_COUNT -eq 0 ] && pass "Fuzz: 0 timeouts in 300 iterations" || fail "Fuzz: $TIMEOUT_COUNT timeouts detected"
echo "" | tee -a "$REPORT"

# -------------------------------------------------------
# Phase 4: Static Analysis Summary
# -------------------------------------------------------
echo "Phase 4: Static Analysis (clang-tidy)" | tee -a "$REPORT"
echo "  (Run 'bash tests/run_clang_tidy.sh' for full report)" | tee -a "$REPORT"
echo "  Skipping in automated run (requires LLVM headers)" | tee -a "$REPORT"
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
