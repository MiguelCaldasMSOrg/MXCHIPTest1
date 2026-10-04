#include <Arduino.h>
#include "RadioDisplay.h"
#include "SerialRadioInput.h"

namespace {
  constexpr uint32_t kScrollMs = 250;
  constexpr size_t kDisplayColumns = 16;
  constexpr size_t kMessageColumns = 12;
  constexpr size_t kScrollGap = 3;
  char message[SerialRadioInput::kMaxPayloadLength + 1] = {};
  size_t messageLength = 0;
  size_t scroll = 0;
  uint32_t lastScrollMs = 0;

  void draw() {
    char line[kDisplayColumns + 1] = "RF: ";
    for (size_t i = 0; i < kMessageColumns; i++) {
      size_t position = scroll + i;
      if (messageLength > kMessageColumns) {
        position %= messageLength + kScrollGap;
      }
      line[4 + i] = position < messageLength ? message[position] : ' ';
    }
    line[kDisplayColumns] = '\0';
    Screen.print(0, line);
  }
}

namespace RadioDisplay {
  void begin() {
    messageLength = 0;
    scroll = 0;
    lastScrollMs = millis();
    Screen.print(0, "RF: waiting");
  }

  void show(const uint8_t *payload, uint8_t length, const char *source) {
    if (payload == nullptr || length == 0 || length > SerialRadioInput::kMaxPayloadLength) {
      Serial.println("Radio display error: invalid payload length.");
      return;
    }
    char text[SerialRadioInput::kMaxPayloadLength + 1];
    size_t used = 0;
    bool substituted = false;
    for (size_t i = 0; i < length && payload[i] != 0; i++) {
      const uint8_t value = payload[i];
      if (value == '\r' || value == '\n' || value == '\t') {
        text[used++] = ' ';
      } else if (value >= 32 && value <= 126) {
        text[used++] = static_cast<char>(value);
      } else {
        text[used++] = '?';
        substituted = true;
      }
    }
    text[used] = '\0';
    if (used == 0) {
      strcpy(text, "(empty)");
      used = 7;
    }
    Serial.print(source);
    Serial.print(" received: ");
    Serial.println(text);
    if (substituted) {
      Serial.println("RF text: unsupported display characters replaced with '?'.");
    }
    if (messageLength == 0 || strcmp(text, message) != 0) {
      memcpy(message, text, used + 1);
      messageLength = used;
      scroll = 0;
      lastScrollMs = millis();
      draw();
    }
  }

  void update() {
    const uint32_t now = millis();
    if (messageLength > kMessageColumns && now - lastScrollMs >= kScrollMs) {
      lastScrollMs = now;
      scroll = (scroll + 1) % (messageLength + kScrollGap);
      draw();
    }
  }
}
