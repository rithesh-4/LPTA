#!/usr/bin/env python3
"""Independent LLVM IR element counter for LPTA validation.

Counts functions, basic blocks, instructions and per-opcode-group elements
without using LPTA's C++ counters, so the two implementations cross-check.

Usage: python3 count_ir.py <file.ll>
Prints KEY=value lines (GT_FUNCTIONS, GT_BBS, GT_INSTRUCTIONS, GT_CALLS,
GT_LOADS, GT_STORES, GT_BRANCHES, GT_PHIS, GT_RETURNS, GT_GLOBALS).
"""
import re
import sys


def count_file(path):
    with open(path) as f:
        lines = f.readlines()

    funcs = bbs = instrs = calls = loads = stores = 0
    branches = phis = rets = globals_ct = 0
    in_func = False
    body = []
    label_re = re.compile(r"^\s*[a-zA-Z0-9_.]+:")
    op_re = re.compile(r"^\s*(?:%[^\s=]+\s*=\s*)?([A-Za-z][\w.]*)\b")

    def flush():
        nonlocal bbs, instrs, calls, loads, stores, branches, phis, rets, body
        if not body:
            return
        bbs += len([line for line in body if label_re.match(line)])
        if not label_re.match(body[0]):
            bbs += 1  # anonymous entry block has no label
        for line in body:
            code = line.split(";", 1)[0]
            if not code.strip() or label_re.match(line):
                continue
            m = op_re.match(code)
            if not m:
                continue
            instrs += 1
            op = m.group(1)
            if op in ("call", "invoke", "callbr", "tail", "musttail"):
                calls += 1
            elif op == "load":
                loads += 1
            elif op == "store":
                stores += 1
            elif op in ("br", "switch", "indirectbr"):
                branches += 1
            elif op == "phi":
                phis += 1
            elif op == "ret":
                rets += 1
        body = []

    for line in lines:
        sline = line.strip()
        if line.startswith("define "):
            in_func = True
            funcs += 1
            body = []
        elif line.startswith("}"):
            flush()
            in_func = False
        elif line.startswith("@") and "=" in line:
            globals_ct += 1
        elif in_func:
            if sline == "" or sline.startswith(";"):
                continue
            body.append(line)

    return {
        "GT_FUNCTIONS": funcs,
        "GT_BBS": bbs,
        "GT_INSTRUCTIONS": instrs,
        "GT_CALLS": calls,
        "GT_LOADS": loads,
        "GT_STORES": stores,
        "GT_BRANCHES": branches,
        "GT_PHIS": phis,
        "GT_RETURNS": rets,
        "GT_GLOBALS": globals_ct,
    }


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.ll>", file=sys.stderr)
        return 1
    for key, value in count_file(sys.argv[1]).items():
        print(f"{key}={value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
