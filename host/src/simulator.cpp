#include "swingtrack/simulator.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace swingtrack {
namespace {

const double kPi = 3.14159265358979323846;
const uint32_t kDtMs = 5;  // 200 Hz, same as the firmware
const double kRadiusM = 0.12;  // sensor distance from the rotation centre
const double kReachMs = 1000;  // no pulse is non-zero further than this from its centre

double gauss(double t, double mu, double sigma) {
  const double z = (t - mu) / sigma;
  return std::exp(-0.5 * z * z);
}

// Angular velocity is a sum of these pulses (per sample, deg/s on each axis).
struct Pulse {
  double centerMs;
  double sigmaRiseMs, sigmaFallMs;
  double amplitude;  // magnitude at the centre, deg/s
  double ux, uy, uz; // unit rotation axis

  double value(double t) const {
    if (t < centerMs - 5 * sigmaRiseMs || t > centerMs + 5 * sigmaFallMs) {
      return 0;
    }
    return amplitude *
           gauss(t, centerMs, t < centerMs ? sigmaRiseMs : sigmaFallMs);
  }
};

struct Spike {  // impact shock in the accelerometer
  uint32_t tMs;
  double g;
};

}  // namespace

SimResult simulate(const SimConfig &cfg) {
  std::mt19937 rng(cfg.seed);
  std::uniform_real_distribution<double> uni(0.0, 1.0);
  std::normal_distribution<double> normal(0.0, 1.0);

  SimResult result;
  std::vector<Pulse> pulses;
  std::vector<Spike> spikes;

  double t = 2000;  // ms; leave a still period at the start
  for (int set = 1; set <= cfg.sets; ++set) {
    if (set > 1) t += 8000;  // rest between sets
    result.session.markers.push_back({set, set == 1 ? 0u : uint32_t(t - 1000)});
    const double setMean =
        cfg.basePeakDps * std::pow(1 + cfg.setChangePercent / 100, set - 1);

    for (int i = 0; i < cfg.swingsPerSet; ++i) {
      const double gapMs = 2500 + uni(rng) * 2500;

      if (cfg.distractors && uni(rng) < 0.5) {
        const double at = t + gapMs * (0.25 + 0.3 * uni(rng));
        if (uni(rng) < 0.6) {
          // Slow movement (walking, bouncing the ball, twirling the racket):
          // can cross the start threshold but never reaches minPeakDps.
          const double amp = 100 + uni(rng) * 320;
          pulses.push_back({at, 200, 200, amp, 0.6, 0.8, 0.0});
        } else {
          // Sharp jolt (tapping the racket on a shoe): high peak but only
          // ~15 ms long, so it must fail the minimum-duration check.
          const double amp = 400 + uni(rng) * 500;
          pulses.push_back({at, 4, 6, amp, 0.0, 0.7071, 0.7071});
          spikes.push_back({uint32_t(at), 3 + uni(rng) * 3});
        }
        ++result.distractorCount;
      }
      t += gapMs;

      const double fatigue =
          cfg.swingsPerSet > 1
              ? 1 + cfg.fatiguePercent / 100 * i / (cfg.swingsPerSet - 1)
              : 1;
      const double peak = std::max(
          550.0, setMean * fatigue * (1 + cfg.variabilityPercent / 100 *
                                              normal(rng)));
      const bool backhand = uni(rng) < cfg.backhandFraction;
      const double sign = backhand ? -1 : 1;
      // Mostly about Z, with some X/Y component (wrist is not a perfect hinge).
      const double nx = 0.35, ny = 0.15 * sign, nz = 1.0 * sign;
      const double norm = std::sqrt(nx * nx + ny * ny + nz * nz);

      const double sigma = 50 + uni(rng) * 15;
      pulses.push_back({t - 320, 90, 90, 0.2 * peak, -nx / norm, -ny / norm,
                        -nz / norm});  // backswing (opposite direction)
      pulses.push_back({t, sigma, sigma * 1.3, peak, nx / norm, ny / norm,
                        nz / norm});   // forward swing + follow-through
      spikes.push_back({uint32_t(t + 10), 3 + uni(rng) * 2});  // ball impact

      result.truth.push_back({set, uint32_t(t), peak, backhand ? "-Z" : "+Z"});
    }
  }
  const uint32_t endMs = uint32_t(t + 3000);

  // Residual gyro bias left after calibration, drifting slowly (random walk).
  double bias[3] = {normal(rng) * 0.8, normal(rng) * 0.8, normal(rng) * 0.8};
  std::sort(pulses.begin(), pulses.end(),
            [](const Pulse &a, const Pulse &b) { return a.centerMs < b.centerMs; });

  size_t firstActive = 0;
  for (uint32_t ms = 0; ms <= endMs; ms += kDtMs) {
    double g[3] = {0, 0, 0};
    while (firstActive < pulses.size() &&
           pulses[firstActive].centerMs + kReachMs < ms) {
      ++firstActive;  // pulses are sorted, so skip ones that have ended
    }
    for (size_t p = firstActive; p < pulses.size(); ++p) {
      if (pulses[p].centerMs - kReachMs > ms) break;
      const double v = pulses[p].value(ms);
      g[0] += v * pulses[p].ux;
      g[1] += v * pulses[p].uy;
      g[2] += v * pulses[p].uz;
    }

    RawSample r;
    r.tMs = ms;
    for (int i = 0; i < 3; ++i) {
      bias[i] += normal(rng) * 0.01;
      g[i] += bias[i] + normal(rng) * 0.7;  // MPU6050 noise ~0.05 dps/sqrt(Hz)
      g[i] = std::clamp(g[i], -2000.0, 2000.0);  // sensor full scale
    }
    r.gx = float(g[0]);
    r.gy = float(g[1]);
    r.gz = float(g[2]);

    // Accelerometer: gravity + centripetal acceleration (w^2 r) along the
    // racket + noise + impact shocks.
    const double wRad =
        std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) * kPi / 180;
    double a[3] = {wRad * wRad * kRadiusM / 9.81, 0.05, 1.0};
    for (const auto &s : spikes) {
      const double dt = double(ms) - s.tMs;
      if (dt >= 0 && dt < 10) {
        a[1] += s.g * (dt < 5 ? 1 : -0.6);
      }
    }
    for (double &v : a) v = std::clamp(v + normal(rng) * 0.02, -16.0, 16.0);
    r.ax = float(a[0]);
    r.ay = float(a[1]);
    r.az = float(a[2]);
    result.session.raw.push_back(r);
  }
  return result;
}

void saveTruth(const std::filesystem::path &path,
               const std::vector<TruthSwing> &truth) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path.string());
  out << "set,t_peak_ms,peak_dps,axis\n";
  for (const auto &s : truth) {
    out << s.set << ',' << s.peakMs << ',' << s.peakDps << ',' << s.axis
        << '\n';
  }
}

std::vector<TruthSwing> loadTruth(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  std::vector<TruthSwing> truth;
  std::string line;
  std::getline(in, line);  // header
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream row(line);
    std::string set, t, peak, axis;
    std::getline(row, set, ',');
    std::getline(row, t, ',');
    std::getline(row, peak, ',');
    std::getline(row, axis);
    if (!axis.empty() && axis.back() == '\r') axis.pop_back();
    try {
      truth.push_back({std::stoi(set), uint32_t(std::stoul(t)),
                       std::stod(peak), axis});
    } catch (const std::exception &) {
      throw std::runtime_error("bad row in " + path.string() + ": " + line);
    }
  }
  return truth;
}

Evaluation evaluate(const std::vector<TruthSwing> &truth,
                    const std::vector<Swing> &detected, uint32_t toleranceMs) {
  Evaluation e;
  e.truthCount = int(truth.size());
  e.detectedCount = int(detected.size());
  std::vector<bool> used(detected.size(), false);
  double errSum = 0, absErrSum = 0;

  for (const auto &ts : truth) {
    for (size_t i = 0; i < detected.size(); ++i) {
      const Swing &d = detected[i];
      const uint32_t lo = d.startMs > toleranceMs ? d.startMs - toleranceMs : 0;
      const uint32_t hi = d.startMs + d.durationMs + toleranceMs;
      if (!used[i] && ts.peakMs >= lo && ts.peakMs <= hi) {
        used[i] = true;
        ++e.matched;
        const double err = (d.peakDps - ts.peakDps) / ts.peakDps * 100;
        errSum += err;
        absErrSum += std::fabs(err);
        if (d.axis == ts.axis) ++e.axisCorrect;
        break;
      }
    }
  }
  e.precision = e.detectedCount ? double(e.matched) / e.detectedCount : 0;
  e.recall = e.truthCount ? double(e.matched) / e.truthCount : 0;
  if (e.matched) {
    e.meanPeakErrorPercent = errSum / e.matched;
    e.meanAbsPeakErrorPercent = absErrSum / e.matched;
  }
  return e;
}

}  // namespace swingtrack
