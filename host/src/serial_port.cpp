#include "swingtrack/serial_port.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

namespace swingtrack {

SerialPort::~SerialPort() { close(); }

#ifdef _WIN32

bool SerialPort::open(const std::string &name, int baud, std::string &error) {
  close();
  // The \\.\ prefix is required for COM10 and above.
  const std::string path = "\\\\.\\" + name;
  HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                         nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    error = "cannot open " + name + " (error " +
            std::to_string(GetLastError()) + "); is the port right and "
            "the Arduino Serial Monitor closed?";
    return false;
  }

  DCB dcb = {};
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(h, &dcb)) {
    error = "GetCommState failed";
    CloseHandle(h);
    return false;
  }
  dcb.BaudRate = static_cast<DWORD>(baud);
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;  // resets an Uno, like the IDE does
  dcb.fOutX = dcb.fInX = FALSE;
  if (!SetCommState(h, &dcb)) {
    error = "unsupported baud rate " + std::to_string(baud);
    CloseHandle(h);
    return false;
  }

  // Return as soon as any bytes arrive, or after 100 ms with none.
  COMMTIMEOUTS t = {};
  t.ReadIntervalTimeout = MAXDWORD;
  t.ReadTotalTimeoutMultiplier = MAXDWORD;
  t.ReadTotalTimeoutConstant = 100;
  t.WriteTotalTimeoutConstant = 500;
  SetCommTimeouts(h, &t);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  handle_ = h;
  return true;
}

void SerialPort::close() {
  if (handle_) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
}

bool SerialPort::isOpen() const { return handle_ != nullptr; }

int SerialPort::read(char *buffer, int size) {
  DWORD got = 0;
  if (!ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(size),
                &got, nullptr)) {
    return -1;
  }
  return static_cast<int>(got);
}

bool SerialPort::write(const std::string &data) {
  DWORD written = 0;
  return WriteFile(static_cast<HANDLE>(handle_), data.data(),
                   static_cast<DWORD>(data.size()), &written, nullptr) &&
         written == data.size();
}

#else  // POSIX

namespace {
bool toSpeed(int baud, speed_t &out) {
  switch (baud) {
    case 9600: out = B9600; return true;
    case 19200: out = B19200; return true;
    case 38400: out = B38400; return true;
    case 57600: out = B57600; return true;
    case 115200: out = B115200; return true;
    case 230400: out = B230400; return true;
    default: return false;
  }
}
}  // namespace

bool SerialPort::open(const std::string &name, int baud, std::string &error) {
  close();
  speed_t speed;
  if (!toSpeed(baud, speed)) {
    error = "unsupported baud rate " + std::to_string(baud);
    return false;
  }
  int fd = ::open(name.c_str(), O_RDWR | O_NOCTTY);
  if (fd < 0) {
    error = "cannot open " + name + ": " + std::strerror(errno);
    return false;
  }
  termios tty = {};
  if (tcgetattr(fd, &tty) != 0) {
    error = std::string("tcgetattr: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  cfmakeraw(&tty);
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
  tty.c_cflag &= ~CRTSCTS;
#endif
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    error = std::string("tcsetattr: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  tcflush(fd, TCIOFLUSH);
  fd_ = fd;
  return true;
}

void SerialPort::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool SerialPort::isOpen() const { return fd_ >= 0; }

int SerialPort::read(char *buffer, int size) {
  pollfd p = {fd_, POLLIN, 0};
  const int ready = poll(&p, 1, 100);
  if (ready < 0) return errno == EINTR ? 0 : -1;
  if (ready == 0) return 0;
  const ssize_t n = ::read(fd_, buffer, static_cast<size_t>(size));
  return n < 0 ? -1 : static_cast<int>(n);
}

bool SerialPort::write(const std::string &data) {
  return ::write(fd_, data.data(), data.size()) ==
         static_cast<ssize_t>(data.size());
}

#endif

}  // namespace swingtrack
