// Synthetic IMU data generator, used to test the detector without hardware
// and to measure detection accuracy against a known ground truth.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "swingtrack/types.h"

namespace swingtrack {

struct SimConfig {
  int sets = 2;
  int swingsPerSet = 25;
  double basePeakDps = 1100;        // mean peak |w| of set 1
  double setChangePercent = 8;      // each set's mean vs the previous set
  double variabilityPercent = 7;    // swing-to-swing standard deviation
  double fatiguePercent = -4;       // drift from first to last swing of a set
  double backhandFraction = 0.3;
  bool distractors = true;          // add non-swing motion that must be ignored
  uint32_t seed = 1;
};

struct TruthSwing {
  int set = 1;
  uint32_t peakMs = 0;
  double peakDps = 0;
  std::string axis;  // "+Z" forehand, "-Z" backhand
};

struct SimResult {
  Session session;  // raw samples at 200 Hz and set markers (no swings)
  std::vector<TruthSwing> truth;
  int distractorCount = 0;
};

SimResult simulate(const SimConfig &config);

void saveTruth(const std::filesystem::path &path,
               const std::vector<TruthSwing> &truth);
std::vector<TruthSwing> loadTruth(const std::filesystem::path &path);

struct Evaluation {
  int truthCount = 0, detectedCount = 0, matched = 0;
  double precision = 0, recall = 0;
  double meanPeakErrorPercent = 0;     // signed (bias)
  double meanAbsPeakErrorPercent = 0;  // magnitude
  int axisCorrect = 0;
};

// Matches each true swing to the detected swing whose time window (with
// `toleranceMs` slack) contains its peak.
Evaluation evaluate(const std::vector<TruthSwing> &truth,
                    const std::vector<Swing> &detected,
                    uint32_t toleranceMs = 150);

}  // namespace swingtrack
