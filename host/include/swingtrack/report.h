// Writes the analysis outputs for a session folder.
#pragma once

#include <filesystem>
#include <vector>

#include "swingtrack/analysis.h"
#include "swingtrack/types.h"

namespace swingtrack {

// Writes into `dir`:
//   summary.txt   text report (same as printed to the console)
//   swings.svg    peak angular velocity per swing, coloured by set
//   trace.svg     gyro magnitude over time with detected swings shaded
//                 (only if raw data is available)
//   report.html   the above on one page
void writeReport(const std::filesystem::path &dir, const Session &session,
                 const std::vector<Swing> &swings,
                 const SessionSummary &summary);

}  // namespace swingtrack
