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


def compare_histories(base, curr):
    """Compare two history.json objects and return a detailed diff.

    Enhanced with:
    - New/removed pass detection
    - Instruction reduction efficiency metrics
    - Pipeline change detection
    - Load/store/branch impact tracking
    - Overall regression score (0-100)
    - Improvements alongside regressions
    """
    # Summary comparison
    sb = base.get("summary", {})
    sc = curr.get("summary", {})

    def diff(a, b):
        return b - a

    def pct_delta(old, new):
        """Percentage change from old to new. Returns 0 if old is 0."""
        if old == 0:
            return 0 if new == 0 else 100
        return ((new - old) * 100) // old

    summary_diff = {
        "total_events": diff(sb.get("total_events", 0), sc.get("total_events", 0)),
        "total_before": diff(sb.get("total_before", 0), sc.get("total_before", 0)),
        "total_after": diff(sb.get("total_after", 0), sc.get("total_after", 0)),
        "total_invalidated": diff(sb.get("total_invalidated", 0), sc.get("total_invalidated", 0)),
        "passes_with_changes": diff(sb.get("passes_with_changes", 0), sc.get("passes_with_changes", 0)),
        "unique_pass_names": diff(sb.get("unique_pass_names", 0), sc.get("unique_pass_names", 0)),
        "total_instructions_before": diff(sb.get("total_instructions_before", 0), sc.get("total_instructions_before", 0)),
        "total_instructions_after": diff(sb.get("total_instructions_after", 0), sc.get("total_instructions_after", 0)),
        "total_bbs_before": diff(sb.get("total_bbs_before", 0), sc.get("total_bbs_before", 0)),
        "total_bbs_after": diff(sb.get("total_bbs_after", 0), sc.get("total_bbs_after", 0)),
        "codegen_asm_lines_before": diff(sb.get("codegen_asm_lines_before", 0), sc.get("codegen_asm_lines_before", 0)),
        "codegen_asm_lines_after": diff(sb.get("codegen_asm_lines_after", 0), sc.get("codegen_asm_lines_after", 0)),
        "codegen_asm_bytes_before": diff(sb.get("codegen_asm_bytes_before", 0), sc.get("codegen_asm_bytes_before", 0)),
        "codegen_asm_bytes_after": diff(sb.get("codegen_asm_bytes_after", 0), sc.get("codegen_asm_bytes_after", 0)),
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

    all_pass_names = sorted(set(base_passes.keys()) | set(curr_passes.keys()))
    pass_comparison = []
    for pn in all_pass_names:
        bp = base_passes.get(pn)
        cp = curr_passes.get(pn)
        base_exists = bp is not None
        curr_exists = cp is not None

        # Detect new/removed passes
        status = "unchanged"
        if not base_exists and curr_exists:
            status = "new"  # pass exists in current but not baseline
        elif base_exists and not curr_exists:
            status = "removed"  # pass existed in baseline but not current

        if bp is None:
            bp = {"count": 0, "instr_delta": 0, "bb_delta": 0,
                  "load_delta": 0, "store_delta": 0, "branch_delta": 0, "phi_delta": 0}
        if cp is None:
            cp = {"count": 0, "instr_delta": 0, "bb_delta": 0,
                  "load_delta": 0, "store_delta": 0, "branch_delta": 0, "phi_delta": 0}

        pass_comparison.append({
            "pass_name": pn,
            "status": status,
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
    new_passes = [p["pass_name"] for p in pass_comparison if p["status"] == "new"]
    removed_passes = [p["pass_name"] for p in pass_comparison if p["status"] == "removed"]

    # --- Cross-target codegen comparison ---
    base_targets = sb.get("codegen_targets", {})
    curr_targets = sc.get("codegen_targets", {})
    all_targets = set(base_targets.keys()) | set(curr_targets.keys())
    target_comparison = []
    for t in sorted(all_targets):
        bt = base_targets.get(t, {})
        ct = curr_targets.get(t, {})
        if bt.get("error") or ct.get("error"):
            target_comparison.append({
                "target": t,
                "base_error": bt.get("error"),
                "curr_error": ct.get("error"),
                "available": False,
            })
        else:
            base_before = bt.get("asm_lines_before", 0)
            base_after = bt.get("asm_lines_after", 0)
            curr_before = ct.get("asm_lines_before", 0)
            curr_after = ct.get("asm_lines_after", 0)
            target_comparison.append({
                "target": t,
                "base_before": base_before,
                "base_after": base_after,
                "base_red": base_before - base_after,
                "curr_before": curr_before,
                "curr_after": curr_after,
                "curr_red": curr_before - curr_after,
                "red_delta": (curr_before - curr_after) - (base_before - base_after),
                "available": True,
            })

    # --- Regression + improvement detection ---
    regressions = []
    improvements = []
    bsum = base.get("summary", {})
    csum = curr.get("summary", {})
    base_instr_after = bsum.get("total_instructions_after", 0)
    curr_instr_after = csum.get("total_instructions_after", 0)

    # Instruction count regression/improvement
    if base_instr_after > 0:
        instr_pct = pct_delta(base_instr_after, curr_instr_after)
        if instr_pct > 5:
            regressions.append({
                "type": "instruction_increase", "severity": "high",
                "message": f"Instruction count increased by {instr_pct}% vs baseline ({base_instr_after} -> {curr_instr_after})",
                "metric": "instructions", "delta": curr_instr_after - base_instr_after,
                "pct": instr_pct,
            })
        elif instr_pct < -5:
            improvements.append({
                "type": "instruction_reduction", "severity": "positive",
                "message": f"Instruction count reduced by {abs(instr_pct)}% vs baseline ({base_instr_after} -> {curr_instr_after})",
                "metric": "instructions", "delta": curr_instr_after - base_instr_after,
                "pct": instr_pct,
            })
    elif curr_instr_after > 50:
        regressions.append({
            "type": "instruction_increase", "severity": "high",
            "message": f"Instruction count increased by {curr_instr_after} vs baseline (0 -> {curr_instr_after})",
            "metric": "instructions", "delta": curr_instr_after, "pct": 100,
        })

    # Codegen regression/improvement
    base_cg_after = bsum.get("codegen_asm_lines_after", 0)
    curr_cg_after = csum.get("codegen_asm_lines_after", 0)
    if base_cg_after > 0:
        cg_pct = pct_delta(base_cg_after, curr_cg_after)
        if cg_pct > 5:
            regressions.append({
                "type": "codegen_regression", "severity": "high",
                "message": f"Codegen assembly lines increased by {cg_pct}% vs baseline ({base_cg_after} -> {curr_cg_after})",
                "metric": "codegen", "delta": curr_cg_after - base_cg_after,
                "pct": cg_pct,
            })
        elif cg_pct < -5:
            improvements.append({
                "type": "codegen_improvement", "severity": "positive",
                "message": f"Codegen assembly lines reduced by {abs(cg_pct)}% vs baseline ({base_cg_after} -> {curr_cg_after})",
                "metric": "codegen", "delta": curr_cg_after - base_cg_after,
                "pct": cg_pct,
            })
    elif curr_cg_after > 50:
        regressions.append({
            "type": "codegen_regression", "severity": "high",
            "message": f"Codegen assembly lines increased by {curr_cg_after} vs baseline (0 -> {curr_cg_after})",
            "metric": "codegen", "delta": curr_cg_after, "pct": 100,
        })

    # Pass-level regressions and improvements
    for pc in pass_comparison:
        if pc["status"] in ("new", "removed"):
            continue  # handled separately
        base_instr = bsum.get("total_instructions_after", 0)
        if base_instr > 0:
            instr_pct = pc["instr_delta_delta"] * 100 // base_instr
            if instr_pct > 5:
                regressions.append({
                    "type": "pass_regression", "severity": "medium",
                    "pass": pc["pass_name"],
                    "message": f"{pc['pass_name']} instruction delta worsened by {instr_pct}% ({pc['base_instr_delta']:+d} -> {pc['curr_instr_delta']:+d})",
                    "metric": "instructions", "delta": pc["instr_delta_delta"], "pct": instr_pct,
                })
            elif instr_pct < -5:
                improvements.append({
                    "type": "pass_improvement", "severity": "positive",
                    "pass": pc["pass_name"],
                    "message": f"{pc['pass_name']} instruction delta improved by {abs(instr_pct)}% ({pc['base_instr_delta']:+d} -> {pc['curr_instr_delta']:+d})",
                    "metric": "instructions", "delta": pc["instr_delta_delta"], "pct": instr_pct,
                })

        # Load/store regression detection
        if abs(pc["load_delta_delta"]) > 5:
            sev = "medium" if pc["load_delta_delta"] > 0 else "low"
            item = {
                "type": "pass_regression" if pc["load_delta_delta"] > 0 else "pass_improvement",
                "severity": sev,
                "pass": pc["pass_name"],
                "metric": "loads",
                "delta": pc["load_delta_delta"],
                "pct": 0,
            }
            if pc["load_delta_delta"] > 0:
                item["message"] = f"{pc['pass_name']} load count delta increased by {pc['load_delta_delta']:+d}"
                regressions.append(item)
            else:
                item["message"] = f"{pc['pass_name']} load count delta decreased by {pc['load_delta_delta']:+d}"
                improvements.append(item)

        if abs(pc["store_delta_delta"]) > 5:
            sev = "medium" if pc["store_delta_delta"] > 0 else "low"
            item = {
                "type": "pass_regression" if pc["store_delta_delta"] > 0 else "pass_improvement",
                "severity": sev,
                "pass": pc["pass_name"],
                "metric": "stores",
                "delta": pc["store_delta_delta"],
                "pct": 0,
            }
            if pc["store_delta_delta"] > 0:
                item["message"] = f"{pc['pass_name']} store count delta increased by {pc['store_delta_delta']:+d}"
                regressions.append(item)
            else:
                item["message"] = f"{pc['pass_name']} store count delta decreased by {pc['store_delta_delta']:+d}"
                improvements.append(item)

    # New/removed pass regressions
    if new_passes:
        regressions.append({
            "type": "new_passes", "severity": "info",
            "message": f"{len(new_passes)} new pass(es) appeared in current run: {', '.join(new_passes[:5])}{', ...' if len(new_passes) > 5 else ''}",
            "passes": new_passes,
        })
    if removed_passes:
        improvements.append({
            "type": "removed_passes", "severity": "info",
            "message": f"{len(removed_passes)} pass(es) removed from current run: {', '.join(removed_passes[:5])}{', ...' if len(removed_passes) > 5 else ''}",
            "passes": removed_passes,
        })

    # Cross-target regressions
    for tc in target_comparison:
        base_red = tc.get("base_red", 0)
        curr_red = tc.get("curr_red", 0)
        if base_red > 0:
            pct_delta_val = (curr_red - base_red) * 100 // base_red
            if pct_delta_val < -5:
                regressions.append({
                    "type": "target_regression", "severity": "medium",
                    "target": tc["target"],
                    "message": f"{tc['target']} codegen regression: {abs(pct_delta_val)}% worse reduction than baseline",
                    "metric": "codegen", "delta": curr_red - base_red, "pct": pct_delta_val,
                })
            elif pct_delta_val > 20:
                improvements.append({
                    "type": "target_improvement", "severity": "positive",
                    "target": tc["target"],
                    "message": f"{tc['target']} codegen improved: {pct_delta_val}% better reduction than baseline",
                    "metric": "codegen", "delta": curr_red - base_red, "pct": pct_delta_val,
                })

    # --- Overall regression score (0-100, higher = worse) ---
    # Weighted: instruction count (40%), codegen (30%), pass-level (20%), events (10%)
    score = 0
    if base_instr_after > 0:
        instr_pct_for_score = pct_delta(base_instr_after, curr_instr_after)
        # Clamp to [-50, 50] range, map to 0-100: 0% = 50, +50% = 100, -50% = 0
        score += max(0, min(100, 50 + instr_pct_for_score)) * 0.4
    if base_cg_after > 0:
        cg_pct_for_score = pct_delta(base_cg_after, curr_cg_after)
        score += max(0, min(100, 50 + cg_pct_for_score)) * 0.3
    high_regressions = sum(1 for r in regressions if r.get("severity") == "high")
    med_regressions = sum(1 for r in regressions if r.get("severity") == "medium")
    score += min(30, high_regressions * 15 + med_regressions * 5) * 0.2
    high_improvements = sum(1 for i in improvements if i.get("severity") == "positive")
    score -= min(20, high_improvements * 10) * 0.1
    regression_score = max(0, min(100, int(score)))

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
        "base_meta": {"module": base.get("module_name"), "pipeline": base.get("pipeline")},
        "curr_meta": {"module": curr.get("module_name"), "pipeline": curr.get("pipeline")},
    }


class LPTAHandler(SimpleHTTPRequestHandler):
    # Injected by serve():
    ai_base_url = DEFAULT_BASE_URL
    ai_model = DEFAULT_MODEL
    ai_key = None

    def do_GET(self):
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
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
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

        messages = payload.get("messages")
        if not isinstance(messages, list) or not messages:
            return self._send_json(400, {"error": "body must contain 'messages': [...]"})

        upstream_body = json.dumps({
            "model": payload.get("model") or self.ai_model,
            "messages": messages,
            "temperature": payload.get("temperature", 0.4),
            "max_tokens": payload.get("max_tokens", 2048),
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
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'")
        self.end_headers()
        self.wfile.write(data)

    def _handle_compare(self):
        raw, length = self._read_body()
        if raw is None:
            return self._send_json(413, {"error": f"request body too large (limit {MAX_BODY_BYTES} bytes)"})
        try:
            payload = json.loads(raw or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self._send_json(400, {"error": "invalid JSON body"})

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

        try:
            result = compare_histories(base, curr)
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
    args = ap.parse_args()

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
