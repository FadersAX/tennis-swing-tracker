// Swing detector: a small state machine over gyro magnitude.
//
// Plain C++ with no Arduino or STL dependencies, so the exact same code runs
// on the microcontroller (live detection) and in the PC tool (re-detection on
// logged data, unit tests, simulator evaluation).
//
// Algorithm
//   1. |w| = sqrt(gx^2 + gy^2 + gz^2), then a first-order IIR low-pass filter
//      to suppress sensor noise and single-sample spikes.
//   2. IDLE -> SWING when the filtered |w| rises above startDps.
//   3. SWING -> IDLE when it stays below endDps for endHoldMs (hysteresis,
//      so one swing is not split in two by a dip).
//   4. A candidate is accepted only if its duration is in
//      [minDurationMs, maxDurationMs] and its peak |w| >= minPeakDps. This
//      rejects bumps (walking, bouncing the ball) and short jolts.
//   5. After an accepted swing (or an over-long motion) the detector re-arms
//      only once refractoryMs has passed AND the signal has dropped below
//      startDps, so a bounce-back is never counted as a new swing. Weak or
//      short candidates re-arm immediately: a hard swing's backswing can
//      cross startDps on its own, and the real swing follows right after.
//
// Each swing also records which gyro axis rotated the most (by integrated
// angle) and in which direction. With a fixed mounting, the sign of that axis
// separates forehands from backhands.
#pragma once

#include <math.h>
#include <stdint.h>

namespace swing {

struct DetectorConfig {
  float startDps = 300.0f;       // enter swing above this (filtered |w|)
  float endDps = 150.0f;         // candidate end below this ...
  uint32_t endHoldMs = 30;       // ... for at least this long
  float minPeakDps = 500.0f;     // reject anything weaker
  uint32_t minDurationMs = 60;   // reject jolts / impacts without a swing
  uint32_t maxDurationMs = 1500; // reject long continuous motion
  uint32_t refractoryMs = 250;   // minimum dead time after a swing
  float lpfAlpha = 0.5f;         // IIR weight of the newest sample (0..1]
};

struct SwingEvent {
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
  float peakDps = 0.0f;  // peak unfiltered |w| during the swing
  float peakG = 0.0f;    // peak |a| during the swing
  uint8_t axis = 2;      // 0 = X, 1 = Y, 2 = Z (largest integrated rotation)
  int8_t sign = 1;       // +1 / -1 direction of rotation about that axis

  char axisChar() const { return static_cast<char>('X' + axis); }
  char signChar() const { return sign >= 0 ? '+' : '-'; }
};

class SwingDetector {
 public:
  explicit SwingDetector(const DetectorConfig &config = DetectorConfig())
      : cfg_(config) {}

  const DetectorConfig &config() const { return cfg_; }
  void setConfig(const DetectorConfig &config) { cfg_ = config; reset(); }

  void reset() {
    inSwing_ = false;
    hasPrev_ = false;
    hasLastEnd_ = false;
    belowActive_ = false;
    armed_ = true;
    filtered_ = 0.0f;
  }

  // Feed one sample (accel in g, gyro in deg/s, timestamp in ms). Returns true
  // when a valid swing has just finished; it is written to `out`.
  bool update(uint32_t tMs, float ax, float ay, float az, float gx, float gy,
              float gz, SwingEvent &out) {
    const float mag = sqrtf(gx * gx + gy * gy + gz * gz);
    const float accMag = sqrtf(ax * ax + ay * ay + az * az);

    if (!hasPrev_) {
      filtered_ = mag;
    } else {
      filtered_ = cfg_.lpfAlpha * mag + (1.0f - cfg_.lpfAlpha) * filtered_;
    }
    const float dtS = hasPrev_ ? (tMs - prevMs_) * 0.001f : 0.0f;
    prevMs_ = tMs;
    hasPrev_ = true;

    if (!inSwing_) {
      if (!armed_) {
        const bool refractory =
            hasLastEnd_ && (tMs - lastEndMs_) < cfg_.refractoryMs;
        if (!refractory && filtered_ < cfg_.startDps) armed_ = true;
        return false;
      }
      if (filtered_ > cfg_.startDps) {
        inSwing_ = true;
        belowActive_ = false;
        startMs_ = tMs;
        peakDps_ = mag;
        peakG_ = accMag;
        angle_[0] = angle_[1] = angle_[2] = 0.0f;
      }
      return false;
    }

    // In a swing: track peaks and integrated rotation per axis.
    if (mag > peakDps_) peakDps_ = mag;
    if (accMag > peakG_) peakG_ = accMag;
    angle_[0] += gx * dtS;
    angle_[1] += gy * dtS;
    angle_[2] += gz * dtS;

    if (tMs - startMs_ > cfg_.maxDurationMs) {
      finish(tMs, true);  // too long: not a swing, wait for it to settle
      return false;
    }

    if (filtered_ < cfg_.endDps) {
      if (!belowActive_) {
        belowActive_ = true;
        belowSinceMs_ = tMs;
      }
      if (tMs - belowSinceMs_ >= cfg_.endHoldMs) {
        const uint32_t duration = belowSinceMs_ - startMs_;
        const bool valid =
            duration >= cfg_.minDurationMs && peakDps_ >= cfg_.minPeakDps;
        finish(tMs, valid);
        if (!valid) return false;
        out.startMs = startMs_;
        out.durationMs = duration;
        out.peakDps = peakDps_;
        out.peakG = peakG_;
        out.axis = dominantAxis();
        out.sign = angle_[out.axis] >= 0.0f ? 1 : -1;
        return true;
      }
    } else {
      belowActive_ = false;
    }
    return false;
  }

 private:
  // `holdOff` starts the refractory period and requires the signal to drop
  // below startDps before the next swing can begin.
  void finish(uint32_t tMs, bool holdOff) {
    inSwing_ = false;
    belowActive_ = false;
    if (holdOff) {
      armed_ = false;
      hasLastEnd_ = true;
      lastEndMs_ = tMs;
    }
  }

  uint8_t dominantAxis() const {
    uint8_t best = 0;
    for (uint8_t i = 1; i < 3; ++i) {
      if (fabsf(angle_[i]) > fabsf(angle_[best])) best = i;
    }
    return best;
  }

  DetectorConfig cfg_;
  bool inSwing_ = false;
  bool hasPrev_ = false;
  bool hasLastEnd_ = false;
  bool belowActive_ = false;
  bool armed_ = true;
  float filtered_ = 0.0f;
  uint32_t prevMs_ = 0;
  uint32_t startMs_ = 0;
  uint32_t belowSinceMs_ = 0;
  uint32_t lastEndMs_ = 0;
  float peakDps_ = 0.0f;
  float peakG_ = 0.0f;
  float angle_[3] = {0.0f, 0.0f, 0.0f};
};

}  // namespace swing
