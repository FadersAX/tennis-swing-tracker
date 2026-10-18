#include "swingtrack/logger.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "swingtrack/analysis.h"
#include "swingtrack/protocol.h"
#include "swingtrack/report.h"
#include "swingtrack/serial_port.h"
#include "swingtrack/session_io.h"

namespace swingtrack {
namespace {

std::atomic<bool> gStop{false};

void onSignal(int) { gStop = true; }

std::string timestampName() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
  return buf;
}

void printHelp() {
  std::cout << "Commands (type then Enter):\n"
               "  n  next set     c  recalibrate gyro (keep still)\n"
               "  ?  status       q  stop and write the report\n\n";
}

}  // namespace

int runLogger(const LoggerOptions &opt) {
  SerialPort port;
  std::string err;
  if (!port.open(opt.port, opt.baud, err)) {
    std::cerr << "error: " << err << "\n";
    return 1;
  }

  const std::filesystem::path dir =
      std::filesystem::path(opt.outDir) / timestampName();
  std::filesystem::create_directories(dir);
  std::ofstream rawOut(dir / "raw.csv"), swingOut(dir / "swings.csv"),
      markerOut(dir / "markers.csv");
  if (!rawOut || !swingOut || !markerOut) {
    std::cerr << "error: cannot create files in " << dir << "\n";
    return 1;
  }
  writeRawHeader(rawOut);
  writeSwingHeader(swingOut);
  writeMarkerHeader(markerOut);

  std::cout << "Logging " << opt.port << " @ " << opt.baud << " baud to "
            << dir.string() << "\n";
  printHelp();

  std::signal(SIGINT, onSignal);
  std::mutex portMutex;

  // Keyboard thread: forwards single-letter commands to the board. It blocks
  // on std::getline, so it is detached rather than joined at exit.
  std::thread input([&] {
    std::string line;
    while (!gStop && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      if (line[0] == 'q') {
        gStop = true;
        break;
      }
      std::lock_guard<std::mutex> lock(portMutex);
      port.write(std::string(1, line[0]));
    }
  });
  input.detach();

  std::string pending;
  char buf[512];
  size_t rawCount = 0, swingCount = 0, badLines = 0;
  bool booted = false;
  auto lastFlush = std::chrono::steady_clock::now();

  while (!gStop) {
    int n;
    {
      std::lock_guard<std::mutex> lock(portMutex);
      n = port.read(buf, sizeof(buf));
    }
    if (n < 0) {
      std::cerr << "error: serial read failed (board unplugged?)\n";
      break;
    }
    pending.append(buf, static_cast<size_t>(n));

    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      const std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      const Message m = parseLine(line);
      switch (m.type) {
        case MessageType::Raw:
          writeRawRow(rawOut, m.raw);
          ++rawCount;
          break;
        case MessageType::Swing:
          writeSwingRow(swingOut, m.swing);
          ++swingCount;
          std::printf("swing %3d  set %d  peak %6.0f dps  %4.1f g  %3u ms  %s\n",
                      m.swing.index, m.swing.set, m.swing.peakDps,
                      m.swing.peakG, m.swing.durationMs, m.swing.axis.c_str());
          break;
        case MessageType::Marker:
          writeMarkerRow(markerOut, m.marker);
          std::cout << "--- set " << m.marker.set << " ---\n";
          if (!booted) {
            booted = true;  // first marker is printed at the end of setup()
            if (!opt.raw) {
              std::lock_guard<std::mutex> lock(portMutex);
              port.write("r");
            }
          }
          break;
        case MessageType::Info:
          std::cout << "[board] " << m.text << "\n";
          break;
        case MessageType::Invalid:
          // Partial lines are normal right after the port opens.
          if (line.size() > 1) ++badLines;
          break;
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - lastFlush > std::chrono::seconds(2)) {
      rawOut.flush();
      swingOut.flush();
      markerOut.flush();
      lastFlush = now;
    }
  }

  rawOut.close();
  swingOut.close();
  markerOut.close();
  port.close();

  std::cout << "\nStopped. " << swingCount << " swings, " << rawCount
            << " raw samples";
  if (badLines) std::cout << ", " << badLines << " unreadable lines";
  std::cout << "\n\n";

  const Session session = loadSession(dir);
  const SessionSummary summary = summarize(session.swings);
  std::cout << formatSummary(summary);
  writeReport(dir, session, session.swings, summary);
  std::cout << "\nReport written to " << (dir / "report.html").string()
            << "\n";
  return 0;
}

}  // namespace swingtrack
