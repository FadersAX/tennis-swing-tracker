// Swing detection on logged data, and session statistics.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "SwingDetector.h"
#include "swingtrack/types.h"

namespace swingtrack {

// Set number in effect at time t (1 if there are no markers before t).
int setAt(const std::vector<SetMarker> &markers, uint32_t tMs);

// Runs the firmware's detector over logged raw samples.
std::vector<Swing> detectSwings(const std::vector<RawSample> &raw,
                                const std::vector<SetMarker> &markers,
                                const swing::DetectorConfig &config = {});

struct Stats {
  size_t n = 0;
  double mean = 0, median = 0, stddev = 0, min = 0, max = 0;
  double cvPercent = 0;  // coefficient of variation: stddev / mean * 100
};

Stats computeStats(std::vector<double> values);

struct SetSummary {
  int set = 1;
  Stats peakDps;
  double meanDurationMs = 0;
  double meanPeakG = 0;
  double swingsPerMinute = 0;  // 0 if fewer than 2 swings
  std::map<std::string, int> axisCounts;
};

// Change in mean peak angular velocity from set A to set B, with a 95%
// confidence interval (Welch). `significant` is true when the interval does
// not include zero, i.e. the change is bigger than swing-to-swing noise.
struct SetComparison {
  int setA = 1, setB = 2;
  double changePercent = 0;
  double ciLowPercent = 0, ciHighPercent = 0;
  bool hasCi = false;
  bool significant = false;
};

// Linear fit of peak angular velocity against swing number.
struct Trend {
  double slopeDpsPerSwing = 0;
  double percentPer10Swings = 0;
};

struct SessionSummary {
  Stats peakDps;
  std::vector<SetSummary> sets;
  std::vector<SetComparison> comparisons;  // each set vs the previous one
  Trend trend;
  std::map<std::string, int> axisCounts;
  double spanMinutes = 0;
};

SetComparison compareSets(const std::vector<double> &a, int setA,
                          const std::vector<double> &b, int setB);
Trend fitTrend(const std::vector<double> &values);
SessionSummary summarize(const std::vector<Swing> &swings);

// Human-readable multi-line report.
std::string formatSummary(const SessionSummary &summary);

}  // namespace swingtrack
