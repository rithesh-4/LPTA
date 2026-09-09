#pragma once

#include <string>
#include <vector>

#include "llvm/Passes/OptimizationLevel.h"

// ============================================================
// Globals
// ============================================================

extern std::string g_output_dir;
extern bool g_snapshots;
extern bool g_no_ir_hash;  // --no-ir-hash: skip IR serialization/hashing (perf mode)
extern std::string g_module_name;
extern std::string g_opt_level;
extern llvm::OptimizationLevel g_opt;

// ============================================================
// Optnone tracking
// ============================================================
extern std::vector<std::string> g_optnone_functions;
extern bool g_optnone_detected;

// ============================================================
// Multi-target codegen
// ============================================================
extern std::vector<std::string> g_target_triples;
