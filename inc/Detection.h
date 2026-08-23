#pragma once //Tells the compiler to include this header only once

#include <string>

#include "Metrics.h"
#include "llvm/ADT/Any.h"

// ============================================================
// IR Unit Detection
// ============================================================

enum class IRUnitKind { Module, Function, Loop, Unknown };

const char *irUnitKindName(IRUnitKind K);

struct IRDetection {
    IRUnitKind kind = IRUnitKind::Unknown;
    std::string name;
    IRMetrics metrics;
};

IRDetection detectIR(const llvm::Any &IR);

// Extract the raw IR unit pointer from the Any container for identity matching.
const void *irUnitPointer(const llvm::Any &IR);

// Serialize IR unit to string for snapshots
std::string serializeIR(const llvm::Any &IR);
