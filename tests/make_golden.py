#!/usr/bin/env python3
"""Generate golden compare fixtures under tests/compare_golden/.

Each case dir holds base.json, curr.json. Expected outputs are NOT generated
here: run both engines, review, then pin reviewed output as expected.json
(see tests/compare_golden.sh).
"""
import json
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "compare_golden")

FULL_METRICS = {
    "instruction_count": 0, "basic_block_count": 0, "function_count": 0,
    "global_count": 0, "call_count": 0, "load_count": 0, "store_count": 0,
    "branch_count": 0, "phi_count": 0, "return_count": 0,
    "op_arith": 0, "op_cmp": 0, "op_memory": 0, "op_control": 0,
    "op_cast": 0, "op_call": 0, "op_vector": 0, "op_other": 0,
}


def M(**kw):
    m = dict(FULL_METRICS)
    m.update(kw)
    return m


def ev_before(i, name, kind="Function", iname="f", ptype="transformation"):
    return {"id": i, "event_type": "before", "pass_name": name,
            "pass_type": ptype, "ir_kind": kind, "ir_name": iname,
            "depth": 0, "metrics": M()}


def ev_after(i, name, mb, ma, changes=True, ir_changed=False,
             kind="Function", iname="f", ptype="transformation"):
    return {"id": i, "event_type": "after", "pass_name": name,
            "pass_type": ptype, "ir_kind": kind, "ir_name": iname,
            "depth": 0, "metrics_before": mb, "metrics_after": ma,
            "has_changes": changes, "ir_changed": ir_changed,
            "ir_before": None, "ir_after": None}


def summary(instr_b=100, instr_a=100, chg=0, irchg=0, cg_b=200, cg_a=200,
            targets=None, events=0, wid="t.ll", pipe="O2", meta_hash="aa",
            llvm="22.1.8", lpta="1.0", schema=2):
    return {
        "total_events": events, "total_before": 0, "total_after": 0,
        "total_invalidated": 0, "passes_with_changes": chg,
        "passes_with_ir_changes": irchg, "unique_pass_names": 1,
        "total_instructions_before": instr_b,
        "total_instructions_after": instr_a,
        "total_bbs_before": 10, "total_bbs_after": 10,
        "codegen_asm_lines_before": cg_b, "codegen_asm_lines_after": cg_a,
        "codegen_asm_bytes_before": cg_b * 10,
        "codegen_asm_bytes_after": cg_a * 10,
        "codegen_error_before": None, "codegen_error_after": None,
        "codegen_targets": targets or {},
        "optnone_detected": False, "optnone_function_count": 0,
        "optnone_functions": [], "optnone_warning": "",
    }


def meta(wid="t.ll", pipe="O2", h="aa", llvm="22.1.8", lpta="1.0",
         triple="x", snaps=False, targets=(), schema=2):
    return {
        "schema_version": schema,
        "module_name": wid,
        "pipeline": pipe,
        "run_metadata": {
            "input_ir_hash": h, "module_identifier": wid,
            "llvm_version": llvm, "lpta_version": lpta,
            "pipeline": pipe, "target_triple": triple,
            "snapshot_enabled": snaps, "codegen_targets": list(targets),
        },
    }


def hist(events, summ, **mkw):
    d = meta(**mkw)
    d["events"] = events
    d["summary"] = summ
    return d


def tgt(lb, la, err=None):
    t = {"asm_lines_before": lb, "asm_lines_after": la,
         "asm_bytes_before": lb * 10, "asm_bytes_after": la * 10,
         "error": err}
    return t


def write_case(name, base, curr):
    d = os.path.join(ROOT, name)
    os.makedirs(d, exist_ok=True)
    for fn, obj in (("base.json", base), ("curr.json", curr)):
        with open(os.path.join(d, fn), "w") as f:
            json.dump(obj, f, indent=2)
            f.write("\n")
    print("wrote", name)


def std_events(delta_base=0, delta_curr=0):
    """One FooPass after-event pair with metric deltas around 100 instr."""
    b = [ev_before(1, "FooPass"),
         ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100 + delta_base),
                  changes=(delta_base != 0), ir_changed=(delta_base != 0))]
    c = [ev_before(1, "FooPass"),
         ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100 + delta_curr),
                  changes=(delta_curr != 0), ir_changed=(delta_curr != 0))]
    return b, c


# 1. identical -> 0 / unchanged
b, c = std_events(0, 0)
write_case("01_identical",
           hist(b, summary(chg=0, irchg=0, events=2)),
           hist(c, summary(chg=0, irchg=0, events=2)))

# 2. newly effectful: executes in both, changes only in current
b = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False)]
c = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=90), changes=True, ir_changed=True)]
write_case("02_newly_effectful",
           hist(b, summary(chg=0, irchg=0, events=2)),
           hist(c, summary(chg=1, irchg=1, events=2)))

# 3. truly added + removed (incl. before events on the owning side only)
b = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False),
     ev_before(2, "GonePass"), ev_after(2, "GonePass", M(instruction_count=50), M(instruction_count=40), changes=True, ir_changed=True)]
c = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False),
     ev_before(2, "NewPass"), ev_after(2, "NewPass", M(instruction_count=50), M(instruction_count=70), changes=True, ir_changed=True)]
write_case("03_added_removed",
           hist(b, summary(chg=1, irchg=1, events=4)),
           hist(c, summary(chg=1, irchg=1, events=4)))

# 4. IR-only change counted in ir-changes
b = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False)]
c = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False, ir_changed=True)]
write_case("04_ir_only",
           hist(b, summary(chg=0, irchg=0, events=2)),
           hist(c, summary(chg=0, irchg=1, events=2)))

# 5. target added + removed (no numbers fabricated)
tt = {"x86": tgt(100, 90)}
write_case("05_targets_added_removed",
           hist([], summary(targets=dict(tt, gone=tgt(50, 40)), events=0, chg=0, irchg=0)),
           hist([], summary(targets=dict(tt, **{"new-t": tgt(50, 40)}), events=0, chg=0, irchg=0)))

# 6. target errors on each side
write_case("06_target_errors",
           hist([], summary(targets={"x86": tgt(0, 0, err="boom-base"), "ok": tgt(100, 90)}, events=0, chg=0, irchg=0)),
           hist([], summary(targets={"x86": tgt(0, 0, err="boom-curr"), "ok": tgt(100, 90)}, events=0, chg=0, irchg=0)))

# 7. zero baseline, nonzero current (absolute finding, null pct)
write_case("07_zero_baseline",
           hist([], summary(instr_b=0, instr_a=0, cg_b=0, cg_a=0, events=0, chg=0, irchg=0)),
           hist([], summary(instr_b=0, instr_a=60, cg_b=0, cg_a=0, events=0, chg=0, irchg=0)))

# 8. boundary percentages (round-half-away agreement)
#    base total 200: idd +11 -> 5.5 finding; idd +9 (other pass) -> 4.5 none.
b = [ev_before(1, "UpPass"), ev_after(1, "UpPass", M(instruction_count=100), M(instruction_count=100), changes=False),
     ev_before(2, "EdgePass"), ev_after(2, "EdgePass", M(instruction_count=100), M(instruction_count=100), changes=False)]
c = [ev_before(1, "UpPass"), ev_after(1, "UpPass", M(instruction_count=100), M(instruction_count=111), changes=True, ir_changed=True),
     ev_before(2, "EdgePass"), ev_after(2, "EdgePass", M(instruction_count=100), M(instruction_count=109), changes=True, ir_changed=True)]
write_case("08_pct_boundary",
           hist(b, summary(instr_b=200, instr_a=200, chg=0, irchg=0, events=4)),
           hist(c, summary(instr_b=200, instr_a=200, chg=2, irchg=2, events=4)))

# 9. different inputs -> blocked (rc=2), override proceeds
b, c = std_events(0, 0)
write_case("09_module_mismatch",
           hist(b, summary(chg=0, irchg=0, events=2), h="aa"),
           hist(c, summary(chg=0, irchg=0, events=2), h="bb"))

# 10. pipeline + toolchain drift warns but proceeds
b, c = std_events(0, 0)
write_case("10_pipeline_drift",
           hist(b, summary(chg=0, irchg=0, events=2), pipe="O2", llvm="22.1.8"),
           hist(c, summary(chg=0, irchg=0, events=2), pipe="O3", llvm="21.0.0"))

# 11. nested adaptor + child both changed (documented: both counted)
b = [ev_before(1, "AdaptPass", kind="Module", iname="t", ptype="adaptor"),
     ev_after(1, "AdaptPass", M(instruction_count=100), M(instruction_count=100), changes=False, kind="Module", iname="t", ptype="adaptor")]
c = [ev_before(1, "AdaptPass", kind="Module", iname="t", ptype="adaptor"),
     ev_after(1, "AdaptPass", M(instruction_count=100), M(instruction_count=80), changes=True, ir_changed=True, kind="Module", iname="t", ptype="adaptor"),
     ev_before(2, "ChildPass"),
     ev_after(2, "ChildPass", M(instruction_count=100), M(instruction_count=80), changes=True, ir_changed=True)]
write_case("11_nested_adaptor",
           hist(b, summary(chg=0, irchg=0, events=2)),
           hist(c, summary(chg=2, irchg=2, events=4)))

# 12. improvements only -> 0 / improved
b = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=100), changes=False)]
c = [ev_before(1, "FooPass"), ev_after(1, "FooPass", M(instruction_count=100), M(instruction_count=80), changes=True, ir_changed=True)]
write_case("12_improved_only",
           hist(b, summary(instr_b=100, instr_a=100, chg=0, irchg=0, events=2)),
           hist(c, summary(instr_b=100, instr_a=80, chg=1, irchg=1, events=2)))
