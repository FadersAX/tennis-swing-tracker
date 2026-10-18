#include "MPU6050.h"

namespace {
// Register addresses (MPU-6000/6050 register map rev 4.2)
const uint8_t REG_SMPLRT_DIV = 0x19;
const uint8_t REG_CONFIG = 0x1A;
const uint8_t REG_GYRO_CONFIG = 0x1B;
const uint8_t REG_ACCEL_CONFIG = 0x1C;
const uint8_t REG_ACCEL_XOUT_H = 0x3B;
const uint8_t REG_PWR_MGMT_1 = 0x6B;
const uint8_t REG_WHO_AM_I = 0x75;

// Full-scale settings and matching sensitivities
const uint8_t GYRO_FS_2000DPS = 0x18;   // FS_SEL = 3
const uint8_t ACCEL_FS_16G = 0x18;      // AFS_SEL = 3
const float GYRO_LSB_PER_DPS = 16.4f;
const float ACCEL_LSB_PER_G = 2048.0f;

// A still sensor's gyro readings should stay within this many LSB (~6 dps)
// during calibration; more than that means it was moved.
const int16_t CALIBRATION_MAX_SPREAD = 100;
}  // namespace

bool MPU6050::begin() {
  Wire.begin();
  Wire.setClock(400000);  // fast-mode I2C, needed for a 200 Hz burst read

  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1)) return false;
  // Genuine parts return 0x68; common clones return 0x70, 0x72 or 0x98.
  if (who != 0x68 && who != 0x70 && who != 0x72 && who != 0x98) return false;

  return writeReg(REG_PWR_MGMT_1, 0x01) &&      // wake, clock = gyro X PLL
         writeReg(REG_CONFIG, 0x03) &&          // DLPF: ~44 Hz bandwidth
         writeReg(REG_SMPLRT_DIV, 4) &&         // 1 kHz / (1 + 4) = 200 Hz
         writeReg(REG_GYRO_CONFIG, GYRO_FS_2000DPS) &&
         writeReg(REG_ACCEL_CONFIG, ACCEL_FS_16G);
}

bool MPU6050::calibrateGyro(uint16_t samples) {
  int32_t sum[3] = {0, 0, 0};
  int16_t lo[3] = {32767, 32767, 32767};
  int16_t hi[3] = {-32768, -32768, -32768};

  for (uint16_t n = 0; n < samples; ++n) {
    int16_t raw[7];
    if (!readRaw(raw)) return false;
    for (uint8_t i = 0; i < 3; ++i) {
      const int16_t g = raw[4 + i];
      sum[i] += g;
      if (g < lo[i]) lo[i] = g;
      if (g > hi[i]) hi[i] = g;
    }
    delay(5);
  }
  for (uint8_t i = 0; i < 3; ++i) {
    if (hi[i] - lo[i] > CALIBRATION_MAX_SPREAD) return false;
  }
  biasX_ = sum[0] / (float)samples / GYRO_LSB_PER_DPS;
  biasY_ = sum[1] / (float)samples / GYRO_LSB_PER_DPS;
  biasZ_ = sum[2] / (float)samples / GYRO_LSB_PER_DPS;
  return true;
}

bool MPU6050::read(ImuSample &out) {
  int16_t raw[7];
  if (!readRaw(raw)) return false;
  out.ax = raw[0] / ACCEL_LSB_PER_G;
  out.ay = raw[1] / ACCEL_LSB_PER_G;
  out.az = raw[2] / ACCEL_LSB_PER_G;
  // raw[3] is temperature, unused
  out.gx = raw[4] / GYRO_LSB_PER_DPS - biasX_;
  out.gy = raw[5] / GYRO_LSB_PER_DPS - biasY_;
  out.gz = raw[6] / GYRO_LSB_PER_DPS - biasZ_;
  return true;
}

bool MPU6050::readRaw(int16_t raw[7]) {
  uint8_t buf[14];
  if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf))) return false;
  for (uint8_t i = 0; i < 7; ++i) {
    raw[i] = (int16_t)((buf[2 * i] << 8) | buf[2 * i + 1]);  // big-endian
  }
  return true;
}

bool MPU6050::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool MPU6050::readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start
  if (Wire.requestFrom(addr_, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}
