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

// FNV-1a 64-bit hash of the serialized IR unit. Used to detect real IR
// changes that leave all structural counters identical (e.g. constant
// folds). Returns 0 for unsupported (Unknown) units, which cannot hash.
uint64_t hashIRUnit(const llvm::Any &IR);

// Hash already-serialized IR text (lets callers serialize once and reuse
// the text for both hashing and snapshots).
uint64_t hashIRText(const std::string &s);
