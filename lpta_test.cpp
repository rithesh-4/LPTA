#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IRReader/IRReader.h"

#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"

#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

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

#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;
namespace fs = std::filesystem;

// ============================================================
// Globals
// ============================================================

static std::string g_output_dir = "report";
static bool g_snapshots = false;
static std::string g_module_name;
static std::string g_opt_level = "O2";
static OptimizationLevel g_opt = OptimizationLevel::O2;

// ============================================================
// IR Metrics
// ============================================================

struct IRMetrics {
    unsigned instruction_count = 0;
    unsigned basic_block_count = 0;
    unsigned function_count = 0;
    unsigned global_count = 0;
    unsigned call_count = 0;
    unsigned load_count = 0;
    unsigned store_count = 0;
    unsigned branch_count = 0;
    unsigned phi_count = 0;
    unsigned return_count = 0;
};

static IRMetrics captureModuleMetrics(const Module &M) {
    IRMetrics m;
    for (auto &F : M) {
        m.function_count++;
        for (auto &BB : F) {
            m.basic_block_count++;
            for (auto &I : BB) {
                m.instruction_count++;
                switch (I.getOpcode()) {
                case Instruction::Call: m.call_count++; break;
                case Instruction::Load: m.load_count++; break;
                case Instruction::Store: m.store_count++; break;
                case Instruction::Br: m.branch_count++; break;
                case Instruction::PHI: m.phi_count++; break;
                case Instruction::Ret: m.return_count++; break;
                default: break;
                }
            }
        }
    }
    m.global_count = M.global_size();
    return m;
}

static IRMetrics captureFunctionMetrics(const Function &F) {
    IRMetrics m;
    m.function_count = 1;
    for (auto &BB : F) {
        m.basic_block_count++;
        for (auto &I : BB) {
            m.instruction_count++;
            switch (I.getOpcode()) {
            case Instruction::Call: m.call_count++; break;
            case Instruction::Load: m.load_count++; break;
            case Instruction::Store: m.store_count++; break;
            case Instruction::Br: m.branch_count++; break;
            case Instruction::PHI: m.phi_count++; break;
            case Instruction::Ret: m.return_count++; break;
            default: break;
            }
        }
    }
    return m;
}

static IRMetrics captureLoopMetrics(const Loop &L) {
    IRMetrics m;
    for (auto *BB : L.blocks()) {
        m.basic_block_count++;
        for (auto &I : *BB) {
            m.instruction_count++;
            switch (I.getOpcode()) {
            case Instruction::Call: m.call_count++; break;
            case Instruction::Load: m.load_count++; break;
            case Instruction::Store: m.store_count++; break;
            case Instruction::Br: m.branch_count++; break;
            case Instruction::PHI: m.phi_count++; break;
            case Instruction::Ret: m.return_count++; break;
            default: break;
            }
        }
    }
    return m;
}

// ============================================================
// IR Unit Detection
// ============================================================

enum class IRUnitKind { Module, Function, Loop, Unknown };

static const char *irUnitKindName(IRUnitKind K) {
    switch (K) {
    case IRUnitKind::Module: return "Module";
    case IRUnitKind::Function: return "Function";
    case IRUnitKind::Loop: return "Loop";
    case IRUnitKind::Unknown: return "Unknown";
    }
    return "Unknown";
}

struct IRDetection {
    IRUnitKind kind = IRUnitKind::Unknown;
    std::string name;
    IRMetrics metrics;
};

static IRDetection detectIR(const Any &IR) {
    IRDetection det;
    if (auto *Mod = any_cast<const Module *>(&IR)) {
        det.kind = IRUnitKind::Module;
        det.name = (*Mod)->getName().str();
        det.metrics = captureModuleMetrics(**Mod);
    } else if (auto *F = any_cast<const Function *>(&IR)) {
        det.kind = IRUnitKind::Function;
        det.name = (*F)->getName().str();
        det.metrics = captureFunctionMetrics(**F);
    } else if (auto *L = any_cast<const Loop *>(&IR)) {
        det.kind = IRUnitKind::Loop;
        if (auto *Header = (*L)->getHeader())
            det.name = Header->getName().str();
        det.metrics = captureLoopMetrics(**L);
    }
    return det;
}

// ============================================================
// Pass Frame Stack
// ============================================================

struct PassFrame {
    std::string pass_name;
    IRUnitKind ir_kind = IRUnitKind::Unknown;
    std::string ir_name;
    unsigned depth;
    IRMetrics before;
    bool invalidated = false;
    std::string ir_before;  // IR text captured at BEFORE
};

static std::vector<PassFrame> pass_stack;

// ============================================================
// Structured Events
// ============================================================

struct Event {
    unsigned id;
    std::string event_type;  // "before", "after", "invalidated"
    std::string pass_name;
    std::string pass_type;   // "adaptor", "pipeline", "transformation", "analysis", "other"
    std::string ir_kind;
    std::string ir_name;
    unsigned depth;
    IRMetrics metrics_before;
    IRMetrics metrics_after;
    bool has_changes = false;
    std::string ir_before;  // IR text snapshot (before state)
    std::string ir_after;   // IR text snapshot (after state)
};

// Serialize IR unit to string for snapshots
static std::string serializeIR(const Any &IR) {
    std::string buf;
    raw_string_ostream os(buf);
    if (auto *Mod = any_cast<const Module *>(&IR)) {
        (*Mod)->print(os, nullptr);
    } else if (auto *F = any_cast<const Function *>(&IR)) {
        (*F)->print(os);
    } else if (auto *L = any_cast<const Loop *>(&IR)) {
        // For loops, print the function containing the loop
        if (auto *Header = (*L)->getHeader())
            Header->getParent()->print(os);
    }
    return buf;
}

static std::vector<Event> g_events;

// ============================================================
// Pass Classification
// ============================================================

static std::string classifyPass(StringRef name) {
    std::string n = name.str();
    if (n.find("Adaptor") != std::string::npos)
        return "adaptor";
    if (n.find("PassManager") != std::string::npos)
        return "pipeline";
    if (n.find("ExtraLoopPassManager") != std::string::npos)
        return "pipeline";
    if (n.find("Analysis") != std::string::npos ||
        n.find("RequireAnalysis") != std::string::npos ||
        n.find("InvalidateAnalysis") != std::string::npos)
        return "analysis";
    return "transformation";
}

// ============================================================
// Delta helpers
// ============================================================

static bool hasAnyDelta(const IRMetrics &a, const IRMetrics &b) {
    return a.instruction_count != b.instruction_count ||
           a.basic_block_count != b.basic_block_count ||
           a.function_count != b.function_count ||
           a.global_count != b.global_count ||
           a.call_count != b.call_count ||
           a.load_count != b.load_count ||
           a.store_count != b.store_count ||
           a.branch_count != b.branch_count ||
           a.phi_count != b.phi_count ||
           a.return_count != b.return_count;
}

static void printDeltaLine(const char *label, int after, int before) {
    int delta = after - before;
    if (delta == 0) return;
    errs() << "      " << label << ": " << before << " -> " << after
           << " (" << (delta > 0 ? "+" : "") << delta << ")\n";
}

// ============================================================
// JSON Serialization
// ============================================================

static std::string jsonEscape(const std::string &s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default: r += c;
        }
    }
    return r;
}

static void writeMetricsJSON(std::ostream &os, const IRMetrics &m, const std::string &pad) {
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

struct CodegenResult {
    unsigned asm_lines_before = 0;
    unsigned asm_lines_after = 0;
    unsigned asm_size_before = 0;
    unsigned asm_size_after = 0;
};

static void writeHistoryJSON(const std::string &filename, const CodegenResult &cg = CodegenResult()) {
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
    // Pipeline summary: total instruction reduction
    if (!g_events.empty()) {
        // Find first and last module-level after event for total metrics
        IRMetrics first_mod, last_mod;
        bool found_first = false;
        for (auto &e : g_events) {
            if (e.ir_kind == "Module" && e.event_type == "after") {
                if (!found_first) { first_mod = e.metrics_before; found_first = true; }
                last_mod = e.metrics_after;
            }
        }
        if (found_first) {
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
    }
    // Codegen data
    f << "    \"codegen_asm_lines_before\": " << cg.asm_lines_before << ",\n";
    f << "    \"codegen_asm_lines_after\": " << cg.asm_lines_after << ",\n";
    f << "    \"codegen_asm_bytes_before\": " << cg.asm_size_before << ",\n";
    f << "    \"codegen_asm_bytes_after\": " << cg.asm_size_after << "\n";
    f << "  }\n";
    f << "}\n";

    f.close();
    errs() << "Wrote " << filename << " (" << g_events.size() << " events)\n";
}

// ============================================================
// IR Snapshots
// ============================================================

static const std::set<std::string> g_snapshot_allowlist = {
    "InstCombinePass", "SimplifyCFGPass", "GVNPass", "LICMPass",
    "SROAPass", "EarlyCSEPass", "DSEPass", "SCCPPass",
    "LoopUnrollPass", "InlinerPass", "GlobalOptPass", "GlobalDCEPass",
};

static bool shouldSnapshot(StringRef pass_name) {
    return g_snapshots && g_snapshot_allowlist.count(pass_name.str());
}

static void saveIRSnapshot(const std::string &suffix, const Any &IR,
                           const std::string &pass_name, unsigned event_id) {
    std::string dir = g_output_dir + "/ir";
    fs::create_directories(dir);

    if (auto *Mod = any_cast<const Module *>(&IR)) {
        std::string fn = dir + "/pass_" + std::to_string(event_id) + "_" +
                         pass_name + "_Module_" + (*Mod)->getName().str() + "_" + suffix + ".ll";
        std::error_code EC;
        raw_fd_ostream file(fn, EC);
        if (!EC) (*Mod)->print(file, nullptr);
    } else if (auto *F = any_cast<const Function *>(&IR)) {
        std::string fn = dir + "/pass_" + std::to_string(event_id) + "_" +
                         pass_name + "_Function_" + (*F)->getName().str() + "_" + suffix + ".ll";
        std::error_code EC;
        raw_fd_ostream file(fn, EC);
        if (!EC) (*F)->print(file);
    }
}

// ============================================================
// Codegen Measurement
// ============================================================

// Discover llc.exe path: check LLVM_DIR env var, then look relative to executable
static std::string findLlcExe() {
    // 1. Check LLVM_DIR environment variable
    if (const char *env = std::getenv("LLVM_DIR")) {
        std::string candidate = std::string(env) + "/bin/llc.exe";
        if (fs::exists(candidate)) return candidate;
    }
    // 2. Look in same directory as this executable
    std::string exe_dir;
    #ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0) exe_dir = fs::path(buf).parent_path().string();
    #else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len > 0) exe_dir = fs::path(std::string(buf, len)).parent_path().string();
    #endif
    if (!exe_dir.empty()) {
        // Try sibling llvm install: ../clang+llvm-.../bin/llc.exe
        for (auto &entry : fs::directory_iterator(fs::path(exe_dir).parent_path())) {
            if (entry.is_directory() && entry.path().filename().string().find("clang+llvm") != std::string::npos) {
                std::string candidate = entry.path().string() + "/bin/llc.exe";
                if (fs::exists(candidate)) return candidate;
            }
        }
    }
    // 3. Fallback: just "llc" and hope it's on PATH
    return "llc";
}

static unsigned countAsmLines(const std::string &path) {
    std::ifstream f(path);
    std::string line;
    unsigned count = 0;
    unsigned bytes = 0;
    while (std::getline(f, line)) {
        count++;
        bytes += line.size() + 1;
    }
    // Return bytes in upper 16 bits, lines in lower 16 bits
    return (bytes << 16) | count;
}

static CodegenResult measureCodegen(const std::string &ir_before_path,
                                    const std::string &ir_after_path,
                                    const std::string &output_dir) {
    CodegenResult result;
    std::string llc = findLlcExe();
    errs() << "  Using llc: " << llc << "\n";

    // Write a temp batch script to run llc (avoids Windows path issues)
    std::string bat_before = output_dir + "/run_llc_before.bat";
    std::string bat_after = output_dir + "/run_llc_after.bat";
    {
        std::ofstream b(bat_before);
        b << "@echo off\n";
        b << "\"" << llc << "\" -filetype=asm -o \"" << output_dir << "\\codegen_before.s\" \"" << ir_before_path << "\"\n";
    }
    {
        std::ofstream b(bat_after);
        b << "@echo off\n";
        b << "\"" << llc << "\" -filetype=asm -o \"" << output_dir << "\\codegen_after.s\" \"" << ir_after_path << "\"\n";
    }

    system(bat_before.c_str());
    system(bat_after.c_str());

    // Count lines
    std::string before_asm = output_dir + "/codegen_before.s";
    std::string after_asm = output_dir + "/codegen_after.s";

    {
        std::ifstream f(before_asm);
        std::string line;
        while (std::getline(f, line)) {
            result.asm_lines_before++;
            result.asm_size_before += line.size() + 1;
        }
    }
    {
        std::ifstream f(after_asm);
        std::string line;
        while (std::getline(f, line)) {
            result.asm_lines_after++;
            result.asm_size_after += line.size() + 1;
        }
    }

    return result;
}

// ============================================================
// Main
// ============================================================

int main(int argc, char **argv) {
    if (argc < 2) {
        errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots]\n";
        return 1;
    }

    // Parse CLI
    std::string input_file = argv[1];
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--snapshots") {
            g_snapshots = true;
        } else if (arg == "-O0" || arg == "--O0") {
            g_opt = OptimizationLevel::O0; g_opt_level = "O0";
        } else if (arg == "-O1" || arg == "--O1") {
            g_opt = OptimizationLevel::O1; g_opt_level = "O1";
        } else if (arg == "-O2" || arg == "--O2") {
            g_opt = OptimizationLevel::O2; g_opt_level = "O2";
        } else if (arg == "-O3" || arg == "--O3") {
            g_opt = OptimizationLevel::O3; g_opt_level = "O3";
        } else if (arg == "-Os" || arg == "--Os") {
            g_opt = OptimizationLevel::Os; g_opt_level = "Os";
        } else if (arg == "-Oz" || arg == "--Oz") {
            g_opt = OptimizationLevel::Oz; g_opt_level = "Oz";
        } else {
            g_output_dir = arg;
        }
    }

    fs::create_directories(g_output_dir);

    // Parse IR
    LLVMContext Context;
    SMDiagnostic Err;
    std::unique_ptr<Module> M = parseIRFile(input_file, Err, Context);
    if (!M) {
        Err.print(argv[0], errs());
        return 1;
    }
    g_module_name = M->getName().str();

    // Callbacks
    PassInstrumentationCallbacks PIC;
    unsigned event_num = 0;

    PIC.registerBeforeNonSkippedPassCallback(
        [&](StringRef PassID, Any IR) {
            IRDetection det = detectIR(IR);

            PassFrame frame;
            frame.pass_name = PassID.str();
            frame.ir_kind = det.kind;
            frame.ir_name = det.name;
            frame.depth = pass_stack.size();
            frame.before = det.metrics;
            frame.invalidated = false;
            frame.ir_before = serializeIR(IR);
            pass_stack.push_back(std::move(frame));

            event_num++;

            // Record event
            Event ev;
            ev.id = event_num;
            ev.event_type = "before";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(det.kind);
            ev.ir_name = det.name;
            ev.depth = frame.depth;
            ev.metrics_before = det.metrics;
            ev.ir_before = serializeIR(IR);
            g_events.push_back(ev);

            // Console output
            errs() << "[" << event_num << "] BEFORE: " << PassID
                   << "  {" << irUnitKindName(det.kind) << " " << det.name << "}"
                   << "  depth=" << frame.depth << "\n";

            // IR snapshot (before)
            if (shouldSnapshot(PassID))
                saveIRSnapshot("before", IR, PassID.str(), event_num);
        });

    PIC.registerAfterPassCallback(
        [&](StringRef PassID, Any IR, const PreservedAnalyses &PA) {
            if (pass_stack.empty()) {
                errs() << "  WARNING: AFTER without matching BEFORE for " << PassID << "\n";
                return;
            }

            PassFrame frame = std::move(pass_stack.back());
            pass_stack.pop_back();
            IRDetection det = detectIR(IR);
            bool changes = hasAnyDelta(frame.before, det.metrics);

            event_num++;

            // Record event
            Event ev;
            ev.id = event_num;
            ev.event_type = "after";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(det.kind);
            ev.ir_name = det.name;
            ev.depth = frame.depth;
            ev.metrics_before = frame.before;
            ev.metrics_after = det.metrics;
            ev.has_changes = changes;
            ev.ir_before = frame.ir_before;
            ev.ir_after = serializeIR(IR);
            g_events.push_back(ev);

            // Console output
            errs() << "[" << event_num << "] AFTER:  " << PassID
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
            } else {
                errs() << "  (no change)\n";
            }

            // IR snapshot (after)
            if (shouldSnapshot(PassID))
                saveIRSnapshot("after", IR, PassID.str(), event_num);
        });

    PIC.registerAfterPassInvalidatedCallback(
        [&](StringRef PassID, const PreservedAnalyses &PA) {
            if (pass_stack.empty()) {
                errs() << "  WARNING: INVALIDATED without matching BEFORE for " << PassID << "\n";
                return;
            }

            PassFrame frame = std::move(pass_stack.back());
            pass_stack.pop_back();
            frame.invalidated = true;

            event_num++;

            Event ev;
            ev.id = event_num;
            ev.event_type = "invalidated";
            ev.pass_name = PassID.str();
            ev.pass_type = classifyPass(PassID);
            ev.ir_kind = irUnitKindName(frame.ir_kind);
            ev.ir_name = frame.ir_name;
            ev.depth = frame.depth;
            ev.metrics_before = frame.before;
            g_events.push_back(ev);

            errs() << "[" << event_num << "] INVALIDATED: " << PassID
                   << "  {" << irUnitKindName(frame.ir_kind) << " " << frame.ir_name << "}"
                   << "  (no after-state available)\n";
        });

    // PassBuilder + Analysis Managers
    PassBuilder PB(nullptr, PipelineTuningOptions(), std::nullopt, &PIC);

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
    errs() << "=================================\n\n";

    // Save initial IR before optimization
    std::string ir_before_path = g_output_dir + "/ir_before_opt.ll";
    {
        std::error_code EC;
        raw_fd_ostream file(ir_before_path, EC);
        if (!EC) M->print(file, nullptr);
    }

    MPM.run(*M, MAM);

    // Save final IR after optimization
    std::string ir_after_path = g_output_dir + "/ir_after_opt.ll";
    {
        std::error_code EC;
        raw_fd_ostream file(ir_after_path, EC);
        if (!EC) M->print(file, nullptr);
    }

    // Measure codegen impact
    errs() << "Measuring codegen impact...\n";
    CodegenResult cg = measureCodegen(ir_before_path, ir_after_path, g_output_dir);

    // Summary
    errs() << "\n=================================\n";
    errs() << "LPTA Summary\n";
    errs() << "  Total events: " << event_num << "\n";
    errs() << "  Stack remaining: " << pass_stack.size();
    if (!pass_stack.empty()) errs() << " (WARNING: stack not empty!)";
    errs() << "\n";

    // Count passes with changes
    unsigned changed = 0;
    for (auto &e : g_events)
        if (e.event_type == "after" && e.has_changes) changed++;
    errs() << "  Passes with changes: " << changed << "\n";
    errs() << "\n";
    errs() << "  Codegen (assembly):\n";
    errs() << "    Before: " << cg.asm_lines_before << " lines (" << cg.asm_size_before << " bytes)\n";
    errs() << "    After:  " << cg.asm_lines_after << " lines (" << cg.asm_size_after << " bytes)\n";
    if (cg.asm_lines_before > 0) {
        int red = cg.asm_lines_before - cg.asm_lines_after;
        int pct = (red * 100) / cg.asm_lines_before;
        errs() << "    Reduction: " << red << " lines (" << pct << "% )\n";
    }
    errs() << "=================================\n";

    // Write JSON output (pass codegen result)
    writeHistoryJSON(g_output_dir + "/history.json", cg);

    return 0;
}
