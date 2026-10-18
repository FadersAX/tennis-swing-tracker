// Unit and end-to-end tests. Tiny self-contained harness, no dependencies.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "SwingDetector.h"
#include "swingtrack/analysis.h"
#include "swingtrack/protocol.h"
#include "swingtrack/session_io.h"
#include "swingtrack/simulator.h"
#include "swingtrack/svg_chart.h"

using namespace swingtrack;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    ++gChecks;                                                         \
    if (!(cond)) {                                                     \
      ++gFailures;                                                     \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                  \
  } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(double(a) - double(b)) <= (tol))

std::vector<std::pair<const char *, std::function<void()>>> &registry() {
  static std::vector<std::pair<const char *, std::function<void()>>> r;
  return r;
}

struct Register {
  Register(const char *name, std::function<void()> fn) {
    registry().emplace_back(name, std::move(fn));
  }
};

#define TEST(name)                                 \
  void name();                                     \
  Register reg_##name(#name, name);                \
  void name()

// Feeds a gaussian angular-velocity pulse about Z into a detector at 200 Hz.
std::vector<swing::SwingEvent> runPulses(
    const std::vector<std::pair<double, double>> &pulses,  // (centre ms, peak)
    double sigmaMs, double totalMs,
    const swing::DetectorConfig &cfg = {}) {
  swing::SwingDetector d(cfg);
  std::vector<swing::SwingEvent> events;
  for (uint32_t t = 0; t <= totalMs; t += 5) {
    double gz = 0;
    for (const auto &[c, peak] : pulses) {
      const double z = (t - c) / sigmaMs;
      gz += peak * std::exp(-0.5 * z * z);
    }
    swing::SwingEvent e;
    if (d.update(t, 0, 0, 1, 0, 0, float(gz), e)) events.push_back(e);
  }
  return events;
}

// ---------------------------------------------------------------- detector

TEST(detector_finds_single_swing) {
  const auto ev = runPulses({{1000, 1200}}, 50, 2000);
  CHECK(ev.size() == 1);
  if (ev.size() != 1) return;
  CHECK_NEAR(ev[0].peakDps, 1200, 1);
  CHECK(ev[0].axisChar() == 'Z');
  CHECK(ev[0].signChar() == '+');
  CHECK(ev[0].startMs > 850 && ev[0].startMs < 1000);
  CHECK(ev[0].durationMs > 100 && ev[0].durationMs < 300);
}

TEST(detector_reports_rotation_direction) {
  const auto ev = runPulses({{1000, -900}}, 50, 2000);
  CHECK(ev.size() == 1);
  if (!ev.empty()) CHECK(ev[0].signChar() == '-');
}

TEST(detector_rejects_weak_motion) {
  // Crosses the start threshold (300) but peaks below minPeakDps (500).
  CHECK(runPulses({{1000, 420}}, 150, 2000).empty());
  // Never crosses the start threshold.
  CHECK(runPulses({{1000, 250}}, 50, 2000).empty());
}

TEST(detector_rejects_short_jolt) {
  // 900 dps but only ~10 ms wide: an impact, not a swing.
  CHECK(runPulses({{1000, 900}}, 4, 2000).empty());
}

TEST(detector_separates_consecutive_swings) {
  const auto ev = runPulses({{1000, 1000}, {2000, 1100}, {3000, 1200}}, 50, 4000);
  CHECK(ev.size() == 3);
  if (ev.size() == 3) CHECK(ev[0].peakDps < ev[1].peakDps && ev[1].peakDps < ev[2].peakDps);
}

TEST(detector_refractory_merges_bounce_back) {
  // A second pulse 200 ms after the first one ends falls inside the 250 ms
  // refractory window, so it is not counted as another swing.
  swing::DetectorConfig cfg;
  const auto ev = runPulses({{1000, 1000}, {1350, 700}}, 30, 2500, cfg);
  CHECK(ev.size() == 1);
  cfg.refractoryMs = 0;
  CHECK(runPulses({{1000, 1000}, {1350, 700}}, 30, 2500, cfg).size() == 2);
}

TEST(detector_keeps_swing_after_strong_backswing) {
  // A backswing that crosses the start threshold but is too weak to count
  // must not block the forward swing 300 ms later.
  const auto ev = runPulses({{700, -330}, {1000, 1500}}, 60, 2000);
  CHECK(ev.size() == 1);
  if (!ev.empty()) CHECK_NEAR(ev[0].peakDps, 1500, 5);
}

TEST(detector_rejects_overlong_motion) {
  swing::DetectorConfig cfg;
  cfg.maxDurationMs = 400;
  CHECK(runPulses({{1500, 900}}, 400, 3000, cfg).empty());
}

// ---------------------------------------------------------------- protocol

TEST(protocol_parses_all_message_types) {
  Message m = parseLine("R,1234,0.01,-0.98,0.05,12,-340,1500\r\n");
  CHECK(m.type == MessageType::Raw);
  CHECK(m.raw.tMs == 1234);
  CHECK_NEAR(m.raw.ay, -0.98, 1e-6);
  CHECK_NEAR(m.raw.gz, 1500, 1e-6);

  m = parseLine("S,7,2,50000,180,1234.5,4.20,-Z");
  CHECK(m.type == MessageType::Swing);
  CHECK(m.swing.index == 7 && m.swing.set == 2);
  CHECK(m.swing.startMs == 50000 && m.swing.durationMs == 180);
  CHECK_NEAR(m.swing.peakDps, 1234.5, 1e-3);
  CHECK(m.swing.axis == "-Z");

  m = parseLine("M,3,99000");
  CHECK(m.type == MessageType::Marker);
  CHECK(m.marker.set == 3 && m.marker.tMs == 99000);

  m = parseLine("# gyro bias dps: 0.1,0.2,0.3");
  CHECK(m.type == MessageType::Info);
  CHECK(m.text == "gyro bias dps: 0.1,0.2,0.3");
}

TEST(protocol_rejects_malformed_lines) {
  CHECK(parseLine("").type == MessageType::Invalid);
  CHECK(parseLine("R,1,2,3").type == MessageType::Invalid);          // too short
  CHECK(parseLine("R,12,0.1,0.2,abc,1,2,3").type == MessageType::Invalid);
  CHECK(parseLine(",0.98,0.05,12,-340,1500").type == MessageType::Invalid);  // partial
  CHECK(parseLine("S,1,1,100,180,900,3.1,+Q").type == MessageType::Invalid);
  CHECK(parseLine("M,-1,5").type == MessageType::Invalid);
  CHECK(parseLine("X,1,2").type == MessageType::Invalid);
}

// ---------------------------------------------------------------- analysis

TEST(stats_basic) {
  const Stats s = computeStats({2, 4, 4, 4, 5, 5, 7, 9});
  CHECK(s.n == 8);
  CHECK_NEAR(s.mean, 5, 1e-9);
  CHECK_NEAR(s.median, 4.5, 1e-9);
  CHECK_NEAR(s.stddev, 2.13809, 1e-4);  // sample standard deviation
  CHECK_NEAR(s.cvPercent, 42.7618, 1e-3);
  CHECK(s.min == 2 && s.max == 9);
  CHECK(computeStats({}).n == 0);
}

TEST(compare_sets_detects_real_change) {
  std::vector<double> a, b;
  for (int i = 0; i < 20; ++i) {
    a.push_back(1000 + (i % 5 - 2) * 20);  // mean 1000, small spread
    b.push_back(1120 + (i % 5 - 2) * 20);  // mean 1120
  }
  const SetComparison c = compareSets(a, 1, b, 2);
  CHECK_NEAR(c.changePercent, 12.0, 1e-9);
  CHECK(c.hasCi && c.significant);
  CHECK(c.ciLowPercent < 12 && c.ciHighPercent > 12);
}

TEST(compare_sets_flags_noise) {
  const SetComparison c =
      compareSets({900, 1100, 950, 1050}, 1, {1000, 1150, 880, 1010}, 2);
  CHECK(c.hasCi);
  CHECK(!c.significant);
}

TEST(trend_slope) {
  const Trend t = fitTrend({100, 102, 104, 106, 108});
  CHECK_NEAR(t.slopeDpsPerSwing, 2, 1e-9);
  CHECK_NEAR(t.percentPer10Swings, 20.0 / 104 * 100, 1e-9);
}

TEST(set_assignment_from_markers) {
  const std::vector<SetMarker> m = {{1, 0}, {2, 5000}, {3, 9000}};
  CHECK(setAt(m, 100) == 1);
  CHECK(setAt(m, 5000) == 2);
  CHECK(setAt(m, 12000) == 3);
  CHECK(setAt({}, 12000) == 1);
}

TEST(summary_groups_by_set) {
  std::vector<Swing> swings;
  for (int i = 0; i < 6; ++i) {
    Swing s;
    s.index = i + 1;
    s.set = i < 3 ? 1 : 2;
    s.startMs = 1000 + i * 4000;
    s.durationMs = 200;
    s.peakDps = float(i < 3 ? 1000 : 1100);
    s.axis = i % 2 ? "-Z" : "+Z";
    swings.push_back(s);
  }
  const SessionSummary sum = summarize(swings);
  CHECK(sum.sets.size() == 2);
  CHECK(sum.comparisons.size() == 1);
  CHECK_NEAR(sum.comparisons[0].changePercent, 10, 1e-6);
  CHECK(sum.axisCounts.at("+Z") == 3 && sum.axisCounts.at("-Z") == 3);
  CHECK_NEAR(sum.sets[0].swingsPerMinute, 2 / (8000 / 60000.0), 1e-6);
  CHECK(formatSummary(sum).find("set 1 -> set 2: +10.0%") != std::string::npos);
}

// ---------------------------------------------------------------- io / svg

TEST(session_roundtrip) {
  const auto dir = std::filesystem::temp_directory_path() / "swingtrack_test_io";
  std::filesystem::remove_all(dir);
  Session s;
  s.raw.push_back({10, 0.1f, 0.2f, 0.98f, 1.5f, -2.5f, 300.0f});
  s.raw.push_back({15, 0.1f, 0.2f, 0.98f, 1.5f, -2.5f, 310.0f});
  Swing sw;
  sw.index = 1; sw.set = 2; sw.startMs = 10; sw.durationMs = 150;
  sw.peakDps = 987.6f; sw.peakG = 3.21f; sw.axis = "-X";
  s.swings.push_back(sw);
  s.markers.push_back({2, 5});
  saveSession(dir, s);

  const Session l = loadSession(dir);
  CHECK(l.raw.size() == 2 && l.swings.size() == 1 && l.markers.size() == 1);
  if (l.raw.size() == 2) CHECK_NEAR(l.raw[1].gz, 310, 0.05);
  if (!l.swings.empty()) {
    CHECK(l.swings[0].axis == "-X" && l.swings[0].set == 2);
    CHECK_NEAR(l.swings[0].peakDps, 987.6, 0.05);
  }
  std::filesystem::remove_all(dir);
}

TEST(nice_ticks) {
  const auto t = niceTicks(0, 1340, 6);
  CHECK(!t.empty());
  CHECK(t.front() == 0);
  if (t.size() > 1) CHECK_NEAR(t[1] - t[0], 200, 1e-9);
  CHECK(t.back() <= 1340);
}

// ---------------------------------------------------------------- end to end

TEST(simulated_session_end_to_end) {
  SimConfig cfg;
  cfg.sets = 3;
  cfg.swingsPerSet = 30;
  cfg.setChangePercent = 10;
  cfg.seed = 42;
  const SimResult r = simulate(cfg);
  CHECK(r.distractorCount > 10);  // the detector must ignore these

  const auto swings = detectSwings(r.session.raw, r.session.markers);
  const Evaluation e = evaluate(r.truth, swings);
  std::printf("    simulated: %d true, %d detected, precision %.1f%%, "
              "recall %.1f%%, mean |peak error| %.2f%%\n",
              e.truthCount, e.detectedCount, e.precision * 100,
              e.recall * 100, e.meanAbsPeakErrorPercent);
  CHECK(e.recall >= 0.97);
  CHECK(e.precision >= 0.97);
  CHECK(e.meanAbsPeakErrorPercent < 2.0);
  CHECK(e.axisCorrect == e.matched);

  // Sets are assigned from markers, and the detected set-to-set change should
  // be close to the simulated +10% (individual swings vary by 7%, so allow
  // some sampling error).
  const SessionSummary sum = summarize(swings);
  CHECK(sum.sets.size() == 3);
  for (const auto &c : sum.comparisons) {
    CHECK(c.changePercent > 4 && c.changePercent < 16);
  }
}

TEST(simulator_is_deterministic_for_a_seed) {
  SimConfig cfg;
  cfg.swingsPerSet = 5;
  const SimResult a = simulate(cfg), b = simulate(cfg);
  CHECK(a.session.raw.size() == b.session.raw.size());
  CHECK(a.truth.size() == b.truth.size());
  if (!a.truth.empty() && a.truth.size() == b.truth.size()) {
    CHECK(a.truth.back().peakMs == b.truth.back().peakMs);
  }
}

}  // namespace

int main() {
  for (const auto &[name, fn] : registry()) {
    const int before = gFailures;
    fn();
    std::printf("%s %s\n", gFailures == before ? "[ ok ]" : "[FAIL]", name);
  }
  std::printf("\n%d checks, %d failed\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
