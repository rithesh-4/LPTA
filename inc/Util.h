#pragma once

#include <string>

// Sanitize a string for safe filesystem usage
// Replaces non-alphanumeric (except _ - .) with _
std::string sanitizeFilename(const std::string& s);