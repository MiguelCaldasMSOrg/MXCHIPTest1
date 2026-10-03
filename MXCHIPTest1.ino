#include <Arduino.h>

namespace {
constexpr unsigned long kBlinkIntervalMs = 500;
constexpr unsigned long kDisplayIntervalMs = 1000;

unsigned long lastBlinkMs = 0;
unsigned long lastDisplayMs = 0;
bool ledOn = false;
}

void setup()
{
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Screen.init();
  Screen.clean();
  Screen.print(0, "MXCHIP AZ3166");
  Screen.print(1, "Arduino CLI");
  Screen.print(2, "Ready on COM3");

  Serial.println("MXCHIP AZ3166 test sketch started.");
}

void loop()
{
  const unsigned long now = millis();

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
