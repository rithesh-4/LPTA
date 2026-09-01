#pragma once

#include <string>
#include <map>
#include <vector>

// ============================================================
// Codegen Measurement
// ============================================================

// Per-target result (flat, not recursive)
struct TargetCodegenResult {
    unsigned asm_lines_before = 0;
    unsigned asm_lines_after = 0;
    unsigned asm_size_before = 0;
    unsigned asm_size_after = 0;
    std::string error;  // empty = success; populated on failure
};

// Updated CodegenResult with per-target map
struct CodegenResult {
    // Legacy native fields (backward compat)
    unsigned asm_lines_before = 0;
    unsigned asm_lines_after = 0;
    unsigned asm_size_before = 0;
    unsigned asm_size_after = 0;
    std::string error_before;  // empty = success; populated on failure
    std::string error_after;   // empty = success; populated on failure

    // Multi-target results
    std::map<std::string, TargetCodegenResult> per_target;
};

// Multi-target config
struct MultiTargetConfig {
    std::vector<std::string> targets;  // Full triples (normalized)
};

// Common target presets — keep in sync with CMakeLists.txt llvm_map_components and src/main.cpp LLVMInitialize* calls
// inline (C++17) is intentional; vector contents are immutable presets
#ifdef _WIN32
inline const std::vector<std::string> COMMON_TARGETS = {
    "x86_64-pc-windows-msvc",
    "aarch64-unknown-linux-gnu",
    "riscv64-unknown-linux-gnu"
};
#else
inline const std::vector<std::string> COMMON_TARGETS = {
    "x86_64-unknown-linux-gnu",
    "aarch64-unknown-linux-gnu",
    "riscv64-unknown-linux-gnu"
};
#endif

// Discover llc path: check LLVM_DIR env var, then CMake-provided path, then PATH
std::string findLlcExe();

int runLlc(const std::string &llc, const std::string &input,
           const std::string &output,
           const std::string &triple = "",            // empty = native default
           [[maybe_unused]] const std::string &cpu = "",        // v1: unused (target defaults)
           [[maybe_unused]] const std::string &features = "");  // v1: unused (target defaults)

CodegenResult measureCodegen(const std::string &ir_before_path,
                             const std::string &ir_after_path,
                             const std::string &output_dir);

CodegenResult measureCodegenMultiTarget(
    const std::string &ir_before_path,
    const std::string &ir_after_path,
    const std::string &output_dir,
    const MultiTargetConfig &config);

// Target normalization & validation
std::string normalizeTargetTriple(const std::string &archOrTriple);
bool validateTargetTriple(const std::string &triple);
