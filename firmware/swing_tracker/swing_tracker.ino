// Tennis swing tracker firmware
//
// Hardware: Arduino Uno/Nano (or any Wire-compatible board) + MPU6050.
//   MPU6050 VCC -> 5V (GY-521 boards have a regulator) or 3.3V
//   MPU6050 GND -> GND
//   MPU6050 SDA -> A4 (Uno/Nano)
//   MPU6050 SCL -> A5 (Uno/Nano)
//   Optional push button: pin 2 -> GND (marks the start of a new set)
//
// Samples the IMU at 200 Hz, detects swings on the board and streams
// line-based CSV over serial at 115200 baud:
//   # <text>                              info / status
//   R,t_ms,ax,ay,az,gx,gy,gz              raw sample (g, deg/s), if enabled
//   S,idx,set,t_start_ms,dur_ms,peak_dps,peak_g,axis   detected swing
//   M,set,t_ms                            new set started
//
// Serial commands (single characters):
//   r  toggle raw streaming      n  start next set
//   c  recalibrate gyro          z  reset swing counter and set
//   ?  print status

#include <Wire.h>

#include "MPU6050.h"
#include "SwingDetector.h"

const uint32_t BAUD = 115200;
const uint32_t SAMPLE_PERIOD_US = 5000;  // 200 Hz
const uint8_t BUTTON_PIN = 2;
const uint32_t BUTTON_DEBOUNCE_MS = 50;
const uint32_t LED_ON_MS = 80;

MPU6050 imu;
swing::SwingDetector detector;

bool rawStreaming = true;
uint16_t swingCount = 0;
uint8_t setNumber = 1;
uint32_t nextSampleUs = 0;
uint32_t ledOffMs = 0;
uint32_t i2cErrors = 0;

bool lastButtonReading = HIGH;
bool buttonState = HIGH;
uint32_t buttonChangedMs = 0;

void calibrate() {
  Serial.println(F("# calibrating gyro - keep the sensor still"));
  while (!imu.calibrateGyro()) {
    Serial.println(F("# moved during calibration, retrying"));
    delay(500);
  }
  Serial.print(F("# gyro bias dps: "));
  Serial.print(imu.gyroBiasX(), 2);
  Serial.print(',');
  Serial.print(imu.gyroBiasY(), 2);
  Serial.print(',');
  Serial.println(imu.gyroBiasZ(), 2);
  detector.reset();
  nextSampleUs = micros();
}

void startNextSet() {
  ++setNumber;
  Serial.print(F("M,"));
  Serial.print(setNumber);
  Serial.print(',');
  Serial.println(millis());
}

void printStatus() {
  Serial.print(F("# set="));
  Serial.print(setNumber);
  Serial.print(F(" swings="));
  Serial.print(swingCount);
  Serial.print(F(" raw="));
  Serial.print(rawStreaming ? F("on") : F("off"));
  Serial.print(F(" i2c_errors="));
  Serial.println(i2cErrors);
}

void handleCommand(char c) {
  switch (c) {
    case 'r':
      rawStreaming = !rawStreaming;
      printStatus();
      break;
    case 'c':
      calibrate();
      break;
    case 'n':
      startNextSet();
      break;
    case 'z':
      swingCount = 0;
      setNumber = 1;
      detector.reset();
      Serial.println(F("# reset"));
      Serial.println(F("M,1,0"));
      break;
    case '?':
      printStatus();
      break;
    default:
      break;  // ignore newlines and unknown characters
  }
}

void pollButton() {
  const bool reading = digitalRead(BUTTON_PIN);
  const uint32_t now = millis();
  if (reading != lastButtonReading) {
    buttonChangedMs = now;
    lastButtonReading = reading;
  }
  if (now - buttonChangedMs > BUTTON_DEBOUNCE_MS && reading != buttonState) {
    buttonState = reading;
    if (buttonState == LOW) startNextSet();  // pressed (active low)
  }
}

void printRaw(uint32_t t, const ImuSample &s) {
  // Accel with 2 decimals (0.01 g), gyro as whole deg/s: ~38 bytes per line,
  // about 66% of the 115200 baud link at 200 Hz.
  Serial.print(F("R,"));
  Serial.print(t);
  Serial.print(',');
  Serial.print(s.ax, 2);
  Serial.print(',');
  Serial.print(s.ay, 2);
  Serial.print(',');
  Serial.print(s.az, 2);
  Serial.print(',');
  Serial.print((long)lroundf(s.gx));
  Serial.print(',');
  Serial.print((long)lroundf(s.gy));
  Serial.print(',');
  Serial.println((long)lroundf(s.gz));
}

void printSwing(const swing::SwingEvent &e) {
  Serial.print(F("S,"));
  Serial.print(swingCount);
  Serial.print(',');
  Serial.print(setNumber);
  Serial.print(',');
  Serial.print(e.startMs);
  Serial.print(',');
  Serial.print(e.durationMs);
  Serial.print(',');
  Serial.print(e.peakDps, 1);
  Serial.print(',');
  Serial.print(e.peakG, 2);
  Serial.print(',');
  Serial.print(e.signChar());
  Serial.println(e.axisChar());
}

void setup() {
  Serial.begin(BAUD);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.println(F("# tennis swing tracker v1.0"));
  while (!imu.begin()) {
    Serial.println(F("# MPU6050 not found - check wiring (SDA/SCL/VCC/GND)"));
    delay(1000);
  }
  calibrate();
  Serial.println(F("M,1,0"));
}

void loop() {
  while (Serial.available() > 0) handleCommand((char)Serial.read());
  pollButton();

  const uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextSampleUs) < 0) return;
  nextSampleUs += SAMPLE_PERIOD_US;
  // If we fell far behind (e.g. a long serial write), resync instead of
  // bursting through missed samples.
  if ((int32_t)(nowUs - nextSampleUs) > (int32_t)(4 * SAMPLE_PERIOD_US)) {
    nextSampleUs = nowUs + SAMPLE_PERIOD_US;
  }

  ImuSample s;
  if (!imu.read(s)) {
    ++i2cErrors;
    return;
  }
  const uint32_t t = millis();  // millis() wraps after 49 days, micros() after 71 min

  if (rawStreaming) printRaw(t, s);

  swing::SwingEvent e;
  if (detector.update(t, s.ax, s.ay, s.az, s.gx, s.gy, s.gz, e)) {
    ++swingCount;
    printSwing(e);
    digitalWrite(LED_BUILTIN, HIGH);
    ledOffMs = millis() + LED_ON_MS;
  }
  if (ledOffMs != 0 && (int32_t)(millis() - ledOffMs) >= 0) {
    digitalWrite(LED_BUILTIN, LOW);
    ledOffMs = 0;
  }
}
