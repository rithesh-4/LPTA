#!/bin/bash
# ============================================================
# LPTA Compare Golden Tests
#
# For every tests/compare_golden/NN_name fixture:
#   1. CLI (--compare --json) and server (/api/compare) must produce
#      byte-equivalent NORMALIZED results (canonical JSON, sorted keys).
#   2. The result must satisfy expected.json (verdict, score, statuses).
# Cases with a flags file also run the CLI override path (must succeed).
#
# Usage: bash tests/compare_golden.sh [build_dir]   (needs python3)
# ============================================================

set -u

BUILD_DIR="${1:-./build}"
EXE="$BUILD_DIR/lpta_test.exe"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# Run from the repo root: lpta_test is a Windows binary and cannot open
# WSL-style /mnt/c absolute paths, so everything below stays relative.
cd "$SCRIPT_DIR/.." || exit 1
GOLDEN_DIR="tests/compare_golden"
PORT=18125
PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); echo "  [PASS] $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  [FAIL] $1"; }

echo "=== LPTA Compare Golden Tests ==="

if ! command -v python3 &>/dev/null; then
    echo "  [SKIP] python3 not found"
    exit 0
fi
if [ ! -f "$EXE" ]; then
    fail "binary not found at $EXE"
    exit 1
fi
if [ ! -d "$GOLDEN_DIR" ]; then
    fail "golden dir missing: $GOLDEN_DIR"
    exit 1
fi

# Server for the /api/compare side (one instance for all cases)
python3 "$SCRIPT_DIR/../serve_dashboard.py" "$BUILD_DIR" -p "$PORT" >"$BUILD_DIR/golden_srv.log" 2>&1 &
SRV=$!
cleanup() { kill "$SRV" 2>/dev/null; }
trap cleanup EXIT
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
    cat "$BUILD_DIR/golden_srv.log" 2>/dev/null
    exit 1
fi

norm() {
    # Canonical normalization: parse + re-emit with sorted keys.
    python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1])), sort_keys=True))" "$1"
}

for case_dir in "$GOLDEN_DIR"/*/; do
    case_name=$(basename "$case_dir")
    base="$case_dir/base.json"
    curr="$case_dir/curr.json"
    expected="$case_dir/expected.json"
    [ -f "$base" ] && [ -f "$curr" ] && [ -f "$expected" ] || { fail "$case_name: fixture files missing"; continue; }

    if "$EXE" --compare "$base" "$curr" --json >"$case_dir/.cli.json" 2>"$case_dir/.cli.err"; then
        cli_rc=0
    else
        cli_rc=$?
    fi
    if ! python3 - "$base" "$curr" "$PORT" <<'PYEOF' >"$case_dir/.srv.json" 2>"$case_dir/.srv.err"
import json, sys, urllib.request
base = json.load(open(sys.argv[1]))
curr = json.load(open(sys.argv[2]))
req = urllib.request.Request(
    f"http://127.0.0.1:{sys.argv[3]}/api/compare",
    data=json.dumps({"base": base, "current": curr}).encode(),
    headers={"Content-Type": "application/json"})
print(json.dumps(json.loads(urllib.request.urlopen(req, timeout=120).read())))
PYEOF
    then
        fail "$case_name: server request failed"
        head -c 600 "$case_dir/.srv.err"
        continue
    fi

    if ! norm "$case_dir/.cli.json" >"$case_dir/.cli.norm" 2>/dev/null; then
        fail "$case_name: CLI output is not valid JSON"; continue
    fi
    if ! norm "$case_dir/.srv.json" >"$case_dir/.srv.norm" 2>/dev/null; then
        fail "$case_name: server output is not valid JSON"; continue
    fi
    if cmp -s "$case_dir/.cli.norm" "$case_dir/.srv.norm"; then
        pass "$case_name: engines byte-equivalent"
    else
        fail "$case_name: CLI/server diverge"
        continue
    fi

    if python3 - "$case_dir/.cli.norm" "$expected" "$cli_rc" <<'PYEOF'
import json, sys
got = json.load(open(sys.argv[1]))
exp = json.load(open(sys.argv[2]))
cli_rc = int(sys.argv[3])
errs = []
if got.get("verdict") != exp.get("verdict"):
    errs.append(f"verdict {got.get('verdict')} != {exp.get('verdict')}")
if got.get("regression_score") != exp.get("score"):
    errs.append(f"score {got.get('regression_score')} != {exp.get('score')}")
if bool(got.get("compat", {}).get("blocked")) != exp.get("blocked", False):
    errs.append("blocked mismatch")
if cli_rc != exp.get("cli_rc", 0):
    errs.append(f"cli rc {cli_rc} != {exp.get('cli_rc', 0)}")
if len(got.get("regressions", [])) != exp.get("regressions", 0):
    errs.append("regressions count mismatch")
if len(got.get("improvements", [])) != exp.get("improvements", 0):
    errs.append("improvements count mismatch")
for pn, want in exp.get("pass_status", {}).items():
    got_p = [p for p in got.get("passes", []) if p.get("pass_name") == pn]
    if not got_p:
        errs.append(f"pass {pn} missing"); continue
    if [got_p[0].get("status"), got_p[0].get("effect_status")] != want:
        errs.append(f"pass {pn} status mismatch")
for tn, want in exp.get("target_states", {}).items():
    got_t = [t for t in got.get("targets", []) if t.get("target") == tn]
    if not got_t:
        errs.append(f"target {tn} missing"); continue
    if got_t[0].get("state") != want:
        errs.append(f"target {tn} state mismatch")
if errs:
    print("; ".join(errs))
    sys.exit(1)
PYEOF
    then
        pass "$case_name: expectations hold"
    else
        fail "$case_name: expectation mismatch"
    fi

    # Override path for flagged cases (e.g. input mismatch with consent)
    if [ -f "$case_dir/flags" ]; then
        # shellcheck disable=SC2086: flags file is trusted test input
        if "$EXE" --compare "$base" "$curr" --json $(cat "$case_dir/flags") >"$case_dir/.ovr.json" 2>/dev/null; then
            if python3 -c "import json; d=json.load(open('$case_dir/.ovr.json')); assert d.get('verdict') not in (None, 'incomparable'), d.get('verdict')" 2>/dev/null; then
                pass "$case_name: override run produces a verdict"
            else
                fail "$case_name: override run has no verdict"
            fi
        else
            fail "$case_name: override run failed"
        fi
        rm -f "$case_dir/.ovr.json"
    fi
    rm -f "$case_dir"/.cli.json "$case_dir"/.srv.json "$case_dir"/.cli.norm "$case_dir"/.srv.norm "$case_dir"/.cli.err "$case_dir"/.srv.err
done

MALFORMED="$BUILD_DIR/.malformed_history.json"
python3 - "$GOLDEN_DIR/01_identical/base.json" "$MALFORMED" <<'PYEOF'
import json, sys
data = json.load(open(sys.argv[1]))
after = next(e for e in data["events"] if e["event_type"] == "after")
after["metrics_after"]["instruction_count"] = "not-a-number"
json.dump(data, open(sys.argv[2], "w"), indent=2)
PYEOF
if "$EXE" --compare "$MALFORMED" "$GOLDEN_DIR/01_identical/curr.json" --json \
    >/dev/null 2>"$BUILD_DIR/.malformed_cli.err"; then
    fail "malformed history: CLI accepted an invalid metric"
else
    pass "malformed history: CLI rejects invalid metric types"
fi
if python3 - "$MALFORMED" "$GOLDEN_DIR/01_identical/curr.json" "$PORT" <<'PYEOF'
import json, sys, urllib.error, urllib.request
base = json.load(open(sys.argv[1]))
curr = json.load(open(sys.argv[2]))
req = urllib.request.Request(
    f"http://127.0.0.1:{sys.argv[3]}/api/compare",
    data=json.dumps({"base": base, "current": curr}).encode(),
    headers={"Content-Type": "application/json"})
try:
    urllib.request.urlopen(req, timeout=30)
except urllib.error.HTTPError as exc:
    body = json.loads(exc.read())
    assert exc.code == 400
    assert body.get("error") == "invalid history schema"
    assert body.get("details")
else:
    raise AssertionError("server accepted malformed history")
PYEOF
then
    pass "malformed history: server returns structured validation errors"
else
    fail "malformed history: server validation contract failed"
fi
rm -f "$MALFORMED" "$BUILD_DIR/.malformed_cli.err"

if python3 - "$EXE" "$GOLDEN_DIR/01_identical/base.json" "$PORT" "$BUILD_DIR" <<'PYEOF'
import copy
import json
import pathlib
import subprocess
import sys
import urllib.error
import urllib.request

exe, source_path, port, build_dir = sys.argv[1:]
source = json.load(open(source_path))
build = pathlib.Path(build_dir)

def server_compare(base, curr):
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/compare",
        data=json.dumps({"base": base, "current": curr}).encode(),
        headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=30).read())

def cli_compare(base_path, curr_path):
    proc = subprocess.run(
        [exe, "--compare", str(base_path), str(curr_path), "--json"],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise AssertionError(proc.stderr)
    return json.loads(proc.stdout)

for name, fail_base, fail_curr in (
        ("baseline", True, False),
        ("current", False, True),
        ("both", True, True)):
    base = copy.deepcopy(source)
    curr = copy.deepcopy(source)
    for report, failed in ((base, fail_base), (curr, fail_curr)):
        if failed:
            report["summary"]["codegen_error_before"] = "llc before failed"
            report["summary"]["codegen_error_after"] = "llc after failed"
            report["summary"]["codegen_asm_lines_before"] = 0
            report["summary"]["codegen_asm_lines_after"] = 0
            report["summary"]["codegen_asm_bytes_before"] = 0
            report["summary"]["codegen_asm_bytes_after"] = 0
    base_path = build / f".codegen_{name}_base.json"
    curr_path = build / f".codegen_{name}_curr.json"
    base_path.write_text(json.dumps(base, indent=2))
    curr_path.write_text(json.dumps(curr, indent=2))
    cli = cli_compare(base_path, curr_path)
    server = server_compare(base, curr)
    assert cli == server, name
    for key in (
            "codegen_asm_lines_before", "codegen_asm_lines_after",
            "codegen_asm_bytes_before", "codegen_asm_bytes_after",
            "codegen_reduction_delta"):
        assert cli["summary"][key] is None, (name, key)
    assert cli["coverage"]["codegen"] is False, name
    assert cli["score_components"]["codegen"] == 0, name
    assert not any(
        item.get("type") in ("codegen_regression", "codegen_improvement")
        for item in cli["regressions"] + cli["improvements"]), name
    base_path.unlink()
    curr_path.unlink()

for name, mutate in (
        ("missing_summary", lambda d:
            d["summary"].pop("total_instructions_after")),
        ("missing_metric", lambda d:
            next(e for e in d["events"]
                 if e["event_type"] == "after")["metrics_after"].pop(
                     "instruction_count"))):
    malformed = copy.deepcopy(source)
    mutate(malformed)
    malformed_path = build / f".{name}.json"
    malformed_path.write_text(json.dumps(malformed, indent=2))
    proc = subprocess.run(
        [exe, "--compare", str(malformed_path), source_path, "--json"],
        capture_output=True, text=True)
    assert proc.returncode != 0, name
    try:
        server_compare(malformed, source)
    except urllib.error.HTTPError as exc:
        body = json.loads(exc.read())
        assert exc.code == 400, name
        assert body.get("error") == "invalid history schema", name
        assert body.get("details"), name
    else:
        raise AssertionError(f"server accepted {name}")
    malformed_path.unlink()
PYEOF
then
    pass "availability/schema fixtures: CLI/server parity holds"
else
    fail "availability/schema fixtures: parity or validation failed"
fi

echo "=== Golden: $PASS passed, $FAIL failed ==="
exit $FAIL
