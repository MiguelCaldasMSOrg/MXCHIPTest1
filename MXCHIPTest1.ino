#include <RGB_LED.h>
#include "src/AppConfig.h"
#include "src/AudioTests.h"
#include "src/RadioBridge.h"

namespace {
  constexpr unsigned long kBlinkIntervalMs = 500;
  constexpr unsigned long kDisplayIntervalMs = 1000;
  constexpr unsigned long kButtonDebounceMs = 30;
  constexpr bool kRgbChannels[][3] = {
    {true, false, false},
    {false, true, false},
    {false, false, true},
    {true, true, false},
    {false, true, true},
    {true, false, true},
    {true, true, true}
  };
  constexpr int kRgbIntensities[] = {32, 128, 255};
  constexpr size_t kRgbColorCount = sizeof(kRgbChannels) / sizeof(kRgbChannels[0]);
  constexpr size_t kRgbIntensityCount = sizeof(kRgbIntensities) / sizeof(kRgbIntensities[0]);

  struct DebouncedButton {
    uint32_t pin;
    bool reading = false;
    bool stablePressed = false;
    unsigned long changedMs = 0;

    explicit DebouncedButton(uint32_t buttonPin) : pin(buttonPin) {
    }

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
  Screen.print(1, AppConfig::kAudioEnabled ? "Audio init..." : "Audio suspended");
  Screen.print(2, "Ready");

  Serial.println("MXCHIP AZ3166 test sketch started.");
  Serial.println("Press button A to cycle RGB colors and brightness.");
  if (AppConfig::kAudioEnabled) {
    if (AudioTests::begin()) {
      Serial.println("Press button B to play a short audio test through the headphone jack.");
    }
  } else {
    Serial.println("Audio suspended: Grove RF TX uses P2/PB_7. Audio code and samples are retained.");
  }
  RadioBridge::begin();
}

void loop() {
  if (AppConfig::kAudioEnabled) {
    AudioTests::update();
  }
  const unsigned long now = millis();
  if (buttonA.pressedEdge(now)) {
    advanceRgbTest();
  }
  if (buttonB.pressedEdge(now)) {
    if (AppConfig::kAudioEnabled) {
      AudioTests::advance();
    } else {
      Serial.println("Button B: audio suspended while RF transmission uses P2.");
      Screen.print(1, "Audio suspended");
    }
  }
  RadioBridge::update();

  if (now - lastBlinkMs >= kBlinkIntervalMs) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(LED_BUILTIN, ledOn ? HIGH : LOW);
  }
  if (now - lastDisplayMs >= kDisplayIntervalMs) {
    lastDisplayMs = now;
    char uptime[17];
    snprintf(uptime, sizeof(uptime), "Uptime: %lus", now / 1000);
    Screen.print(3, uptime);
    Serial.print("Uptime: ");
    Serial.print(now / 1000);
    Serial.println(" seconds");
  }
}
