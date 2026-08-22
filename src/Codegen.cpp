#include "Codegen.h"

#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;
namespace fs = std::filesystem;

// Discover llc path: check LLVM_DIR env var, then CMake-provided path, then PATH
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
        std::error_code ec;
        for (auto &entry : fs::directory_iterator(fs::path(exe_dir).parent_path(), ec)) {
            std::error_code ec_entry;
            if (!ec && entry.is_directory(ec_entry)) {
                std::string r3 = tryFind(entry.path().string());
                if (!r3.empty()) return r3;
            }
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

int runLlc(const std::string &llc, const std::string &input,
           const std::string &output) {
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
#ifdef _WIN32
    // On Windows, system() uses cmd.exe which chokes on nested quotes.
    // Write a temp .bat file and execute that instead.
    std::string bat = abs_output + ".run_llc.bat";
    {
        std::ofstream b(bat);
        b << "@echo off\n";
        b << "\"" << abs_llc << "\" -filetype=asm -o \"" << abs_output << "\" \"" << abs_input << "\"\n";
    }
    int rc = system(("call \"" + bat + "\"").c_str());
    std::error_code ec_rm;
    fs::remove(bat, ec_rm);
    return rc;
#else
    // Use single-quote wrapping with embedded-quote escaping to prevent
    // shell injection from malicious filenames.
    std::string cmd = shellQuoteBare(abs_llc) + " -filetype=asm -o " +
                      shellQuoteBare(abs_output) + " " +
                      shellQuoteBare(abs_input);
    return system(cmd.c_str());
#endif
}

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

    int rc1 = runLlc(llc, ir_before_path, before_asm);
    int rc2 = runLlc(llc, ir_after_path, after_asm);

    if (rc1 != 0)
        errs() << "  WARNING: llc failed for before-state (exit code " << rc1 << ")\n";
    if (rc2 != 0)
        errs() << "  WARNING: llc failed for after-state (exit code " << rc2 << ")\n";

    // Count lines and bytes (only if llc succeeded and files exist)
    std::error_code ec_fs;
    if (rc1 == 0 && fs::exists(before_asm, ec_fs)) {
        std::ifstream f(before_asm);
        std::string line;
        while (std::getline(f, line)) {
            result.asm_lines_before++;
        }
        result.asm_size_before = static_cast<unsigned>(fs::file_size(before_asm, ec_fs));
    }
    if (rc2 == 0 && fs::exists(after_asm, ec_fs)) {
        std::ifstream f(after_asm);
        std::string line;
        while (std::getline(f, line)) {
            result.asm_lines_after++;
        }
        result.asm_size_after = static_cast<unsigned>(fs::file_size(after_asm, ec_fs));
    }

    return result;
}
