#include <Arduino.h>
#include <mico.h>
#include "AppConfig.h"
#include "DiagnosticChecks.h"
#include "OnboardTests.h"

namespace {
  struct Sensor {
    mico_i2c_device_t device;
    const char *name;
    uint8_t id;
    uint8_t idRegister;
    uint8_t statusRegister;
    uint8_t readyMask;
    bool configured = false;
    bool valid = false;
    bool sampled = false;
    uint32_t changes = 0;
    float values[6] = {};

    Sensor(uint16_t address, const char *label, uint8_t identity, uint8_t idReg, uint8_t statusReg, uint8_t mask): device{Arduino_I2C, address, I2C_ADDRESS_WIDTH_7BIT, I2C_STANDARD_SPEED_MODE}, name(label), id(identity), idRegister(idReg), statusRegister(statusReg), readyMask(mask) {}
  };

  Sensor sensors[] = {{0x5F, "HTS221", 0xBC, 0x0F, 0x27, 0x03}, {0x5C, "LPS22HB", 0xB1, 0x0F, 0x27, 0x03}, {0x6A, "LSM6DSL", 0x6A, 0x0F, 0x1E, 0x03}, {0x1E, "LIS2MDL", 0x40, 0x4F, 0x67, 0x08}};
  constexpr uint32_t kIntervalMs = 2000;
  uint8_t calibration[16] = {};
  unsigned int page = 0;
  uint32_t lastSampleMs = 0;

  void reportError(Sensor &sensor, const char *reason) {
    sensor.valid = false;
    Serial.print(sensor.name);
    Serial.print(F(" FAIL: "));
    Serial.println(reason);
  }

  bool read(Sensor &sensor, uint8_t reg, uint8_t *data, uint16_t size) {
    if (sensor.device.address == 0x5F && size > 1) {
      reg |= 0x80;
    }
    mico_i2c_message_t message;
    if (MicoI2cBuildCombinedMessage(&message, &reg, data, 1, size, 3) != kNoErr || MicoI2cTransfer(&sensor.device, &message, 1) != kNoErr) {
      reportError(sensor, "I2C read");
      return false;
    }
    return true;
  }

  bool configure(Sensor &sensor, uint8_t reg, uint8_t value) {
    const uint8_t data[] = {reg, value};
    mico_i2c_message_t message;
    uint8_t actual = 0;
    if (MicoI2cBuildTxMessage(&message, data, sizeof(data), 3) != kNoErr || MicoI2cTransfer(&sensor.device, &message, 1) != kNoErr) {
      reportError(sensor, "I2C configuration write");
      return false;
    }
    if (!read(sensor, reg, &actual, 1) || actual != value) {
      reportError(sensor, "configuration readback");
      return false;
    }
    return true;
  }

  bool initialize(Sensor &sensor, unsigned int index) {
    sensor.valid = sensor.configured = sensor.sampled = false;
    sensor.changes = 0;
    uint8_t id = 0;
    if (MicoI2cInitialize(&sensor.device) != kNoErr || !MicoI2cProbeDevice(&sensor.device, 3)) {
      reportError(sensor, "device absent");
      return false;
    }
    if (!read(sensor, sensor.idRegister, &id, 1)) {
      return false;
    }
    char text[64];
    snprintf(text, sizeof(text), "%s WHO_AM_I: 0x%02X (expected 0x%02X).", sensor.name, id, sensor.id);
    Serial.println(text);
    if (id != sensor.id) {
      reportError(sensor, "unexpected device identity");
      return false;
    }
    switch (index) {
      case 0:
        if (!read(sensor, 0x30, calibration, sizeof(calibration)) || DiagnosticChecks::signed16(calibration + 6) == DiagnosticChecks::signed16(calibration + 10) || DiagnosticChecks::signed16(calibration + 12) == DiagnosticChecks::signed16(calibration + 14)) {
          reportError(sensor, "invalid factory calibration");
          return false;
        }
        return configure(sensor, 0x20, 0x85); // Power on, BDU, 1 Hz.
      case 1:
        return configure(sensor, 0x11, 0x10) && configure(sensor, 0x10, 0x12);
      case 2:
        return configure(sensor, 0x12, 0x44) && configure(sensor, 0x10, 0x40) && configure(sensor, 0x11, 0x40);
      default:
        return configure(sensor, 0x62, 0x10) && configure(sensor, 0x60, 0x8C);
    }
  }

  void sample(Sensor &sensor, unsigned int index) {
    sensor.valid = false;
    if (!sensor.configured) {
      return;
    }
    uint8_t status = 0;
    if (!read(sensor, sensor.statusRegister, &status, 1)) {
      return;
    }
    if ((status & sensor.readyMask) != sensor.readyMask) {
      Serial.print(sensor.name);
      Serial.println(F(": waiting for fresh data."));
      return;
    }
    uint8_t raw[12] = {};
    float values[6] = {};
    size_t count = 0;
    bool plausible = true;
    switch (index) {
      case 0: {
        if (!read(sensor, 0x28, raw, 4)) {
          return;
        }
        const int t0 = calibration[2] | ((calibration[5] & 3) << 8);
        const int t1 = calibration[3] | ((calibration[5] & 12) << 6);
        plausible = DiagnosticChecks::interpolate(DiagnosticChecks::signed16(raw), DiagnosticChecks::signed16(calibration + 6), DiagnosticChecks::signed16(calibration + 10), calibration[0] / 2.0f, calibration[1] / 2.0f, values[0]) &&
          DiagnosticChecks::interpolate(DiagnosticChecks::signed16(raw + 2), DiagnosticChecks::signed16(calibration + 12), DiagnosticChecks::signed16(calibration + 14), t0 / 8.0f, t1 / 8.0f, values[1]) && DiagnosticChecks::inRange(values[0], 0, 100) && DiagnosticChecks::inRange(values[1], -40, 120);
        count = 2;
        break;
      }
      case 1:
        if (!read(sensor, 0x28, raw, 5)) {
          return;
        }
        values[0] = static_cast<uint32_t>(raw[0] | (raw[1] << 8) | (raw[2] << 16)) / 4096.0f;
        values[1] = DiagnosticChecks::signed16(raw + 3) / 100.0f;
        plausible = DiagnosticChecks::inRange(values[0], 260, 1260) && DiagnosticChecks::inRange(values[1], -40, 85);
        count = 2;
        break;
      case 2:
        if (!read(sensor, 0x22, raw, 12)) {
          return;
        }
        for (size_t axis = 0; axis < 3; ++axis) {
          values[axis] = DiagnosticChecks::signed16(raw + axis * 2) * 0.00875f;
          values[axis + 3] = DiagnosticChecks::signed16(raw + 6 + axis * 2) * 0.000061f;
          plausible = plausible && DiagnosticChecks::inRange(values[axis], -287, 287) && DiagnosticChecks::inRange(values[axis + 3], -2, 2);
        }
        count = 6;
        break;
      default:
        if (!read(sensor, 0x68, raw, 6)) {
          return;
        }
        for (size_t axis = 0; axis < 3; ++axis) {
          values[axis] = DiagnosticChecks::signed16(raw + axis * 2) * 1.5f;
          plausible = plausible && DiagnosticChecks::inRange(values[axis], -49152, 49152);
        }
        count = 3;
        break;
    }
    if (!plausible) {
      reportError(sensor, "converted values outside sensor limits");
      return;
    }
    if (sensor.sampled && memcmp(values, sensor.values, count * sizeof(float)) != 0) {
      ++sensor.changes;
    }
    memcpy(sensor.values, values, sizeof(values));
    sensor.valid = sensor.sampled = true;
    char line[192];
    if (index == 0) {
      snprintf(line, sizeof(line), "HTS221: humidity %.2f %%RH, temperature %.2f C", static_cast<double>(values[0]), static_cast<double>(values[1]));
    } else if (index == 1) {
      snprintf(line, sizeof(line), "LPS22HB: pressure %.2f hPa, temperature %.2f C", static_cast<double>(values[0]), static_cast<double>(values[1]));
    } else if (index == 2) {
      snprintf(line, sizeof(line), "LSM6DSL: gyro %.2f %.2f %.2f dps; accel %.3f %.3f %.3f g", static_cast<double>(values[0]), static_cast<double>(values[1]), static_cast<double>(values[2]), static_cast<double>(values[3]), static_cast<double>(values[4]), static_cast<double>(values[5]));
    } else {
      snprintf(line, sizeof(line), "LIS2MDL: %.1f %.1f %.1f mG", static_cast<double>(values[0]), static_cast<double>(values[1]), static_cast<double>(values[2]));
    }
    Serial.print(line);
    Serial.print(F("; changed samples="));
    Serial.println(sensor.changes);
  }

  void show() {
    const unsigned int index = page < 3 ? page : page - 1;
    Sensor &sensor = sensors[index];
    Screen.print(0, page == 2 ? "Accelerometer g" : (page == 3 ? "Gyroscope dps" : sensor.name));
    if (!sensor.valid) {
      Screen.print(1, sensor.configured ? "No fresh reading" : "Init/ID failed");
      Screen.print(2, "See USB serial");
      Screen.print(3, "A:next B:sample");
      return;
    }
    char line[17];
    if (index < 2) {
      snprintf(line, sizeof(line), index == 0 ? "RH %6.2f %%" : "P %7.2f hPa", static_cast<double>(sensor.values[0]));
      Screen.print(1, line);
      snprintf(line, sizeof(line), "T %7.2f C", static_cast<double>(sensor.values[1]));
      Screen.print(2, line);
      Screen.print(3, "A:next B:sample");
    } else {
      const size_t first = page == 2 ? 3 : 0;
      for (size_t axis = 0; axis < 3; ++axis) {
        snprintf(line, sizeof(line), "%c %10.3f", "XYZ"[axis], static_cast<double>(sensor.values[first + axis]));
        Screen.print(static_cast<unsigned int>(axis + 1), line);
      }
    }
  }
}

namespace SensorTests {
  void begin() {
    if (!AppConfig::kSensorsEnabled) {
      return;
    }
    page = 0;
    for (unsigned int index = 0; index < 4; ++index) {
      sensors[index].configured = initialize(sensors[index], index);
    }
    Serial.println(F("Sensor checks cover identities, configuration readback, calibrated ranges and observed changes, not accuracy calibration or built-in MEMS self-tests."));
    lastSampleMs = millis();
    show();
  }

  void sampleNow() {
    if (!AppConfig::kSensorsEnabled) {
      return;
    }
    for (unsigned int index = 0; index < 4; ++index) {
      sample(sensors[index], index);
    }
    lastSampleMs = millis();
    show();
  }

  void update() {
    if (AppConfig::kSensorsEnabled && static_cast<uint32_t>(millis()) - lastSampleMs >= kIntervalMs) {
      sampleNow();
    }
  }

  void nextPage() {
    if (AppConfig::kSensorsEnabled) {
      page = (page + 1) % 5;
      show();
    }
  }
}
