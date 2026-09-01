#include "Metrics.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"

#include <cassert>

using namespace llvm;

// Formal verification: captureModuleMetrics counts all IR elements in a module.
// Postcondition: instruction_count >= call_count + load_count + store_count
//                (every call/load/store is an instruction)
// Postcondition: basic_block_count >= return_count
//                (every return is in a basic block)
IRMetrics captureModuleMetrics(const Module &M) {
    IRMetrics m;
    for (auto &F : M) {
        if (F.isDeclaration()) continue;
        m.function_count++;
        for (auto &BB : F) {
            m.basic_block_count++;
            for (auto &I : BB) {
                m.instruction_count++;
                switch (I.getOpcode()) {
                case Instruction::Call:
                case Instruction::Invoke:
                case Instruction::CallBr:
                    m.call_count++;
                    break;
                case Instruction::Load: m.load_count++; break;
                case Instruction::Store: m.store_count++; break;
                case Instruction::Br:
                case Instruction::Switch:
                case Instruction::IndirectBr:
                    m.branch_count++; break;
                case Instruction::PHI: m.phi_count++; break;
                case Instruction::Ret: m.return_count++; break;
                default: break;
                }
            }
        }
    }
    m.global_count = M.global_size();
    // Formal verification: verify counting invariants
    assert(m.instruction_count >= m.call_count + m.load_count + m.store_count &&
           "call/load/store counts must be <= instruction count");
    assert(m.basic_block_count >= m.return_count &&
           "return count must be <= basic block count");
    return m;
}

IRMetrics captureFunctionMetrics(const Function &F) {
    IRMetrics m;
    m.function_count = 1;
    for (auto &BB : F) {
        m.basic_block_count++;
        for (auto &I : BB) {
            m.instruction_count++;
            switch (I.getOpcode()) {
            case Instruction::Call:
            case Instruction::Invoke:
            case Instruction::CallBr:
                m.call_count++; break;
            case Instruction::Load: m.load_count++; break;
            case Instruction::Store: m.store_count++; break;
            case Instruction::Br:
            case Instruction::Switch:
            case Instruction::IndirectBr:
                m.branch_count++; break;
            case Instruction::PHI: m.phi_count++; break;
            case Instruction::Ret: m.return_count++; break;
            default: break;
            }
        }
    }
    assert(m.instruction_count >= m.call_count + m.load_count + m.store_count &&
           "call/load/store counts must be <= instruction count");
    assert(m.basic_block_count >= m.return_count &&
           "return count must be <= basic block count");
    return m;
}

IRMetrics captureLoopMetrics(const Loop &L) {
    IRMetrics m;
    for (auto *BB : L.blocks()) {
        m.basic_block_count++;
        for (auto &I : *BB) {
            m.instruction_count++;
            switch (I.getOpcode()) {
            case Instruction::Call:
            case Instruction::Invoke:
            case Instruction::CallBr:
                m.call_count++; break;
            case Instruction::Load: m.load_count++; break;
            case Instruction::Store: m.store_count++; break;
            case Instruction::Br:
            case Instruction::Switch:
            case Instruction::IndirectBr:
                m.branch_count++; break;
            case Instruction::PHI: m.phi_count++; break;
            case Instruction::Ret: m.return_count++; break;
            default: break;
            }
        }
    }
    assert(m.instruction_count >= m.call_count + m.load_count + m.store_count &&
           "call/load/store counts must be <= instruction count");
    assert(m.basic_block_count >= m.return_count &&
           "return count must be <= basic block count");
    return m;
}

// ============================================================
// Delta helpers
// ============================================================

bool hasAnyDelta(const IRMetrics &a, const IRMetrics &b) {
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
