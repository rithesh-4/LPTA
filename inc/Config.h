#pragma once

#include <string>
#include <vector>

#include "llvm/Passes/OptimizationLevel.h"

// ============================================================
// Globals
// ============================================================

inline constexpr const char *LPTA_VERSION = "1.0";
inline constexpr int LPTA_SCHEMA_VERSION = 2;

extern std::string g_output_dir;
extern bool g_snapshots;
extern bool g_no_ir_hash;  // --no-ir-hash: skip IR serialization/hashing (perf mode)
// --compare options (parsed in the pre-scan, apply to compare mode only)
extern bool g_compare_json;            // --json: emit canonical result JSON
extern bool g_allow_different_input;   // --allow-different-input: compare across inputs
extern std::string g_module_name;
extern std::string g_opt_level;
extern llvm::OptimizationLevel g_opt;

// Provenance for run_metadata (comparability checks)
extern std::string g_input_ir_hash;  // hex FNV-1a of the input file bytes
extern std::string g_target_triple;  // normalized triple of the analyzed module

// ============================================================
// Optnone tracking
// ============================================================
extern std::vector<std::string> g_optnone_functions;
extern bool g_optnone_detected;

// ============================================================
// Multi-target codegen
// ============================================================
extern std::vector<std::string> g_target_triples;
