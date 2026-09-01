#include "Snapshots.h"

#include "Config.h"
#include "Util.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <filesystem>

using namespace llvm;
namespace fs = std::filesystem;

const std::set<std::string> g_snapshot_allowlist = {
    "InstCombinePass", "SimplifyCFGPass", "GVNPass", "LICMPass",
    "SROAPass", "EarlyCSEPass", "DSEPass", "SCCPPass",
    "LoopUnrollPass", "InlinerPass", "GlobalOptPass", "GlobalDCEPass",
    "LoopVectorizePass", "SLPVectorizerPass", "JumpThreadingPass",
    "CorrelatedValuePropagationPass", "BDCEPass", "ADCEPass",
    "ReassociatePass", "LoopDistributePass", "LoopRotatePass",
    "LoopIdiomRecognizePass", "IndVarSimplifyPass", "LoopSinkPass",
    "MemCpyOptPass", "TailCallElimPass", "DeadArgumentEliminationPass",
    "ConstantMergePass", "AggressiveInstCombinePass", "SpeculativeExecutionPass",
};

// Formal verification: shouldSnapshot is a pure predicate.
// Precondition: pass_name is a valid LLVM pass identifier string.
// Postcondition: returns true only if snapshots are enabled AND pass is in allowlist.
bool shouldSnapshot(StringRef pass_name) {
    return g_snapshots && g_snapshot_allowlist.count(pass_name.str());
}

void saveIRSnapshot(const std::string &suffix, const Any &IR,
                    const std::string &pass_name, unsigned event_id) {
    std::string dir = g_output_dir + "/ir";
    std::error_code ec_dir;
    fs::create_directories(dir, ec_dir);
    if (ec_dir) {
        errs() << "  WARNING: could not create snapshot dir '" << dir
               << "': " << ec_dir.message() << "\n";
        return;
    }

    if (auto *Mod = any_cast<const Module *>(&IR)) {
        std::string mod_name = sanitizeFilename((*Mod)->getName().str());
        std::string pn = sanitizeFilename(pass_name);
        std::string fn = dir + "/pass_" + std::to_string(event_id) + "_" +
                         pn + "_Module_" + mod_name + "_" + suffix + ".ll";
        std::error_code EC;
        raw_fd_ostream file(fn, EC);
        if (!EC) (*Mod)->print(file, nullptr);
    } else if (auto *F = any_cast<const Function *>(&IR)) {
        std::string func_name = sanitizeFilename((*F)->getName().str());
        std::string pn = sanitizeFilename(pass_name);
        std::string fn = dir + "/pass_" + std::to_string(event_id) + "_" +
                         pn + "_Function_" + func_name + "_" + suffix + ".ll";
        std::error_code EC;
        raw_fd_ostream file(fn, EC);
        if (!EC) (*F)->print(file);
    } else if (auto *L = any_cast<const Loop *>(&IR)) {
        std::string loop_name = "unknown";
        if (auto *Header = (*L)->getHeader())
            loop_name = sanitizeFilename(Header->getName().str());
        std::string pn = sanitizeFilename(pass_name);
        std::string fn = dir + "/pass_" + std::to_string(event_id) + "_" +
                         pn + "_Loop_" + loop_name + "_" + suffix + ".ll";
        std::error_code EC;
        raw_fd_ostream file(fn, EC);
        if (!EC) {
            for (auto *BB : (*L)->blocks()) {
                BB->print(file);
                file << "\n";
            }
        }
    }
}
