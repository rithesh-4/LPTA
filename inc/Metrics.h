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
    // Opcode-group histogram: a partition of every instruction (groups sum
    // to instruction_count). Answers "WHAT changed" one level below the
    // feature counters above (e.g. arith -2 while totals barely move).
    unsigned op_arith = 0;    // int/fp arithmetic + bitwise + fneg
    unsigned op_cmp = 0;      // icmp/fcmp/select/phi (dataflow joins)
    unsigned op_memory = 0;   // alloca/load/store/gep/fence/atomics
    unsigned op_control = 0;  // terminators (ret/br/switch/invoke/...)
    unsigned op_cast = 0;     // trunc/ext/fp/int/ptr casts
    unsigned op_call = 0;     // call/invoke/callbr
    unsigned op_vector = 0;   // extract/insert/shuffle (elem + value)
    unsigned op_other = 0;    // landingpad/freeze/va_arg/funclet pads/future
};

IRMetrics captureModuleMetrics(const llvm::Module &M);

IRMetrics captureFunctionMetrics(const llvm::Function &F);

IRMetrics captureLoopMetrics(const llvm::Loop &L);

// ============================================================
// Delta helpers
// ============================================================

bool hasAnyDelta(const IRMetrics &a, const IRMetrics &b);
