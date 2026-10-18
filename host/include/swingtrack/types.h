// Shared data types for the PC-side tools.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace swingtrack {

// One IMU sample: accel in g, gyro in deg/s.
struct RawSample {
  uint32_t tMs = 0;
  float ax = 0, ay = 0, az = 0;
  float gx = 0, gy = 0, gz = 0;
};

// One detected swing (from the firmware or re-detected on the PC).
struct Swing {
  int index = 0;           // 1-based within the session
  int set = 1;             // set number (1-based)
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
  float peakDps = 0;       // peak angular velocity magnitude
  float peakG = 0;         // peak acceleration magnitude
  std::string axis = "+Z"; // dominant rotation axis and direction
};

// Start of a set.
struct SetMarker {
  int set = 1;
  uint32_t tMs = 0;
};

// Everything recorded in one session.
struct Session {
  std::vector<RawSample> raw;
  std::vector<Swing> swings;
  std::vector<SetMarker> markers;
};

}  // namespace swingtrack
