// Minimal MPU6050 driver over I2C (Wire). No third-party libraries, so every
// register access is explicit.
//
// Datasheet / register map: InvenSense RM-MPU-6000A-00 rev 4.2
#pragma once

#include <Arduino.h>
#include <Wire.h>

struct ImuSample {
  float ax, ay, az;  // acceleration in g
  float gx, gy, gz;  // angular velocity in deg/s (bias corrected)
};

class MPU6050 {
 public:
  explicit MPU6050(uint8_t address = 0x68) : addr_(address) {}

  // Configures the sensor for fast motion:
  //   accel +/-16 g, gyro +/-2000 dps, DLPF ~44 Hz, 200 Hz internal sample rate.
  // Returns false if the chip does not answer or WHO_AM_I is wrong.
  bool begin();

  // Averages `samples` gyro readings while the sensor is still and stores the
  // result as the bias. Returns false if it moved too much during calibration.
  bool calibrateGyro(uint16_t samples = 400);

  // Burst-reads accel + gyro (14 bytes from ACCEL_XOUT_H). Returns false on an
  // I2C error.
  bool read(ImuSample &out);

  float gyroBiasX() const { return biasX_; }
  float gyroBiasY() const { return biasY_; }
  float gyroBiasZ() const { return biasZ_; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len);
  bool readRaw(int16_t raw[7]);

  uint8_t addr_;
  float biasX_ = 0, biasY_ = 0, biasZ_ = 0;
};
