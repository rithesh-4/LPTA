#pragma once

#include <string>

// ============================================================
// Codegen Measurement
// ============================================================

struct CodegenResult {
    unsigned asm_lines_before = 0;
    unsigned asm_lines_after = 0;
    unsigned asm_size_before = 0;
    unsigned asm_size_after = 0;
};

// Discover llc path: check LLVM_DIR env var, then CMake-provided path, then PATH
std::string findLlcExe();

int runLlc(const std::string &llc, const std::string &input,
           const std::string &output);

CodegenResult measureCodegen(const std::string &ir_before_path,
                             const std::string &ir_after_path,
                             const std::string &output_dir);
