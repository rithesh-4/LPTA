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
    label_rest_re = re.compile(r"^\s*[a-zA-Z0-9_.]+:\s*(.+)$")
    op_re = re.compile(r"^\s*(?:%[^\s=]+\s*=\s*)?([A-Za-z][\w.]*)\b")
    # Every opcode the LLVM text printer can emit at instruction start.
    # A wrapped instruction's continuation lines (switch case lists, phi
    # incoming lists, landingpad clauses) never start with one of these,
    # so this set is what separates real instructions from continuations.
    known_ops = frozenset(
        "ret br switch indirectbr invoke callbr resume unreachable "
        "cleanupret catchret catchswitch "
        "add fadd sub fsub mul fmul udiv sdiv fdiv urem srem frem "
        "shl lshr ashr and or xor fneg "
        "alloca load store getelementptr fence atomicrmw cmpxchg "
        "trunc zext sext fptoui fptosi uitofp sitofp fptrunc fpext "
        "ptrtoint inttoptr bitcast addrspacecast "
        "icmp fcmp phi call select va_arg "
        "extractelement insertelement shufflevector extractvalue insertvalue "
        "landingpad freeze cleanuppad catchpad tail musttail".split())
    assign_re = re.compile(r"^\s*%[^\s=]+\s*=\s*")
    opstart_re = re.compile(r"^\s*([A-Za-z][\w.]*)\b")

    def logical_lines(lines):
        """Join printer-wrapped instructions into logical lines.

        LLVM wraps long instructions (switch case lists, phi incoming
        lists, landingpad clauses) across physical lines. Counting physical
        lines overcounts vs the API counter, so continuation lines are
        folded into the instruction they belong to.
        """
        out = []
        cur = ""
        for line in lines:
            s = line.strip()
            if not s or s.startswith(";") or s in ("{", "}"):
                continue
            if label_re.match(line):
                if cur:
                    out.append(cur)
                    cur = ""
                out.append(line)
                continue
            code = line.split(";", 1)[0]
            if not code.strip():
                continue
            if assign_re.match(code):
                starts_new = True
            else:
                om = opstart_re.match(code)
                starts_new = bool(om and om.group(1) in known_ops)
            if starts_new:
                if cur:
                    out.append(cur)
                cur = code.strip()
            else:
                cur += (" " + code.strip()) if cur else code.strip()
        if cur:
            out.append(cur)
        return out

    def flush():
        nonlocal bbs, instrs, calls, loads, stores, branches, phis, rets, body
        if not body:
            return
        bbs += len([line for line in body if label_re.match(line)])
        if not label_re.match(body[0]):
            bbs += 1  # anonymous entry block has no label
        for line in logical_lines(body):
            if label_re.match(line):
                # Label sharing its line with an instruction (rare) still
                # carries one instruction after the colon.
                m2 = label_rest_re.match(line.split(";", 1)[0])
                if not m2:
                    continue
                code = m2.group(1)
            else:
                code = line.split(";", 1)[0]
            if not code.strip():
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
