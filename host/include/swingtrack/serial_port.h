// Minimal cross-platform serial port (Win32 / POSIX termios).
#pragma once

#include <string>

namespace swingtrack {

class SerialPort {
 public:
  SerialPort() = default;
  ~SerialPort();
  SerialPort(const SerialPort &) = delete;
  SerialPort &operator=(const SerialPort &) = delete;

  // `name` is e.g. "COM5" on Windows or "/dev/ttyUSB0" on Linux/macOS.
  // 8N1, no flow control. On failure returns false and fills `error`.
  bool open(const std::string &name, int baud, std::string &error);
  void close();
  bool isOpen() const;

  // Reads up to `size` bytes, waiting at most ~100 ms. Returns the number of
  // bytes read (0 on timeout) or -1 on error.
  int read(char *buffer, int size);
  bool write(const std::string &data);

 private:
#ifdef _WIN32
  void *handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

}  // namespace swingtrack
