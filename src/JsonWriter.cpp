#include "JsonWriter.h"

#include "Config.h"
#include "Tracker.h"

#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <set>

using namespace llvm;

// ============================================================
// JSON Serialization
// ============================================================

// Formal verification: jsonEscape produces a valid JSON string fragment.
// Postcondition: result contains no unescaped control chars or special JSON chars.
std::string jsonEscape(const std::string &s) {
    std::string r;
    r.reserve(s.size() * 2);
    for (unsigned char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '/': r += "\\/"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        case '\b': r += "\\b"; break;
        case '\f': r += "\\f"; break;
        default:
            if (c < 0x20) {
                // Control characters: \uXXXX
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                r += buf;
            } else {
                r += c;
            }
        }
    }
    return r;
}

void writeMetricsJSON(std::ostream &os, const IRMetrics &m, const std::string &pad) {
    os << pad << "\"instruction_count\": " << m.instruction_count << ",\n"
       << pad << "\"basic_block_count\": " << m.basic_block_count << ",\n"
       << pad << "\"function_count\": " << m.function_count << ",\n"
       << pad << "\"global_count\": " << m.global_count << ",\n"
       << pad << "\"call_count\": " << m.call_count << ",\n"
       << pad << "\"load_count\": " << m.load_count << ",\n"
       << pad << "\"store_count\": " << m.store_count << ",\n"
       << pad << "\"branch_count\": " << m.branch_count << ",\n"
       << pad << "\"phi_count\": " << m.phi_count << ",\n"
       << pad << "\"return_count\": " << m.return_count << "\n";
}

void writeHistoryJSON(const std::string &filename, const CodegenResult &cg) {
    std::ofstream f(filename);
    if (!f.is_open()) {
        errs() << "ERROR: could not open " << filename << "\n";
        return;
    }

    // Summary stats
    unsigned before_count = 0, after_count = 0, invalidated_count = 0;
    unsigned passes_with_changes = 0;
    std::set<std::string> unique_passes;
    for (auto &e : g_events) {
        if (e.event_type == "before") before_count++;
        else if (e.event_type == "after") after_count++;
        else invalidated_count++;
        if (e.event_type == "after" && e.has_changes) passes_with_changes++;
        unique_passes.insert(e.pass_name);
    }

    f << "{\n";
    f << "  \"module_name\": \"" << jsonEscape(g_module_name) << "\",\n";
    f << "  \"pipeline\": \"" << g_opt_level << "\",\n";

    // Events array
    f << "  \"events\": [\n";
    for (size_t i = 0; i < g_events.size(); i++) {
        auto &e = g_events[i];
        f << "    {\n";
        f << "      \"id\": " << e.id << ",\n";
        f << "      \"event_type\": \"" << e.event_type << "\",\n";
        f << "      \"pass_name\": \"" << jsonEscape(e.pass_name) << "\",\n";
        f << "      \"pass_type\": \"" << e.pass_type << "\",\n";
        f << "      \"ir_kind\": \"" << e.ir_kind << "\",\n";
        f << "      \"ir_name\": \"" << jsonEscape(e.ir_name) << "\",\n";
        f << "      \"depth\": " << e.depth;

        if (e.event_type == "before") {
            f << ",\n      \"metrics\": {\n";
            writeMetricsJSON(f, e.metrics_before, "        ");
            f << "      }\n";
        } else if (e.event_type == "after") {
            f << ",\n      \"metrics_before\": {\n";
            writeMetricsJSON(f, e.metrics_before, "        ");
            f << "      },\n      \"metrics_after\": {\n";
            writeMetricsJSON(f, e.metrics_after, "        ");
            f << "      },\n";
            f << "      \"has_changes\": " << (e.has_changes ? "true" : "false") << ",\n";
            if (e.has_changes && !e.ir_before.empty()) {
                f << "      \"ir_before\": \"" << jsonEscape(e.ir_before) << "\",\n";
                f << "      \"ir_after\": \"" << jsonEscape(e.ir_after) << "\"\n";
            } else {
                f << "      \"ir_before\": null,\n";
                f << "      \"ir_after\": null\n";
            }
        } else {
            f << ",\n      \"metrics\": {\n";
            writeMetricsJSON(f, e.metrics_before, "        ");
            f << "      },\n";
            f << "      \"invalidated\": true\n";
        }

        f << "    }";
        if (i < g_events.size() - 1) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // Summary
    f << "  \"summary\": {\n";
    f << "    \"total_events\": " << g_events.size() << ",\n";
    f << "    \"total_before\": " << before_count << ",\n";
    f << "    \"total_after\": " << after_count << ",\n";
    f << "    \"total_invalidated\": " << invalidated_count << ",\n";
    f << "    \"passes_with_changes\": " << passes_with_changes << ",\n";
    f << "    \"unique_pass_names\": " << unique_passes.size() << ",\n";
    // Pipeline summary: total instruction reduction. Keys are ALWAYS emitted
    // (zeros when unavailable) so the schema is stable even for runs that
    // produced no events (e.g. empty input modules).
    IRMetrics first_mod, last_mod;
    bool found_first = false, found_last = false;
    if (!g_events.empty()) {
        // 1. Prefer first Module-level event for initial state
        for (auto &e : g_events) {
            if (e.ir_kind == "Module") {
                first_mod = e.metrics_before;
                found_first = true;
                break;
            }
        }
        if (!found_first) {
            first_mod = g_events.front().metrics_before;
            found_first = true;
        }

        // 2. Prefer last Module-level after event for final state
        for (auto it = g_events.rbegin(); it != g_events.rend(); ++it) {
            if (it->ir_kind == "Module" && it->event_type == "after") {
                last_mod = it->metrics_after;
                found_last = true;
                break;
            }
        }
        if (!found_last) {
            for (auto it = g_events.rbegin(); it != g_events.rend(); ++it) {
                if (it->event_type == "after") {
                    last_mod = it->metrics_after;
                    found_last = true;
                    break;
                }
            }
        }
    }
    if (found_first && found_last) {
        f << "    \"total_instructions_before\": " << first_mod.instruction_count << ",\n";
        f << "    \"total_instructions_after\": " << last_mod.instruction_count << ",\n";
        f << "    \"total_bbs_before\": " << first_mod.basic_block_count << ",\n";
        f << "    \"total_bbs_after\": " << last_mod.basic_block_count << ",\n";
    } else {
        f << "    \"total_instructions_before\": 0,\n";
        f << "    \"total_instructions_after\": 0,\n";
        f << "    \"total_bbs_before\": 0,\n";
        f << "    \"total_bbs_after\": 0,\n";
    }
    // Codegen data
    f << "    \"codegen_asm_lines_before\": " << cg.asm_lines_before << ",\n";
    f << "    \"codegen_asm_lines_after\": " << cg.asm_lines_after << ",\n";
    f << "    \"codegen_asm_bytes_before\": " << cg.asm_size_before << ",\n";
    f << "    \"codegen_asm_bytes_after\": " << cg.asm_size_after << ",\n";
    // Optnone data
    f << "    \"optnone_detected\": " << (g_optnone_detected ? "true" : "false") << ",\n";
    f << "    \"optnone_function_count\": " << g_optnone_functions.size() << ",\n";
    f << "    \"optnone_functions\": [";
    for (size_t i = 0; i < g_optnone_functions.size(); i++) {
        if (i > 0) f << ", ";
        f << "\"" << jsonEscape(g_optnone_functions[i]) << "\"";
    }
    f << "],\n";
    f << "    \"optnone_warning\": \"";
    if (g_optnone_detected) {
        f << jsonEscape(
            std::to_string(g_optnone_functions.size()) +
            " function(s) with optnone were not optimized. "
            "Recompile with -O2 to see optimization effects.");
    }
    f << "\"\n";
    f << "  }\n";
    f << "}\n";

    f.flush();
    if (!f.good()) {
        errs() << "ERROR: I/O failure while writing " << filename
               << " (disk full?)\n";
        return;
    }
    f.close();
    errs() << "Wrote " << filename << " (" << g_events.size() << " events)\n";
}
