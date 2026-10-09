#include <RGB_LED.h>
#include "src/AppConfig.h"
#include "src/AudioTests.h"
#include "src/GroveEInkTests.h"
#include "src/GroveOledTests.h"
#include "src/GroveNfcTests.h"
#include "src/LoRaBridge.h"
#include "src/OnboardTests.h"
#include "src/RadioBridge.h"
#include "src/RtcTests.h"
#include "src/SecureProvisioningMode.h"
#include "src/WiFiProvisioning.h"
#include "src/WiFiTests.h"

namespace {
  constexpr unsigned long kBlinkIntervalMs = 500;
  constexpr unsigned long kDisplayIntervalMs = 1000;
  constexpr unsigned long kButtonDebounceMs = 30;
  constexpr bool kRgbChannels[][3] = {{true, false, false}, {false, true, false}, {false, false, true}, {true, true, false}, {false, true, true}, {true, false, true}, {true, true, true}};
  constexpr int kRgbIntensities[] = {32, 128, 255};
  constexpr size_t kRgbColorCount = sizeof(kRgbChannels) / sizeof(kRgbChannels[0]);
  constexpr size_t kRgbIntensityCount = sizeof(kRgbIntensities) / sizeof(kRgbIntensities[0]);

  struct DebouncedButton {
    uint32_t pin;
    bool reading = false;
    bool stablePressed = false;
    unsigned long changedMs = 0;

    explicit DebouncedButton(uint32_t buttonPin): pin(buttonPin) {}

    void begin() {
      pinMode(pin, INPUT);
      reading = digitalRead(pin) == LOW;
      stablePressed = reading;
      changedMs = millis();
    }

    bool pressedEdge(unsigned long now) {
      const bool current = digitalRead(pin) == LOW;
      if (current != reading) {
        reading = current;
        changedMs = now;
      }
      if (now - changedMs < kButtonDebounceMs || reading == stablePressed) {
        return false;
      }
      stablePressed = reading;
      return stablePressed;
    }
  };

  RGB_LED rgbLed;
  DebouncedButton buttonA(USER_BUTTON_A);
  DebouncedButton buttonB(USER_BUTTON_B);
  size_t nextRgbStep = 0;
  unsigned long lastBlinkMs = 0;
  unsigned long lastDisplayMs = 0;
  bool ledOn = false;
}

static void advanceRgbTest() {
  const size_t color = nextRgbStep / kRgbIntensityCount;
  const int intensity = kRgbIntensities[nextRgbStep % kRgbIntensityCount];
  const int red = kRgbChannels[color][0] ? intensity : 0;
  const int green = kRgbChannels[color][1] ? intensity : 0;
  const int blue = kRgbChannels[color][2] ? intensity : 0;
  rgbLed.setColor(red, green, blue);
  nextRgbStep = (nextRgbStep + 1) % (kRgbColorCount * kRgbIntensityCount);

  char status[80];
  snprintf(status, sizeof(status), "Button A: RGB(%d, %d, %d), intensity %d/255", red, green, blue, intensity);
  Serial.println(status);
  snprintf(status, sizeof(status), "RGB %3d %3d %3d", red, green, blue);
  Screen.print(2, status);
}

static void updateRtcResultLed() {
  static RtcTests::Status shown = RtcTests::Status::NotReady;
  const RtcTests::Status current = RtcTests::status();
  if (current == shown) {
    return;
  }
  shown = current;
  rgbLed.setColor(current == RtcTests::Status::Failed ? 64 : 0, current == RtcTests::Status::Passed ? 64 : 0, current == RtcTests::Status::Testing ? 64 : 0);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  buttonA.begin();
  buttonB.begin();
  rgbLed.turnOff();

  Screen.init();
  Screen.clean();
  Screen.print(0, "MXCHIP AZ3166");
  Screen.print(
    1,
    AppConfig::kRtcEnabled
      ? "RTC init..."
      : (AppConfig::kWiFiEnabled ? "Wi-Fi init..." : (AppConfig::kWiFiProvisioningEnabled ? "Wi-Fi provision" : (AppConfig::kGroveOledEnabled ? "Grove OLED init" : (AppConfig::kGroveEInkEnabled ? "Grove E-ink init" : (AppConfig::kAudioEnabled ? "Audio init..." : "Audio suspended")))))
  );
  Screen.print(2, "Ready");

  Serial.println("MXCHIP AZ3166 test sketch started.");
  if (AppConfig::kNfcEnabled) {
    GroveNfcTests::begin();
    return;
  }
  if (AppConfig::kNfcEnabled) {
    GroveNfcTests::buttonA();
  } else if (AppConfig::kSecureProvisioningEnabled) {
    SecureProvisioningMode::begin();
    return;
  }
  if (AppConfig::kOnboardTestsEnabled) {
    OnboardTests::begin();
    return;
  }
  if (AppConfig::kRtcEnabled) {
    Serial.println(AppConfig::kDs1307Enabled ? F("Grove RTC v1.2 (DS1307, 0x68) mode.") : F("Grove High Precision RTC v1.0 mode."));
    RtcTests::printHelp();
    if (!RtcTests::begin()) {
      updateRtcResultLed();
      return;
    }
    RtcTests::runFull();
    updateRtcResultLed();
    return;
  }
  if (AppConfig::kWiFiEnabled) {
    WiFiTests::begin();
    return;
  }
  if (AppConfig::kWiFiProvisioningEnabled) {
    WiFiProvisioning::begin();
    return;
  }
  if (AppConfig::kGroveOledEnabled) {
    GroveOledTests::begin();
    return;
  }
  if (AppConfig::kGroveEInkEnabled) {
    GroveEInkTests::begin();
    return;
  }

  Serial.println("Press button A to cycle RGB colors and brightness.");
  if (AppConfig::kAudioEnabled) {
    if (AudioTests::begin()) {
      Serial.println("Press button B to play a short audio test through the headphone jack.");
    }
  } else {
    Serial.println("Audio suspended: the selected radio mode uses the shared Grove pins. Audio code and samples are retained.");
  }
  if (AppConfig::kLoRaEnabled) {
    LoRaBridge::begin();
  } else {
    RadioBridge::begin();
  }
}

void loop() {
  if (AppConfig::kAudioEnabled) {
    AudioTests::update();
  }
  if (buttonA.pressedEdge(millis())) {
    if (AppConfig::kNfcEnabled) {
      GroveNfcTests::buttonB();
    } else if (AppConfig::kSecureProvisioningEnabled) {
      SecureProvisioningMode::authorize();
    } else if (AppConfig::kOnboardTestsEnabled) {
      OnboardTests::buttonA();
    } else if (AppConfig::kRtcEnabled) {
      RtcTests::runFull();
    } else if (AppConfig::kWiFiEnabled) {
      WiFiTests::report();
    } else if (AppConfig::kWiFiProvisioningEnabled) {
      Serial.println(F("Complete provisioning over USB serial."));
    } else if (AppConfig::kGroveOledEnabled) {
      GroveOledTests::nextPattern();
    } else if (AppConfig::kGroveEInkEnabled) {
      GroveEInkTests::requestRefresh();
    } else {
      advanceRgbTest();
    }
  }
  if (buttonB.pressedEdge(millis())) {
    if (AppConfig::kNfcEnabled) {
      GroveNfcTests::update();
    } else if (AppConfig::kSecureProvisioningEnabled) {
      SecureProvisioningMode::abort();
    } else if (AppConfig::kOnboardTestsEnabled) {
      OnboardTests::buttonB();
    } else if (AppConfig::kRtcEnabled) {
      RtcTests::setToBuildTime();
    } else if (AppConfig::kWiFiEnabled) {
      WiFiTests::report();
    } else if (AppConfig::kWiFiProvisioningEnabled) {
      Serial.println(F("Complete provisioning over USB serial."));
    } else if (AppConfig::kGroveOledEnabled) {
      GroveOledTests::nextPattern();
    } else if (AppConfig::kGroveEInkEnabled) {
      GroveEInkTests::requestRefresh();
    } else if (AppConfig::kAudioEnabled) {
      AudioTests::advance();
    } else {
      Serial.println("Button B: audio suspended in the selected radio mode.");
      if (!AppConfig::kLoRaEnabled) {
        Screen.print(1, "Audio suspended");
      }
    }
  }
  if (AppConfig::kSecureProvisioningEnabled) {
    SecureProvisioningMode::update();
  } else if (AppConfig::kOnboardTestsEnabled) {
    while (Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) {
        Serial.println(F("Diagnostic USB serial read failed."));
        break;
      }
      OnboardTests::handleSerial(static_cast<char>(value));
    }
    OnboardTests::update();
  } else if (AppConfig::kRtcEnabled) {
    while (Serial.available() > 0) {
      RtcTests::handleSerial(static_cast<char>(Serial.read()));
    }
    RtcTests::updateDisplay();
    updateRtcResultLed();
  } else if (AppConfig::kWiFiEnabled) {
    WiFiTests::update();
  } else if (AppConfig::kWiFiProvisioningEnabled) {
    WiFiProvisioning::update();
  } else if (AppConfig::kGroveOledEnabled) {
    GroveOledTests::update();
  } else if (AppConfig::kGroveEInkEnabled) {
    return;
  } else if (AppConfig::kLoRaEnabled) {
    LoRaBridge::update();
  } else {
    RadioBridge::update();
  }

  const unsigned long now = millis();
  if (now - lastBlinkMs >= kBlinkIntervalMs) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
  }
  if (AppConfig::kRadioEnabled && now - lastDisplayMs >= kDisplayIntervalMs) {
    lastDisplayMs = now;
    char uptime[17];
    snprintf(uptime, sizeof(uptime), "Uptime: %lus", now / 1000);
    Screen.print(3, uptime);
    Serial.print("Uptime: ");
    Serial.print(now / 1000);
    Serial.println(" seconds");
  }
}
