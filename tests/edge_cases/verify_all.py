#!/usr/bin/env python3
"""
LPTA Edge Case Verification - v2 (Windows-safe, no unicode)
"""
import json, re, os, sys, subprocess

RESULTS_DIR = "tests/edge_cases/results"
IR_DIR = "tests/edge_cases/ir"
OPT = "C:/LLVM-full/clang+llvm-22.1.8-x86_64-pc-windows-msvc/bin/opt.exe"

METRICS = [
    ("function_count",    "Functions"),
    ("basic_block_count", "Basic Blocks"),
    ("instruction_count", "Instructions"),
    ("call_count",        "Calls"),
    ("load_count",        "Loads"),
    ("store_count",       "Stores"),
    ("branch_count",      "Branches"),
    ("phi_count",         "PHIs"),
    ("return_count",      "Returns"),
    ("global_count",      "Globals"),
]


def count_ir_ground_truth(ll_path):
    with open(ll_path) as f:
        content = f.read()
    lines = content.split("\n")

    c = {k: 0 for k, _ in METRICS}

    # Track function ranges
    func_ranges = []
    depth = 0
    start = None
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("define "):
            depth = 1
            start = i
        elif depth > 0:
            depth += line.count("{") - line.count("}")
            if depth == 0 and start is not None:
                func_ranges.append((start, i))
                start = None

    c["function_count"] = len(func_ranges)

    for fstart, fend in func_ranges:
        body = lines[fstart:fend + 1]
        labeled_blocks = 0
        entry_has_label = False
        instrs_in_func = 0

        for li, line in enumerate(body[1:], 1):
            s = line.strip()
            if not s or s.startswith(";") or s.startswith("target") or s.startswith("declare"):
                continue
            if s == "{" or s == "}":
                continue

            # Block label?
            if re.match(r"^[a-zA-Z0-9_.$\"<>/]+\s*:", s):
                labeled_blocks += 1
                rest = re.sub(r"^[a-zA-Z0-9_.$\"<>/]+\s*:\s*", "", s)
                if rest and not rest.startswith(";") and rest != "{":
                    instrs_in_func += 1
                    _classify(rest, c)
                if li == 1 or (li == 2 and not body[1].strip().startswith(";")):
                    entry_has_label = True
                continue

            instrs_in_func += 1
            _classify(s, c)

        # Entry block: if first instruction has no label, it's an unlabeled entry block
        first_line = None
        for line in body[1:]:
            s = line.strip()
            if s and not s.startswith(";") and not s.startswith("target") and s != "{" and s != "}":
                first_line = s
                break

        if first_line and not re.match(r"^[a-zA-Z0-9_.$\"<>/]+\s*:", first_line):
            c["basic_block_count"] += labeled_blocks + 1
        else:
            c["basic_block_count"] += labeled_blocks
        c["instruction_count"] += instrs_in_func

    # Globals
    globals_seen = set()
    for line in lines:
        s = line.strip()
        m = re.match(r"^(@\w+)\s*=", s)
        if m and not m.group(1).startswith("@llvm."):
            globals_seen.add(m.group(1))
    c["global_count"] = len(globals_seen)

    return c


def _classify(inst, c):
    s = inst.strip()
    if not s or s.startswith(";"):
        return
    if re.search(r"\bcall\b", s):
        c["call_count"] += 1
    elif re.search(r"\binvoke\b|\bcallbr\b", s):
        # invoke/callbr are call opcodes without a literal "call" token
        c["call_count"] += 1
    if re.search(r"=\s*load\b", s) or s.startswith("load "):
        c["load_count"] += 1
    if re.search(r"\bstore\b", s):
        c["store_count"] += 1
    if re.match(r"\s*br\b", s):
        c["branch_count"] += 1
    if re.search(r"=\s*phi\b", s):
        c["phi_count"] += 1
    if re.match(r"\s*ret\b", s):
        c["return_count"] += 1


def get_lpta_initial(json_path):
    with open(json_path) as f:
        data = json.load(f)
    for e in data["events"]:
        if e["event_type"] == "before" and e.get("ir_kind") == "Module":
            return e["metrics"]
    return None


def get_lpta_final(json_path):
    with open(json_path) as f:
        data = json.load(f)
    last = None
    for e in data["events"]:
        if e["event_type"] == "after" and e.get("ir_kind") == "Module":
            last = e["metrics_after"]
    return last


def get_summary(json_path):
    with open(json_path) as f:
        return json.load(f)["summary"]


def check_consistency(json_path):
    issues = []
    with open(json_path) as f:
        data = json.load(f)
    events = data["events"]
    s = data["summary"]

    bc = sum(1 for e in events if e["event_type"] == "before")
    ac = sum(1 for e in events if e["event_type"] == "after")
    ic = sum(1 for e in events if e["event_type"] == "invalidated")
    cc = sum(1 for e in events if e["event_type"] == "after" and e.get("has_changes"))
    ic = sum(1 for e in events if e["event_type"] == "after" and (e.get("has_changes") or e.get("ir_changed")))

    if s["total_before"] != bc:
        issues.append(f"summary.total_before={s['total_before']} != {bc}")
    if s["total_after"] != ac:
        issues.append(f"summary.total_after={s['total_after']} != {ac}")
    if s["total_invalidated"] != ic:
        issues.append(f"summary.total_invalidated={s['total_invalidated']} != {ic}")
    if s["passes_with_changes"] != cc:
        issues.append(f"summary.passes_with_changes={s['passes_with_changes']} != {cc}")
    if "passes_with_ir_changes" in s and s["passes_with_ir_changes"] != ic:
        issues.append(f"summary.passes_with_ir_changes={s['passes_with_ir_changes']} != {ic}")
    if s["total_events"] != len(events):
        issues.append(f"summary.total_events={s['total_events']} != {len(events)}")

    # Event IDs are shared by before/after pairs: only before-IDs are sequential
    b_ids = [e["id"] for e in events if e["event_type"] == "before"]
    if b_ids != list(range(1, len(b_ids) + 1)):
        issues.append("Before-event IDs not sequential")

    OP_KEYS = ["op_arith", "op_cmp", "op_memory", "op_control",
               "op_cast", "op_call", "op_vector", "op_other"]
    for e in events:
        mkeys = []
        if "metrics" in e:
            mkeys.append("metrics")
        if e["event_type"] == "after":
            mkeys += ["metrics_before", "metrics_after"]
        for mk in mkeys:
            m = e.get(mk, {})
            if any(k not in m for k in OP_KEYS):
                issues.append(f"Event {e['id']}: {mk} missing opcode-group keys")
            elif sum(m[k] for k in OP_KEYS) != m.get("instruction_count", 0):
                issues.append(f"Event {e['id']}: opcode groups do not partition in {mk}")

    for e in events:
        if e["event_type"] == "after":
            if "metrics_before" not in e or "metrics_after" not in e:
                issues.append(f"Event {e['id']}: missing metrics")
                continue
            has_diff = any(e["metrics_before"][k] != e["metrics_after"][k] for k in e["metrics_before"])
            if e.get("has_changes") != has_diff:
                issues.append(f"Event {e['id']}: has_changes mismatch")

    before_ids = {e["id"] for e in events if e["event_type"] == "before"}
    handled = {e["id"] for e in events if e["event_type"] in ("after", "invalidated")}
    unpaired = before_ids - handled
    if unpaired:
        issues.append(f"Unpaired before events: {unpaired}")

    return issues


def run_opt_stats(ll_path):
    try:
        r = subprocess.run([OPT, "-O2", "-stats", "-disable-output", ll_path],
                           capture_output=True, text=True, timeout=30)
        m = re.search(r"(\d+)\s+instructions", r.stderr)
        if m:
            return int(m.group(1))
    except Exception:
        pass
    return None


def main():
    print("=" * 80)
    print("LPTA EDGE CASE VERIFICATION")
    print("=" * 80)

    total = passed = failed = 0
    issues = []
    tests = sorted([d for d in os.listdir(RESULTS_DIR)
                    if os.path.isdir(os.path.join(RESULTS_DIR, d))])

    for tname in tests:
        ll = os.path.join(IR_DIR, f"{tname}.ll")
        jp = os.path.join(RESULTS_DIR, tname, "history.json")
        if not os.path.exists(ll) or not os.path.exists(jp):
            print(f"\n[SKIP] {tname}")
            continue

        print(f"\n{'='*60}")
        print(f"  {tname}")
        print(f"{'='*60}")

        gt = count_ir_ground_truth(ll)
        lpta = get_lpta_initial(jp)
        lpta_f = get_lpta_final(jp)
        summ = get_summary(jp)

        if lpta is None:
            print("  ERROR: No Module-level before event")
            failed += 1
            issues.append((tname, "No Module-level before event"))
            continue

        print(f"\n  {'Metric':<20} {'GT':>6} {'LPTA':>6} {'Result':>8}")
        print(f"  {'-'*20} {'-'*6} {'-'*6} {'-'*8}")

        for key, label in METRICS:
            g, v = gt[key], lpta[key]
            total += 1
            if g == v:
                result = "  OK"
                passed += 1
            else:
                result = "  FAIL"
                failed += 1
                issues.append((tname, f"{label}: LPTA={v} != GT={g}"))
            print(f"  {label:<20} {g:>6} {v:>6} {result}")

        print(f"\n  Summary: {summ['total_events']} events, "
              f"{summ['passes_with_changes']} changed, "
              f"codegen {summ['codegen_asm_lines_before']}->{summ['codegen_asm_lines_after']} lines")

        # Self-consistency
        total += 1
        si = check_consistency(jp)
        if not si:
            passed += 1
            print(f"  Consistency: OK")
        else:
            failed += 1
            for x in si:
                issues.append((tname, f"Consistency: {x}"))
                print(f"  Consistency: FAIL {x}")

        # opt cross-validation
        opt_i = run_opt_stats(ll)
        if opt_i is not None and lpta_f:
            total += 1
            lf = lpta_f["instruction_count"]
            d = abs(lf - opt_i)
            if d <= 5:
                passed += 1
                print(f"  opt vs LPTA-final: opt={opt_i} LPTA={lf} diff={d} OK")
            else:
                failed += 1
                issues.append((tname, f"opt={opt_i} vs LPTA-final={lf} diff={d}"))
                print(f"  opt vs LPTA-final: opt={opt_i} LPTA={lf} diff={d} FAIL")

        # Invariants
        total += 1
        if lpta["instruction_count"] < lpta["call_count"] + lpta["load_count"] + lpta["store_count"]:
            failed += 1
            issues.append((tname, "Invariant: instr < call+load+store"))
            print(f"  Invariant: FAIL instr < call+load+store")
        elif lpta["basic_block_count"] < lpta["return_count"]:
            failed += 1
            issues.append((tname, "Invariant: bbs < returns"))
            print(f"  Invariant: FAIL bbs < returns")
        else:
            passed += 1
            print(f"  Invariants: OK")

    print(f"\n{'='*80}")
    print(f"RESULTS: {passed}/{total} passed, {failed} failed")
    if issues:
        print(f"\nISSUES ({len(issues)}):")
        for t, msg in issues:
            print(f"  [{t}] {msg}")
    else:
        print("\nALL CHECKS PASSED")
    print()
    return failed


if __name__ == "__main__":
    sys.exit(main())
