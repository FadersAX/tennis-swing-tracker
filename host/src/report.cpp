#include "swingtrack/report.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>

#include "swingtrack/svg_chart.h"

namespace swingtrack {
namespace {

// Colour-blind-friendly categorical palette (Okabe-Ito).
const char *kSetColors[] = {"#0072B2", "#E69F00", "#009E73", "#CC79A7",
                            "#D55E00", "#56B4E9", "#F0E442", "#000000"};

const char *setColor(int set) {
  const int n = static_cast<int>(sizeof(kSetColors) / sizeof(kSetColors[0]));
  return kSetColors[((set - 1) % n + n) % n];
}

void writeSwingChart(const std::filesystem::path &path,
                     const std::vector<Swing> &swings,
                     const SessionSummary &summary) {
  SvgChart chart(900, 420, "Peak angular velocity per swing", "swing number",
                 "peak angular velocity (deg/s)");
  double yMax = 0;
  for (const auto &s : swings) yMax = std::max(yMax, double(s.peakDps));
  chart.setXRange(0, std::max(double(swings.size()), 1.0) + 1);
  chart.setYRange(0, yMax * 1.15 + 1);

  std::map<int, std::vector<Point>> bySet;
  for (size_t i = 0; i < swings.size(); ++i) {
    bySet[swings[i].set].emplace_back(double(i + 1), swings[i].peakDps);
  }
  for (const auto &[set, pts] : bySet) {
    chart.addPoints(pts, setColor(set));
    chart.addLegend("set " + std::to_string(set), setColor(set));
  }
  // Mean line spanning each set's swings
  for (const auto &ss : summary.sets) {
    const auto &pts = bySet[ss.set];
    if (pts.empty()) continue;
    chart.addLine({{pts.front().first - 0.4, ss.peakDps.mean},
                   {pts.back().first + 0.4, ss.peakDps.mean}},
                  setColor(ss.set), 2.5);
  }
  chart.save(path.string());
}

void writeTraceChart(const std::filesystem::path &path, const Session &session,
                     const std::vector<Swing> &swings) {
  const auto &raw = session.raw;
  const double t0 = raw.front().tMs;
  const double spanS = (raw.back().tMs - t0) / 1000.0;
  SvgChart chart(1100, 380, "Gyro magnitude with detected swings",
                 "time (s)", "|angular velocity| (deg/s)");
  chart.setXRange(0, std::max(spanS, 1.0));

  // Min/max per pixel column keeps peaks visible even for long sessions.
  const int columns = 1000;
  std::vector<double> lo(columns, 1e9), hi(columns, -1);
  double yMax = 0;
  for (const auto &r : raw) {
    const double mag = std::sqrt(r.gx * r.gx + r.gy * r.gy + r.gz * r.gz);
    const double t = (r.tMs - t0) / 1000.0;
    int c = static_cast<int>(t / std::max(spanS, 1e-9) * (columns - 1));
    c = std::clamp(c, 0, columns - 1);
    lo[c] = std::min(lo[c], mag);
    hi[c] = std::max(hi[c], mag);
    yMax = std::max(yMax, mag);
  }
  chart.setYRange(0, yMax * 1.1 + 1);

  for (const auto &s : swings) {
    const double a = (s.startMs - t0) / 1000.0;
    chart.addBand(a, a + s.durationMs / 1000.0, setColor(s.set), 0.45);
  }
  std::vector<Point> pts;
  for (int c = 0; c < columns; ++c) {
    if (hi[c] < 0) continue;
    const double t = c * spanS / (columns - 1);
    pts.emplace_back(t, lo[c]);
    if (hi[c] != lo[c]) pts.emplace_back(t, hi[c]);
  }
  chart.addLine(pts, "#3b4a5a", 1.0);
  for (const auto &m : session.markers) {
    if (m.tMs > t0) {
      chart.addVLine((m.tMs - t0) / 1000.0, "#57606a",
                     "set " + std::to_string(m.set));
    }
  }
  chart.addLegend("detected swing", setColor(1));
  chart.save(path.string());
}

std::string htmlEscape(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else out += c;
  }
  return out;
}

}  // namespace

void writeReport(const std::filesystem::path &dir, const Session &session,
                 const std::vector<Swing> &swings,
                 const SessionSummary &summary) {
  const std::string text = formatSummary(summary);
  {
    std::ofstream out(dir / "summary.txt");
    if (!out) throw std::runtime_error("cannot write summary.txt");
    out << text;
  }
  writeSwingChart(dir / "swings.svg", swings, summary);
  const bool hasTrace = session.raw.size() >= 2;
  if (hasTrace) writeTraceChart(dir / "trace.svg", session, swings);

  std::ofstream html(dir / "report.html");
  if (!html) throw std::runtime_error("cannot write report.html");
  html << "<!doctype html>\n<html><head><meta charset=\"utf-8\">"
       << "<title>Swing session report</title><style>"
       << "body{font-family:Segoe UI,Helvetica,Arial,sans-serif;margin:24px;"
       << "background:#fff;color:#1f2328;max-width:1140px}"
       << "pre{background:#f6f8fa;padding:16px;border-radius:6px;"
       << "overflow-x:auto}img{max-width:100%;height:auto;margin:12px 0}"
       << "</style></head><body>\n<h1>Swing session report</h1>\n<pre>"
       << htmlEscape(text) << "</pre>\n<img src=\"swings.svg\" "
       << "alt=\"Peak angular velocity per swing\">\n";
  if (hasTrace) {
    html << "<img src=\"trace.svg\" alt=\"Gyro magnitude over time\">\n";
  }
  html << "</body></html>\n";
}

}  // namespace swingtrack
