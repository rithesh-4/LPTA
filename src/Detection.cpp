#include "Detection.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

const char *irUnitKindName(IRUnitKind K) {
    switch (K) {
    case IRUnitKind::Module: return "Module";
    case IRUnitKind::Function: return "Function";
    case IRUnitKind::Loop: return "Loop";
    case IRUnitKind::Unknown: return "Unknown";
    }
    return "Unknown";
}

IRDetection detectIR(const Any &IR) {
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
    } else {
        det.kind = IRUnitKind::Unknown;
        det.name = "Unknown";
    }
    return det;
}

// Extract the raw IR unit pointer from the Any container for identity matching.
// The pointer is stable between BEFORE and AFTER callbacks for the same pass
// execution (unlike the address of the StringRef PassID parameter, which is
// stack-local and differs between callbacks).
const void *irUnitPointer(const Any &IR) {
    if (auto *Mod = any_cast<const Module *>(&IR)) return *Mod;
    if (auto *F = any_cast<const Function *>(&IR)) return *F;
    if (auto *L = any_cast<const Loop *>(&IR)) return *L;
    return nullptr;
}

// Serialize IR unit to string for snapshots
std::string serializeIR(const Any &IR) {
    std::string buf;
    raw_string_ostream os(buf);
    if (auto *Mod = any_cast<const Module *>(&IR)) {
        (*Mod)->print(os, nullptr);
    } else if (auto *F = any_cast<const Function *>(&IR)) {
        (*F)->print(os);
    } else if (auto *L = any_cast<const Loop *>(&IR)) {
        // For loops, print only the blocks belonging to this loop
        // (not the entire parent function, which would be misleading).
        const Function *F = (*L)->getHeader() ? (*L)->getHeader()->getParent() : nullptr;
        if (F) {
            ModuleSlotTracker MST(F->getParent());
            MST.incorporateFunction(*F);
            for (auto *BB : (*L)->blocks()) {
                static_cast<const Value *>(BB)->print(os, MST);
                os << "\n";
            }
        } else {
            for (auto *BB : (*L)->blocks()) {
                BB->print(os);
                os << "\n";
            }
        }
    }
    return buf;
}

// FNV-1a 64-bit over the canonical serialization. In practice equal hashes
// mean byte-identical IR text, so any textual IR mutation (operands,
// constants, attributes, ordering) flips the hash even when all structural
// metric counters are unchanged. (Probabilistic: a 64-bit hash can collide
// in theory; the chance per comparison is ~2^-64.)
uint64_t hashIRUnit(const Any &IR) {
    return hashIRText(serializeIR(IR));
}

uint64_t hashIRText(const std::string &s) {
    if (s.empty()) return 0;
    uint64_t h = 14695981039346656037ULL;  // FNV offset basis
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;  // FNV prime
    }
    return h;
}
