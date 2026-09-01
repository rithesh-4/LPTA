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