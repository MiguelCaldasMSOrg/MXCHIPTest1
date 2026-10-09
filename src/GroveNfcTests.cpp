#include <Arduino.h>
#include "AppConfig.h"
#include "GroveNfcTests.h"
#include "NfcUart.h"
#include "SerialLineInput.h"

namespace {
  NfcProtocol::Tag tag;
  NfcProtocol::WriteTest writeTest;
  SerialLineInput<32, 1> input;
  bool ready = false;
  bool haveTag = false;
  bool armed = false;
  uint32_t armedMs = 0;
  NfcProtocol::Type selected = NfcProtocol::Type::Ntag213;
  bool writeMode = false;
  bool previousButtonB = false;
  const char *displayStatus = "Ready";
  char displayedUid[21] = {};

  const char *typeName() {
    switch (selected) {
      case NfcProtocol::Type::Ntag213:
        return "NTAG213";
      case NfcProtocol::Type::Classic1k:
        return "Classic 1K";
      default:
        return "NTAG215/216";
    }
  }
  void showControls() {
    char state[17];
    snprintf(state, sizeof(state), "%s: %.9s", writeMode ? "Write" : "Read", displayStatus);
    Screen.print(0, typeName());
    Screen.print(1, state);
    Screen.print(2, displayedUid[0] ? displayedUid : "No UID", true);
    Screen.print(3, armed ? "Armed 60 seconds" : (writeMode ? "A:write B:read" : "A:read B:type"));
    Serial.print(F("Selected: "));
    Serial.print(typeName());
    Serial.println(writeMode ? F("; A=Write (arm UID command).") : F("; A=Read."));
  }
  void status(const char *value) {
    displayStatus = value;
    showControls();
  }
  const char *detectedModel(const NfcProtocol::Tag &value) {
    if (value.type == NfcProtocol::Type::Classic1k) {
      return "MIFARE Classic 1K";
    }
    if (value.storageCode == 0x0F) {
      return "NTAG213";
    }
    return value.storageCode == 0x11 ? "NTAG215" : "NTAG216";
  }
  void printData(const uint8_t *data) {
    char bytes[48];
    for (size_t i = 0; i < 16; ++i) {
      snprintf(bytes + i * 3, sizeof(bytes) - i * 3, i == 15 ? "%02X" : "%02X ", data[i]);
    }
    Serial.print(tag.type == NfcProtocol::Type::Classic1k ? F("Block 4 (16 bytes): ") : F("Pages 4-7 (16 bytes; only page 4 is used for writes): "));
    Serial.println(bytes);
  }

  NfcProtocol::Reader reader() {
    return NfcProtocol::Reader(NfcUart::link());
  }
  void uidText(const NfcProtocol::Tag &value, char *text) {
    const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < value.uidLength; ++i) {
      text[i * 2] = digits[value.uid[i] >> 4];
      text[i * 2 + 1] = digits[value.uid[i] & 15];
    }
    text[value.uidLength * 2] = 0;
  }
  void help() {
    Serial.println(F("NFC mode16: Grove P0 -> NFC RX, P14 <- NFC TX; 3.3V, factory UART 115200."));
    Serial.println(F("A=Read: scan/validate selected type and read scratch data, then A=Write. A=Write: arm one UID command for 60s, then A=Read."));
    Serial.println(F("B always cancels arming and resets A=Read. Consecutive B presses cycle NTAG213 -> Classic1K -> NTAG215/216; first B after A only resets."));
    Serial.println(F("WRITE <UID>: Classic1K block4/default KeyA/access FF0780, or unlocked/password-disabled NTAG page4. Pattern readback + original restoration."));
    Serial.println(F("Use a spare tag. Keep it on the antenna and keep board power on. No manufacturer, trailer, key, lock or config writes."));
    Serial.println(F("If restoration fails, DO NOT RESET: original block remains in RAM. Return the same tag, press A, send RESTORE <UID>."));
  }
  void fail(const char *reason) {
    status(writeTest.pending() ? "Restore!" : "Error");
    Serial.print(F("NFC FAIL: "));
    Serial.println(reason);
  }
  void scan() {
    armed = false;
    writeMode = false;
    haveTag = false;
    if (!writeTest.pending()) {
      displayedUid[0] = 0;
    }
    status("Scanning");
    if (!ready) {
      fail("Module not initialized; reset after checking power/UART wiring.");
      return;
    }
    if (writeTest.pending()) {
      fail("Restore the saved block first; automatic scans are suspended.");
      return;
    }
    auto device = reader();
    if (!device.scan(tag, haveTag)) {
      ready = false;
      fail("PN532 scan response/transport error; no automatic command retry.");
      return;
    }
    if (!haveTag) {
      status("No tag");
      Serial.println(F("NFC: no ISO14443-A tag detected."));
      return;
    }
    char uid[21] = {};
    char message[100];
    uidText(tag, uid);
    memcpy(displayedUid, uid, sizeof(displayedUid));
    snprintf(message, sizeof(message), "Tag UID=%s ATQA=%04X SAK=%02X.", uid, tag.atqa, tag.sak);
    Serial.println(message);
    if (!device.identify(tag, selected)) {
      haveTag = false;
      fail("Tag does not match the selected type or its identity response failed. A remains Read.");
      return;
    }
    uint8_t data[16];
    if (!device.readSelected(tag, data)) {
      haveTag = false;
      fail("Selected tag's scratch data could not be read/authenticated. A remains Read.");
      return;
    }
    writeMode = true;
    snprintf(message, sizeof(message), "Detected model: %s; UID length=%u; storage code=%02X.", detectedModel(tag), tag.uidLength, tag.storageCode);
    Serial.println(message);
    printData(data);
    if (tag.type == NfcProtocol::Type::Classic1k) {
      Serial.println(F("Read authenticated with factory Key A. Write will additionally verify sector1 access bytes; block4 only."));
    } else {
      Serial.println(F("Write will verify static/dynamic locks, disabled password protection, config lock and mirroring; page4 only."));
    }
    status("Read OK");
    Serial.println(F("Verified: selected tag identity and scratch data read. A now Write."));
  }
  void command(const char *line) {
    if (strcmp(line, "SCAN") == 0) {
      scan();
      return;
    }
    if (strcmp(line, "HELP") == 0) {
      help();
      return;
    }
    const bool restore = strncmp(line, "RESTORE ", 8) == 0;
    const bool write = strncmp(line, "WRITE ", 6) == 0;
    if (!write && !restore) {
      armed = false;
      writeMode = false;
      showControls();
      fail("Unknown command; send HELP.");
      return;
    }
    const bool authorized = armed && static_cast<uint32_t>(millis()) - armedMs < 60000;
    armed = false;
    writeMode = false;
    showControls();
    if (!authorized) {
      fail("A fresh physical Button A confirmation is required; serial input cannot arm writes.");
      return;
    }
    char uid[21];
    const auto &expected = writeTest.pending() ? writeTest.tag() : tag;
    uidText(expected, uid);
    if ((!writeTest.pending() && !haveTag) || strcmp(uid, line + (restore ? 8 : 6)) != 0) {
      fail("Confirmation UID does not match the selected tag.");
      return;
    }
    auto device = reader();
    if (restore) {
      if (!writeTest.pending()) {
        fail("No pending original block.");
        return;
      }
      if (!writeTest.restore(device)) {
        fail("Original block restoration not verified. Keep power on; do not reset.");
        return;
      }
      ready = true;
      status("Restored");
      Serial.println(F("Verified: original scratch data restored on the selected tag."));
      return;
    }
    if (!ready || writeTest.pending()) {
      fail("Write unavailable: module error or original block needs restoration.");
      return;
    }
    status("Testing");
    Serial.print(F("Write test target: "));
    Serial.print(detectedModel(tag));
    Serial.println(tag.type == NfcProtocol::Type::Classic1k ? F(", block4, 16 bytes; original will be restored.") : F(", page4, 4 bytes; adjacent pages remain unchanged."));
    const auto result = writeTest.run(device, tag);
    if (result == NfcProtocol::WriteResult::Passed) {
      status("PASS");
      Serial.println(F("Verified: scratch test pattern readback and original restoration (Classic block4 or NTAG page4)."));
    } else if (result == NfcProtocol::WriteResult::TestFailedRestored) {
      fail("Write/readback test failed, but original scratch data was restored and verified. No automatic test retry.");
    } else if (result == NfcProtocol::WriteResult::RestoreRequired) {
      fail("Original bytes retained in RAM. Keep power on, return the same tag, press A and send RESTORE <UID>.");
    } else {
      fail("Identity/authentication/lock/protection/access/readback precondition failed; no tag write attempted.");
    }
  }
}

namespace GroveNfcTests {
  void begin() {
    if (!AppConfig::kNfcEnabled) {
      return;
    }
    if (writeTest.pending()) {
      fail("Original scratch data still needs restoration; do not restart the mode.");
      return;
    }
    selected = NfcProtocol::Type::Ntag213;
    haveTag = armed = writeMode = previousButtonB = false;
    displayedUid[0] = 0;
    displayStatus = "Init";
    input.reset();
    help();
    showControls();
    NfcUart::wake();
    uint8_t version[4];
    auto device = reader();
    ready = device.initialize(version);
    if (!ready) {
      fail("Firmware query/SAM/retry configuration failed. Check module factory UART pads and P0/P14.");
      return;
    }
    char message[160];
    snprintf(message, sizeof(message), "Verified: PN532 firmware %u.%u; support mask %02X. Tag writes require explicit confirmation.", version[1], version[2], version[3]);
    Serial.println(message);
    status("Ready");
  }
  void buttonA() {
    if (!AppConfig::kNfcEnabled) {
      return;
    }
    previousButtonB = false;
    if (!writeMode && !writeTest.pending()) {
      scan();
      return;
    }
    if (!haveTag && !writeTest.pending()) {
      writeMode = false;
      fail("Read a matching tag first.");
      return;
    }
    armed = true;
    writeMode = false;
    armedMs = millis();
    status("Armed");
    Serial.println(F("NFC WRITE/RESTORE authorization armed for one UID command, 60 seconds. A is now Read."));
    char uid[21];
    uidText(writeTest.pending() ? writeTest.tag() : tag, uid);
    Serial.print(writeTest.pending() ? F("Send RESTORE ") : F("Send WRITE "));
    Serial.println(uid);
  }
  void buttonB() {
    if (!AppConfig::kNfcEnabled) {
      return;
    }
    armed = false;
    writeMode = false;
    haveTag = false;
    input.reset();
    if (previousButtonB && !writeTest.pending()) {
      selected = static_cast<NfcProtocol::Type>((static_cast<unsigned int>(selected) + 1) % 3);
      displayedUid[0] = 0;
    }
    previousButtonB = true;
    status(writeTest.pending() ? "Restore!" : "Ready");
    if (writeTest.pending()) {
      fail("Original scratch data pending; tag-type cycling is blocked. A arms RESTORE.");
    }
  }
  void update() {
    if (!AppConfig::kNfcEnabled) {
      return;
    }
    if (armed && static_cast<uint32_t>(millis()) - armedMs >= 60000) {
      armed = false;
      status("Expired");
      Serial.println(F("NFC write/restore authorization expired. A=Read."));
    }
    while (Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) {
        fail("USB serial read failed.");
        return;
      }
      using Result = SerialLineInput<32, 1>::Result;
      const auto result = input.push(static_cast<uint8_t>(value));
      if (result == Result::Queued) {
        uint8_t payload[32] = {};
        uint8_t length = 0;
        if (!input.read(payload, length)) {
          fail("Command queue error.");
          return;
        }
        char line[33] = {};
        memcpy(line, payload, length);
        command(line);
      } else if (result != Result::None && result != Result::Empty) {
        armed = false;
        writeMode = false;
        showControls();
        fail("Invalid or overlong NFC command; send HELP.");
      }
    }
  }
}
