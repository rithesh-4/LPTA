#include "Config.h"

// Global variable definitions (originally in main.cpp)
std::string g_output_dir = "report";
bool g_snapshots = false;
bool g_no_ir_hash = false;
bool g_compare_json = false;
bool g_allow_different_input = false;
std::string g_module_name;
std::string g_opt_level = "O2";
llvm::OptimizationLevel g_opt = llvm::OptimizationLevel::O2;

// Optnone tracking
std::vector<std::string> g_optnone_functions;
bool g_optnone_detected = false;

// Multi-target codegen
std::vector<std::string> g_target_triples;

// Provenance for run_metadata (comparability checks)
std::string g_input_ir_hash;
std::string g_target_triple;

// Pass tracking globals are defined in Tracker.cpp
