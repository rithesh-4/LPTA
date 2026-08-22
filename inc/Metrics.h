#pragma once

namespace llvm {
class Module;
class Function;
class Loop;
}

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

IRMetrics captureModuleMetrics(const llvm::Module &M);

IRMetrics captureFunctionMetrics(const llvm::Function &F);

IRMetrics captureLoopMetrics(const llvm::Loop &L);

// ============================================================
// Delta helpers
// ============================================================

bool hasAnyDelta(const IRMetrics &a, const IRMetrics &b);
