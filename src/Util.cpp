#include "Util.h"

#include <cassert>
#include <cctype>

std::string sanitizeFilename(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            r += c;
        } else {
            r += '_';
        }
    }
    if (r.empty()) r = "unnamed";
    // Cap length so composed snapshot paths (pass_<id>_<pass>_<kind>_<name>_
    // {before,after}.ll) stay well under filesystem limits (e.g. Windows
    // MAX_PATH 260). Mangled C++ symbols can be thousands of chars.
    static constexpr size_t kMaxNameLen = 120;
    if (r.size() > kMaxNameLen) r.resize(kMaxNameLen);
    // Formal verification
    // Note: collision possible if two different targets sanitize to the same name
    // (e.g. "my target" and "my_target" both -> "my_target"). Avoid by using
    // distinct target names or running with unique output directories.
    for (char c : r) {
        assert((std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') &&
               "sanitizeFilename: result contains unsafe character");
    }
    return r;
}