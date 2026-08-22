#pragma once

#include <string>
#include <vector>

#include "Detection.h"
#include "Metrics.h"
#include "llvm/ADT/StringRef.h"

// ============================================================
// Pass Frame Stack
// ============================================================

struct PassFrame {
    std::string pass_name;
    const void *ir_ptr = nullptr;  // Pointer to the IR unit being processed (identity matching)
    IRUnitKind ir_kind = IRUnitKind::Unknown;
    std::string ir_name;
    unsigned depth = 0;
    IRMetrics before;
    bool invalidated = false;
    unsigned event_id = 0;  // Shared event ID for before/after pair
    std::string ir_before;  // IR text captured at BEFORE (only if snapshots enabled)
};

extern std::vector<PassFrame> pass_stack;

// ============================================================
// Structured Events
// ============================================================

struct Event {
    unsigned id = 0;
    std::string event_type;  // "before", "after", "invalidated"
    std::string pass_name;
    std::string pass_type;   // "adaptor", "pipeline", "transformation", "analysis", "other"
    std::string ir_kind;
    std::string ir_name;
    unsigned depth = 0;
    IRMetrics metrics_before;
    IRMetrics metrics_after;
    bool has_changes = false;
    std::string ir_before;  // IR text snapshot (before state)
    std::string ir_after;   // IR text snapshot (after state)
};

extern std::vector<Event> g_events;

// ============================================================
// Pass Classification
// ============================================================

std::string classifyPass(llvm::StringRef name);

// ============================================================
// Delta helpers
// ============================================================

void printDeltaLine(const char *label, unsigned after, unsigned before);
