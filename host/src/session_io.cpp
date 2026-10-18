#include "swingtrack/session_io.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

#include "swingtrack/protocol.h"

namespace swingtrack {
namespace {

std::ofstream openOut(const fs::path &p) {
  std::ofstream out(p);
  if (!out) throw std::runtime_error("cannot write " + p.string());
  return out;
}

// Each CSV row maps 1:1 onto a protocol line, so reuse the protocol parser by
// prefixing the row with the matching tag.
template <typename Fn>
void readCsv(const fs::path &p, char tag, Fn onMessage) {
  std::ifstream in(p);
  if (!in) throw std::runtime_error("cannot read " + p.string());
  std::string line;
  std::getline(in, line);  // header
  int lineNo = 1;
  while (std::getline(in, line)) {
    ++lineNo;
    if (line.empty() || line == "\r") continue;
    const Message m = parseLine(std::string(1, tag) + "," + line);
    if (m.type == MessageType::Invalid) {
      throw std::runtime_error(p.string() + ":" + std::to_string(lineNo) +
                               ": " + m.text);
    }
    onMessage(m);
  }
}

}  // namespace

void writeRawHeader(std::ostream &out) { out << "t_ms,ax,ay,az,gx,gy,gz\n"; }

void writeRawRow(std::ostream &out, const RawSample &r) {
  out << r.tMs << std::fixed << std::setprecision(3) << ',' << r.ax << ','
      << r.ay << ',' << r.az << std::setprecision(1) << ',' << r.gx << ','
      << r.gy << ',' << r.gz << '\n';
  out.unsetf(std::ios::floatfield);
}

void writeSwingHeader(std::ostream &out) {
  out << "idx,set,t_start_ms,duration_ms,peak_dps,peak_g,axis\n";
}

void writeSwingRow(std::ostream &out, const Swing &s) {
  out << s.index << ',' << s.set << ',' << s.startMs << ',' << s.durationMs
      << std::fixed << std::setprecision(1) << ',' << s.peakDps
      << std::setprecision(2) << ',' << s.peakG << ',' << s.axis << '\n';
  out.unsetf(std::ios::floatfield);
}

void writeMarkerHeader(std::ostream &out) { out << "set,t_ms\n"; }

void writeMarkerRow(std::ostream &out, const SetMarker &m) {
  out << m.set << ',' << m.tMs << '\n';
}

void saveSession(const fs::path &dir, const Session &session) {
  fs::create_directories(dir);
  if (!session.raw.empty()) {
    auto out = openOut(dir / "raw.csv");
    writeRawHeader(out);
    for (const auto &r : session.raw) writeRawRow(out, r);
  }
  {
    auto out = openOut(dir / "swings.csv");
    writeSwingHeader(out);
    for (const auto &s : session.swings) writeSwingRow(out, s);
  }
  if (!session.markers.empty()) {
    auto out = openOut(dir / "markers.csv");
    writeMarkerHeader(out);
    for (const auto &m : session.markers) writeMarkerRow(out, m);
  }
}

Session loadSession(const fs::path &dir) {
  if (!fs::is_directory(dir)) {
    throw std::runtime_error("not a session folder: " + dir.string());
  }
  Session s;
  if (fs::exists(dir / "raw.csv")) {
    readCsv(dir / "raw.csv", 'R',
            [&](const Message &m) { s.raw.push_back(m.raw); });
  }
  if (fs::exists(dir / "swings.csv")) {
    readCsv(dir / "swings.csv", 'S',
            [&](const Message &m) { s.swings.push_back(m.swing); });
  }
  if (fs::exists(dir / "markers.csv")) {
    readCsv(dir / "markers.csv", 'M',
            [&](const Message &m) { s.markers.push_back(m.marker); });
  }
  return s;
}

}  // namespace swingtrack
