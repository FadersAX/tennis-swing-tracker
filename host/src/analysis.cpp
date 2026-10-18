#include "swingtrack/analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <sstream>

namespace swingtrack {
namespace {

// Two-sided 95% critical values of Student's t for df = 1..30.
double tCritical95(double df) {
  static const double table[] = {
      12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
      2.201,  2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
      2.080,  2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
  if (df < 1) return table[0];
  if (df > 30) return 1.96;
  return table[static_cast<int>(df) - 1];  // round df down: conservative
}

double variance(const std::vector<double> &v, double mean) {
  if (v.size() < 2) return 0;
  double ss = 0;
  for (double x : v) ss += (x - mean) * (x - mean);
  return ss / (v.size() - 1);
}

std::string fmt(const char *format, double a) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), format, a);
  return buf;
}

}  // namespace

int setAt(const std::vector<SetMarker> &markers, uint32_t tMs) {
  int set = 1;
  uint32_t best = 0;
  bool found = false;
  for (const auto &m : markers) {
    if (m.tMs <= tMs && (!found || m.tMs >= best)) {
      best = m.tMs;
      set = m.set;
      found = true;
    }
  }
  return set;
}

std::vector<Swing> detectSwings(const std::vector<RawSample> &raw,
                                const std::vector<SetMarker> &markers,
                                const swing::DetectorConfig &config) {
  swing::SwingDetector detector(config);
  std::vector<Swing> swings;
  for (const auto &r : raw) {
    swing::SwingEvent e;
    if (detector.update(r.tMs, r.ax, r.ay, r.az, r.gx, r.gy, r.gz, e)) {
      Swing s;
      s.index = static_cast<int>(swings.size()) + 1;
      s.set = setAt(markers, e.startMs);
      s.startMs = e.startMs;
      s.durationMs = e.durationMs;
      s.peakDps = e.peakDps;
      s.peakG = e.peakG;
      s.axis = std::string() + e.signChar() + e.axisChar();
      swings.push_back(s);
    }
  }
  return swings;
}

Stats computeStats(std::vector<double> v) {
  Stats s;
  s.n = v.size();
  if (v.empty()) return s;
  std::sort(v.begin(), v.end());
  s.min = v.front();
  s.max = v.back();
  s.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
  const size_t mid = v.size() / 2;
  s.median = v.size() % 2 ? v[mid] : (v[mid - 1] + v[mid]) / 2;
  s.stddev = std::sqrt(variance(v, s.mean));
  s.cvPercent = s.mean != 0 ? s.stddev / s.mean * 100 : 0;
  return s;
}

SetComparison compareSets(const std::vector<double> &a, int setA,
                          const std::vector<double> &b, int setB) {
  SetComparison c;
  c.setA = setA;
  c.setB = setB;
  const Stats sa = computeStats(a), sb = computeStats(b);
  if (sa.n == 0 || sb.n == 0 || sa.mean == 0) return c;
  const double diff = sb.mean - sa.mean;
  c.changePercent = diff / sa.mean * 100;
  if (sa.n < 2 || sb.n < 2) return c;

  // Welch's t interval for the difference of means.
  const double va = sa.stddev * sa.stddev / sa.n;
  const double vb = sb.stddev * sb.stddev / sb.n;
  const double se = std::sqrt(va + vb);
  double df = 1;
  if (va + vb > 0) {
    df = (va + vb) * (va + vb) /
         (va * va / (sa.n - 1) + vb * vb / (sb.n - 1));
  }
  const double half = tCritical95(df) * se;
  c.ciLowPercent = (diff - half) / sa.mean * 100;
  c.ciHighPercent = (diff + half) / sa.mean * 100;
  c.hasCi = true;
  c.significant = c.ciLowPercent > 0 || c.ciHighPercent < 0;
  return c;
}

Trend fitTrend(const std::vector<double> &y) {
  Trend t;
  const size_t n = y.size();
  if (n < 2) return t;
  const double xMean = (n - 1) / 2.0;
  const double yMean = std::accumulate(y.begin(), y.end(), 0.0) / n;
  double sxy = 0, sxx = 0;
  for (size_t i = 0; i < n; ++i) {
    sxy += (i - xMean) * (y[i] - yMean);
    sxx += (i - xMean) * (i - xMean);
  }
  t.slopeDpsPerSwing = sxy / sxx;
  t.percentPer10Swings = yMean != 0 ? t.slopeDpsPerSwing * 10 / yMean * 100 : 0;
  return t;
}

SessionSummary summarize(const std::vector<Swing> &swings) {
  SessionSummary out;
  std::vector<double> allPeaks;
  std::map<int, std::vector<const Swing *>> bySet;
  for (const auto &s : swings) {
    allPeaks.push_back(s.peakDps);
    bySet[s.set].push_back(&s);
    ++out.axisCounts[s.axis];
  }
  out.peakDps = computeStats(allPeaks);
  out.trend = fitTrend(allPeaks);
  if (swings.size() >= 2) {
    out.spanMinutes = (swings.back().startMs - swings.front().startMs) / 60000.0;
  }

  std::vector<std::vector<double>> setPeaks;
  for (const auto &[set, list] : bySet) {
    SetSummary ss;
    ss.set = set;
    std::vector<double> peaks;
    double dur = 0, g = 0;
    for (const Swing *s : list) {
      peaks.push_back(s->peakDps);
      dur += s->durationMs;
      g += s->peakG;
      ++ss.axisCounts[s->axis];
    }
    ss.peakDps = computeStats(peaks);
    ss.meanDurationMs = dur / list.size();
    ss.meanPeakG = g / list.size();
    if (list.size() >= 2) {
      const double minutes =
          (list.back()->startMs - list.front()->startMs) / 60000.0;
      if (minutes > 0) ss.swingsPerMinute = (list.size() - 1) / minutes;
    }
    if (!out.sets.empty()) {
      out.comparisons.push_back(
          compareSets(setPeaks.back(), out.sets.back().set, peaks, set));
    }
    out.sets.push_back(ss);
    setPeaks.push_back(peaks);
  }
  return out;
}

std::string formatSummary(const SessionSummary &s) {
  std::ostringstream o;
  o << "Session summary\n"
    << "===============\n";
  if (s.peakDps.n == 0) {
    o << "No swings detected.\n";
    return o.str();
  }
  o << "Swings:            " << s.peakDps.n << " over "
    << fmt("%.1f", s.spanMinutes) << " min\n"
    << "Peak ang. vel.:    mean " << fmt("%.0f", s.peakDps.mean) << " dps, "
    << "median " << fmt("%.0f", s.peakDps.median) << ", range "
    << fmt("%.0f", s.peakDps.min) << "-" << fmt("%.0f", s.peakDps.max) << "\n"
    << "Consistency (CV):  " << fmt("%.1f", s.peakDps.cvPercent)
    << "%  (lower = more consistent)\n"
    << "Trend:             " << fmt("%+.1f", s.trend.percentPer10Swings)
    << "% per 10 swings\n"
    << "Rotation axes:     ";
  bool first = true;
  for (const auto &[axis, count] : s.axisCounts) {
    o << (first ? "" : ", ") << axis << " x" << count;
    first = false;
  }
  o << "\n\nPer set\n-------\n";
  char line[160];
  std::snprintf(line, sizeof(line), "%-4s %6s %10s %8s %8s %9s %8s\n", "set",
                "swings", "mean dps", "sd", "CV %", "dur ms", "per min");
  o << line;
  for (const auto &ss : s.sets) {
    std::snprintf(line, sizeof(line),
                  "%-4d %6zu %10.0f %8.0f %8.1f %9.0f %8.1f\n", ss.set,
                  ss.peakDps.n, ss.peakDps.mean, ss.peakDps.stddev,
                  ss.peakDps.cvPercent, ss.meanDurationMs, ss.swingsPerMinute);
    o << line;
  }
  if (!s.comparisons.empty()) {
    o << "\nSet-to-set change in mean peak angular velocity\n"
      << "-----------------------------------------------\n";
    for (const auto &c : s.comparisons) {
      o << "set " << c.setA << " -> set " << c.setB << ": "
        << fmt("%+.1f", c.changePercent) << "%";
      if (c.hasCi) {
        o << "  (95% CI " << fmt("%+.1f", c.ciLowPercent) << "% to "
          << fmt("%+.1f", c.ciHighPercent) << "%, "
          << (c.significant ? "a real change" : "within noise") << ")";
      } else {
        o << "  (too few swings for a confidence interval)";
      }
      o << "\n";
    }
  }
  return o.str();
}

}  // namespace swingtrack
