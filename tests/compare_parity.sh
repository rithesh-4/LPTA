#!/bin/bash
# ============================================================
# LPTA Compare Parity Test
#
# Runs CLI `--compare` and serve_dashboard.py `/api/compare` on the SAME
# pair of histories and asserts they agree: scores within tolerance and
# the same regressed/not verdict. Guards the AGENTS.md sync rule that
# both implementations share one formula.
#
# Usage: bash tests/compare_parity.sh [build_dir]   (needs python3)
# ============================================================

set -u

BUILD_DIR="${1:-./build}"
EXE="$BUILD_DIR/lpta_test.exe"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$BUILD_DIR/parity_tmp"
PORT=18123
PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); echo "  [PASS] $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  [FAIL] $1"; }

echo "=== LPTA Compare Parity (CLI vs server) ==="

if ! command -v python3 &>/dev/null; then
    echo "  [SKIP] python3 not found"
    exit 0
fi
if [ ! -f "$EXE" ]; then
    fail "binary not found at $EXE"
    exit 1
fi

rm -rf "$TMP"
mkdir -p "$TMP"

# Two genuinely different histories (opt level + targets on one side)
"$EXE" real_test.ll "$TMP/base" -O2 >/dev/null 2>&1 || { fail "baseline run failed"; exit 1; }
"$EXE" real_test.ll "$TMP/curr" -O3 --targets=common >/dev/null 2>&1 || { fail "current run failed"; exit 1; }

# --- CLI side ---
CLI_OUT=$("$EXE" --compare "$TMP/base/history.json" "$TMP/curr/history.json" 2>&1)
CLI_RC=$?
CLI_SCORE=$(echo "$CLI_OUT" | grep -oE "Heuristic indicator: [0-9]+" | grep -oE "[0-9]+")
if [ -z "$CLI_SCORE" ]; then
    fail "could not parse CLI score"
    echo "$CLI_OUT" | head -5
    exit 1
fi
echo "  CLI score: $CLI_SCORE (rc=$CLI_RC)"

# --- Server side ---
python3 "$SCRIPT_DIR/../serve_dashboard.py" "$TMP/base" -p "$PORT" >"$TMP/srv.log" 2>&1 &
SRV=$!
cleanup() { kill "$SRV" 2>/dev/null; rm -rf "$TMP"; }
trap cleanup EXIT
# Wait for readiness: fixed sleeps flake on cold interpreter starts.
READY=0
for _w in $(seq 1 30); do
    if python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$PORT/api/health', timeout=3)" >/dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 1
done
if [ $READY -ne 1 ]; then
    fail "server never became ready on port $PORT"
    echo "--- server log ---"
    cat "$TMP/srv.log" 2>/dev/null
    echo "--- listening ports ---"
    (netstat -an 2>/dev/null || ss -tlnp 2>/dev/null) | grep -E "18123|LISTEN" | head -5
    exit 1
fi
SRV_JSON=$(python3 - "$TMP/base/history.json" "$TMP/curr/history.json" "$PORT" <<'PYEOF'
import json, sys, urllib.request
base = json.load(open(sys.argv[1]))
curr = json.load(open(sys.argv[2]))
req = urllib.request.Request(
    f"http://127.0.0.1:{sys.argv[3]}/api/compare",
    data=json.dumps({"base": base, "current": curr}).encode(),
    headers={"Content-Type": "application/json"})
print(json.dumps(json.loads(urllib.request.urlopen(req, timeout=120).read())))
PYEOF
)
if [ -z "$SRV_JSON" ]; then
    fail "server compare returned nothing"
    exit 1
fi
SRV_SCORE=$(echo "$SRV_JSON" | python3 -c "import json,sys; print(json.load(sys.stdin)['regression_score'])")
echo "  Server score: $SRV_SCORE"

# --- Parity assertions ---
DIFF=$((CLI_SCORE - SRV_SCORE))
ABS_DIFF=${DIFF#-}
if [ "$ABS_DIFF" -le 2 ]; then
    pass "scores agree within tolerance (CLI=$CLI_SCORE server=$SRV_SCORE)"
else
    fail "score drift too large (CLI=$CLI_SCORE server=$SRV_SCORE)"
fi

# Verdict boundary parity: CLI exits 1 iff score > 60
if { [ "$CLI_RC" -ne 0 ] && [ "$SRV_SCORE" -gt 60 ]; } || \
   { [ "$CLI_RC" -eq 0 ] && [ "$SRV_SCORE" -le 60 ]; }; then
    pass "verdict boundary agrees (CLI rc=$CLI_RC server=$SRV_SCORE)"
else
    fail "verdict mismatch (CLI rc=$CLI_RC server=$SRV_SCORE)"
fi

echo "=== Parity: $PASS passed, $FAIL failed ==="
exit $FAIL
