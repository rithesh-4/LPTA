#!/usr/bin/env node
/**
 * Playwright browser smoke test for LPTA dashboard.
 * Loads report/index.html, verifies key rendering and interactions.
 *
 * Usage: node tests/dashboard_browser.js [report_dir]
 * Requires: npm install playwright && npx playwright install chromium
 */
const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const reportDir = process.argv[2] || "report";
const historyPath = path.join(reportDir, "history.json");
const indexPath = path.join(reportDir, "index.html");

let passed = 0, failed = 0;
function check(name, cond, detail) {
    if (cond) { passed++; console.log("  [PASS] " + name); }
    else { failed++; console.log("  [FAIL] " + name + (detail ? " -- " + detail : "")); }
}

(async () => {
    if (!fs.existsSync(indexPath)) {
        console.log("  [SKIP] No report/index.html found (run lpta_test first)");
        process.exit(0);
    }
    if (!fs.existsSync(historyPath)) {
        console.log("  [SKIP] No report/history.json found (run lpta_test first)");
        process.exit(0);
    }
    const history = JSON.parse(fs.readFileSync(historyPath, "utf8"));
    const historyJson = JSON.stringify(history);
    const absIndex = path.resolve(indexPath).replace(/\\/g, "/");

    const browser = await chromium.launch({
        headless: true,
        executablePath: "C:/Users/ramri/AppData/Local/ms-playwright/chromium-1243/chrome-win64/chrome.exe",
    });
    const page = await browser.newPage();

    try {
        const errors = [];
        page.on("pageerror", (e) => errors.push(e.message));

        await page.route("**/history.json", (route) => {
            route.fulfill({
                status: 200,
                contentType: "application/json",
                body: historyJson,
            });
        });

        await page.goto("file:///" + absIndex, { waitUntil: "networkidle" });
        await page.waitForTimeout(300);

        const cardsHtml = await page.$eval("#overview-cards", (el) => el.innerHTML);
        check("overview cards rendered", cardsHtml.length > 100, "length=" + cardsHtml.length);

        check("has 'Completed callbacks' label", cardsHtml.includes("Completed callbacks"));
        check("has 'Structural-change callbacks' label", cardsHtml.includes("Structural-change callbacks"));
        check("has 'Any-IR-change callbacks' label", cardsHtml.includes("Any-IR-change callbacks"));
        check("has 'IR-changing callback rate' label", cardsHtml.includes("IR-changing callback rate"));

        const rate = history.summary.total_after
            ? Math.round(((history.summary.passes_with_ir_changes ?? history.summary.passes_with_changes) / history.summary.total_after) * 100)
            : 0;
        check("callback rate value correct", cardsHtml.includes(rate + "%"), "expected " + rate + "%");

        await page.evaluate(() => goTo("functions"));
        await page.waitForTimeout(300);
        const funcHtml = await page.$eval("#func-grid", (el) => el.innerHTML);
        check("function grid rendered", funcHtml.length > 50, "length=" + funcHtml.length);

        await page.evaluate(() => goTo("passes"));
        await page.waitForTimeout(300);
        const evHtml = await page.$eval("#events", (el) => el.innerHTML);
        check("events rendered", evHtml.length > 100, "length=" + evHtml.length);

        const topHtml = await page.$eval("#top-passes-grid", (el) => el.innerHTML);
        check("top passes rendered", topHtml.length > 50, "length=" + topHtml.length);

        check("no JS errors", errors.length === 0, errors.join("; "));

        await page.evaluate(() => goTo("overview"));
        await page.waitForTimeout(200);
        const backToOverview = await page.$eval("#overview-cards", (el) => el.innerHTML.length);
        check("navigation round-trip", backToOverview > 100);

    } catch (err) {
        failed++;
        console.log("  [FAIL] test threw: " + err.message);
    } finally {
        await browser.close();
    }

    console.log("\n=== Browser Test Summary ===");
    console.log("  Passed: " + passed);
    console.log("  Failed: " + failed);
    console.log("  Status: " + (failed === 0 ? "ALL PASS" : "FAILURES"));
    process.exit(failed === 0 ? 0 : 1);
})();
