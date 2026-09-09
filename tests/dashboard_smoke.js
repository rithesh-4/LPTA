#!/usr/bin/env node
// Headless smoke test for dashboard.html application logic.
//
// No browser needed: stubs the DOM/fetch surface the inline script touches,
// loads a real history.json, drives render paths, and asserts on the
// resulting HTML strings. Fails loudly (exit 1) with the first violation.
// Everything runs inside ONE eval so `let`-scoped dashboard state is
// reachable by the driver code appended to the source.
//
// Usage: node tests/dashboard_smoke.js [history.json]
//   default history: report/history.json
"use strict";
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const HIST = process.argv[2] || path.join(ROOT, "report", "history.json");

// ---------- minimal DOM stubs (only what the script touches) ----------
function mkCtx() {
    const px = new Proxy(function () {}, {
        get(t, p) {
            if (p === "canvas") return {};
            if (p === Symbol.toPrimitive) return () => 0;
            return (...a) => px;
        },
        set: () => true,
        apply: () => px,
    });
    return px;
}
function mkEl() {
    return {
        innerHTML: "", textContent: "", value: "", disabled: false,
        style: {}, dataset: {},
        classList: { add() {}, remove() {}, contains: () => false },
        addEventListener() {}, appendChild() {}, removeChild() {},
        click() {}, focus() {}, scrollIntoView() {},
        getContext: () => mkCtx(),
        getBoundingClientRect: () => ({ width: 800, height: 200 }),
        scrollTop: 0, scrollHeight: 0,
        querySelector: () => mkEl(), querySelectorAll: () => [],
    };
}
const __els = {};
let fetchMode = "reject"; // 'reject' | 'compare-ok'
const cannedCompare = {
    summary: { total_instructions_before: 100, total_instructions_after: 90 },
    base_meta: { module: "a.ll", pipeline: "O2" },
    curr_meta: { module: "a.ll", pipeline: "O2" },
    regression_score: 35, regressions: [], improvements: [],
    passes: [], targets: [], new_passes: [], removed_passes: [],
};
function setGlobal(k, v) {
    try { global[k] = v; }
    catch (e) { Object.defineProperty(global, k, { value: v, writable: true, configurable: true }); }
}
setGlobal("document", {
    getElementById: (id) => __els[id] || (__els[id] = mkEl()),
    createElement: () => mkEl(),
    querySelector: () => mkEl(),
    querySelectorAll: () => [],
    addEventListener() {},
    body: mkEl(),
});
setGlobal("window", global);
setGlobal("location", { hash: "" });
setGlobal("localStorage", {
    _s: {},
    getItem(k) { return this._s[k] || null; },
    setItem(k, v) { this._s[k] = String(v); },
    removeItem(k) { delete this._s[k]; },
});
setGlobal("requestAnimationFrame", () => 0);
setGlobal("scrollTo", () => {});
setGlobal("history", { pushState() {}, replaceState() {}, state: null });
setGlobal("ResizeObserver", class { observe() {} disconnect() {} });
setGlobal("IntersectionObserver", class { observe() {} disconnect() {} });
setGlobal("performance", { now: () => 0 });
setGlobal("devicePixelRatio", 1);
setGlobal("navigator", {});
setGlobal("fetch", (url) => {
    if (fetchMode === "compare-ok" && String(url) === "/api/compare") {
        return Promise.resolve({ ok: true, json: async () => cannedCompare });
    }
    return Promise.reject(new TypeError("fetch failed"));
});
setGlobal("__els", __els);
setGlobal("__fs", fs);
setGlobal("__setFetchMode", (m) => { fetchMode = m; });

// ---------- load the real inline script + driver in ONE eval ----------
const html = fs.readFileSync(path.join(ROOT, "dashboard.html"), "utf8");
const m = html.match(/<script(?![^>]*src=)[^>]*>([\s\S]*?)<\/script>/);
if (!m) { console.log("  [FAIL] no inline script found"); process.exit(2); }

const driver = `
;(async () => {
let __failures = 0;
function check(name, cond, extra) {
    if (cond) console.log("  [PASS] " + name);
    else { __failures++; console.log("  [FAIL] " + name + (extra ? " -- " + extra : "")); }
}
process.on("unhandledRejection", (e) => {
    __failures++;
    console.log("  [FAIL] unhandled rejection: " + (e && e.message));
});
try {
    for (let i = 0; i < 20; i++) await Promise.resolve(); // settle load()

    // 1. load() with dead backend shows guidance, never throws
    const __loadHtml = (__els["events"].innerHTML + __els["overview-cards"].innerHTML);
    check("dead backend shows load error", __loadHtml.includes("history.json"));

    // 2. render a real report
    D = JSON.parse(__fs.readFileSync(${JSON.stringify(HIST)}, "utf8"));
    render();
    for (let i = 0; i < 5; i++) await Promise.resolve();
    check("events rendered", __els["events"].innerHTML.length > 500,
        "events html length=" + __els["events"].innerHTML.length);

    // 3. openDiff on a snapshot event: footer rows well-formed, copy present
    const snap = D.events.find((e) => e.event_type === "after" && e.ir_before && e.ir_after);
    check("fixture has a snapshot event", !!snap);
    if (snap) {
        openDiff(snap.id);
        const foot = __els["modal-footer"].innerHTML;
        check("footer has full metric names", foot.includes("Instructions:") && foot.includes("Arith ops"));
        check("footer deltas parenthesized, never glued",
            !/\\d→\\d{2,}</.test(foot) && /\\(\\+?\\-?\\d+\\)/.test(foot), foot.slice(0, 200));
        check("footer tooltips present", foot.includes("title="));
        check("before pane rendered with line numbers",
            __els["ir-before"].innerHTML.includes('class="ln"'));
    }

    // 4. func modal path: list tooltips + copy buttons + metric rows
    const fn = [...new Set(D.events.filter((e) => e.ir_kind === "Function" && e.event_type === "after" && (e.has_changes || e.ir_changed)).map((e) => e.ir_name))][0];
    check("fixture has a changed function", !!fn);
    if (fn) {
        openFuncDetail(fn);
        for (let i = 0; i < 5; i++) await Promise.resolve();
        const list = __els["func-pass-list"].innerHTML;
        check("pass rows explain themselves", list.includes("title=") && list.includes("instr"));
        const matches = D.events.map((e, i) => ({ e, i })).filter(({ e }) => e.ir_kind === "Function" && e.ir_name === fn && e.event_type === "after");
        const at = matches.findIndex(({ e }) => e.ir_before && e.ir_after);
        if (at >= 0) {
            selectFuncPass(at);
            const area = __els["func-diff-area"].innerHTML;
            check("func diff has copy buttons", (area.match(/copyDiffBtn\\(this/g) || []).length === 2,
                "found " + (area.match(/copyDiffBtn\\(this/g) || []).length);
            check("func diff has readable metrics", area.includes("Instructions:"));
        } else {
            check("snapshot pass selectable", false, "no snapshot pass in " + fn);
        }
    }

    // 5. runCompare against canned backend labels the heuristic
    COMPARE_BASE = { events: [], summary: {} };
    COMPARE_CURR = { events: [], summary: {} };
    __setFetchMode("compare-ok");
    await runCompare();
    for (let i = 0; i < 5; i++) await Promise.resolve();
    check("compare labels heuristic indicator",
        __els["compare-results"].innerHTML.includes("Heuristic indicator"));

    // 6. old-schema summary (no passes_with_ir_changes) still renders
    const s2 = JSON.parse(JSON.stringify(D.summary));
    delete s2.passes_with_ir_changes;
    D.summary = s2;
    renderOverview();
    check("old summary schema renders", __els["overview-cards"].innerHTML.length > 100);
} catch (err) {
    __failures++;
    console.log("  [FAIL] driver threw: " + (err && err.stack));
}
console.log(__failures === 0 ? "SMOKE ALL PASS" : "SMOKE FAILURES: " + __failures);
process.exit(__failures === 0 ? 0 : 1);
})();
`;
eval.call(global, m[1] + driver);
