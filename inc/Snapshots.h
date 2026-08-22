#pragma once

#include <set>
#include <string>

#include "llvm/ADT/Any.h"
#include "llvm/ADT/StringRef.h"

extern const std::set<std::string> g_snapshot_allowlist;

bool shouldSnapshot(llvm::StringRef pass_name);

// Sanitize filename for filesystem safety
std::string sanitizeFilename(const std::string &s);

void saveIRSnapshot(const std::string &suffix, const llvm::Any &IR,
                    const std::string &pass_name, unsigned event_id);
