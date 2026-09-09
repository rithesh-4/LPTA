#!/usr/bin/env python3
"""
LPTA Dashboard Server - static file serving + optional AI proxy.

Replaces `python -m http.server` for viewing LPTA reports. Everything works
without an API key; setting NVIDIA_API_KEY additionally enables the
dashboard's AI Insights panel (chat presets + per-pass explanations).

The API key lives ONLY in this process's environment. It is never sent to
the browser and never appears in the repo.

Usage:
    python serve_dashboard.py [report_dir] [-p PORT] [--host HOST]

Configuration (checked in order):
  1. Config file: .lpta_config.json (current dir or home dir)
  2. Environment variables (override config file)
  3. Defaults

Config file example (.lpta_config.json):
{
  "NVIDIA_API_KEY": "nvapi-...",
  "LPTA_AI_MODEL": "nvidia/nemotron-3-ultra-550b-a55b",
  "LPTA_AI_BASE_URL": "https://integrate.api.nvidia.com/v1"
}

Environment (override config file):
    NVIDIA_API_KEY     nvapi-... key from https://build.nvidia.com
    LPTA_AI_MODEL      model id (default: nvidia/nemotron-3-ultra-550b-a55b)
    LPTA_AI_BASE_URL   OpenAI-compatible endpoint
                       (default: https://integrate.api.nvidia.com/v1)
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

DEFAULT_MODEL = "nvidia/nemotron-3-ultra-550b-a55b"

def _is_valid_nvidia_key(key):
    """Check if key looks like a real NVIDIA API key (starts with nvapi-)."""
    return bool(key) and key.startswith("nvapi-") and len(key) > 10
DEFAULT_BASE_URL = "https://integrate.api.nvidia.com/v1"
CONFIG_FILENAME = ".lpta_config.json"

# Largest accepted POST body (histories with IR snapshots reach low tens of
# MB; anything bigger is abuse or a client bug — refuse before buffering).
MAX_BODY_BYTES = 64 * 1024 * 1024


def load_config():
    """Load config from .lpta_config.json (current dir or home dir)."""
    config = {}
    for path in [Path.cwd() / CONFIG_FILENAME, Path.home() / CONFIG_FILENAME]:
        if path.is_file():
            try:
                with open(path, "r") as f:
                    config = json.load(f)
                    break
            except json.JSONDecodeError as e:
                print(f"  WARNING: ignoring malformed config {path}: {e}",
                      file=sys.stderr)
            except OSError as e:
                print(f"  WARNING: cannot read config {path}: {e}",
                      file=sys.stderr)
    return config


SCHEMA_VERSION = 2


def check_compat(base, curr, allow_different_input=False):
    """Decide whether two histories may be compared.

    Returns {"blocked": str|None, "warnings": [str]}. Blocking happens only
    when both sides carry run metadata that proves different inputs (unless
    explicitly allowed) or an unreadably new schema version. Everything else
    is a warning: version drift, pipeline drift, differing target sets.
    Histories without metadata (schema 1) proceed with a warning.
    """
    warnings = []
    bm = base.get("run_metadata", {}) if isinstance(base, dict) else {}
    cm = curr.get("run_metadata", {}) if isinstance(curr, dict) else {}
    bv = base.get("schema_version", 1) if isinstance(base, dict) else 1
    cv = curr.get("schema_version", 1) if isinstance(curr, dict) else 1
    try:
        bv = int(bv)
    except (TypeError, ValueError):
        bv = 1
    try:
        cv = int(cv)
    except (TypeError, ValueError):
        cv = 1
    if bv > SCHEMA_VERSION or cv > SCHEMA_VERSION:
        return {"blocked": f"unsupported schema_version (base={bv}, current={cv}, max={SCHEMA_VERSION})",
                "warnings": warnings}
    if bv < SCHEMA_VERSION or cv < SCHEMA_VERSION or not bm or not cm:
        warnings.append("at least one report predates run metadata; comparability checks are limited")
    else:
        bh, ch = bm.get("input_ir_hash"), cm.get("input_ir_hash")
        if bh and ch and bh != ch and not allow_different_input:
            return {"blocked": "reports were generated from different input IR "
                               "(input_ir_hash differs); pass --allow-different-input to compare anyway",
                    "warnings": warnings}
        if bh and ch and bh != ch:
            warnings.append("reports were generated from different input IR (override accepted)")
        for key in ("llvm_version", "lpta_version"):
            if bm.get(key) and cm.get(key) and bm.get(key) != cm.get(key):
                warnings.append(f"toolchain drift: {key} {bm.get(key)!r} vs {cm.get(key)!r}")
        if bm.get("pipeline") and cm.get("pipeline") and bm.get("pipeline") != cm.get("pipeline"):
            warnings.append(f"pipeline differs: {bm.get('pipeline')!r} vs {cm.get('pipeline')!r} (experiment, not necessarily regression)")
        # Target sets are compared from the measured summary maps (not the
        # requested metadata lists, which may use un-normalized aliases).
        bt = set((base.get("summary", {}) or {}).get("codegen_targets", {}) or {})
        ct = set((curr.get("summary", {}) or {}).get("codegen_targets", {}) or {})
        if bt != ct:
            warnings.append(f"codegen target sets differ: {sorted(bt)} vs {sorted(ct)}")
    return {"blocked": None, "warnings": warnings}


def compare_histories(base, curr, allow_different_input=False):
    """Compare two history.json objects and return a detailed diff.

    Score contract (mirrored in the C++ CLI): identical valid reports score
    exactly 0 with verdict "unchanged"; improvements alone score 0 with
    verdict "improved"; any regression yields a positive proportional score.
    Incomparable inputs return verdict "incomparable" with a null score.
    """
    compat = check_compat(base, curr, allow_different_input)
    if compat["blocked"]:
        return {
            "summary": {}, "passes": [], "targets": [],
            "regressions": [], "improvements": [],
            "new_passes": [], "removed_passes": [],
            "regression_score": None, "verdict": "incomparable",
            "score_components": {"instructions": None, "codegen": None,
                                 "pass_effects": None, "measurement_quality": None},
            "coverage": {"instructions": False, "codegen": False,
                         "pass_effects": False, "measurement_quality": False},
            "compat": {"blocked": compat["blocked"], "warnings": compat["warnings"]},
            "base_meta": {"module": base.get("module_name"), "pipeline": base.get("pipeline")},
            "curr_meta": {"module": curr.get("module_name"), "pipeline": curr.get("pipeline")},
        }
    # Summary comparison
    sb = base.get("summary", {})
    sc = curr.get("summary", {})

    def diff(a, b):
        return b - a

    def round1(x):
        """Round to 1 decimal, half away from zero (matches the C++ engine)."""
        import math
        return math.floor(x * 10 + 0.5) / 10 if x >= 0 else math.ceil(x * 10 - 0.5) / 10

    def pct(old, new):
        """Percentage change. Returns (available, value); a zero baseline
        with nonzero current is unavailable (absolute delta only)."""
        if old > 0:
            return True, round1(100.0 * (new - old) / old)
        if old == 0 and new == 0:
            return True, 0.0
        return False, 0.0

    def pct_delta(old, new):
        """Legacy wrapper: unavailable percentages report 0.0."""
        return pct(old, new)[1]

    summary_diff = {
        "total_events": diff(sb.get("total_events", 0), sc.get("total_events", 0)),
        "total_before": diff(sb.get("total_before", 0), sc.get("total_before", 0)),
        "total_after": diff(sb.get("total_after", 0), sc.get("total_after", 0)),
        "total_invalidated": diff(sb.get("total_invalidated", 0), sc.get("total_invalidated", 0)),
        "passes_with_changes": diff(sb.get("passes_with_changes", 0), sc.get("passes_with_changes", 0)),
        "passes_with_ir_changes": diff(sb.get("passes_with_ir_changes", sb.get("passes_with_changes", 0)), sc.get("passes_with_ir_changes", sc.get("passes_with_changes", 0))),
        "unique_pass_names": diff(sb.get("unique_pass_names", 0), sc.get("unique_pass_names", 0)),
        "total_instructions_before": diff(sb.get("total_instructions_before", 0), sc.get("total_instructions_before", 0)),
        "total_instructions_after": diff(sb.get("total_instructions_after", 0), sc.get("total_instructions_after", 0)),
        "total_bbs_before": diff(sb.get("total_bbs_before", 0), sc.get("total_bbs_before", 0)),
        "total_bbs_after": diff(sb.get("total_bbs_after", 0), sc.get("total_bbs_after", 0)),
        "codegen_asm_lines_before": diff(sb.get("codegen_asm_lines_before", 0), sc.get("codegen_asm_lines_before", 0)),
        "codegen_asm_lines_after": diff(sb.get("codegen_asm_lines_after", 0), sc.get("codegen_asm_lines_after", 0)),
        "codegen_asm_bytes_before": diff(sb.get("codegen_asm_bytes_before", 0), sc.get("codegen_asm_bytes_before", 0)),
        "codegen_asm_bytes_after": diff(sb.get("codegen_asm_bytes_after", 0), sc.get("codegen_asm_bytes_after", 0)),
        "passes_with_ir_changes": diff(sb.get("passes_with_ir_changes", 0), sc.get("passes_with_ir_changes", 0)),
    }

    # Instruction/codegen reduction deltas and efficiency
    base_instr_red = sb.get("total_instructions_before", 0) - sb.get("total_instructions_after", 0)
    curr_instr_red = sc.get("total_instructions_before", 0) - sc.get("total_instructions_after", 0)
    summary_diff["instruction_reduction_delta"] = curr_instr_red - base_instr_red
    summary_diff["instruction_reduction_pct"] = pct_delta(
        sb.get("total_instructions_before", 0), sb.get("total_instructions_after", 0))
    summary_diff["curr_instruction_reduction_pct"] = pct_delta(
        sc.get("total_instructions_before", 0), sc.get("total_instructions_after", 0))

    base_cg_red = sb.get("codegen_asm_lines_before", 0) - sb.get("codegen_asm_lines_after", 0)
    curr_cg_red = sc.get("codegen_asm_lines_before", 0) - sc.get("codegen_asm_lines_after", 0)
    summary_diff["codegen_reduction_delta"] = curr_cg_red - base_cg_red
    summary_diff["codegen_reduction_pct"] = pct_delta(
        sb.get("codegen_asm_lines_before", 0), sb.get("codegen_asm_lines_after", 0))
    summary_diff["curr_codegen_reduction_pct"] = pct_delta(
        sc.get("codegen_asm_lines_before", 0), sc.get("codegen_asm_lines_after", 0))

    # Pipeline change detection
    base_pipeline = base.get("pipeline", "?")
    curr_pipeline = curr.get("pipeline", "?")
    summary_diff["pipeline_changed"] = base_pipeline != curr_pipeline
    summary_diff["base_pipeline"] = base_pipeline
    summary_diff["curr_pipeline"] = curr_pipeline

    # --- Pass-level comparison ---
    # Track all metrics per pass: instructions, bbs, loads, stores, branches, phis
    base_passes = {}
    curr_passes = {}

    def _accumulate_pass(passes_dict, e):
        pn = e.get("pass_name", "")
        if pn not in passes_dict:
            passes_dict[pn] = {
                "count": 0, "instr_delta": 0, "bb_delta": 0,
                "load_delta": 0, "store_delta": 0, "branch_delta": 0, "phi_delta": 0,
            }
        d = passes_dict[pn]
        d["count"] += 1
        mb = e.get("metrics_before", {})
        ma = e.get("metrics_after", {})
        d["instr_delta"] += ma.get("instruction_count", 0) - mb.get("instruction_count", 0)
        d["bb_delta"] += ma.get("basic_block_count", 0) - mb.get("basic_block_count", 0)
        d["load_delta"] += ma.get("load_count", 0) - mb.get("load_count", 0)
        d["store_delta"] += ma.get("store_count", 0) - mb.get("store_count", 0)
        d["branch_delta"] += ma.get("branch_count", 0) - mb.get("branch_count", 0)
        d["phi_delta"] += ma.get("phi_count", 0) - mb.get("phi_count", 0)

    for e in base.get("events", []):
        if e.get("event_type") == "after" and (e.get("has_changes") or e.get("ir_changed")):
            _accumulate_pass(base_passes, e)
    for e in curr.get("events", []):
        if e.get("event_type") == "after" and (e.get("has_changes") or e.get("ir_changed")):
            _accumulate_pass(curr_passes, e)

    def _exec_ids(events):
        """Executed event IDs per pass. Before/after/invalidated share one ID
        per execution, so distinct IDs count executions exactly — including
        minimal files that carry after-events without befores."""
        m = {}
        for e in events:
            if e.get("event_type") in ("before", "after", "invalidated"):
                pn = e.get("pass_name", "")
                m.setdefault(pn, set()).add(e.get("id"))
        return m

    base_exec = {k: len(v) for k, v in _exec_ids(base.get("events", [])).items()}
    curr_exec = {k: len(v) for k, v in _exec_ids(curr.get("events", [])).items()}

    all_pass_names = sorted(set(base_exec) | set(curr_exec) |
                            set(base_passes) | set(curr_passes))
    pass_comparison = []
    for pn in all_pass_names:
        bp = base_passes.get(pn)
        cp = curr_passes.get(pn)
        base_effect = bp is not None
        curr_effect = cp is not None

        # Presence is about execution, never about effect.
        if pn not in base_exec:
            presence = "added"
        elif pn not in curr_exec:
            presence = "removed"
        else:
            presence = "present_in_both"
        if base_effect and not curr_effect:
            effect = "no_longer_effectful"
        elif curr_effect and not base_effect:
            effect = "newly_effectful"
        else:
            effect = "unchanged_effect"

        if bp is None:
            bp = {"count": 0, "instr_delta": 0, "bb_delta": 0,
                  "load_delta": 0, "store_delta": 0, "branch_delta": 0, "phi_delta": 0}
        if cp is None:
            cp = {"count": 0, "instr_delta": 0, "bb_delta": 0,
                  "load_delta": 0, "store_delta": 0, "branch_delta": 0, "phi_delta": 0}

        pass_comparison.append({
            "pass_name": pn,
            "status": presence,
            "effect_status": effect,
            "base_exec_count": base_exec.get(pn, 0),
            "curr_exec_count": curr_exec.get(pn, 0),
            "base_count": bp["count"],
            "curr_count": cp["count"],
            "count_delta": cp["count"] - bp["count"],
            "base_instr_delta": bp["instr_delta"],
            "curr_instr_delta": cp["instr_delta"],
            "instr_delta_delta": cp["instr_delta"] - bp["instr_delta"],
            "base_bb_delta": bp["bb_delta"],
            "curr_bb_delta": cp["bb_delta"],
            "bb_delta_delta": cp["bb_delta"] - bp["bb_delta"],
            "base_load_delta": bp["load_delta"],
            "curr_load_delta": cp["load_delta"],
            "load_delta_delta": cp["load_delta"] - bp["load_delta"],
            "base_store_delta": bp["store_delta"],
            "curr_store_delta": cp["store_delta"],
            "store_delta_delta": cp["store_delta"] - bp["store_delta"],
            "base_branch_delta": bp["branch_delta"],
            "curr_branch_delta": cp["branch_delta"],
            "branch_delta_delta": cp["branch_delta"] - bp["branch_delta"],
            "base_phi_delta": bp["phi_delta"],
            "curr_phi_delta": cp["phi_delta"],
            "phi_delta_delta": cp["phi_delta"] - bp["phi_delta"],
        })

    # Summarize new/removed passes for the response
    new_passes = [p["pass_name"] for p in pass_comparison if p["status"] == "added"]
    removed_passes = [p["pass_name"] for p in pass_comparison if p["status"] == "removed"]

    # --- Cross-target codegen comparison ---
    # Every target gets an explicit state. Numbers and deltas exist ONLY for
    # "comparable"; missing or failed sides never become zero-valued data.
    base_targets = sb.get("codegen_targets", {})
    curr_targets = sc.get("codegen_targets", {})
    all_targets = set(base_targets.keys()) | set(curr_targets.keys())
    target_comparison = []
    for t in sorted(all_targets):
        bt = base_targets.get(t)
        ct = curr_targets.get(t)
        if bt is None:
            entry = {"target": t, "state": "new_target"}
            if isinstance(ct, dict) and ct.get("error"):
                entry["curr_error"] = ct.get("error")
            target_comparison.append(entry)
            continue
        if ct is None:
            entry = {"target": t, "state": "removed_target"}
            if isinstance(bt, dict) and bt.get("error"):
                entry["base_error"] = bt.get("error")
            target_comparison.append(entry)
            continue
        if not isinstance(bt, dict) or not isinstance(ct, dict):
            target_comparison.append({"target": t, "state": "both_error",
                                      "base_error": "malformed entry",
                                      "curr_error": "malformed entry"})
            continue
        if bt.get("error") or ct.get("error"):
            st = "both_error" if (bt.get("error") and ct.get("error")) \
                else ("baseline_error" if bt.get("error") else "current_error")
            entry = {"target": t, "state": st}
            # Error keys appear only when set (mirrors the C++ engine).
            if bt.get("error"):
                entry["base_error"] = bt.get("error")
            if ct.get("error"):
                entry["curr_error"] = ct.get("error")
            target_comparison.append(entry)
            continue
        base_before = bt.get("asm_lines_before", 0)
        base_after = bt.get("asm_lines_after", 0)
        curr_before = ct.get("asm_lines_before", 0)
        curr_after = ct.get("asm_lines_after", 0)
        target_comparison.append({
            "target": t, "state": "comparable",
            "base_before": base_before, "base_after": base_after,
            "base_red": base_before - base_after,
            "curr_before": curr_before, "curr_after": curr_after,
            "curr_red": curr_before - curr_after,
            "red_delta": (curr_before - curr_after) - (base_before - base_after),
        })

    # --- Regression + improvement detection ---
    regressions = []
    improvements = []
    bsum = base.get("summary", {})
    csum = curr.get("summary", {})
    base_instr_after = bsum.get("total_instructions_after", 0)
    curr_instr_after = csum.get("total_instructions_after", 0)

    def finding(ftype, sev, msg, metric=None, delta=None, pct=None,
                pass_name=None, target=None, extra=None):
        """Canonical finding object. Every key is always present (None when
        not applicable) so the C++ engine can emit byte-equivalent results."""
        item = {"type": ftype, "severity": sev, "message": msg,
                "pass": pass_name, "target": target, "metric": metric,
                "delta": delta, "pct": pct}
        if extra:
            item.update(extra)
        return item

    def pct1(x):
        return f"{x:.1f}"

    # Instruction count regression/improvement
    instr_avail, instr_pct = pct(base_instr_after, curr_instr_after)
    if instr_avail:
        if instr_pct > 5:
            regressions.append(finding(
                "instruction_increase", "high",
                f"Instruction count increased by {pct1(instr_pct)}% vs baseline "
                f"({base_instr_after} -> {curr_instr_after})",
                metric="instructions", delta=curr_instr_after - base_instr_after,
                pct=instr_pct))
        elif instr_pct < -5:
            improvements.append(finding(
                "instruction_reduction", "positive",
                f"Instruction count reduced by {pct1(-instr_pct)}% vs baseline "
                f"({base_instr_after} -> {curr_instr_after})",
                metric="instructions", delta=curr_instr_after - base_instr_after,
                pct=instr_pct))
    elif curr_instr_after > 50:
        regressions.append(finding(
            "instruction_increase", "high",
            f"Instruction count increased by {curr_instr_after} vs baseline "
            f"(0 -> {curr_instr_after})",
            metric="instructions", delta=curr_instr_after, pct=None))

    # Codegen regression/improvement
    base_cg_after = bsum.get("codegen_asm_lines_after", 0)
    curr_cg_after = csum.get("codegen_asm_lines_after", 0)
    cg_avail, cg_pct = pct(base_cg_after, curr_cg_after)
    if cg_avail:
        if cg_pct > 5:
            regressions.append(finding(
                "codegen_regression", "high",
                f"Codegen assembly lines increased by {pct1(cg_pct)}% vs baseline "
                f"({base_cg_after} -> {curr_cg_after})",
                metric="codegen", delta=curr_cg_after - base_cg_after,
                pct=cg_pct))
        elif cg_pct < -5:
            improvements.append(finding(
                "codegen_improvement", "positive",
                f"Codegen assembly lines reduced by {pct1(-cg_pct)}% vs baseline "
                f"({base_cg_after} -> {curr_cg_after})",
                metric="codegen", delta=curr_cg_after - base_cg_after,
                pct=cg_pct))
    elif curr_cg_after > 50:
        regressions.append(finding(
            "codegen_regression", "high",
            f"Codegen assembly lines increased by {curr_cg_after} vs baseline "
            f"(0 -> {curr_cg_after})",
            metric="codegen", delta=curr_cg_after, pct=None))

    # Pass-level regressions and improvements
    for pc in pass_comparison:
        if pc["status"] in ("added", "removed"):
            continue  # handled separately; deltas against zero are meaningless
        base_instr = bsum.get("total_instructions_after", 0)
        if base_instr > 0:
            instr_pct = round1(pc["instr_delta_delta"] * 100.0 / base_instr)
            if instr_pct > 5:
                regressions.append(finding(
                    "pass_regression", "medium",
                    f"{pc['pass_name']} instruction delta worsened by {pct1(instr_pct)}% "
                    f"({pc['base_instr_delta']:+d} -> {pc['curr_instr_delta']:+d})",
                    metric="instructions", delta=pc["instr_delta_delta"],
                    pct=instr_pct, pass_name=pc["pass_name"]))
            elif instr_pct < -5:
                improvements.append(finding(
                    "pass_improvement", "positive",
                    f"{pc['pass_name']} instruction delta improved by {pct1(-instr_pct)}% "
                    f"({pc['base_instr_delta']:+d} -> {pc['curr_instr_delta']:+d})",
                    metric="instructions", delta=pc["instr_delta_delta"],
                    pct=instr_pct, pass_name=pc["pass_name"]))

        # Load/store regression detection (severities mirror the C++ engine:
        # regressions are medium, improvements are positive)
        if abs(pc["load_delta_delta"]) > 5:
            if pc["load_delta_delta"] > 0:
                regressions.append(finding(
                    "pass_regression", "medium",
                    f"{pc['pass_name']} load count delta increased by {pc['load_delta_delta']:+d}",
                    metric="loads", delta=pc["load_delta_delta"], pct=None,
                    pass_name=pc["pass_name"]))
            else:
                improvements.append(finding(
                    "pass_improvement", "positive",
                    f"{pc['pass_name']} load count delta decreased by {pc['load_delta_delta']:+d}",
                    metric="loads", delta=pc["load_delta_delta"], pct=None,
                    pass_name=pc["pass_name"]))

        if abs(pc["store_delta_delta"]) > 5:
            if pc["store_delta_delta"] > 0:
                regressions.append(finding(
                    "pass_regression", "medium",
                    f"{pc['pass_name']} store count delta increased by {pc['store_delta_delta']:+d}",
                    metric="stores", delta=pc["store_delta_delta"], pct=None,
                    pass_name=pc["pass_name"]))
            else:
                improvements.append(finding(
                    "pass_improvement", "positive",
                    f"{pc['pass_name']} store count delta decreased by {pc['store_delta_delta']:+d}",
                    metric="stores", delta=pc["store_delta_delta"], pct=None,
                    pass_name=pc["pass_name"]))

    # New/removed pass regressions
    if new_passes:
        regressions.append(finding(
            "new_passes", "info",
            f"{len(new_passes)} new pass(es) appeared in current run: "
            f"{', '.join(new_passes[:5])}{', ...' if len(new_passes) > 5 else ''}",
            extra={"passes": new_passes}))
    if removed_passes:
        improvements.append(finding(
            "removed_passes", "info",
            f"{len(removed_passes)} pass(es) removed from current run: "
            f"{', '.join(removed_passes[:5])}{', ...' if len(removed_passes) > 5 else ''}",
            extra={"passes": removed_passes}))


    # Cross-target regressions (comparable targets only; every other state
    # carries no numbers by construction)
    for tc in target_comparison:
        if tc.get("state") != "comparable":
            continue
        base_red = tc.get("base_red", 0)
        curr_red = tc.get("curr_red", 0)
        red_avail = base_red > 0
        red_pct = round1((curr_red - base_red) * 100.0 / base_red) if red_avail else 0.0
        if red_avail and red_pct < -5:
            regressions.append(finding(
                "target_regression", "medium",
                f"{tc['target']} codegen regression: {pct1(-red_pct)}% worse "
                f"reduction than baseline ({base_red} -> {curr_red} lines saved)",
                metric="codegen", delta=curr_red - base_red, pct=red_pct,
                target=tc["target"]))
        elif red_avail and red_pct > 20:
            improvements.append(finding(
                "target_improvement", "positive",
                f"{tc['target']} codegen improved: {pct1(red_pct)}% better "
                f"reduction than baseline ({base_red} -> {curr_red} lines saved)",
                metric="codegen", delta=curr_red - base_red, pct=red_pct,
                target=tc["target"]))

    # Target presence/error findings (numbers live in the table; these are
    # the human-readable counterparts, mirroring the C++ engine)
    for tc in target_comparison:
        st = tc.get("state")
        if st == "new_target":
            msg = f"New codegen target in current run: {tc['target']}"
            if tc.get("curr_error"):
                msg += f" (failed: {tc['curr_error']})"
            regressions.append(finding("new_target", "info", msg,
                                       target=tc["target"]))
        elif st == "removed_target":
            msg = f"Codegen target removed from current run: {tc['target']}"
            if tc.get("base_error"):
                msg += f" (baseline had failed: {tc['base_error']})"
            improvements.append(finding("removed_target", "info", msg,
                                        target=tc["target"]))
        elif st in ("baseline_error", "current_error", "both_error"):
            msg = f"Target {tc['target']} codegen failed"
            if tc.get("base_error"):
                msg += f" (baseline: {tc['base_error']})"
            if tc.get("curr_error"):
                msg += f" (current: {tc['curr_error']})"
            regressions.append(finding("target_error", "info", msg,
                                       target=tc["target"]))

    # --- Regression risk score (0-100, higher = worse) ---
    # Heuristic composite. Identical inputs score exactly 0; improvements
    # alone never add points. Component definition (mirrored in C++):
    #   instructions: 40 * clamp(max(0, after-pct)/50)      (needs baseline)
    #   codegen:      30 * clamp(max(0, after-pct)/50)      (needs baseline)
    #   pass_effects: min(20, 10*high + 3*medium)
    #   measurement:  min(10, 5 * targets with an error on either side)
    comp = {"instructions": 0.0, "codegen": 0.0,
            "pass_effects": 0.0, "measurement_quality": 0.0}
    cov = {"instructions": False, "codegen": False,
           "pass_effects": True, "measurement_quality": True}
    instr_avail, instr_pct_for_score = pct(base_instr_after, curr_instr_after)
    if instr_avail:
        cov["instructions"] = True
        comp["instructions"] = round1(min(1.0, max(0.0, instr_pct_for_score) / 50.0) * 40.0)
    cg_avail, cg_pct_for_score = pct(base_cg_after, curr_cg_after)
    if cg_avail:
        cov["codegen"] = True
        comp["codegen"] = round1(min(1.0, max(0.0, cg_pct_for_score) / 50.0) * 30.0)
    high_regressions = sum(1 for r in regressions if r.get("severity") == "high")
    med_regressions = sum(1 for r in regressions if r.get("severity") == "medium")
    comp["pass_effects"] = round1(min(20.0, high_regressions * 10.0 + med_regressions * 3.0))
    err_targets = sum(1 for tc in target_comparison
                      if tc.get("state") in ("baseline_error", "current_error", "both_error"))
    comp["measurement_quality"] = round1(min(10.0, err_targets * 5.0))
    total = comp["instructions"] + comp["codegen"] + comp["pass_effects"] + comp["measurement_quality"]
    regression_score = max(0, min(100, int(total)))
    has_improvements = any(i.get("severity") == "positive" for i in improvements)
    if regression_score == 0:
        verdict = "improved" if has_improvements else "unchanged"
    elif regression_score <= 60:
        verdict = "mixed"
    else:
        verdict = "regressed"

    # Sort regressions by severity (high > medium > info), improvements by positive > info
    severity_order = {"high": 0, "medium": 1, "info": 2, "low": 3, "positive": 0}
    regressions.sort(key=lambda r: severity_order.get(r.get("severity", "info"), 9))
    improvements.sort(key=lambda i: severity_order.get(i.get("severity", "info"), 9))

    return {
        "summary": summary_diff,
        "passes": pass_comparison,
        "targets": target_comparison,
        "regressions": regressions,
        "improvements": improvements,
        "new_passes": new_passes,
        "removed_passes": removed_passes,
        "regression_score": regression_score,
        "verdict": verdict,
        "score_components": comp,
        "coverage": cov,
        "compat": {"blocked": None, "warnings": compat["warnings"]},
        "base_meta": {"module": base.get("module_name"), "pipeline": base.get("pipeline")},
        "curr_meta": {"module": curr.get("module_name"), "pipeline": curr.get("pipeline")},
    }


class LPTAHandler(SimpleHTTPRequestHandler):
    # Injected by serve():
    ai_base_url = DEFAULT_BASE_URL
    ai_model = DEFAULT_MODEL
    ai_key = None

    def do_GET(self):
        self._csp_sent = False
        if self.path == "/api/health":
            valid_key = _is_valid_nvidia_key(self.ai_key)
            body = {
                "static": True,
                "ai": valid_key,
                "model": self.ai_model if valid_key else None,
            }
            return self._send_json(200, body)
        return super().do_GET()

    def do_OPTIONS(self):
        # No Access-Control-Allow-Origin: the dashboard is same-origin, and
        # wildcard CORS would let any site drive the AI proxy with the
        # operator's credential.
        self.send_response(204)
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        self._csp_sent = False
        if self.path == "/api/compare":
            return self._handle_compare()

        if self.path != "/api/chat":
            return self._send_json(404, {"error": f"unknown endpoint {self.path}"})

        if not self.ai_key:
            return self._send_json(501, {
                "error": "AI Insights is not configured.",
                "hint": (
                    "Get a free key at https://build.nvidia.com "
                    "(build.nvidia.com/settings/api-keys), then restart:\n"
                    "  set NVIDIA_API_KEY=nvapi-...        (Windows)\n"
                    "  export NVIDIA_API_KEY=nvapi-...     (Linux/macOS)\n"
                    "  python serve_dashboard.py"
                ),
            })

        if not _is_valid_nvidia_key(self.ai_key):
            return self._send_json(501, {
                "error": "API key appears to be a placeholder (not a real nvapi-... key).",
                "hint": (
                    "Replace YOUR_API_KEY_HERE with a real key from "
                    "https://build.nvidia.com/settings/api-keys and restart the server."
                ),
            })

        raw, length = self._read_body()
        if raw is None:
            return self._send_json(413, {"error": f"request body too large (limit {MAX_BODY_BYTES} bytes)"})
        try:
            payload = json.loads(raw or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self._send_json(400, {"error": "invalid JSON body"})
        if not isinstance(payload, dict):
            return self._send_json(400, {"error": "JSON body must be an object"})

        messages = payload.get("messages")
        if not isinstance(messages, list) or not messages:
            return self._send_json(400, {"error": "body must contain 'messages': [...]"})
        # Bounds: this proxy forwards to a billed/rate-limited upstream API
        # with the server operator's credential — cap what one request can do.
        if len(messages) > 100:
            return self._send_json(400, {"error": "at most 100 messages per request"})
        for msg in messages:
            if not isinstance(msg, dict) or not isinstance(msg.get("role"), str) \
                    or not isinstance(msg.get("content"), str):
                return self._send_json(400, {"error": "each message needs string 'role' and 'content'"})
            if len(msg["content"]) > 50000:
                return self._send_json(400, {"error": "single message over 50000 chars"})
        try:
            max_tokens = int(payload.get("max_tokens", 2048))
        except (TypeError, ValueError):
            return self._send_json(400, {"error": "max_tokens must be an integer"})
        if not 1 <= max_tokens <= 4096:
            return self._send_json(400, {"error": "max_tokens must be 1..4096"})
        try:
            temperature = float(payload.get("temperature", 0.4))
        except (TypeError, ValueError):
            return self._send_json(400, {"error": "temperature must be a number"})
        if not 0.0 <= temperature <= 2.0:
            return self._send_json(400, {"error": "temperature must be 0..2"})

        upstream_body = json.dumps({
            # Model is server-configured only: callers must not redirect the
            # operator's credential to an arbitrary model/endpoint.
            "model": self.ai_model,
            "messages": messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
            "stream": False,
        }).encode("utf-8")

        req = urllib.request.Request(
            self.ai_base_url.rstrip("/") + "/chat/completions",
            data=upstream_body,
            headers={
                "Authorization": "Bearer " + self.ai_key,
                "Content-Type": "application/json",
                "Accept": "application/json",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                return self._send_json(resp.status, json.loads(resp.read().decode("utf-8")))
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", errors="replace")
            try:
                detail = json.loads(detail)
            except json.JSONDecodeError:
                detail = {"error": detail[:500]}
            return self._send_json(e.code, detail)
        except urllib.error.URLError as e:
            return self._send_json(502, {"error": f"upstream unreachable: {e.reason}"})
        except OSError as e:  # read timeouts (socket.timeout) and dropped connections
            return self._send_json(502, {"error": f"upstream I/O failure: {e}"})

    def _read_body(self):
        """Read the POST body, enforcing MAX_BODY_BYTES. Returns bytes or
        None (caller sends 413) and raw length for diagnostics. Oversized
        bodies are drained in small chunks (bounded RAM) so the client gets
        a clean 413 instead of a connection reset."""
        try:
            length = int(self.headers.get("Content-Length", 0))
        except (TypeError, ValueError):
            length = 0
        if length > MAX_BODY_BYTES:
            remaining = length
            while remaining > 0:
                chunk = self.rfile.read(min(65536, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
            return None, length
        return self.rfile.read(max(length, 0)), length

    def _send_json(self, code, obj):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'")
        self._csp_sent = True
        self.end_headers()
        self.wfile.write(data)

    def end_headers(self):
        # Static dashboard HTML gets a page CSP (scripts/styles are inline
        # by design; fonts and self-origin fetches stay allowed). API
        # responses already carry their own CSP via _send_json.
        if not getattr(self, "_csp_sent", False):
            self.send_header(
                "Content-Security-Policy",
                "default-src 'self'; script-src 'self' 'unsafe-inline'; "
                "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; "
                "font-src 'self' https://fonts.gstatic.com; "
                "img-src 'self' data:; connect-src 'self'")
        super().end_headers()

    def _handle_compare(self):
        raw, length = self._read_body()
        if raw is None:
            return self._send_json(413, {"error": f"request body too large (limit {MAX_BODY_BYTES} bytes)"})
        try:
            payload = json.loads(raw or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self._send_json(400, {"error": "invalid JSON body"})
        if not isinstance(payload, dict):
            return self._send_json(400, {"error": "JSON body must be an object"})

        base_data = payload.get("base")
        curr_data = payload.get("current")
        if not base_data or not curr_data:
            return self._send_json(400, {"error": "body must contain 'base' and 'current' history objects"})

        def coerce_history(v):
            # Accept a parsed history.json object or its raw JSON text.
            if isinstance(v, dict):
                return v
            if isinstance(v, str):
                return json.loads(v)
            raise ValueError("history must be an object or a JSON string")

        try:
            base = coerce_history(base_data)
            curr = coerce_history(curr_data)
        except (ValueError, json.JSONDecodeError):
            return self._send_json(400, {
                "error": "body 'base' and 'current' must be history.json objects (or JSON strings)"})

        allow_input = payload.get("allow_different_input", False) is True
        try:
            result = compare_histories(base, curr, allow_different_input=allow_input)
            return self._send_json(200, result)
        except Exception as e:
            return self._send_json(500, {"error": f"comparison failed: {e}"})

    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), fmt % args))


def serve():
    ap = argparse.ArgumentParser(description="LPTA dashboard server (+ optional AI proxy)")
    ap.add_argument("directory", nargs="?", default="report", help="report dir to serve (default: report)")
    ap.add_argument("-p", "--port", type=int, default=8080)
    ap.add_argument("--host", default="127.0.0.1", help="bind address (default: 127.0.0.1)")
    ap.add_argument("--allow-remote", action="store_true",
                    help="permit binding a non-loopback address. Reports contain "
                         "source IR and the server spends your AI credential: "
                         "only use on networks you trust.")
    args = ap.parse_args()

    if args.host not in ("127.0.0.1", "localhost", "::1") and not args.allow_remote:
        sys.exit(f"error: refusing to bind non-loopback address '{args.host}' "
                 f"without --allow-remote (reports expose source IR; see --help)")

    if not os.path.isdir(args.directory):
        sys.exit(f"error: directory '{args.directory}' does not exist "
                 f"(run lpta_test first, or pass the right path)")

    # Load config file (checked first)
    config = load_config()

    # Priority: env var > config file > default
    LPTAHandler.ai_key = os.environ.get("NVIDIA_API_KEY") or config.get("NVIDIA_API_KEY")
    LPTAHandler.ai_model = os.environ.get("LPTA_AI_MODEL") or config.get("LPTA_AI_MODEL", DEFAULT_MODEL)
    LPTAHandler.ai_base_url = os.environ.get("LPTA_AI_BASE_URL") or config.get("LPTA_AI_BASE_URL", DEFAULT_BASE_URL)

    LPTAHandler.directory = os.path.abspath(args.directory)
    os.chdir(LPTAHandler.directory)

    server = ThreadingHTTPServer((args.host, args.port), LPTAHandler)
    print(f"  Serving '{LPTAHandler.directory}' at http://{args.host}:{args.port}")
    if args.allow_remote:
        print("  WARNING: --allow-remote: reachable clients can read reports "
              "and spend your AI credential. Trust this network.")
    if LPTAHandler.ai_key:
        print(f"  AI Insights: ON  (model: {LPTAHandler.ai_model})")
    else:
        print("  AI Insights: OFF (set NVIDIA_API_KEY in .lpta_config.json or env - see README)")
    print("  Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  Bye.")


if __name__ == "__main__":
    serve()
