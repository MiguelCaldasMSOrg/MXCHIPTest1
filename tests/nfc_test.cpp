#include "stubs/DiagnosticHardware.h"
#include "AppConfig.h"
#include "GroveNfcTests.h"
#include "NfcUart.h"
#include <vector>

namespace {
  NfcProtocol::Tag expected;
  struct Model: NfcProtocol::Link {
    unsigned int calls = 0;
    unsigned int writes = 0;
    unsigned int reads = 0;
    bool present = true;
    bool switched = false;
    bool authFails = false;
    bool protectedSector = false;
    bool writeAckLost = false;
    bool badReadback = false;
    bool restoreFails = false;
    unsigned int restoreFailAfter = 1;
    bool initializedFails = false;
    bool corruptBackupRead = false;
    uint8_t storageCode = 0x0F;
    bool unknownVersion = false;
    bool staticLocked = false;
    bool dynamicLocked = false;
    bool passwordEnabled = false;
    bool configLocked = false;
    bool mirrorEnabled = false;
    bool readFails = false;
    uint8_t original[16];
    uint8_t stored[16];
    std::vector<uint8_t> lastWrite;
    Model() {
      for (size_t i = 0; i < 16; ++i) {
        original[i] = static_cast<uint8_t>(i * 7);
      }
      reset();
    }
    void reset() {
      calls = writes = reads = 0;
      present = true;
      restoreFailAfter = 1;
      switched = authFails = protectedSector = writeAckLost = badReadback = restoreFails = initializedFails = corruptBackupRead = false;
      unknownVersion = staticLocked = dynamicLocked = passwordEnabled = configLocked = mirrorEnabled = false;
      readFails = false;
      memcpy(stored, original, 16);
      lastWrite.clear();
    }
    bool exchange(const uint8_t *command, size_t size, uint8_t *response, size_t capacity, size_t &length) override {
      ++calls;
      length = 0;
      if (command[0] == 0x02) {
        if (initializedFails) {
          return false;
        }
        check(size == 1 && capacity >= 4, "bounded firmware query");
        const uint8_t version[] = {0x32, 1, 6, 7};
        memcpy(response, version, sizeof(version));
        length = 4;
      } else if (command[0] == 0x14) {
        check(size == 4 && command[1] == 1 && command[3] == 0, "SAM normal mode without IRQ wiring");
      } else if (command[0] == 0x32) {
        check(size == 5 && command[1] == 5 && command[4] == 0, "bounded passive activation retry setting");
      } else if (command[0] == 0x4A) {
        check(size == 3 && command[1] == 1 && command[2] == 0 && capacity >= 16, "one ISO14443-A tag");
        if (!present || (restoreFails && writes >= restoreFailAfter)) {
          response[0] = 0;
          length = 1;
          return true;
        }
        response[0] = response[1] = 1;
        response[2] = 0;
        response[3] = 4;
        response[4] = expected.sak;
        response[5] = expected.uidLength;
        memcpy(response + 6, expected.uid, expected.uidLength);
        if (switched && writes != 0) {
          response[6] ^= 1;
        }
        length = 6 + expected.uidLength;
      } else if (command[0] == 0x42) {
        check(size == 2 && command[1] == 0x60 && capacity == 9, "NTAG GET_VERSION through PN532 communicate-through");
        const uint8_t version[] = {0, 0, 4, 4, 2, 1, 0, storageCode, 3};
        memcpy(response, version, 9);
        if (unknownVersion) {
          response[2] = 0x99;
        }
        length = 9;
      } else {
        if (readFails) {
          return false;
        }
        check(command[0] == 0x40 && command[1] == 1, "only explicit target data exchange");
        response[0] = 0;
        length = 1;
        if (command[2] == 0x60) {
          check(size == 14 && command[3] == 4 && memcmp(command + 10, expected.uid + expected.uidLength - 4, 4) == 0, "authenticate only sector1 with last four UID bytes");
          for (size_t i = 4; i < 10; ++i) {
            check(command[i] == 255, "factory Key A only");
          }
          if (authFails) {
            response[0] = 0x14;
          }
        } else if (command[2] == 0x30) {
          check(size == 4 && capacity >= 17, "bounded scratch/access metadata READ");
          length = 17;
          if (expected.type != NfcProtocol::Type::Classic1k && command[3] == 2) {
            memset(response + 1, 0, 16);
            response[3] = staticLocked ? 1 : 0;
          } else if (expected.type != NfcProtocol::Type::Classic1k && command[3] != 4) {
            const uint8_t lockPage = storageCode == 0x0F ? 40 : (storageCode == 0x11 ? 130 : 226);
            check(command[3] == lockPage, "correct per-NTAG lock/config pages");
            memset(response + 1, 0, 16);
            response[1] = dynamicLocked ? 1 : 0;
            response[8] = passwordEnabled ? 4 : 255;
            response[9] = configLocked ? 0x40 : 0;
            response[5] = mirrorEnabled ? 0x40 : 0;
          } else if (command[3] == 7) {
            memset(response + 1, 0, 16);
            response[7] = protectedSector ? 0 : 0xFF;
            response[8] = 0x07;
            response[9] = 0x80;
          } else {
            ++reads;
            memcpy(response + 1, stored, 16);
            if ((badReadback && writes == 1) || (corruptBackupRead && reads == 2)) {
              response[1] ^= 1;
            }
          }
        } else {
          const bool classic = expected.type == NfcProtocol::Type::Classic1k;
          check(command[2] == (classic ? 0xA0 : 0xA2) && command[3] == 4 && size == (classic ? 20U : 8U), "only Classic block4 or single NTAG page4 written");
          ++writes;
          memcpy(stored, command + 4, classic ? 16 : 4);
          lastWrite.assign(command + 4, command + size);
          if (writeAckLost && writes == 1) {
            return false;
          }
        }
      }
      return true;
    }
  } model;
  unsigned int wakes = 0;
  void send(const std::string &value) {
    Serial.input.insert(Serial.input.end(), value.begin(), value.end());
    GroveNfcTests::update();
  }
  void configureTag() {
    expected = {};
    expected.uidLength = 4;
    expected.sak = 8;
    expected.uid[0] = 1;
    expected.uid[1] = 2;
    expected.uid[2] = 3;
    expected.uid[3] = 4;
  }
}

namespace NfcUart {
  void wake() {
    ++wakes;
  }
  NfcProtocol::Link &link() {
    return model;
  }
}

int main() {
  configureTag();
  const uint8_t command[] = {2};
  uint8_t frame[NfcProtocol::kFrameBytes];
  const uint8_t golden[] = {0, 0, 255, 2, 254, 0xD4, 2, 0x2A, 0};
  check(NfcProtocol::encode(command, 1, frame) == sizeof(golden) && memcmp(frame, golden, sizeof(golden)) == 0, "PN532 command framing golden bytes");
  const uint8_t response[] = {0, 0, 255, 6, 250, 0xD5, 3, 0x32, 1, 6, 7, 0xE8, 0};
  uint8_t data[64];
  size_t length = 0;
  check(NfcProtocol::decode(response, sizeof(response), 2, data, sizeof(data), length) && length == 4 && data[0] == 0x32, "firmware response checksum and shape");
  for (size_t i = 0; i < sizeof(response); ++i) {
    uint8_t invalid[sizeof(response)];
    memcpy(invalid, response, sizeof(response));
    invalid[i] ^= 1;
    check(!NfcProtocol::decode(invalid, sizeof(invalid), 2, data, sizeof(data), length), "all malformed frame fields rejected");
  }
  check(!NfcProtocol::decode(response, sizeof(response) - 1, 2, data, sizeof(data), length) && !NfcProtocol::decode(response, sizeof(response), 2, data, 3, length), "truncation and output capacity");
  NfcProtocol::Reader device(model);
  for (int scenario = 0; scenario < 9; ++scenario) {
    model.reset();
    NfcProtocol::WriteTest test;
    if (scenario == 1) {
      model.authFails = true;
    }
    if (scenario == 2) {
      model.protectedSector = true;
    }
    if (scenario == 3) {
      model.corruptBackupRead = true;
    }
    if (scenario == 4) {
      model.writeAckLost = true;
    }
    if (scenario == 5) {
      model.badReadback = true;
    }
    if (scenario == 6) {
      model.restoreFails = true;
    }
    if (scenario == 7) {
      model.switched = true;
    }
    if (scenario == 8) {
      expected.sak = 0;
    }
    const auto result = test.run(device, expected);
    if (scenario == 0) {
      check(result == NfcProtocol::WriteResult::Passed && model.writes == 2 && !test.pending(), "pattern and original restored exactly");
    } else if (scenario == 4 || scenario == 5) {
      check(result == NfcProtocol::WriteResult::TestFailedRestored && model.writes == 2 && !test.pending(), "lost write ack or wrong readback restores originals without blind test replay");
    } else if (scenario == 6 || scenario == 7) {
      check(result == NfcProtocol::WriteResult::RestoreRequired && test.pending() && model.writes == 1, "removed/swapped tag retains RAM original and blocks restoration to another UID");
      check(test.run(device, expected) == NfcProtocol::WriteResult::Rejected && model.writes == 1, "pending original prevents another write test");
      model.restoreFails = model.switched = false;
      check(test.restore(device) && !test.pending() && model.writes == 2, "same tag explicitly restored later");
    } else {
      check(result == NfcProtocol::WriteResult::Rejected && model.writes == 0 && !test.pending(), "safe precondition failure never writes tag");
    }
    if (!test.pending()) {
      check(memcmp(model.stored, model.original, 16) == 0, "original bytes intact after test");
    }
    expected.sak = 8;
  }
  expected.uidLength = 7;
  expected.uid[4] = 5;
  expected.uid[5] = 6;
  expected.uid[6] = 7;
  model.reset();
  NfcProtocol::WriteTest sevenByte;
  check(sevenByte.run(device, expected) == NfcProtocol::WriteResult::Passed, "seven-byte Classic UID uses correct auth suffix");
  for (uint8_t code: {uint8_t(0x0F), uint8_t(0x11), uint8_t(0x13)}) {
    model.storageCode = code;
    expected.sak = 0;
    const auto type = code == 0x0F ? NfcProtocol::Type::Ntag213 : NfcProtocol::Type::Ntag215216;
    for (unsigned int fault = 0; fault < 7; ++fault) {
      model.reset();
      model.staticLocked = fault == 1;
      model.dynamicLocked = fault == 2;
      model.passwordEnabled = fault == 3;
      model.configLocked = fault == 4;
      model.mirrorEnabled = fault == 5;
      model.unknownVersion = fault == 6;
      const bool identified = device.identify(expected, type);
      NfcProtocol::WriteTest ntagTest;
      if (fault == 6) {
        check(!identified && model.writes == 0, "unsupported NTAG version rejected");
        continue;
      }
      check(identified, "exact NTAG GET_VERSION family and storage identification");
      const auto result = ntagTest.run(device, expected);
      if (fault == 0) {
        check(result == NfcProtocol::WriteResult::Passed && model.writes == 2 && model.lastWrite.size() == 4 && memcmp(model.stored, model.original, 16) == 0, "NTAG one-page pattern/restoration retains adjacent pages");
      } else {
        check(result == NfcProtocol::WriteResult::Rejected && model.writes == 0, "NTAG lock/password/config/mirror preconditions prevent writes");
      }
    }
    for (unsigned int fault = 0; fault < 3; ++fault) {
      model.reset();
      check(device.identify(expected, type), "identify before NTAG failure tests");
      model.writeAckLost = fault == 0;
      model.badReadback = fault == 1;
      model.switched = fault == 2;
      NfcProtocol::WriteTest ntagTest;
      const auto result = ntagTest.run(device, expected);
      if (fault < 2) {
        check(result == NfcProtocol::WriteResult::TestFailedRestored && !ntagTest.pending() && model.writes == 2, "NTAG uncertain write or verification failure restores exact original page");
      } else {
        check(result == NfcProtocol::WriteResult::RestoreRequired && model.writes == 1, "NTAG changed UID cannot receive saved page");
        model.switched = false;
        check(ntagTest.restore(device) && !ntagTest.pending(), "same NTAG page restored later");
      }
      check(memcmp(model.stored, model.original, 16) == 0, "NTAG adjacent pages and original page intact");
    }
  }
  configureTag();
  model.reset();
  GroveNfcTests::begin();
  if (!AppConfig::kNfcEnabled) {
    GroveNfcTests::buttonA();
    GroveNfcTests::buttonB();
    send("SCAN\nWRITE 01020304\n");
    check(wakes == 0 && model.calls == 0 && Serial.output.empty(), "inactive NFC claims no pins or protocol actions");
    std::cout << "PASS: NFC mode isolation and frame/write engine\n";
    return 0;
  }
  check(wakes == 1 && model.writes == 0 && Serial.output.find("Selected: NTAG213; A=Read.") != std::string::npos, "startup initializes and leaves A in Read with NTAG213 selected");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG213", "first B only resets to Read");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "Classic 1K", "consecutive B selects Classic");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG215/216", "consecutive B selects large NTAG family");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG213", "type cycle wraps");
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "A:read B:type", "type mismatch does not enable Write");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG213", "B immediately after A only resets");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "Classic 1K", "next B after reset cycles type");
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "A:write B:read", "successful selected-type data read changes A to Write");
  check(Screen.lines[0] == "Classic 1K" && Screen.lines[1] == "Write: Read OK" && Screen.lines[2] == "01020304", "OLED always shows selected model, current A mode and tag UID");
  check(Serial.output.find("Detected model: MIFARE Classic 1K") != std::string::npos && Serial.output.find("Block 4 (16 bytes): 00 07 0E") != std::string::npos, "serial reports actual model and scratch contents");
  send("WRITE 01020304\n");
  check(model.writes == 0, "physical arming required");
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonA();
  send("WRITE DEADBEEF\n");
  check(model.writes == 0 && Serial.output.find("Confirmation UID does not match") != std::string::npos, "typed UID match required after physical arming");
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonA();
  FakeHardware::nowUs += 60000000;
  send("WRITE 01020304\n");
  check(model.writes == 0, "60-second confirmation expires");
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonA();
  send("WRITE 01020304\r\n");
  check(model.writes == 2 && memcmp(model.stored, model.original, 16) == 0, "confirmed write test and original readback");
  send("WRITE 01020304\n");
  check(model.writes == 2, "arming is one-use");
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonB();
  send("WRITE 01020304\n");
  check(model.writes == 2, "cancel never writes tag");
  check(Screen.lines[0] == "Classic 1K" && Screen.lines[3] == "A:read B:type", "B after A resets without changing type");
  model.restoreFails = true;
  model.restoreFailAfter = 3;
  GroveNfcTests::buttonA();
  GroveNfcTests::buttonA();
  send("WRITE 01020304\n");
  check(model.writes == 3, "restore failure retains original");
  check(Screen.lines[1] == "Read: Restore!" && Screen.lines[2] == "01020304", "pending restoration keeps mode, model and original UID visible");
  send("SCAN\n");
  check(model.writes == 3 && Serial.output.find("Restore the saved block first") != std::string::npos, "pending restoration stops normal scanning");
  model.restoreFails = false;
  GroveNfcTests::buttonB();
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "Classic 1K", "B cancels but cannot cycle types with an original pending");
  GroveNfcTests::buttonA();
  send("RESTORE 01020304\n");
  check(model.writes == 4 && memcmp(model.stored, model.original, 16) == 0, "explicit restore requires physical confirmation");
  send(std::string(60, 'X') + "\n");
  check(Serial.output.find("overlong") != std::string::npos && !Screen.invalidWrite, "bounded commands and onboard OLED layout");
  model.reset();
  expected = {};
  expected.uidLength = 7;
  expected.sak = 0;
  expected.type = NfcProtocol::Type::Ntag213;
  for (size_t i = 0; i < 7; ++i) {
    expected.uid[i] = static_cast<uint8_t>(i + 1);
  }
  model.storageCode = 0x0F;
  GroveNfcTests::begin();
  model.present = false;
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "A:read B:type", "no tag retains A=Read");
  model.present = true;
  model.readFails = true;
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "A:read B:type", "failed scratch read retains A=Read");
  model.readFails = false;
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "A:write B:read", "successful NTAG213 read enables Write");
  check(Serial.output.find("Detected model: NTAG213") != std::string::npos && Serial.output.find("Pages 4-7 (16 bytes") != std::string::npos, "NTAG read detail identifies model and page range");
  GroveNfcTests::buttonA();
  check(Screen.lines[3] == "Armed 60 seconds" && model.writes == 0, "A in Write arms only; no physical-press write");
  send("WRITE 01020304050607\n");
  check(model.writes == 2 && Screen.lines[3] == "A:read B:type", "NTAG confirmed write returns to Read");
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG213", "first B after NTAG A resets only");
  GroveNfcTests::buttonB();
  GroveNfcTests::buttonB();
  check(Screen.lines[0] == "NTAG215/216", "consecutive B reaches grouped larger type");
  expected.type = NfcProtocol::Type::Ntag215216;
  for (uint8_t code: {uint8_t(0x11), uint8_t(0x13)}) {
    model.storageCode = code;
    GroveNfcTests::buttonA();
    check(Screen.lines[3] == "A:write B:read", "grouped selector accepts both NTAG215 and NTAG216 after exact version check");
    check(Serial.output.find(code == 0x11 ? "Detected model: NTAG215;" : "Detected model: NTAG216;") != std::string::npos, "serial distinguishes exact detected models within grouped selector");
    GroveNfcTests::buttonA();
    send("WRITE 01020304050607\n");
  }
  check(model.writes == 6 && memcmp(model.stored, model.original, 16) == 0 && !Screen.invalidWrite, "all three selected families keep the button flow and restoration");
  std::cout << "PASS: all tag families, page/block-only writes, lock/protection checks, UID restoration and exact A/B Read/Write/type-cycle state machine\n";
}
