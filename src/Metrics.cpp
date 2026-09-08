#include "Metrics.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/LoopInfo.h"

#include <cassert>

using namespace llvm;

// Opcode-group histogram: maps every opcode into exactly one group so the
// groups partition instruction_count (used as "what changed" evidence).
static void countOpcodeGroup(IRMetrics &m, unsigned opcode) {
    switch (opcode) {
    case Instruction::Ret:
    case Instruction::Br:
    case Instruction::Switch:
    case Instruction::IndirectBr:
    case Instruction::Resume:
    case Instruction::Unreachable:
    case Instruction::CleanupRet:
    case Instruction::CatchRet:
    case Instruction::CatchSwitch:
        m.op_control++;
        break;
    case Instruction::Call:
    case Instruction::Invoke:
    case Instruction::CallBr:
        m.op_call++;
        break;
    case Instruction::PHI:
    case Instruction::Select:
    case Instruction::ICmp:
    case Instruction::FCmp:
        m.op_cmp++;
        break;
    case Instruction::Alloca:
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::GetElementPtr:
    case Instruction::Fence:
    case Instruction::AtomicCmpXchg:
    case Instruction::AtomicRMW:
        m.op_memory++;
        break;
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
        m.op_cast++;
        break;
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
    case Instruction::ExtractValue:
    case Instruction::InsertValue:
        m.op_vector++;
        break;
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::FNeg:
        m.op_arith++;
        break;
    default:
        // LandingPad, Freeze, VAArg, funclet pads, and any future opcode.
        m.op_other++;
        break;
    }
}

// Partition invariant shared by all three capture functions.
static void assertPartition(const IRMetrics &m) {
    assert(m.op_arith + m.op_cmp + m.op_memory + m.op_control +
           m.op_cast + m.op_call + m.op_vector + m.op_other ==
           m.instruction_count &&
           "opcode groups must partition instruction_count");
}

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
                countOpcodeGroup(m, I.getOpcode());
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
    assertPartition(m);
    return m;
}

IRMetrics captureFunctionMetrics(const Function &F) {
    IRMetrics m;
    m.function_count = 1;
    for (auto &BB : F) {
        m.basic_block_count++;
        for (auto &I : BB) {
            m.instruction_count++;
            countOpcodeGroup(m, I.getOpcode());
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
    assertPartition(m);
    return m;
}

IRMetrics captureLoopMetrics(const Loop &L) {
    IRMetrics m;
    for (auto *BB : L.blocks()) {
        m.basic_block_count++;
        for (auto &I : *BB) {
            m.instruction_count++;
            countOpcodeGroup(m, I.getOpcode());
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
    assertPartition(m);
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
           a.return_count != b.return_count ||
           a.op_arith != b.op_arith ||
           a.op_cmp != b.op_cmp ||
           a.op_memory != b.op_memory ||
           a.op_control != b.op_control ||
           a.op_cast != b.op_cast ||
           a.op_call != b.op_call ||
           a.op_vector != b.op_vector ||
           a.op_other != b.op_other;
}
