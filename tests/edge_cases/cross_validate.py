#!/usr/bin/env python3
"""Cross-validate LPTA output against opt -O2 for all edge case test files."""
import json, subprocess, re, os, sys

TMP = r"C:\Users\ramri\AppData\Local\Temp\lpta_edge"
OPT = r"C:\LLVM-full\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin\opt.exe"

test_cases = [
    '01_dead_code', '02_loops', '03_inline', '04_bitops',
    '05_switch', '06_memory', '07_phi_nodes', '08_globals'
]

def count_instructions_in_ll(ll_path):
    """Count LLVM IR instructions in a .ll file by parsing text."""
    count = 0
    in_function = False
    with open(ll_path, errors='replace') as f:
        for line in f:
            s = line.strip()
            if s.startswith('define '):
                in_function = True
                continue
            if s.startswith('}'):
                in_function = False
                continue
            if not in_function:
                continue
            # Skip comments, metadata refs, attribute groups, labels
            if s.startswith(';') or s.startswith('!') or s.startswith('attributes'):
                continue
            if s == '' or s.startswith('{'):
                continue
            # Label-only lines like "if.end:"
            if re.match(r'^[a-zA-Z_]\w*:', s) and '=' not in s:
                continue
            # Skip source-level metadata debug lines
            if s.startswith('!') or s.startswith('dbg'):
                continue
            # An instruction is indented and either has an assignment or is a terminator
            if line[0] in (' ', '\t'):
                if re.match(r'^[\w%"].*\s*=', s):
                    count += 1
                elif re.match(r'^(ret |br |switch |unreachable|resume |invoke |call |load |store |getelementptr |alloca |atomicrmw |cmpxchg |fence )', s):
                    count += 1
                elif re.match(r'^(fneg|fadd|fsub|fmul|fdiv|frem|add|sub|mul|sdiv|udiv|srem|urem|and|or|xor|shl|lshr|ashr|icmp|fcmp|zext|sext|trunc|bitcast|inttoptr|ptrtoint|select|freeze|extractelement|insertelement|extractvalue|insertvalue|phi|landingpad|cleanupret|catchret|catchswitch|addrspacecast) ', s):
                    count += 1
    return count

def run_opt(input_ll, output_ll):
    """Run opt -O2 on input_ll, produce output_ll."""
    result = subprocess.run(
        [OPT, '-O2', input_ll, '-S', '-o', output_ll],
        capture_output=True, text=True, timeout=60
    )
    return result.returncode == 0

print("=" * 90)
print("LPTA vs opt -O2 Cross-Validation (post-fix: optnone stripped)")
print("=" * 90)
print()
header = f"{'Test Case':<15} | {'LPTA Before':>12} | {'LPTA After':>12} | {'LPTA Delta':>11} | {'opt -O2':>8} | {'Match?':>6}"
print(header)
print("-" * len(header))

total_pass = 0
total_fail = 0
issues = []

for tc in test_cases:
    json_path = os.path.join(TMP, tc, 'history.json')
    ir_path = f'tests/edge_cases/ir/{tc}.ll'
    opt_ll = os.path.join(TMP, f'{tc}_opt_cross.ll')

    if not os.path.exists(json_path):
        print(f"{tc:<15} | {'N/A':>12} | {'N/A':>12} | {'N/A':>11} | {'N/A':>8} | {'SKIP':>6}")
        continue

    with open(json_path) as f:
        data = json.load(f)

    summary = data.get('summary', {})
    lpta_before = summary.get('total_instructions_before', 0)
    lpta_after = summary.get('total_instructions_after', 0)
    lpta_delta = lpta_after - lpta_before if isinstance(lpta_after, int) and isinstance(lpta_before, int) else 0

    # Run opt -O2
    opt_ok = run_opt(ir_path, opt_ll)
    if opt_ok and os.path.exists(opt_ll):
        opt_instrs = count_instructions_in_ll(opt_ll)
    else:
        opt_instrs = -1

    match = 'YES' if lpta_after == opt_instrs else 'NO'
    if match == 'YES':
        total_pass += 1
    else:
        total_fail += 1
        issues.append((tc, lpta_before, lpta_after, lpta_delta, opt_instrs))

    print(f"{tc:<15} | {lpta_before:>12} | {lpta_after:>12} | {lpta_delta:>+11} | {opt_instrs:>8} | {match:>6}")

print("-" * len(header))
print()
print(f"Results: {total_pass} MATCH, {total_fail} MISMATCH out of {total_pass + total_fail}")

if issues:
    print()
    print("=== ISSUES FOUND ===")
    for tc, before, after, delta, opt_i in issues:
        diff = after - opt_i
        print(f"  {tc}: LPTA after={after}, opt={opt_i}, diff={diff:+d}")
        if diff < 0:
            print(f"    -> LPTA reports FEWER instructions than opt (LPTA optimized more)")
        elif diff > 0:
            print(f"    -> LPTA reports MORE instructions than opt (LPTA optimized less)")
else:
    print()
    print("ALL MATCH - LPTA numbers are correct!")

# Also check determinism
print()
print("=== Determinism Check ===")
for tc in test_cases:
    json_path1 = os.path.join(TMP, tc, 'history.json')
    if not os.path.exists(json_path1):
        continue
    # Run LPTA again
    lpta_bin = './build/lpta_test.exe'
    out_dir = os.path.join(TMP, f'{tc}_det')
    subprocess.run(
        [lpta_bin, f'tests/edge_cases/ir/{tc}.ll', out_dir, '-O2'],
        capture_output=True, timeout=120
    )
    json_path2 = os.path.join(out_dir, 'history.json')
    if os.path.exists(json_path2):
        with open(json_path1) as f1, open(json_path2) as f2:
            d1 = json.load(f1)
            d2 = json.load(f2)
        s1 = d1.get('summary', {})
        s2 = d2.get('summary', {})
        eq = s1 == s2
        if eq:
            print(f"  {tc}: DETERMINISTIC (identical summary)")
        else:
            # Show what differs
            diffs = []
            for k in s1:
                if s1.get(k) != s2.get(k):
                    diffs.append(f"    {k}: {s1.get(k)} vs {s2.get(k)}")
            print(f"  {tc}: NOT DETERMINISTIC")
            for d in diffs:
                print(d)
    else:
        print(f"  {tc}: SKIP (no output)")
