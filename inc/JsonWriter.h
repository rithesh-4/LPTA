#pragma once

#include <iosfwd>
#include <string>

#include "Codegen.h"
#include "Metrics.h"

std::string jsonEscape(const std::string &s);

void writeMetricsJSON(std::ostream &os, const IRMetrics &m, const std::string &pad);

void writeHistoryJSON(const std::string &filename, const CodegenResult &cg = CodegenResult());
