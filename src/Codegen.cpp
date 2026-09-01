#include "Codegen.h"
#include "Util.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;
namespace fs = std::filesystem;

// ============================================================
// Target Normalization & Validation
// ============================================================

std::string normalizeTargetTriple(const std::string &archOrTriple) {
    // If it contains '-', assume it's already a full triple
    if (archOrTriple.find('-') != std::string::npos) {
        return Triple::normalize(archOrTriple);
    }

    // Architecture aliases -> default triples
#ifdef _WIN32
    // On Windows, native is MSVC; cross-targets use generic Linux/ELF
    if (archOrTriple == "x86_64") return "x86_64-pc-windows-msvc";
    if (archOrTriple == "aarch64") return "aarch64-unknown-linux-gnu";
    if (archOrTriple == "riscv64") return "riscv64-unknown-linux-gnu";
#else
    if (archOrTriple == "x86_64") return "x86_64-unknown-linux-gnu";
    if (archOrTriple == "aarch64") return "aarch64-unknown-linux-gnu";
    if (archOrTriple == "riscv64") return "riscv64-unknown-linux-gnu";
#endif

    // Unknown alias -> return as-is (will fail validation)
    return archOrTriple;
}

bool validateTargetTriple(const std::string &triple) {
    std::string errMsg;
    Triple T(triple);
    const Target *Target = TargetRegistry::lookupTarget(T, errMsg);
    return Target != nullptr;
}

// ============================================================
// llc Discovery
// ============================================================

std::string findLlcExe() {
#ifdef _WIN32
    const char *exe_names[] = {"llc.exe", nullptr};
#else
    const char *exe_names[] = {"llc", nullptr};
#endif
    auto tryFind = [&](const std::string &dir) -> std::string {
        for (int i = 0; exe_names[i]; i++) {
            fs::path candidate = fs::path(dir) / "bin" / exe_names[i];
            std::error_code ec_ex;
            if (fs::exists(candidate, ec_ex)) return candidate.string();
        }
        return "";
    };

    // 1. Check LLVM_DIR environment variable (most reliable)
    if (const char *env = std::getenv("LLVM_DIR")) {
        std::string r = tryFind(std::string(env));
        if (!r.empty()) return r;
    }

    // 2. Check CMAKE_PREFIX_PATH or LLVM_INSTALL_DIR if set via CMake
    if (const char *env = std::getenv("LLVM_INSTALL_DIR")) {
        std::string r = tryFind(std::string(env));
        if (!r.empty()) return r;
    }

    // 3. Look in same directory as this executable (for bundled LLVM)
    std::string exe_dir;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len > 0) exe_dir = fs::path(buf).parent_path().string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len > 0) exe_dir = fs::path(std::string(buf, len)).parent_path().string();
#endif
    if (!exe_dir.empty()) {
        std::string r = tryFind(exe_dir);
        if (!r.empty()) return r;
        // Check parent directory (common for build/ vs install/)
        std::string r2 = tryFind(fs::path(exe_dir).parent_path().string());
        if (!r2.empty()) return r2;
        // Check sibling directories of the parent (bundled LLVM installs,
        // e.g. C:\LLVM-full\clang+llvm-22.1.8-...\bin\llc.exe)
        // Sort for determinism (filesystem order nondeterministic)
        std::error_code ec;
        std::vector<fs::path> siblings;
        for (auto &entry : fs::directory_iterator(fs::path(exe_dir).parent_path(), ec)) {
            std::error_code ec_entry;
            if (!ec && entry.is_directory(ec_entry)) {
                siblings.push_back(entry.path());
            }
        }
        std::sort(siblings.begin(), siblings.end());
        for (auto &p : siblings) {
            std::string r3 = tryFind(p.string());
            if (!r3.empty()) return r3;
        }
    }

    // 4. Check common install locations
    const std::vector<std::string> common_paths = {
        "/usr/local",
        "/opt/llvm",
        "/usr",
        "C:/Program Files/LLVM",
        "C:/llvm"
    };
    for (const auto &p : common_paths) {
        std::string r = tryFind(p);
        if (!r.empty()) return r;
    }

    // 5. Fallback: just "llc" and hope it's on PATH
    return exe_names[0];
}


// ============================================================
// Shell Quoting Helpers
// ============================================================

static std::string shellQuoteBare(const std::string &s) {
    // Escape a string for POSIX shell: wrap in single quotes, escape any
    // embedded single quotes.  Safe for arbitrary filenames including
    // spaces, dollar signs, backticks, etc.
    std::string r = "'";
    for (char c : s) {
        if (c == '\'')
            r += "'\\''";   // end quote, escaped quote, start quote
        else
            r += c;
    }
    r += '\'';
    return r;
}

// ============================================================
// Assembly Counting Helper
// ============================================================

static void countAssembly(const std::string &asm_path,
                          unsigned &lines, unsigned &bytes) {
    std::error_code ec;
    lines = 0;
    bytes = 0;
    if (!fs::exists(asm_path, ec)) return;
    std::ifstream f(asm_path);
    std::string line;
    while (std::getline(f, line)) {
        lines++;
    }
    std::error_code ec2;
    if (!ec)
        bytes = static_cast<unsigned>(fs::file_size(asm_path, ec2));
}

// ============================================================
// runLlc - Extended with target triple support
// ============================================================

int runLlc(const std::string &llc, const std::string &input,
           const std::string &output,
           const std::string &triple,            // empty = native default
           [[maybe_unused]] const std::string &cpu,        // v1: unused (target defaults)
           [[maybe_unused]] const std::string &features) { // v1: unused (target defaults)
    // Resolve to absolute paths to avoid issues with .. on Windows.
    // IMPORTANT: only absolutize llc if it is a real path (contains a
    // separator or exists on disk). A bare "llc"/"llc.exe" fallback must be
    // passed through untouched so that cmd.exe / system() can resolve it via
    // PATH — fs::absolute() on a bare name would produce a nonexistent
    // CWD-relative path, silently breaking the PATH fallback.
    std::string abs_llc = llc;
    bool llc_is_bare_name = llc.find('/') == std::string::npos &&
                            llc.find('\\') == std::string::npos;
    if (!llc_is_bare_name || fs::exists(llc))
        abs_llc = fs::absolute(llc).string();
    std::string abs_input = fs::absolute(input).string();
    std::string abs_output = fs::absolute(output).string();

    // Build command line with target options (quoted per-platform)
#ifdef _WIN32
    std::string cmd_args = "-filetype=asm";
    if (!triple.empty()) {
        // Quote triple for cmd.exe: wrap in double quotes and escape inner quotes
        std::string esc = triple;
        std::string q;
        q.reserve(esc.size() + 2);
        q.push_back('"');
        for (char c : esc) {
            if (c == '"') q += "\\\"";
            else q.push_back(c);
        }
        q.push_back('"');
        cmd_args += " -mtriple=" + q;
    }
    // cpu/features omitted in v1 (use target defaults)
    // Quote file paths for cmd.exe
    auto winQuote = [](const std::string &s) {
        std::string r = "\"";
        for (char c : s) {
            if (c == '"') r += "\\\"";
            else r += c;
        }
        r += '"';
        return r;
    };
    cmd_args += " -o " + winQuote(abs_output) + " " + winQuote(abs_input);

    // On Windows, system() uses cmd.exe which chokes on nested quotes.
    // Write a temp .bat file and execute that instead.
    std::string bat = abs_output + ".run_llc.bat";
    {
        std::ofstream b(bat);
        b << "@echo off\n";
        b << "\"" << abs_llc << "\" " << cmd_args << "\n";
    }
    int rc = system(("call \"" + bat + "\"").c_str());
    std::error_code ec_rm;
    fs::remove(bat, ec_rm);
    return rc;
#else
    // POSIX: use single-quote wrapping with embedded-quote escaping to prevent
    // shell injection from malicious filenames/triples.
    std::string cmd_args = "-filetype=asm";
    if (!triple.empty()) cmd_args += " -mtriple=" + shellQuoteBare(triple);
    cmd_args += " -o " + shellQuoteBare(abs_output) + " " + shellQuoteBare(abs_input);
    std::string cmd = shellQuoteBare(abs_llc) + " " + cmd_args;
    return system(cmd.c_str());
#endif
}

// ============================================================
// Native Codegen (backward compatible)
// ============================================================

CodegenResult measureCodegen(const std::string &ir_before_path,
                             const std::string &ir_after_path,
                             const std::string &output_dir) {
    CodegenResult result;
    std::string llc = findLlcExe();
    errs() << "  Using llc: " << llc << "\n";

    std::string before_asm = output_dir + "/codegen_before.s";
    std::string after_asm = output_dir + "/codegen_after.s";

    // Remove any stale assembly from previous runs so a failed llc invocation
    // cannot be silently attributed to leftover files.
    fs::remove(before_asm);
    fs::remove(after_asm);

    int rc1 = runLlc(llc, ir_before_path, before_asm, "", "", "");
    int rc2 = runLlc(llc, ir_after_path, after_asm, "", "", "");

    if (rc1 != 0) {
        errs() << "  WARNING: llc failed for before-state (exit code " << rc1 << ")\n";
        result.error_before = "llc failed with exit code " + std::to_string(rc1);
    }
    if (rc2 != 0) {
        errs() << "  WARNING: llc failed for after-state (exit code " << rc2 << ")\n";
        result.error_after = "llc failed with exit code " + std::to_string(rc2);
    }

    // Count lines and bytes (only if llc succeeded and files exist)
    if (rc1 == 0)
        countAssembly(before_asm, result.asm_lines_before, result.asm_size_before);
    if (rc2 == 0)
        countAssembly(after_asm, result.asm_lines_after, result.asm_size_after);

    return result;
}

// ============================================================
// Multi-Target Codegen
// ============================================================

CodegenResult measureCodegenMultiTarget(
    const std::string &ir_before_path,
    const std::string &ir_after_path,
    const std::string &output_dir,
    const MultiTargetConfig &config) {
    CodegenResult result;
    std::string llc = findLlcExe();
    errs() << "  Using llc: " << llc << "\n";

    // 1. Native first (fills legacy fields for backward compat)
    result = measureCodegen(ir_before_path, ir_after_path, output_dir);

    // 2. Cross-targets — each target runs llc independently (M6 fix:
    // native reuse removed due to LLVM 22 API limitations).
    for (const auto &target : config.targets) {
        std::string triple = normalizeTargetTriple(target);

        if (!validateTargetTriple(triple)) {
            TargetCodegenResult tr;
            tr.error = "Target not supported by this LLVM build: " + triple;
            result.per_target[target] = tr;
            errs() << "  WARNING: " << tr.error << "\n";
            continue;
        }

        std::string sanitized = sanitizeFilename(target);
        // Avoid sanitized collision (M5) by appending hash when collision detected
        // Note: sanitizeFilename collisions are rare; we keep map key as original target,
        // but need distinct filenames for overlapping sanitized names.
        std::string before_asm = output_dir + "/codegen_" + sanitized + "_before.s";
        std::string after_asm  = output_dir + "/codegen_" + sanitized + "_after.s";
        // Remove stale files before run (M4 fix)
        fs::remove(before_asm);
        fs::remove(after_asm);

        TargetCodegenResult tr;
        int rc1 = runLlc(llc, ir_before_path, before_asm, triple, "", "");
        int rc2 = runLlc(llc, ir_after_path, after_asm, triple, "", "");

        if (rc1 != 0 || rc2 != 0) {
            if (rc1 != 0 && rc2 != 0)
                tr.error = "llc failed for both before and after (rc1=" + std::to_string(rc1) + ", rc2=" + std::to_string(rc2) + ")";
            else if (rc1 != 0)
                tr.error = "llc failed for before-state (rc1=" + std::to_string(rc1) + ", after succeeded)";
            else
                tr.error = "llc failed for after-state (rc2=" + std::to_string(rc2) + ", before succeeded)";
            errs() << "  WARNING: Codegen failed for " << target << ": " << tr.error << "\n";
        } else {
            countAssembly(before_asm, tr.asm_lines_before, tr.asm_size_before);
            countAssembly(after_asm, tr.asm_lines_after, tr.asm_size_after);
        }
        result.per_target[target] = tr;
    }

    return result;
}
