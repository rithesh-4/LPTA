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

using namespace llvm;
namespace fs = std::filesystem;

// Globals are defined in globals.cpp (shared with test_utilities).

// ============================================================
// Main
// ============================================================

int main(int argc, char **argv) {
    if (argc < 2) {
        errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file]\n";
        return 1;
    }

    // Parse CLI
    std::string input_file;
    bool output_dir_set = false;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--snapshots") {
            g_snapshots = true;
        } else if (arg.rfind("--targets=", 0) == 0) {
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
                g_target_triples.assign(COMMON_TARGETS.begin(), COMMON_TARGETS.end());
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
                while (std::getline(f, line)) {
                    line = trimCopy(line);
                    if (!line.empty() && line[0] != '#') {
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
        } else if (!arg.empty() && arg[0] == '-') {
            errs() << "ERROR: unknown option '" << arg << "'\n";
            errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file]\n";
            return 1;
        } else if (input_file.empty()) {
            input_file = arg;
        } else if (!output_dir_set) {
            g_output_dir = arg;
            output_dir_set = true;
        } else {
            errs() << "ERROR: unexpected argument '" << arg << "'\n";
            errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file]\n";
            return 1;
        }
    }

    if (input_file.empty()) {
        errs() << "Usage: lpta_test <input.ll> [output_dir] [-O0|-O1|-O2|-O3|-Os|-Oz] [--snapshots] [--targets=common|triple,...|@file]\n";
        return 1;
    }

    std::error_code ec;
    fs::create_directories(g_output_dir, ec);
    if (ec) {
        errs() << "ERROR: could not create output directory '" << g_output_dir
               << "': " << ec.message() << "\n";
        return 1;
    }

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
                // Check whitespace-only file (e.g. "\n  \n") -> treat as empty
                std::ifstream cf(input_file);
                std::string content((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
                bool all_ws = !content.empty() && std::all_of(content.begin(), content.end(),
                    [](unsigned char c){ return std::isspace(c); });
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
            // Only capture IR text if snapshots enabled for this pass.
            // Cap at 4 MB per snapshot to avoid unbounded memory growth
            // on large modules.
            if (shouldSnapshot(PassID)) {
                std::string snap = serializeIR(IR);
                static constexpr size_t kMaxSnapshotBytes = 4 * 1024 * 1024;
                if (snap.size() <= kMaxSnapshotBytes)
                    frame.ir_before = std::move(snap);
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
            ev.ir_before = std::move(frame.ir_before);
            // Snapshot size cap is decided jointly for the pair: keep
            // before/after only if BOTH sides fit under the cap, so the
            // dashboard never renders a one-sided diff. Also clean orphaned before file.
            bool both_fit = true;
            {
                static constexpr size_t kMaxSnapshotBytes = 4 * 1024 * 1024;
                std::string afterSnap;
                if (changes && shouldSnapshot(PassID))
                    afterSnap = serializeIR(IR);
                both_fit = ev.ir_before.size() <= kMaxSnapshotBytes &&
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
            } else {
                errs() << "  (no change)\n";
            }

            // IR snapshot (after) — gated by same joint cap as JSON
            if (shouldSnapshot(PassID) && changes && both_fit)
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
        long long red = static_cast<long long>(cg.asm_lines_before)
                      - static_cast<long long>(cg.asm_lines_after);
        long long pct = (red * 100) / cg.asm_lines_before;
        errs() << "    Reduction: " << red << " lines (" << pct << "% )\n";
    }
    errs() << "=================================\n";

    // Write JSON output (pass codegen result)
    writeHistoryJSON(g_output_dir + "/history.json", cg);

    return stack_unbalanced ? 1 : 0;
}
