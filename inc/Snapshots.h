#pragma once

#include <cstddef>
#include <set>
#include <string>

#include "llvm/ADT/Any.h"
#include "llvm/ADT/StringRef.h"

// Single source of truth for the per-snapshot size cap (AGENTS.md invariant).
// A before/after pair is kept only if BOTH sides fit; otherwise both are
// dropped so the dashboard never renders a one-sided diff.
inline constexpr size_t kMaxSnapshotBytes = 4 * 1024 * 1024;

extern const std::set<std::string> g_snapshot_allowlist;

bool shouldSnapshot(llvm::StringRef pass_name);

// Sanitize filename for filesystem safety
std::string sanitizeFilename(const std::string &s);

void saveIRSnapshot(const std::string &suffix, const llvm::Any &IR,
                    const std::string &pass_name, unsigned event_id);
