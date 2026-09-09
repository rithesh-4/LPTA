#include "Config.h"
#include "Codegen.h"
#include "Detection.h"
#include "JsonWriter.h"
#include "Metrics.h"
#include "Snapshots.h"
#include "Tracker.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IRReader/IRReader.h"

#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cctype>

#include <map>
#include <tuple>

using namespace llvm;
namespace fs = std::filesystem;

// Globals are defined in globals.cpp (shared with test_utilities).


// ============================================================
// --compare CLI mode
// ============================================================

struct CompareEvent {
    unsigned id = 0;
    std::string event_type;
    std::string pass_name;
    bool has_changes = false;
    bool ir_changed = false;
    unsigned instr_before = 0, instr_after = 0;
    unsigned bb_before = 0, bb_after = 0;
    unsigned load_before = 0, load_after = 0;
    unsigned store_before = 0, store_after = 0;
    unsigned branch_before = 0, branch_after = 0;
    unsigned phi_before = 0, phi_after = 0;
};

struct CompareTarget {
    std::string name;
    long long lines_before = 0, lines_after = 0;
    long long bytes_before = 0, bytes_after = 0;
    std::string error;  // empty = success
};

static std::string trimWS(const std::string &s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static int parseJsonInt(const std::string &line, const std::string &key) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = line.find(':', pos);
    if (pos == std::string::npos) return 0;
    auto val = line.find_first_not_of(" \t", pos + 1);
    if (val == std::string::npos) return 0;
    auto end = line.find_first_of(",\n\r ]}", val);
    std::string num = trimWS(line.substr(val, end == std::string::npos ? std::string::npos : end - val));
    // Handle null
    if (num == "null" || num == "true" || num == "false") return 0;
    try { return std::stoi(num); } catch (...) { return 0; }
}

static std::string parseJsonString(const std::string &line, const std::string &key) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    pos = line.find(':', pos);
    if (pos == std::string::npos) return "";
    auto q1 = line.find('"', pos + 1);
    if (q1 == std::string::npos) return "";
    // Find closing quote, skipping escaped quotes (\")
    auto q2 = q1 + 1;
    while (true) {
        q2 = line.find('"', q2);
        if (q2 == std::string::npos) return "";
        // Count consecutive backslashes immediately before q2: odd => escaped
        size_t bs = 0;
        size_t k = q2;
        while (k > q1 + 1 && line[k - 1] == '\\') { bs++; k--; }
        if (bs % 2 == 0) break;  // unescaped quote
        q2++;  // escaped, keep searching
    }
    return line.substr(q1 + 1, q2 - q1 - 1);
}

static bool parseJsonBool(const std::string &line, const std::string &key) {
    auto pos = line.find("\"" + key + "\"");
    if (pos == std::string::npos) return false;
    pos = line.find(':', pos);
    if (pos == std::string::npos) return false;
    auto val = line.find_first_not_of(" \t", pos + 1);
    if (val == std::string::npos) return false;
    return line.compare(val, 4, "true") == 0;
}

// First quoted string on the line (for map keys like target triples in
// "codegen_targets"). Skips escaped quotes; JSON escapes stay encoded,
// which is consistent between the two files being compared.
static std::string parseFirstQuoted(const std::string &line) {
    auto q1 = line.find('"');
    if (q1 == std::string::npos) return "";
    auto q2 = q1 + 1;
    while (true) {
        q2 = line.find('"', q2);
        if (q2 == std::string::npos) return "";
        size_t bs = 0, k = q2;
        while (k > q1 + 1 && line[k - 1] == '\\') { bs++; k--; }
        if (bs % 2 == 0) break;  // unescaped quote
        q2++;
    }
    return line.substr(q1 + 1, q2 - q1 - 1);
}

static long long pctDelta(long long old_val, long long new_val) {
    if (old_val == 0) return (new_val == 0) ? 0 : 100;
    return ((new_val - old_val) * 100) / old_val;
}

static int compareJsonFiles(const std::string &basePath, const std::string &currPath) {
    // --- Read and parse baseline ---
    std::ifstream baseFile(basePath);
    if (!baseFile.is_open()) {
        errs() << "ERROR: cannot open baseline file '" << basePath << "'\n";
        return 1;
    }

    std::string line;
    std::string basePipeline, baseModule;
    long long baseInstrBefore = 0, baseInstrAfter = 0;
    long long baseBbBefore = 0, baseBbAfter = 0;
    long long baseCgLinesBefore = 0, baseCgLinesAfter = 0;
    std::vector<CompareEvent> baseEvents;
    std::vector<CompareTarget> baseTargets;

    {
        bool inEvents = false;
        bool inMetricsBefore = false, inMetricsAfter = false;
        bool inTargets = false, inTargetObj = false;
        CompareEvent ev;
        CompareTarget tev;
        while (std::getline(baseFile, line)) {
            if (!inEvents) {
                // Top-level fields
                auto s = parseJsonString(line, "pipeline");
                if (!s.empty()) basePipeline = s;
                s = parseJsonString(line, "module_name");
                if (!s.empty()) baseModule = s;
                // Summary fields
                auto v = parseJsonInt(line, "total_instructions_before");
                if (v) baseInstrBefore = v;
                v = parseJsonInt(line, "total_instructions_after");
                if (v) baseInstrAfter = v;
                v = parseJsonInt(line, "total_bbs_before");
                if (v) baseBbBefore = v;
                v = parseJsonInt(line, "total_bbs_after");
                if (v) baseBbAfter = v;
                v = parseJsonInt(line, "codegen_asm_lines_before");
                if (v) baseCgLinesBefore = v;
                v = parseJsonInt(line, "codegen_asm_lines_after");
                if (v) baseCgLinesAfter = v;

                // Per-target codegen map (summary.codegen_targets). This
                // branch also runs for summary lines after the events array
                // closes, which is where codegen_targets lives.
                if (line.find("\"codegen_targets\"") != std::string::npos) {
                    // Writer emits {} inline when empty — nothing to parse.
                    if (line.find("{}") == std::string::npos) inTargets = true;
                } else if (inTargets) {
                    if (!inTargetObj && line.find("\": {") != std::string::npos) {
                        tev = CompareTarget();
                        tev.name = parseFirstQuoted(line);
                        if (!tev.name.empty()) inTargetObj = true;
                    } else if (inTargetObj) {
                        std::string t = trimWS(line);
                        if (t == "}," || t == "}") {
                            baseTargets.push_back(tev);
                            tev = CompareTarget();
                            inTargetObj = false;
                        } else {
                            int lv = parseJsonInt(line, "asm_lines_before");
                            if (line.find("\"asm_lines_before\"") != std::string::npos) tev.lines_before = lv;
                            lv = parseJsonInt(line, "asm_lines_after");
                            if (line.find("\"asm_lines_after\"") != std::string::npos) tev.lines_after = lv;
                            lv = parseJsonInt(line, "asm_bytes_before");
                            if (line.find("\"asm_bytes_before\"") != std::string::npos) tev.bytes_before = lv;
                            lv = parseJsonInt(line, "asm_bytes_after");
                            if (line.find("\"asm_bytes_after\"") != std::string::npos) tev.bytes_after = lv;
                            if (line.find("\"error\"") != std::string::npos) {
                                std::string es = parseJsonString(line, "error");
                                // null (no quotes) parses as "" — same as no error.
                                if (!es.empty() && es != "null") tev.error = es;
                            }
                        }
                    } else if (trimWS(line) == "},") {
                        inTargets = false;  // end of codegen_targets map
                    }
                }

                if (line.find("\"events\"") != std::string::npos) {
                    auto bp = line.find('[', line.find("\"events\""));
                    if (bp != std::string::npos) {
                        // Empty array on one line ("events": []) — no events
                        // to parse; stay out of events mode.
                        std::string rest = trimWS(line.substr(bp + 1));
                        if (rest.empty() || rest[0] != ']') inEvents = true;
                    }
                }
            } else {
                // Inside events array
                if (line.find("\"event_type\"") != std::string::npos) {
                    auto s = parseJsonString(line, "event_type");
                    if (!s.empty()) {
                        ev = CompareEvent();
                        ev.event_type = s;
                    }
                }
                if (line.find("\"id\"") != std::string::npos) {
                    ev.id = (unsigned)parseJsonInt(line, "id");
                }
                if (line.find("\"pass_name\"") != std::string::npos) {
                    auto s = parseJsonString(line, "pass_name");
                    if (!s.empty()) ev.pass_name = s;
                }
                if (line.find("\"has_changes\"") != std::string::npos) {
                    ev.has_changes = parseJsonBool(line, "has_changes");
                }
                if (line.find("\"ir_changed\"") != std::string::npos) {
                    ev.ir_changed = parseJsonBool(line, "ir_changed");
                }

                // Detect metrics_before / metrics_after blocks
                if (line.find("\"metrics_before\"") != std::string::npos) inMetricsBefore = true;
                if (line.find("\"metrics_after\"") != std::string::npos) { inMetricsAfter = true; inMetricsBefore = false; }

                // Per-key accumulation: each metrics line carries exactly one
                // key, so only assign fields whose key appears on this line.
                // (Assigning all fields per line would overwrite previously
                // parsed keys with zeros.)
                if (inMetricsBefore || inMetricsAfter) {
                    auto assignMetric = [&](const std::string &key, unsigned &field) {
                        if (line.find("\"" + key + "\"") != std::string::npos) {
                            int v = parseJsonInt(line, key);
                            field = (unsigned)(v < 0 ? 0 : v);
                        }
                    };
                    if (inMetricsBefore) {
                        assignMetric("instruction_count", ev.instr_before);
                        assignMetric("basic_block_count", ev.bb_before);
                        assignMetric("load_count", ev.load_before);
                        assignMetric("store_count", ev.store_before);
                        assignMetric("branch_count", ev.branch_before);
                        assignMetric("phi_count", ev.phi_before);
                    } else {
                        assignMetric("instruction_count", ev.instr_after);
                        assignMetric("basic_block_count", ev.bb_after);
                        assignMetric("load_count", ev.load_after);
                        assignMetric("store_count", ev.store_after);
                        assignMetric("branch_count", ev.branch_after);
                        assignMetric("phi_count", ev.phi_after);
                    }
                }

                // End of event object (getline strips '\n', so match trimmed
                // "}" for the final event and "}," for the rest).
                {
                    std::string t = trimWS(line);
                    if (t == "}," || t == "}") {
                        if (inMetricsBefore || inMetricsAfter) {
                            inMetricsBefore = false;
                            inMetricsAfter = false;
                        } else if (!ev.event_type.empty()) {
                            baseEvents.push_back(ev);
                            ev = CompareEvent();
                        }
                    }
                }

                // End of events array
                {
                    std::string t = trimWS(line);
                    if ((t == "]," || t == "]") && inEvents) {
                        // Flush a pending event in case the closing brace
                        // heuristic above missed it (e.g. minified JSON).
                        if (!ev.event_type.empty()) {
                            baseEvents.push_back(ev);
                            ev = CompareEvent();
                        }
                        inEvents = false;
                    }
                }
            }
        }
    }

    // --- Read and parse current ---
    std::ifstream currFile(currPath);
    if (!currFile.is_open()) {
        errs() << "ERROR: cannot open current file '" << currPath << "'\n";
        return 1;
    }

    std::string currPipeline, currModule;
    long long currInstrBefore = 0, currInstrAfter = 0;
    long long currBbBefore = 0, currBbAfter = 0;
    long long currCgLinesBefore = 0, currCgLinesAfter = 0;
    std::vector<CompareEvent> currEvents;
    std::vector<CompareTarget> currTargets;

    {
        bool inEvents = false;
        bool inMetricsBefore = false, inMetricsAfter = false;
        bool inTargets = false, inTargetObj = false;
        CompareEvent ev;
        CompareTarget tev;
        while (std::getline(currFile, line)) {
            if (!inEvents) {
                auto s = parseJsonString(line, "pipeline");
                if (!s.empty()) currPipeline = s;
                s = parseJsonString(line, "module_name");
                if (!s.empty()) currModule = s;
                auto v = parseJsonInt(line, "total_instructions_before");
                if (v) currInstrBefore = v;
                v = parseJsonInt(line, "total_instructions_after");
                if (v) currInstrAfter = v;
                v = parseJsonInt(line, "total_bbs_before");
                if (v) currBbBefore = v;
                v = parseJsonInt(line, "total_bbs_after");
                if (v) currBbAfter = v;
                v = parseJsonInt(line, "codegen_asm_lines_before");
                if (v) currCgLinesBefore = v;
                v = parseJsonInt(line, "codegen_asm_lines_after");
                if (v) currCgLinesAfter = v;

                // Per-target codegen map (see baseline parser above).
                if (line.find("\"codegen_targets\"") != std::string::npos) {
                    if (line.find("{}") == std::string::npos) inTargets = true;
                } else if (inTargets) {
                    if (!inTargetObj && line.find("\": {") != std::string::npos) {
                        tev = CompareTarget();
                        tev.name = parseFirstQuoted(line);
                        if (!tev.name.empty()) inTargetObj = true;
                    } else if (inTargetObj) {
                        std::string t = trimWS(line);
                        if (t == "}," || t == "}") {
                            currTargets.push_back(tev);
                            tev = CompareTarget();
                            inTargetObj = false;
                        } else {
                            int lv = parseJsonInt(line, "asm_lines_before");
                            if (line.find("\"asm_lines_before\"") != std::string::npos) tev.lines_before = lv;
                            lv = parseJsonInt(line, "asm_lines_after");
                            if (line.find("\"asm_lines_after\"") != std::string::npos) tev.lines_after = lv;
                            lv = parseJsonInt(line, "asm_bytes_before");
                            if (line.find("\"asm_bytes_before\"") != std::string::npos) tev.bytes_before = lv;
                            lv = parseJsonInt(line, "asm_bytes_after");
                            if (line.find("\"asm_bytes_after\"") != std::string::npos) tev.bytes_after = lv;
                            if (line.find("\"error\"") != std::string::npos) {
                                std::string es = parseJsonString(line, "error");
                                if (!es.empty() && es != "null") tev.error = es;
                            }
                        }
                    } else if (trimWS(line) == "},") {
                        inTargets = false;  // end of codegen_targets map
                    }
                }

                if (line.find("\"events\"") != std::string::npos) {
                    auto bp = line.find('[', line.find("\"events\""));
                    if (bp != std::string::npos) {
                        // Empty array on one line ("events": []) — no events
                        // to parse; stay out of events mode.
                        std::string rest = trimWS(line.substr(bp + 1));
                        if (rest.empty() || rest[0] != ']') inEvents = true;
                    }
                }
            } else {
                if (line.find("\"event_type\"") != std::string::npos) {
                    auto s = parseJsonString(line, "event_type");
                    if (!s.empty()) {
                        ev = CompareEvent();
                        ev.event_type = s;
                    }
                }
                if (line.find("\"id\"") != std::string::npos) {
                    ev.id = (unsigned)parseJsonInt(line, "id");
                }
                if (line.find("\"pass_name\"") != std::string::npos) {
                    auto s = parseJsonString(line, "pass_name");
                    if (!s.empty()) ev.pass_name = s;
                }
                if (line.find("\"has_changes\"") != std::string::npos) {
                    ev.has_changes = parseJsonBool(line, "has_changes");
                }
                if (line.find("\"ir_changed\"") != std::string::npos) {
                    ev.ir_changed = parseJsonBool(line, "ir_changed");
                }
                if (line.find("\"metrics_before\"") != std::string::npos) inMetricsBefore = true;
                if (line.find("\"metrics_after\"") != std::string::npos) { inMetricsAfter = true; inMetricsBefore = false; }
                // Per-key accumulation: each metrics line carries exactly one
                // key, so only assign fields whose key appears on this line.
                // (Assigning all fields per line would overwrite previously
                // parsed keys with zeros.)
                if (inMetricsBefore || inMetricsAfter) {
                    auto assignMetric = [&](const std::string &key, unsigned &field) {
                        if (line.find("\"" + key + "\"") != std::string::npos) {
                            int v = parseJsonInt(line, key);
                            field = (unsigned)(v < 0 ? 0 : v);
                        }
                    };
                    if (inMetricsBefore) {
                        assignMetric("instruction_count", ev.instr_before);
                        assignMetric("basic_block_count", ev.bb_before);
                        assignMetric("load_count", ev.load_before);
                        assignMetric("store_count", ev.store_before);
                        assignMetric("branch_count", ev.branch_before);
                        assignMetric("phi_count", ev.phi_before);
                    } else {
                        assignMetric("instruction_count", ev.instr_after);
                        assignMetric("basic_block_count", ev.bb_after);
                        assignMetric("load_count", ev.load_after);
                        assignMetric("store_count", ev.store_after);
                        assignMetric("branch_count", ev.branch_after);
                        assignMetric("phi_count", ev.phi_after);
                    }
                }
                {
                    std::string t = trimWS(line);
                    if (t == "}," || t == "}") {
                        if (inMetricsBefore || inMetricsAfter) {
                            inMetricsBefore = false;
                            inMetricsAfter = false;
                        } else if (!ev.event_type.empty()) {
                            currEvents.push_back(ev);
                            ev = CompareEvent();
                        }
                    }
                }
                {
                    std::string t = trimWS(line);
                    if ((t == "]," || t == "]") && inEvents) {
                        if (!ev.event_type.empty()) {
                            currEvents.push_back(ev);
                            ev = CompareEvent();
                        }
                        inEvents = false;
                    }
                }
            }
        }
    }

    // ============================================================
    // Compute comparison
    // ============================================================
    struct Finding {
        std::string severity;  // "high", "medium", "positive", "info"
        std::string type;
        std::string message;
        std::string pass_name;
    };

    std::vector<Finding> regressions;
    std::vector<Finding> improvements;

    // 1. Instruction count regression/improvement
    if (baseInstrAfter > 0) {
        long long ip = pctDelta(baseInstrAfter, currInstrAfter);
        if (ip > 5) {
            regressions.push_back({"high", "instruction_increase",
                "Instruction count increased by " + std::to_string(ip) + "% vs baseline ("
                + std::to_string(baseInstrAfter) + " -> " + std::to_string(currInstrAfter) + ")", ""});
        } else if (ip < -5) {
            improvements.push_back({"positive", "instruction_reduction",
                "Instruction count reduced by " + std::to_string(-ip) + "% vs baseline ("
                + std::to_string(baseInstrAfter) + " -> " + std::to_string(currInstrAfter) + ")", ""});
        }
    }

    // 2. Codegen regression/improvement
    if (baseCgLinesAfter > 0) {
        long long cp = pctDelta(baseCgLinesAfter, currCgLinesAfter);
        if (cp > 5) {
            regressions.push_back({"high", "codegen_regression",
                "Codegen assembly lines increased by " + std::to_string(cp) + "% vs baseline ("
                + std::to_string(baseCgLinesAfter) + " -> " + std::to_string(currCgLinesAfter) + ")", ""});
        } else if (cp < -5) {
            improvements.push_back({"positive", "codegen_improvement",
                "Codegen assembly lines reduced by " + std::to_string(-cp) + "% vs baseline ("
                + std::to_string(baseCgLinesAfter) + " -> " + std::to_string(currCgLinesAfter) + ")", ""});
        }
    }

    // 3. Pass-level comparison
    // Aggregate deltas per pass name from after-events with changes
    struct PassAgg {
        unsigned count = 0;
        long long instr_delta = 0, bb_delta = 0, load_delta = 0, store_delta = 0;
    };
    std::map<std::string, PassAgg> basePasses, currPasses;

    for (auto &e : baseEvents) {
        if (e.event_type == "after" && (e.has_changes || e.ir_changed)) {
            auto &a = basePasses[e.pass_name];
            a.count++;
            a.instr_delta += (long long)e.instr_after - (long long)e.instr_before;
            a.bb_delta += (long long)e.bb_after - (long long)e.bb_before;
            a.load_delta += (long long)e.load_after - (long long)e.load_before;
            a.store_delta += (long long)e.store_after - (long long)e.store_before;
        }
    }
    for (auto &e : currEvents) {
        if (e.event_type == "after" && (e.has_changes || e.ir_changed)) {
            auto &a = currPasses[e.pass_name];
            a.count++;
            a.instr_delta += (long long)e.instr_after - (long long)e.instr_before;
            a.bb_delta += (long long)e.bb_after - (long long)e.bb_before;
            a.load_delta += (long long)e.load_after - (long long)e.load_before;
            a.store_delta += (long long)e.store_after - (long long)e.store_before;
        }
    }

    // Collect all pass names
    std::set<std::string> allPassNames;
    for (auto &kv : basePasses) allPassNames.insert(kv.first);
    for (auto &kv : currPasses) allPassNames.insert(kv.first);

    // Detect new/removed passes
    std::vector<std::string> newPasses, removedPasses;
    for (auto &pn : allPassNames) {
        if (basePasses.find(pn) == basePasses.end() && currPasses.find(pn) != currPasses.end())
            newPasses.push_back(pn);
        else if (basePasses.find(pn) != basePasses.end() && currPasses.find(pn) == currPasses.end())
            removedPasses.push_back(pn);
    }

    for (auto &pn : allPassNames) {
        auto bp_it = basePasses.find(pn);
        auto cp_it = currPasses.find(pn);
        PassAgg bp = (bp_it != basePasses.end()) ? bp_it->second : PassAgg{};
        PassAgg cp = (cp_it != currPasses.end()) ? cp_it->second : PassAgg{};

        if (baseInstrAfter > 0) {
            long long idd = cp.instr_delta - bp.instr_delta;
            long long ip = (idd * 100) / baseInstrAfter;
            if (ip > 5) {
                regressions.push_back({"medium", "pass_regression",
                    pn + " instruction delta worsened by " + std::to_string(ip)
                    + "% (" + std::to_string(bp.instr_delta) + " -> " + std::to_string(cp.instr_delta) + ")",
                    pn});
            } else if (ip < -5) {
                improvements.push_back({"positive", "pass_improvement",
                    pn + " instruction delta improved by " + std::to_string(-ip)
                    + "% (" + std::to_string(bp.instr_delta) + " -> " + std::to_string(cp.instr_delta) + ")",
                    pn});
            }
        }

        // Load/store delta detection
        long long loadIdd = cp.load_delta - bp.load_delta;
        if (loadIdd > 5) {
            regressions.push_back({"medium", "pass_regression",
                pn + " load count delta increased by " + std::to_string(loadIdd), pn});
        } else if (loadIdd < -5) {
            improvements.push_back({"positive", "pass_improvement",
                pn + " load count delta decreased by " + std::to_string(-loadIdd), pn});
        }
        long long storeIdd = cp.store_delta - bp.store_delta;
        if (storeIdd > 5) {
            regressions.push_back({"medium", "pass_regression",
                pn + " store count delta increased by " + std::to_string(storeIdd), pn});
        } else if (storeIdd < -5) {
            improvements.push_back({"positive", "pass_improvement",
                pn + " store count delta decreased by " + std::to_string(-storeIdd), pn});
        }
    }

    // New/removed pass findings
    if (!newPasses.empty()) {
        std::string names;
        for (size_t i = 0; i < newPasses.size() && i < 5; i++) {
            if (i) names += ", ";
            names += newPasses[i];
        }
        if (newPasses.size() > 5) names += ", ...";
        regressions.push_back({"info", "new_passes",
            std::to_string(newPasses.size()) + " new pass(es) appeared in current run: " + names, ""});
    }
    if (!removedPasses.empty()) {
        std::string names;
        for (size_t i = 0; i < removedPasses.size() && i < 5; i++) {
            if (i) names += ", ";
            names += removedPasses[i];
        }
        if (removedPasses.size() > 5) names += ", ...";
        improvements.push_back({"info", "removed_passes",
            std::to_string(removedPasses.size()) + " pass(es) removed from current run: " + names, ""});
    }

    // 4. Per-target codegen comparison. Mirrors serve_dashboard.py bands:
    // compare optimization *reduction efficiency* per target; a >5% worse
    // reduction is a medium regression, >20% better is an improvement.
    // (Joined maps are also reused for the Summary printout below.)
    std::map<std::string, CompareTarget> btMap, ctMap;
    for (auto &t : baseTargets) btMap[t.name] = t;
    for (auto &t : currTargets) ctMap[t.name] = t;
    {
        std::set<std::string> allT;
        for (auto &kv : btMap) allT.insert(kv.first);
        for (auto &kv : ctMap) allT.insert(kv.first);
        for (auto &tn : allT) {
            auto bi = btMap.find(tn);
            auto ci = ctMap.find(tn);
            if (bi == btMap.end()) {
                std::string msg = "New codegen target in current run: " + tn;
                if (!ci->second.error.empty()) msg += " (failed: " + ci->second.error + ")";
                regressions.push_back({"info", "new_target", msg, ""});
                continue;
            }
            if (ci == ctMap.end()) {
                std::string msg = "Codegen target removed from current run: " + tn;
                if (!bi->second.error.empty()) msg += " (baseline had failed: " + bi->second.error + ")";
                improvements.push_back({"info", "removed_target", msg, ""});
                continue;
            }
            const CompareTarget &b = bi->second, &c = ci->second;
            if (!b.error.empty() || !c.error.empty()) {
                std::string msg = "Target " + tn + " codegen failed";
                if (!b.error.empty()) msg += " (baseline: " + b.error + ")";
                if (!c.error.empty()) msg += " (current: " + c.error + ")";
                regressions.push_back({"info", "target_error", msg, ""});
                continue;
            }
            long long bRed = b.lines_before - b.lines_after;
            long long cRed = c.lines_before - c.lines_after;
            if (bRed > 0) {
                long long tp = ((cRed - bRed) * 100) / bRed;
                if (tp < -5) {
                    regressions.push_back({"medium", "target_regression",
                        tn + " codegen regression: " + std::to_string(-tp) +
                        "% worse reduction than baseline (" + std::to_string(bRed) +
                        " -> " + std::to_string(cRed) + " lines saved)", ""});
                } else if (tp > 20) {
                    improvements.push_back({"positive", "target_improvement",
                        tn + " codegen improved: " + std::to_string(tp) +
                        "% better reduction than baseline (" + std::to_string(bRed) +
                        " -> " + std::to_string(cRed) + " lines saved)", ""});
                }
            }
        }
    }

    // ============================================================
    // Overall regression score (0-100)
    // ============================================================
    double score = 0;
    if (baseInstrAfter > 0) {
        long long ip = pctDelta(baseInstrAfter, currInstrAfter);
        score += std::max(0.0, std::min(100.0, 50.0 + ip)) * 0.4;
    }
    if (baseCgLinesAfter > 0) {
        long long cp = pctDelta(baseCgLinesAfter, currCgLinesAfter);
        score += std::max(0.0, std::min(100.0, 50.0 + cp)) * 0.3;
    }
    {
        int highReg = 0, medReg = 0, highImp = 0;
        for (auto &r : regressions) {
            if (r.severity == "high") highReg++;
            else if (r.severity == "medium") medReg++;
        }
        for (auto &i : improvements) {
            if (i.severity == "positive") highImp++;
        }
        score += std::min(30.0, highReg * 15.0 + medReg * 5.0) * 0.2;
        score -= std::min(20.0, highImp * 10.0) * 0.1;
    }
    int regressionScore = std::max(0, std::min(100, (int)score));

    // Sort: high severity first, then medium, then info/positive
    auto sevOrd = [](const std::string &s) -> int {
        if (s == "high") return 0;
        if (s == "medium") return 1;
        if (s == "info") return 2;
        if (s == "positive") return 0;
        return 3;
    };
    std::stable_sort(regressions.begin(), regressions.end(),
        [&](const Finding &a, const Finding &b) { return sevOrd(a.severity) < sevOrd(b.severity); });
    std::stable_sort(improvements.begin(), improvements.end(),
        [&](const Finding &a, const Finding &b) { return sevOrd(a.severity) < sevOrd(b.severity); });

    // ============================================================
    // Print report
    // ============================================================
    errs() << "\n";
    errs() << "=====================================================\n";
    errs() << "  LPTA Cross-Run Comparison\n";
    errs() << "=====================================================\n";
    errs() << "\n";
    errs() << "  Baseline: " << baseModule << " (" << basePipeline << ")\n";
    errs() << "  Current:  " << currModule << " (" << currPipeline << ")\n";
    errs() << "\n";

    errs() << "  Heuristic indicator: " << regressionScore << "/100";
    if (regressionScore <= 10) errs() << "  [IMPROVED]";
    else if (regressionScore <= 30) errs() << "  [MIXED]";
    else if (regressionScore <= 60) errs() << "  [MIXED]";
    else errs() << "  [REGRESSED]";
    errs() << "\n\n";

    errs() << "--- Summary ---\n";
    errs() << "  Instructions (before): " << baseInstrBefore << " -> " << currInstrBefore << "\n";
    errs() << "  Instructions (after):  " << baseInstrAfter << " -> " << currInstrAfter << "\n";
    if (baseInstrAfter > 0) {
        long long baseRed = baseInstrBefore - baseInstrAfter;
        long long currRed = currInstrBefore - currInstrAfter;
        long long redDelta = currRed - baseRed;
        errs() << "  Instruction reduction: " << baseRed << " -> " << currRed
               << " (delta: " << (redDelta >= 0 ? "+" : "") << redDelta << ")\n";
    }
    errs() << "  Codegen lines (after): " << baseCgLinesAfter << " -> " << currCgLinesAfter << "\n";
    if (!btMap.empty() || !ctMap.empty()) {
        errs() << "  Per-target codegen (lines after):\n";
        std::set<std::string> allT;
        for (auto &kv : btMap) allT.insert(kv.first);
        for (auto &kv : ctMap) allT.insert(kv.first);
        for (auto &tn : allT) {
            auto bi = btMap.find(tn), ci = ctMap.find(tn);
            std::string b = (bi == btMap.end()) ? "-" : std::to_string(bi->second.lines_after);
            std::string c = (ci == ctMap.end()) ? "-" : std::to_string(ci->second.lines_after);
            errs() << "    " << tn << ": " << b << " -> " << c << "\n";
        }
    }
    errs() << "\n";

    errs() << "--- Findings ---\n\n";

    if (regressions.empty() && improvements.empty()) {
        errs() << "  No significant differences found.\n\n";
    }

    if (!regressions.empty()) {
        errs() << "  Regressions (" << regressions.size() << "):\n";
        for (auto &r : regressions) {
            const char *icon = (r.severity == "high") ? "[HIGH]  " :
                               (r.severity == "medium") ? "[MED]   " : "[INFO]  ";
            errs() << "    " << icon << r.message << "\n";
            if (!r.pass_name.empty())
                errs() << "           Pass: " << r.pass_name << "\n";
        }
        errs() << "\n";
    }

    if (!improvements.empty()) {
        errs() << "  Improvements (" << improvements.size() << "):\n";
        for (auto &i : improvements) {
            const char *icon = (i.severity == "positive") ? "[+]     " : "[INFO]  ";
            errs() << "    " << icon << i.message << "\n";
            if (!i.pass_name.empty())
                errs() << "           Pass: " << i.pass_name << "\n";
        }
        errs() << "\n";
    }

    if (!newPasses.empty()) {
        errs() << "  New passes (" << newPasses.size() << "): ";
        for (size_t i = 0; i < newPasses.size(); i++) {
            if (i) errs() << ", ";
            errs() << newPasses[i];
        }
        errs() << "\n";
    }
    if (!removedPasses.empty()) {
        errs() << "  Removed passes (" << removedPasses.size() << "): ";
        for (size_t i = 0; i < removedPasses.size(); i++) {
            if (i) errs() << ", ";
            errs() << removedPasses[i];
        }
        errs() << "\n";
    }

    errs() << "\n=====================================================\n";
    errs() << "  Verdict: ";
    if (regressionScore <= 10) errs() << "Consistent improvements across instructions, codegen, and pass behavior.";
    else if (regressionScore <= 30) errs() << "Mostly improvements with minor variations.";
    else if (regressionScore <= 60) errs() << "Mixed results -- some areas improved, others regressed.";
    else errs() << "Significant regressions that may warrant investigation.";
    errs() << "\n=====================================================\n\n";

    return (regressionScore > 60) ? 1 : 0;
}

// ============================================================
// Main
// ============================================================

int main(int argc, char **argv) {
    if (argc < 2) {
        errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file] [--no-ir-hash] [--]\n       lpta_test --compare <base.json> <curr.json>\n";
        return 1;
    }

    // Check for --compare mode (standalone, no LLVM pipeline)
    {
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++) args.push_back(argv[i]);
        bool end_of_flags = false;
        for (size_t i = 0; i < args.size(); i++) {
            if (!end_of_flags && args[i] == "--") {
                end_of_flags = true;
                continue;
            }
            if (end_of_flags) continue;  // positional (e.g. a file named --compare)
            if (args[i] == "--version" || args[i] == "-version") {
                outs() << "lpta_test (LLVM " << LLVM_VERSION_STRING << ")\n";
                return 0;
            }
            if (args[i] == "--compare") {
                if (i + 2 >= args.size()) {
                    errs() << "ERROR: --compare requires two arguments: <base.json> <curr.json>\n";
                    return 1;
                }
                std::string base = args[i + 1];
                std::string curr = args[i + 2];
                return compareJsonFiles(base, curr);
            }
        }
    }

    // Parse CLI
    std::string input_file;
    bool output_dir_set = false;
    bool end_of_flags = false;
    auto isValidTriple = [](const std::string &t) -> bool {
        // Real target triples only contain alnum plus - _ . (e.g.
        // x86_64-pc-windows-msvc). Anything else is malformed or hostile
        // (shell metacharacters) — reject early with a clear error instead
        // of recording a confusing per-target llc failure.
        if (t.empty()) return false;
        for (unsigned char c : t) {
            if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.'))
                return false;
        }
        return true;
    };
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (!end_of_flags && arg == "--") {
            end_of_flags = true;
            continue;
        }
        if (!end_of_flags && arg == "--snapshots") {
            g_snapshots = true;
        } else if (!end_of_flags && arg == "--no-ir-hash") {
            g_no_ir_hash = true;
        } else if (!end_of_flags && arg.rfind("--targets=", 0) == 0) {
            std::string targets_arg = arg.substr(10); // skip "--targets="
            auto trimCopy = [](std::string s) -> std::string {
                auto first = s.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return "";
                auto last = s.find_last_not_of(" \t\r\n");
                return s.substr(first, last - first + 1);
            };
            if (targets_arg.empty()) {
                errs() << "ERROR: --targets requires a value (common|triple,...|@file)\n";
                return 1;
            }
            if (targets_arg == "common") {
                for (auto &t : COMMON_TARGETS) g_target_triples.push_back(t);
            } else if (targets_arg.rfind("@", 0) == 0) {
                // File-based: --targets=@targets.txt
                std::string filename = trimCopy(targets_arg.substr(1));
                if (filename.empty()) {
                    errs() << "ERROR: --targets=@file requires a filename\n";
                    return 1;
                }
                std::ifstream f(filename);
                if (!f) {
                    errs() << "ERROR: cannot open targets file '" << filename << "'\n";
                    return 1;
                }
                std::string line;
                bool first_line = true;
                while (std::getline(f, line)) {
                    // Strip a UTF-8 BOM if the file starts with one so the
                    // first triple is not silently poisoned.
                    if (first_line) {
                        first_line = false;
                        const char bom[] = {'\xEF', '\xBB', '\xBF'};
                        if (line.size() >= 3 && line.compare(0, 3, bom, 3) == 0)
                            line.erase(0, 3);
                    }
                    line = trimCopy(line);
                    if (!line.empty() && line[0] != '#') {
                        if (!isValidTriple(line)) {
                            errs() << "ERROR: invalid target triple '" << line
                                   << "' (allowed: letters, digits, '-', '_', '.')\n";
                            return 1;
                        }
                        g_target_triples.push_back(line);
                    }
                }
            } else {
                // Comma-separated list (also supports 'common' as an item to expand)
                std::stringstream ss(targets_arg);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trimCopy(item);
                    if (item.empty()) continue;
                    if (item == "common") {
                        for (auto &t : COMMON_TARGETS) g_target_triples.push_back(t);
                    } else {
                        if (!isValidTriple(item)) {
                            errs() << "ERROR: invalid target triple '" << item
                                   << "' (allowed: letters, digits, '-', '_', '.')\n";
                            return 1;
                        }
                        g_target_triples.push_back(item);
                    }
                }
            }
            // Deduplicate preserving first occurrence order
            {
                std::set<std::string> seen;
                std::vector<std::string> dedup;
                dedup.reserve(g_target_triples.size());
                for (auto &t : g_target_triples) {
                    if (seen.insert(t).second) dedup.push_back(t);
                }
                g_target_triples.swap(dedup);
            }
            if (g_target_triples.empty()) {
                errs() << "ERROR: --targets produced no valid targets\n";
                return 1;
            }
            // Each target costs 2 llc spawns; warn before a typo'd list
            // burns minutes (e.g. 500 targets = 1000 llc invocations).
            if (g_target_triples.size() > 16) {
                errs() << "  WARNING: " << g_target_triples.size()
                       << " codegen targets requested — each runs llc twice; "
                       << "expect a slow run\n";
            }
        } else if (!end_of_flags && (arg == "-O0" || arg == "--O0")) {
            g_opt = OptimizationLevel::O0; g_opt_level = "O0";
        } else if (!end_of_flags && (arg == "-O1" || arg == "--O1")) {
            g_opt = OptimizationLevel::O1; g_opt_level = "O1";
        } else if (!end_of_flags && (arg == "-O2" || arg == "--O2")) {
            g_opt = OptimizationLevel::O2; g_opt_level = "O2";
        } else if (!end_of_flags && (arg == "-O3" || arg == "--O3")) {
            g_opt = OptimizationLevel::O3; g_opt_level = "O3";
        } else if (!end_of_flags && (arg == "-Os" || arg == "--Os")) {
            g_opt = OptimizationLevel::Os; g_opt_level = "Os";
        } else if (!end_of_flags && (arg == "-Oz" || arg == "--Oz")) {
            g_opt = OptimizationLevel::Oz; g_opt_level = "Oz";
        } else if (!end_of_flags && !arg.empty() && arg[0] == '-') {
            errs() << "ERROR: unknown option '" << arg << "'\n";
            errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file] [--no-ir-hash] [--]\n       lpta_test --compare <base.json> <curr.json>\n";
            return 1;
        } else if (input_file.empty()) {
            input_file = arg;
        } else if (!output_dir_set) {
            g_output_dir = arg;
            output_dir_set = true;
        } else {
            errs() << "ERROR: unexpected argument '" << arg << "'\n";
            errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file] [--no-ir-hash] [--]\n       lpta_test --compare <base.json> <curr.json>\n";
            return 1;
        }
    }

    if (input_file.empty()) {
        errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file] [--no-ir-hash] [--]\n       lpta_test --compare <base.json> <curr.json>\n";
        return 1;
    }

    // NOTE: output dir is created only after the input parses (below), so
    // failed runs don't litter empty report directories.

    // Parse IR — empty .ll must succeed (AGENTS invariant: empty = exit 0)
    LLVMContext Context;
    SMDiagnostic Err;
    std::unique_ptr<Module> M;
    {
        std::error_code ec_fs;
        auto fsize = fs::file_size(input_file, ec_fs);
        if (!ec_fs && fsize == 0) {
            M = std::make_unique<Module>(input_file, Context);
        } else {
            M = parseIRFile(input_file, Err, Context);
            if (!M) {
                // Check whitespace-only file (e.g. "\n  \n") -> treat as empty.
                // Reads in bounded chunks so a huge whitespace file cannot
                // exhaust RAM by slurping it whole.
                std::ifstream cf(input_file, std::ios::binary);
                bool all_ws = false;
                if (cf) {
                    all_ws = true;
                    bool any = false;
                    char buf[8192];
                    while (cf && all_ws) {
                        cf.read(buf, sizeof(buf));
                        std::streamsize n = cf.gcount();
                        if (n == 0) break;
                        any = true;
                        for (std::streamsize k = 0; k < n; ++k) {
                            if (!std::isspace(static_cast<unsigned char>(buf[k]))) {
                                all_ws = false;
                                break;
                            }
                        }
                    }
                    all_ws = all_ws && any;
                }
                if (all_ws) {
                    M = std::make_unique<Module>(input_file, Context);
                } else {
                    Err.print(argv[0], errs());
                    return 1;
                }
            }
        }
    }
    // Use basename for determinism (M3 fix: absolute path would leak into history.json)
    g_module_name = fs::path(M->getName().str().empty() ? input_file : M->getName().str()).filename().string();
    if (g_module_name.empty()) g_module_name = fs::path(input_file).filename().string();

    // Input parsed OK — now create the output directory.
    {
        std::error_code ec;
        fs::create_directories(g_output_dir, ec);
        if (ec) {
            errs() << "ERROR: could not create output directory '" << g_output_dir
                   << "': " << ec.message() << "\n";
            return 1;
        }
    }

    // Remove stale per-run artifacts from previous runs reusing this
    // directory: per-pass snapshots (ir/), codegen assembly, and temp llc
    // scripts. Without this, a run with fewer snapshots/targets leaves
    // orphans behind that look like current output. history.json and the
    // before/after IR are overwritten below, so they need no cleanup.
    // Never sweep the current working directory itself (report_dir=.):
    // patterns like codegen_*.s could match user files there.
    {
        std::error_code ec_eq, ec_cwd;
        auto cwd = fs::current_path(ec_cwd);
        bool is_cwd = !ec_cwd && !ec_eq &&
                      fs::equivalent(g_output_dir, cwd, ec_eq);
        if (is_cwd) {
            errs() << "  NOTE: output is the working directory; skipping stale-file sweep\n";
        } else {
        auto ends_with = [](const std::string &s, const char *suf) {
            std::string su(suf);
            return s.size() >= su.size() &&
                   s.compare(s.size() - su.size(), su.size(), su) == 0;
        };
        std::error_code ec_iter;
        for (auto &entry : fs::directory_iterator(g_output_dir, ec_iter)) {
            if (ec_iter) break;
            std::error_code ec_e;
            if (entry.is_directory(ec_e)) {
                if (entry.path().filename() == "ir") {
                    std::error_code ec_rm;
                    fs::remove_all(entry.path(), ec_rm);
                }
            } else {
                std::string fn = entry.path().filename().string();
                bool is_codegen = fn.rfind("codegen_", 0) == 0 && ends_with(fn, ".s");
                bool is_bat = ends_with(fn, ".run_llc.bat") ||
                              (fn.rfind("run_llc_", 0) == 0 && ends_with(fn, ".bat"));
                if (is_codegen || is_bat) {
                    std::error_code ec_rm;
                    fs::remove(entry.path(), ec_rm);
                }
            }
        }
        }  // end else (not CWD): stale sweep
    }

    // ============================================================
    // Detect optnone functions
    // ============================================================
    for (Function &F : *M) {
        if (F.isDeclaration()) continue;
        if (F.hasFnAttribute(Attribute::OptimizeNone)) {
            g_optnone_functions.push_back(F.getName().str());
        }
    }
    g_optnone_detected = !g_optnone_functions.empty();

    if (g_optnone_detected) {
        errs() << "\n=== LPTA Optnone Detection ===\n";
        errs() << "Module contains " << g_optnone_functions.size()
               << " function(s) with 'optnone' attribute:\n";
        for (auto &name : g_optnone_functions)
            errs() << "  - " << name << "\n";
    }

    // Callbacks
    PassInstrumentationCallbacks PIC;
    unsigned event_num = 0;

    PIC.registerBeforeNonSkippedPassCallback(
        [&](StringRef PassID, Any IR) {
            IRDetection det = detectIR(IR);

            // Warn once per unique unsupported pass (e.g. CGSCC-level passes
            // operating on LazyCallGraph::SCC) instead of flooding stderr.
            if (det.kind == IRUnitKind::Unknown) {
                static std::set<std::string> warned_unknown;
                if (warned_unknown.insert(PassID.str()).second) {
                    errs() << "  WARNING: pass '" << PassID
                           << "' operates on an unsupported IR unit (CGSCC?); metrics will be zero\n";
                }
            }

            event_num++;
            unsigned current_event_id = event_num;

            PassFrame frame;
            frame.pass_name = PassID.str();
            frame.ir_ptr = irUnitPointer(IR);
            frame.ir_kind = det.kind;
            frame.ir_name = det.name;
            frame.depth = pass_stack.size();
            frame.before = det.metrics;
            frame.invalidated = false;
            frame.event_id = current_event_id;
            // IR-change hash: serialize once per pass; reuse the text for the
            // snapshot when this pass is allowlisted so hashing costs no
            // extra serialization there. --no-ir-hash skips serialization
            // entirely unless snapshots need the text (perf mode: counters
            // and pairing still work, ir_changed stays false).
            std::string snap;
            if (shouldSnapshot(PassID) || !g_no_ir_hash) snap = serializeIR(IR);
            frame.before_hash = g_no_ir_hash ? 0 : hashIRText(snap);
            // Only capture IR text if snapshots enabled for this pass.
            // Cap at kMaxSnapshotBytes per snapshot to avoid unbounded memory
            // growth on large modules. If BEFORE exceeds the cap we record
            // ir_before_dropped so AFTER drops the pair atomically (never
            // emit after-without-before).
            if (shouldSnapshot(PassID)) {
                if (snap.size() <= kMaxSnapshotBytes)
                    frame.ir_before = std::move(snap);
                else
                    frame.ir_before_dropped = true;
            }
            unsigned frame_depth = frame.depth;
            bool before_kept = !frame.ir_before.empty();
            pass_stack.push_back(std::move(frame));

            // Record event. Note: ir_before is intentionally NOT copied into
            // the before event — the frame on the stack holds the single copy,
            // and the matching AFTER event reuses it for JSON output.
            Event ev;
            ev.id = current_event_id;
            ev.event_type = "before";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(det.kind);
            ev.ir_name = det.name;
            ev.depth = frame_depth;
            ev.metrics_before = det.metrics;
            g_events.push_back(ev);

            // Console output
            errs() << "[" << current_event_id << "] BEFORE: " << PassID
                   << "  {" << irUnitKindName(det.kind) << " " << det.name << "}"
                   << "  depth=" << frame_depth << "\n";

            // IR snapshot (before) — gated by same 4 MiB cap as JSON (H5 fix)
            if (shouldSnapshot(PassID) && before_kept)
                saveIRSnapshot("before", IR, PassID.str(), current_event_id);
        });

    PIC.registerAfterPassCallback(
        [&](StringRef PassID, Any IR, const PreservedAnalyses &PA) {
            if (pass_stack.empty()) {
                errs() << "  WARNING: AFTER without matching BEFORE for " << PassID << "\n";
                return;
            }

            // Find matching frame by IR unit pointer (stable identity).
            // For Unknown (nullptr) we cannot rely on pointer, so match by
            // kind+name to avoid popping wrong IR unit (H1 fix).
            const void *ir_ptr = irUnitPointer(IR);
            IRDetection detEarly = detectIR(IR);
            auto it = pass_stack.rend();
            if (ir_ptr != nullptr) {
                it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                    [&](const PassFrame &f) { return f.pass_name == PassID.str() && f.ir_ptr == ir_ptr; });
                if (it == pass_stack.rend()) {
                    it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                        [&](const PassFrame &f) { return f.pass_name == PassID.str(); });
                    if (it != pass_stack.rend()) {
                        errs() << "  WARNING: AFTER '" << PassID
                               << "': exact IR-unit match missed; fell back to name-only match"
                               << " (possible sibling-unit mispair)\n";
                    }
                }
            } else {
                // Unknown — pointer is null, match by kind/name first
                it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                    [&](const PassFrame &f) {
                        return f.pass_name == PassID.str() && f.ir_ptr == nullptr &&
                               f.ir_kind == detEarly.kind && f.ir_name == detEarly.name;
                    });
                if (it == pass_stack.rend()) {
                    it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                        [&](const PassFrame &f) { return f.pass_name == PassID.str() && f.ir_ptr == nullptr; });
                }
                if (it == pass_stack.rend()) {
                    it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                        [&](const PassFrame &f) { return f.pass_name == PassID.str(); });
                }
            }

            if (it == pass_stack.rend()) {
                errs() << "  WARNING: AFTER callback: no matching BEFORE frame for " << PassID << "\n";
                return;
            }

            PassFrame frame = std::move(*it);
            unsigned current_event_id = frame.event_id;
            // Remove the matched frame
            pass_stack.erase(std::next(it).base());

            IRDetection det = detEarly;
            bool changes = hasAnyDelta(frame.before, det.metrics);
            // Real IR change: hash differs. Catches mutations invisible to
            // counters (operand/constant/attribute edits). Both-zero means
            // unhashable (Unknown unit) — never report a change there.
            // Serialize once: when snapshotting, hash the same text instead
            // of serializing a second time.
            std::string afterSnap;
            uint64_t after_hash;
            if (shouldSnapshot(PassID)) {
                afterSnap = serializeIR(IR);
                after_hash = g_no_ir_hash ? 0 : hashIRText(afterSnap);
            } else if (g_no_ir_hash) {
                after_hash = 0;
            } else {
                after_hash = hashIRUnit(IR);
            }
            bool ir_changed = (after_hash != frame.before_hash);
            // Snapshots/diffs key off either signal: a pass that rewrote IR
            // without moving counters still deserves its before/after text.
            bool changed_any = changes || ir_changed;

            // Record event (using same ID as BEFORE)
            Event ev;
            ev.id = current_event_id;
            ev.event_type = "after";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(det.kind);
            ev.ir_name = det.name;
            ev.depth = frame.depth;
            ev.metrics_before = frame.before;
            ev.metrics_after = det.metrics;
            ev.has_changes = changes;
            ev.ir_changed = ir_changed;
            ev.ir_before = std::move(frame.ir_before);
            bool before_dropped = frame.ir_before_dropped;
            // Snapshot size cap is decided jointly for the pair: keep
            // before/after only if BOTH sides fit under the cap, so the
            // dashboard never renders a one-sided diff. Also clean orphaned before file.
            bool both_fit = true;
            {
                // afterSnap was captured above when snapshotting; drop it
                // unless this pass actually changed something.
                if (!changed_any) afterSnap.clear();
                both_fit = !before_dropped &&
                           ev.ir_before.size() <= kMaxSnapshotBytes &&
                           afterSnap.size() <= kMaxSnapshotBytes;
                if (!both_fit) {
                    ev.ir_before.clear();
                    afterSnap.clear();
                    // Remove orphaned before file written in BEFORE stage (H5 fix)
                    std::string prefix = "pass_" + std::to_string(current_event_id) + "_";
                    std::string dir = g_output_dir + "/ir";
                    std::error_code ec_iter;
                    for (auto &entry : fs::directory_iterator(dir, ec_iter)) {
                        if (ec_iter) break;
                        std::string fname = entry.path().filename().string();
                        if (fname.rfind(prefix, 0) == 0 && fname.find("_before.ll") != std::string::npos) {
                            std::error_code ec_rm;
                            fs::remove(entry.path(), ec_rm);
                        }
                    }
                }
                ev.ir_after = std::move(afterSnap);
            }
            g_events.push_back(ev);

            // Console output
            errs() << "[" << current_event_id << "] AFTER:  " << PassID
                   << "  {" << irUnitKindName(det.kind) << " " << det.name << "}";

            if (changes) {
                errs() << "\n";
                printDeltaLine("instructions", det.metrics.instruction_count, frame.before.instruction_count);
                printDeltaLine("basic_blocks", det.metrics.basic_block_count, frame.before.basic_block_count);
                printDeltaLine("functions", det.metrics.function_count, frame.before.function_count);
                printDeltaLine("globals", det.metrics.global_count, frame.before.global_count);
                printDeltaLine("calls", det.metrics.call_count, frame.before.call_count);
                printDeltaLine("loads", det.metrics.load_count, frame.before.load_count);
                printDeltaLine("stores", det.metrics.store_count, frame.before.store_count);
                printDeltaLine("branches", det.metrics.branch_count, frame.before.branch_count);
                printDeltaLine("phis", det.metrics.phi_count, frame.before.phi_count);
                printDeltaLine("returns", det.metrics.return_count, frame.before.return_count);
                printDeltaLine("op_arith", det.metrics.op_arith, frame.before.op_arith);
                printDeltaLine("op_cmp", det.metrics.op_cmp, frame.before.op_cmp);
                printDeltaLine("op_memory", det.metrics.op_memory, frame.before.op_memory);
                printDeltaLine("op_control", det.metrics.op_control, frame.before.op_control);
                printDeltaLine("op_cast", det.metrics.op_cast, frame.before.op_cast);
                printDeltaLine("op_call", det.metrics.op_call, frame.before.op_call);
                printDeltaLine("op_vector", det.metrics.op_vector, frame.before.op_vector);
                printDeltaLine("op_other", det.metrics.op_other, frame.before.op_other);
                if (ir_changed && !changes)
                    errs() << "      (IR text changed; all counters equal)\n";
            } else if (ir_changed) {
                errs() << "  (IR changed, metrics unchanged)\n";
            } else {
                errs() << "  (no change)\n";
            }

            // IR snapshot (after) — gated by same joint cap as JSON
            if (shouldSnapshot(PassID) && changed_any && both_fit)
                saveIRSnapshot("after", IR, PassID.str(), current_event_id);
        });

    PIC.registerAfterPassInvalidatedCallback(
        [&](StringRef PassID, const PreservedAnalyses &PA) {
            if (pass_stack.empty()) {
                errs() << "  WARNING: INVALIDATED without matching BEFORE for " << PassID << "\n";
                return;
            }

            // INVALIDATED has no IR argument — must match by name only (LIFO).
            // Stack discipline requires popping the most-recent matching name;
            // heuristic scoring without IR is unsound and was removed (H1 fix).
            auto it = std::find_if(pass_stack.rbegin(), pass_stack.rend(),
                [&](const PassFrame &f) { return f.pass_name == PassID.str(); });

            if (it == pass_stack.rend()) {
                errs() << "  WARNING: INVALIDATED callback: no matching BEFORE frame for " << PassID << "\n";
                return;
            }

            // Check for ambiguity — LIFO assumption documented in DATA_FLOW.md
            auto name_matches = std::count_if(pass_stack.begin(), pass_stack.end(),
                [&](const PassFrame &f) { return f.pass_name == PassID.str(); });
            if (name_matches > 1) {
                errs() << "  WARNING: INVALIDATED: " << name_matches
                       << " frames named '" << PassID
                       << "' on stack; popping topmost (LIFO assumption)\n";
            }

            PassFrame frame = std::move(*it);
            unsigned current_event_id = frame.event_id;
            pass_stack.erase(std::next(it).base());
            frame.invalidated = true;

            Event ev;
            ev.id = current_event_id;
            ev.event_type = "invalidated";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(frame.ir_kind);
            ev.ir_name = frame.ir_name;
            ev.depth = frame.depth;
            ev.metrics_before = frame.before;
            g_events.push_back(ev);

            errs() << "[" << current_event_id << "] INVALIDATED: " << PassID
                   << "  {" << irUnitKindName(frame.ir_kind) << " " << frame.ir_name << "}"
                   << "  (no after-state available)\n";
        });

// ============================================================
    // Initialize native target so TargetRegistry::lookupTarget works.
    // Uses LLVM_NATIVE_TARGET macros which resolve to the linked
    // target backend (e.g. x86) at compile time.
    // ============================================================
    LLVM_NATIVE_TARGET();
    LLVM_NATIVE_TARGETINFO();
    LLVM_NATIVE_TARGETMC();
    LLVM_NATIVE_ASMPARSER();
    LLVM_NATIVE_ASMPRINTER();

    // ============================================================
    // Initialize additional targets for cross-target codegen via llc/TargetRegistry.
    // Only initialize targets that are linked into this build (see CMakeLists.txt).
    // ============================================================
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();

    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
    LLVMInitializeRISCVAsmPrinter();

    // ============================================================
    // Create TargetMachine
    // ============================================================
    Triple TheTriple(M->getTargetTriple());
    if (TheTriple.getTriple().empty())
        TheTriple = Triple(sys::getDefaultTargetTriple());

    std::string errMsg;
    const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, errMsg);
    std::unique_ptr<TargetMachine> TM;
    if (TheTarget) {
        TargetOptions TO;
        TM.reset(TheTarget->createTargetMachine(
            TheTriple, /*CPU=*/"", /*Features=*/"", TO,
            std::nullopt));
    } else {
        errs() << "  WARNING: Target '" << TheTriple.getTriple()
               << "' not supported by this build: " << errMsg
               << "; target-specific codegen features disabled\n";
    }

    // ============================================================
    // Handle optnone: always strip before running the pipeline
    // ============================================================
    // LLVM's new pass manager only partially respects 'optnone':
    // the CGSCC-level inliner pipeline checks it, but the early
    // ModuleToFunctionPassAdaptor (SROA, EarlyCSE, SimplifyCFG,
    // InstCombine, etc.) does NOT. This means optnone functions
    // get partially optimized regardless, producing inconsistent
    // results that match neither 'opt -O2' nor 'opt -O0'.
    //
    // To make LPTA's behavior explicit and reproducible, we always
    // strip optnone and run the full pipeline on all functions.
    // This shows the complete optimization potential of each pass.
    if (g_optnone_detected) {
        errs() << "\n=== LPTA Optnone Handling ===\n";
        errs() << "Stripping 'optnone' from " << g_optnone_functions.size()
               << " function(s) before running the pipeline.\n";
        errs() << "LPTA always runs the full pipeline to show what each\n";
        errs() << "pass CAN do. For optnone-aware comparison, recompile\n";
        errs() << "with: clang -O2 -emit-llvm -S -o input.ll file.c\n";
        errs() << "===========================\n\n";
        for (Function &F : *M) {
            if (F.isDeclaration()) continue;
            if (F.hasFnAttribute(Attribute::OptimizeNone)) {
                F.removeFnAttr(Attribute::OptimizeNone);
            }
        }
    }

    // PassBuilder + Analysis Managers
    PassBuilder PB(TM.get(), PipelineTuningOptions(), std::nullopt, &PIC);

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Build pipeline
    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(g_opt);

    errs() << "=== LPTA Pass Instrumentation ===\n";
    errs() << "Module: " << M->getName() << "\n";
    errs() << "Pipeline: -" << g_opt_level << "\n";
    errs() << "Output: " << g_output_dir << "\n";
    if (g_no_ir_hash)
        errs() << "IR-hash detection: OFF (--no-ir-hash perf mode; ir_changed stays false)\n";
    errs() << "=================================\n\n";

    // Save initial IR before optimization (secondary artifact: warn, don't
    // fail — history.json remains the fatal artifact, checked at the end).
    std::string ir_before_path = g_output_dir + "/ir_before_opt.ll";
    {
        std::error_code EC;
        raw_fd_ostream file(ir_before_path, EC);
        if (EC) {
            errs() << "  WARNING: could not write '" << ir_before_path
                   << "': " << EC.message() << "\n";
        } else {
            M->print(file, nullptr);
            file.flush();
            if (file.has_error())
                errs() << "  WARNING: I/O failure writing '" << ir_before_path << "'\n";
        }
    }

    MPM.run(*M, MAM);

    // Save final IR after optimization (same warning-only policy).
    std::string ir_after_path = g_output_dir + "/ir_after_opt.ll";
    {
        std::error_code EC;
        raw_fd_ostream file(ir_after_path, EC);
        if (EC) {
            errs() << "  WARNING: could not write '" << ir_after_path
                   << "': " << EC.message() << "\n";
        } else {
            M->print(file, nullptr);
            file.flush();
            if (file.has_error())
                errs() << "  WARNING: I/O failure writing '" << ir_after_path << "'\n";
        }
    }

    // Measure codegen impact
    errs() << "Measuring codegen impact...\n";
    MultiTargetConfig mt_config;
    mt_config.targets = g_target_triples;
    CodegenResult cg;
    if (g_target_triples.empty()) {
        cg = measureCodegen(ir_before_path, ir_after_path, g_output_dir);
    } else {
        cg = measureCodegenMultiTarget(ir_before_path, ir_after_path, g_output_dir, mt_config);
    }

    // Summary
    errs() << "\n=================================\n";
    errs() << "LPTA Summary\n";
    errs() << "  Pass executions (unique IDs): " << event_num << "\n";
    errs() << "  Total recorded events: " << g_events.size() << "\n";
    errs() << "  Stack remaining: " << pass_stack.size();
    bool stack_unbalanced = !pass_stack.empty();
    if (stack_unbalanced) errs() << " (WARNING: stack not empty!)";
    errs() << "\n";

    // Count passes with changes (metric counters) and with any IR change
    // (either flag — matches the dashboard "Changed" filter).
    unsigned changed = 0, ir_changed_ct = 0, snap_kept = 0;
    for (auto &e : g_events) {
        if (e.event_type != "after") continue;
        if (e.has_changes) changed++;
        if (e.has_changes || e.ir_changed) ir_changed_ct++;
        if (!e.ir_before.empty()) snap_kept++;
    }
    errs() << "  Passes with changes: " << changed << " (metrics) / "
           << ir_changed_ct << " (any IR change)\n";
    if (g_snapshots && changed > 0 && snap_kept == 0) {
        errs() << "  NOTE: --snapshots enabled but no IR snapshots were kept:\n"
               << "        no allowlisted pass changed the IR (or every pair exceeded the "
               << kMaxSnapshotBytes / (1024 * 1024) << " MiB cap)\n";
    }
    errs() << "\n";
    errs() << "  Codegen (assembly):\n";
    errs() << "    Before: " << cg.asm_lines_before << " lines (" << cg.asm_size_before << " bytes)\n";
    errs() << "    After:  " << cg.asm_lines_after << " lines (" << cg.asm_size_after << " bytes)\n";
    if (cg.asm_lines_before > 0) {
        long long red = static_cast<long long>(cg.asm_lines_before)
                      - static_cast<long long>(cg.asm_lines_after);
        long long pct = (red * 100) / cg.asm_lines_before;
        errs() << "    Reduction: " << red << " lines (" << pct << "% )\n";
    }
    errs() << "=================================\n";

    // Write JSON output (pass codegen result). A run without history.json
    // is a failed run, even if the pipeline itself succeeded.
    bool wrote_json = writeHistoryJSON(g_output_dir + "/history.json", cg);

    if (!wrote_json) return 1;
    return stack_unbalanced ? 1 : 0;
}
