#include "swingtrack/protocol.h"

#include <cstdlib>
#include <sstream>
#include <vector>

namespace swingtrack {
namespace {

std::vector<std::string> split(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string field;
  std::istringstream in(s);
  while (std::getline(in, field, sep)) out.push_back(field);
  if (!s.empty() && s.back() == sep) out.emplace_back();
  return out;
}

bool toFloat(const std::string &s, float &out) {
  if (s.empty()) return false;
  char *end = nullptr;
  out = std::strtof(s.c_str(), &end);
  return end == s.c_str() + s.size();
}

bool toUint(const std::string &s, uint32_t &out) {
  if (s.empty() || s[0] == '-') return false;
  char *end = nullptr;
  const unsigned long v = std::strtoul(s.c_str(), &end, 10);
  out = static_cast<uint32_t>(v);
  return end == s.c_str() + s.size();
}

bool toInt(const std::string &s, int &out) {
  uint32_t v = 0;
  if (!toUint(s, v)) return false;
  out = static_cast<int>(v);
  return true;
}

bool validAxis(const std::string &s) {
  return s.size() == 2 && (s[0] == '+' || s[0] == '-') &&
         (s[1] == 'X' || s[1] == 'Y' || s[1] == 'Z');
}

Message invalid(const std::string &why) {
  Message m;
  m.type = MessageType::Invalid;
  m.text = why;
  return m;
}

}  // namespace

Message parseLine(const std::string &input) {
  std::string line = input;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  if (line.empty()) return invalid("empty line");

  Message m;
  if (line[0] == '#') {
    m.type = MessageType::Info;
    m.text = line.size() > 2 ? line.substr(2) : "";
    return m;
  }

  const std::vector<std::string> f = split(line, ',');
  const std::string &tag = f[0];

  if (tag == "R") {
    if (f.size() != 8) return invalid("raw line needs 8 fields");
    RawSample &r = m.raw;
    if (!toUint(f[1], r.tMs) || !toFloat(f[2], r.ax) || !toFloat(f[3], r.ay) ||
        !toFloat(f[4], r.az) || !toFloat(f[5], r.gx) || !toFloat(f[6], r.gy) ||
        !toFloat(f[7], r.gz)) {
      return invalid("bad number in raw line");
    }
    m.type = MessageType::Raw;
    return m;
  }

  if (tag == "S") {
    if (f.size() != 8) return invalid("swing line needs 8 fields");
    Swing &s = m.swing;
    if (!toInt(f[1], s.index) || !toInt(f[2], s.set) ||
        !toUint(f[3], s.startMs) || !toUint(f[4], s.durationMs) ||
        !toFloat(f[5], s.peakDps) || !toFloat(f[6], s.peakG) ||
        !validAxis(f[7])) {
      return invalid("bad field in swing line");
    }
    s.axis = f[7];
    m.type = MessageType::Swing;
    return m;
  }

  if (tag == "M") {
    if (f.size() != 3) return invalid("marker line needs 3 fields");
    if (!toInt(f[1], m.marker.set) || !toUint(f[2], m.marker.tMs)) {
      return invalid("bad field in marker line");
    }
    m.type = MessageType::Marker;
    return m;
  }

  return invalid("unknown message tag '" + tag + "'");
}

}  // namespace swingtrack
