#include "Tracker.h"

#include "llvm/Support/raw_ostream.h"

using namespace llvm;

std::vector<PassFrame> pass_stack;

std::vector<Event> g_events;

// ============================================================
// Pass Classification
// ============================================================

std::string classifyPass(StringRef name) {
    if (name.find("Adaptor") != StringRef::npos)
        return "adaptor";
    if (name.find("PassManager") != StringRef::npos)
        return "pipeline";
    if (name.find("ExtraLoopPassManager") != StringRef::npos)
        return "pipeline";
    if (name.find("RequireAnalysis") != StringRef::npos)
        return "analysis";
    if (name.find("InvalidateAnalysis") != StringRef::npos)
        return "adaptor";
    if (name.find("Analysis") != StringRef::npos)
        return "analysis";
    return "transformation";
}

// Formal verification: classifyPass categorizes LLVM pass names.
// Postcondition: result is one of {"adaptor", "pipeline", "analysis", "transformation"}.
// Precondition: "InvalidateAnalysis" must be checked BEFORE "Analysis" to ensure
//   correct classification priority.

// ============================================================
// Delta helpers
// ============================================================

// Formal verification: printDeltaLine safely computes and displays a delta.
// Uses long long to prevent overflow when subtracting large unsigned values.
void printDeltaLine(const char *label, unsigned after, unsigned before) {
    long long delta = static_cast<long long>(after) - static_cast<long long>(before);
    if (delta == 0) return;
    errs() << "      " << label << ": " << before << " -> " << after
           << " (" << (delta > 0 ? "+" : "") << delta << ")\n";
}
