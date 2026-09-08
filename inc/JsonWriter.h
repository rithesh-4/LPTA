#pragma once

#include <iosfwd>
#include <string>

#include "Codegen.h"
#include "Metrics.h"

std::string jsonEscape(const std::string &s);

void writeMetricsJSON(std::ostream &os, const IRMetrics &m, const std::string &pad);

// Returns false when history.json could not be written (open failure or I/O
// error mid-write, e.g. disk full). Callers must fail the run on false:
// a "successful" run without history.json is a lie to scripts.
bool writeHistoryJSON(const std::string &filename, const CodegenResult &cg = CodegenResult());
