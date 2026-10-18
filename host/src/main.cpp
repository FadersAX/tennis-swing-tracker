// swingtrack: command-line tool for the tennis swing tracker.
//
//   swingtrack log --port COM5 [--baud 115200] [--out sessions] [--no-raw]
//   swingtrack analyze <session> [--redetect] [detector options]
//   swingtrack simulate <session> [--sets N] [--swings N] [--change PCT]
//                       [--seed N] [--no-distractors]
//   swingtrack evaluate <session> [--expected N] [detector options]
//
// Detector options (re-run the firmware algorithm with different thresholds):
//   --start DPS  --end DPS  --min-peak DPS  --min-duration MS  --hold MS
//   --refractory MS

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "swingtrack/analysis.h"
#include "swingtrack/logger.h"
#include "swingtrack/report.h"
#include "swingtrack/session_io.h"
#include "swingtrack/simulator.h"

using namespace swingtrack;
namespace fs = std::filesystem;

namespace {

const char *kUsage =
    "usage:\n"
    "  swingtrack log --port COM5 [--baud 115200] [--out sessions] [--no-raw]\n"
    "  swingtrack analyze <session> [--redetect] [detector options]\n"
    "  swingtrack simulate <session> [--sets N] [--swings N] [--change PCT]\n"
    "                      [--seed N] [--no-distractors]\n"
    "  swingtrack evaluate <session> [--expected N] [detector options]\n"
    "\n"
    "detector options:\n"
    "  --start DPS --end DPS --min-peak DPS --min-duration MS --hold MS\n"
    "  --refractory MS\n";

// Parsed command line: positional arguments and --key [value] options.
struct Args {
  std::vector<std::string> positional;
  std::map<std::string, std::string> options;  // flags map to ""

  bool has(const std::string &k) const { return options.count(k) > 0; }

  std::string get(const std::string &k, const std::string &def) const {
    auto it = options.find(k);
    return it == options.end() ? def : it->second;
  }

  double number(const std::string &k, double def) const {
    auto it = options.find(k);
    if (it == options.end()) return def;
    char *end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    if (it->second.empty() || *end != '\0') {
      throw std::invalid_argument("--" + k + " needs a number");
    }
    return v;
  }
};

const char *kFlags[] = {"redetect", "no-raw", "no-distractors", "help"};

Args parseArgs(int argc, char **argv, int first) {
  Args a;
  for (int i = first; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      const std::string key = s.substr(2);
      bool isFlag = false;
      for (const char *f : kFlags) isFlag |= key == f;
      if (isFlag) {
        a.options[key] = "";
      } else if (i + 1 < argc) {
        a.options[key] = argv[++i];
      } else {
        throw std::invalid_argument("--" + key + " needs a value");
      }
    } else {
      a.positional.push_back(s);
    }
  }
  return a;
}

swing::DetectorConfig detectorConfig(const Args &a) {
  swing::DetectorConfig c;
  c.startDps = float(a.number("start", c.startDps));
  c.endDps = float(a.number("end", c.endDps));
  c.minPeakDps = float(a.number("min-peak", c.minPeakDps));
  c.minDurationMs = uint32_t(a.number("min-duration", c.minDurationMs));
  c.endHoldMs = uint32_t(a.number("hold", c.endHoldMs));
  c.refractoryMs = uint32_t(a.number("refractory", c.refractoryMs));
  return c;
}

bool usesCustomDetector(const Args &a) {
  for (const char *k : {"start", "end", "min-peak", "min-duration", "hold",
                        "refractory"}) {
    if (a.has(k)) return true;
  }
  return false;
}

fs::path sessionArg(const Args &a) {
  if (a.positional.empty()) throw std::invalid_argument("missing <session>");
  return a.positional[0];
}

// Swings to analyse: re-detected from raw data when asked (or when the
// board's swings are missing), otherwise the ones the board reported.
std::vector<Swing> chooseSwings(const Session &s, const Args &a) {
  const bool redetect =
      a.has("redetect") || usesCustomDetector(a) || s.swings.empty();
  if (!redetect) return s.swings;
  if (s.raw.empty()) {
    if (a.has("redetect") || usesCustomDetector(a)) {
      throw std::runtime_error("re-detection needs raw.csv in the session");
    }
    return s.swings;
  }
  std::cout << "(re-detecting swings from " << s.raw.size()
            << " raw samples)\n";
  return detectSwings(s.raw, s.markers, detectorConfig(a));
}

int cmdLog(const Args &a) {
  LoggerOptions o;
  o.port = a.get("port", "");
  if (o.port.empty()) throw std::invalid_argument("log needs --port");
  o.baud = int(a.number("baud", o.baud));
  o.outDir = a.get("out", o.outDir);
  o.raw = !a.has("no-raw");
  return runLogger(o);
}

int cmdAnalyze(const Args &a) {
  const fs::path dir = sessionArg(a);
  const Session s = loadSession(dir);
  const std::vector<Swing> swings = chooseSwings(s, a);
  const SessionSummary summary = summarize(swings);
  std::cout << formatSummary(summary);
  writeReport(dir, s, swings, summary);
  std::cout << "\nReport written to " << (dir / "report.html").string()
            << "\n";
  return 0;
}

int cmdSimulate(const Args &a) {
  const fs::path dir = sessionArg(a);
  SimConfig c;
  c.sets = int(a.number("sets", c.sets));
  c.swingsPerSet = int(a.number("swings", c.swingsPerSet));
  c.setChangePercent = a.number("change", c.setChangePercent);
  c.seed = uint32_t(a.number("seed", c.seed));
  c.distractors = !a.has("no-distractors");
  if (c.sets < 1 || c.swingsPerSet < 1) {
    throw std::invalid_argument("--sets and --swings must be at least 1");
  }

  SimResult r = simulate(c);
  // Run the same detector the firmware runs, as if the board had sent them.
  r.session.swings = detectSwings(r.session.raw, r.session.markers);
  saveSession(dir, r.session);
  saveTruth(dir / "truth.csv", r.truth);
  std::cout << "Simulated " << r.truth.size() << " swings in " << c.sets
            << " sets (+" << r.distractorCount << " non-swing movements), "
            << r.session.raw.size() << " samples -> " << dir.string() << "\n"
            << "Next: swingtrack analyze " << dir.string()
            << "   or   swingtrack evaluate " << dir.string() << "\n";
  return 0;
}

int cmdEvaluate(const Args &a) {
  const fs::path dir = sessionArg(a);
  const Session s = loadSession(dir);
  const std::vector<Swing> swings = chooseSwings(s, a);

  if (fs::exists(dir / "truth.csv")) {
    const Evaluation e = evaluate(loadTruth(dir / "truth.csv"), swings);
    std::printf(
        "Detection accuracy vs truth.csv\n"
        "  true swings:      %d\n"
        "  detected:         %d\n"
        "  matched:          %d\n"
        "  precision:        %.1f%%  (detections that were real swings)\n"
        "  recall:           %.1f%%  (real swings that were detected)\n"
        "  peak error:       %+.2f%% mean, %.2f%% mean absolute\n"
        "  forehand/backhand %d/%d correct\n",
        e.truthCount, e.detectedCount, e.matched, e.precision * 100,
        e.recall * 100, e.meanPeakErrorPercent, e.meanAbsPeakErrorPercent,
        e.axisCorrect, e.matched);
    return 0;
  }
  if (a.has("expected")) {
    const int expected = int(a.number("expected", 0));
    const int got = int(swings.size());
    std::printf("Swing count: detected %d, you counted %d -> error %+d (%.1f%%)\n",
                got, expected, got - expected,
                expected ? 100.0 * (got - expected) / expected : 0.0);
    return 0;
  }
  throw std::invalid_argument(
      "evaluate needs truth.csv in the session or --expected N "
      "(the number of swings you counted)");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2 || std::string(argv[1]) == "--help" ||
      std::string(argv[1]) == "-h") {
    std::cout << kUsage;
    return argc < 2 ? 1 : 0;
  }
  const std::string cmd = argv[1];
  try {
    const Args a = parseArgs(argc, argv, 2);
    if (cmd == "log") return cmdLog(a);
    if (cmd == "analyze") return cmdAnalyze(a);
    if (cmd == "simulate") return cmdSimulate(a);
    if (cmd == "evaluate") return cmdEvaluate(a);
    std::cerr << "unknown command '" << cmd << "'\n\n" << kUsage;
    return 1;
  } catch (const std::invalid_argument &e) {
    std::cerr << "error: " << e.what() << "\n\n" << kUsage;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
