// Live serial logger: records a session from the Arduino to a folder.
#pragma once

#include <string>

namespace swingtrack {

struct LoggerOptions {
  std::string port;
  int baud = 115200;
  std::string outDir = "sessions";
  bool raw = true;  // ask the firmware to stream raw samples
};

// Blocks until the user types "q" (or presses Ctrl+C), then writes the session
// report. Returns a process exit code.
int runLogger(const LoggerOptions &options);

}  // namespace swingtrack
