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
DEFAULT_BASE_URL = "https://integrate.api.nvidia.com/v1"
CONFIG_FILENAME = ".lpta_config.json"


def load_config():
    """Load config from .lpta_config.json (current dir or home dir)."""
    config = {}
    for path in [Path.cwd() / CONFIG_FILENAME, Path.home() / CONFIG_FILENAME]:
        if path.is_file():
            try:
                with open(path, "r") as f:
                    config = json.load(f)
                    break
            except (json.JSONDecodeError, OSError):
                pass
    return config


def compare_histories(base, curr):
    """Compare two history.json objects and return a detailed diff."""
    # Summary comparison
    sb = base.get("summary", {})
    sc = curr.get("summary", {})

    def diff(a, b):
        return b - a

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

    # Calculate instruction reduction delta
    base_instr_red = sb.get("total_instructions_before", 0) - sb.get("total_instructions_after", 0)
    curr_instr_red = sc.get("total_instructions_before", 0) - sc.get("total_instructions_after", 0)
    summary_diff["instruction_reduction_delta"] = curr_instr_red - base_instr_red

    base_cg_red = sb.get("codegen_asm_lines_before", 0) - sb.get("codegen_asm_lines_after", 0)
    curr_cg_red = sc.get("codegen_asm_lines_before", 0) - sc.get("codegen_asm_lines_after", 0)
    summary_diff["codegen_reduction_delta"] = curr_cg_red - base_cg_red

    # Pass-level comparison (only after events with changes)
    base_passes = {}
    curr_passes = {}

    for e in base.get("events", []):
        if e.get("event_type") == "after" and e.get("has_changes"):
            pn = e.get("pass_name", "")
            if pn not in base_passes:
                base_passes[pn] = {"count": 0, "instr_delta": 0, "bb_delta": 0}
            base_passes[pn]["count"] += 1
            base_passes[pn]["instr_delta"] += e.get("metrics_after", {}).get("instruction_count", 0) - e.get("metrics_before", {}).get("instruction_count", 0)
            base_passes[pn]["bb_delta"] += e.get("metrics_after", {}).get("basic_block_count", 0) - e.get("metrics_before", {}).get("basic_block_count", 0)

    for e in curr.get("events", []):
        if e.get("event_type") == "after" and e.get("has_changes"):
            pn = e.get("pass_name", "")
            if pn not in curr_passes:
                curr_passes[pn] = {"count": 0, "instr_delta": 0, "bb_delta": 0}
            curr_passes[pn]["count"] += 1
            curr_passes[pn]["instr_delta"] += e.get("metrics_after", {}).get("instruction_count", 0) - e.get("metrics_before", {}).get("instruction_count", 0)
            curr_passes[pn]["bb_delta"] += e.get("metrics_after", {}).get("basic_block_count", 0) - e.get("metrics_before", {}).get("basic_block_count", 0)

    all_pass_names = set(base_passes.keys()) | set(curr_passes.keys())
    pass_comparison = []
    for pn in sorted(all_pass_names):
        bp = base_passes.get(pn, {"count": 0, "instr_delta": 0, "bb_delta": 0})
        cp = curr_passes.get(pn, {"count": 0, "instr_delta": 0, "bb_delta": 0})
        pass_comparison.append({
            "pass_name": pn,
            "base_count": bp["count"],
            "curr_count": cp["count"],
            "count_delta": cp["count"] - bp["count"],
            "base_instr_delta": bp["instr_delta"],
            "curr_instr_delta": cp["instr_delta"],
            "instr_delta_delta": cp["instr_delta"] - bp["instr_delta"],
            "base_bb_delta": bp["bb_delta"],
            "curr_bb_delta": cp["bb_delta"],
            "bb_delta_delta": cp["bb_delta"] - bp["bb_delta"],
        })

    # Cross-target codegen comparison
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

    # Regression detection
    regressions = []
    # Use relative thresholds based on baseline size; fall back to absolute if baseline is 0
    bsum = base.get("summary", {})
    csum = curr.get("summary", {})
    base_instr_after = bsum.get("total_instructions_after", 0)
    curr_instr_after = csum.get("total_instructions_after", 0)
    if base_instr_after > 0:
        pct_increase = (curr_instr_after - base_instr_after) * 100 // base_instr_after
        if pct_increase > 5:  # >5% increase flagged as high
            regressions.append({"type": "instruction_increase", "severity": "high",
                                "message": f"Instruction count increased by {pct_increase}% vs baseline"})
    elif curr_instr_after > 50:
        regressions.append({"type": "instruction_increase", "severity": "high",
                            "message": f"Instruction count increased by {curr_instr_after} vs baseline"})
    base_cg_after = bsum.get("codegen_asm_lines_after", 0)
    curr_cg_after = csum.get("codegen_asm_lines_after", 0)
    if base_cg_after > 0:
        pct_cg_increase = (curr_cg_after - base_cg_after) * 100 // base_cg_after
        if pct_cg_increase > 5:
            regressions.append({"type": "codegen_regression", "severity": "high",
                                "message": f"Codegen assembly lines increased by {pct_cg_increase}% vs baseline"})
    elif curr_cg_after > 50:
        regressions.append({"type": "codegen_regression", "severity": "high",
                            "message": f"Codegen assembly lines increased by {curr_cg_after} vs baseline"})
    # Pass-level regressions using delta_delta relative to baseline
    for pc in pass_comparison:
        base_instr = bsum.get("total_instructions_after", 0)
        if base_instr > 0:
            pct_delta = pc["instr_delta_delta"] * 100 // base_instr
            if abs(pct_delta) > 5:
                regressions.append({"type": "pass_regression", "severity": "medium",
                                    "pass": pc["pass_name"],
                                    "message": f"{pc['pass_name']} instruction delta worsened by {abs(pct_delta)}%"})
        bb_pct = pc["bb_delta_delta"] * 100 // base_instr if base_instr > 0 else pc["bb_delta_delta"]
        if abs(bb_pct) > 10:
            regressions.append({"type": "pass_regression", "severity": "medium",
                                "pass": pc["pass_name"],
                                "message": f"{pc['pass_name']} BB delta worsened by {abs(bb_pct)}%"})
    # Cross-target regressions
    for tc in target_comparison:
        base_red = tc.get("base_red", 0)
        curr_red = tc.get("curr_red", 0)
        if base_red > 0:
            pct_delta = (curr_red - base_red) * 100 // base_red
            if pct_delta < -5:  # negative = current reduces LESS = regression
                regressions.append({"type": "target_regression", "severity": "medium",
                                    "target": tc["target"],
                                    "message": f"{tc['target']} codegen regression: {abs(pct_delta)}% worse than baseline"})

    return {
        "summary": summary_diff,
        "passes": pass_comparison,
        "targets": target_comparison,
        "regressions": regressions,
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
            body = {
                "static": True,
                "ai": bool(self.ai_key),
                "model": self.ai_model if self.ai_key else None,
            }
            return self._send_json(200, body)
        return super().do_GET()

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

        try:
            length = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(length) or b"{}")
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

    def _send_json(self, code, obj):
        data = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Security-Policy", "default-src 'self'")
        self.end_headers()
        self.wfile.write(data)

    def _handle_compare(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, json.JSONDecodeError):
            return self._send_json(400, {"error": "invalid JSON body"})

        base_data = payload.get("base")
        curr_data = payload.get("current")
        if not base_data or not curr_data:
            return self._send_json(400, {"error": "body must contain 'base' and 'current' history objects"})

        try:
            result = compare_histories(base_data, curr_data)
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
