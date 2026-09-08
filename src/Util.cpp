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
    // Windows refuses reserved device names (CON, NUL, COM1, ...) and
    // trailing dots/spaces — both would make snapshot creation fail.
    // Suffix such names so they stay valid on every platform.
    // (A name with an extension, e.g. "nul.ll", is legal as-is.)
    {
        std::string up = r;
        for (char &c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        static const char *reserved[] = {"CON",  "PRN",  "AUX",  "NUL",  "COM1",
                                         "COM2", "COM3", "COM4", "COM5", "COM6",
                                         "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                         "LPT3", "LPT4", "LPT5", "LPT6", "LPT7",
                                         "LPT8", "LPT9", nullptr};
        if (up.find('.') == std::string::npos) {
            for (int i = 0; reserved[i]; i++) {
                if (up == reserved[i]) {
                    r += "_";
                    break;
                }
            }
        }
        if (!r.empty() && (r.back() == '.' || r.back() == ' ')) r += "_";
    }
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