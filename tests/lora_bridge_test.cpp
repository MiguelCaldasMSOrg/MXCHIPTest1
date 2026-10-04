#include <Arduino.h>
#include "AppConfig.h"
#include "LoRaBridge.h"
#include "LoRaFrame.h"
#include "SoftwareUart.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace FakeHardware {
  uint64_t nowUs = 0;
  bool loopback = false;
  bool outputLevel = false;
  unsigned int outputConstructions = 0;
  unsigned int outputWrites = 0;
  unsigned int inputConstructions = 0;
  bool pinLevels[4] = {true, true, true, true};
  unsigned int pinOutputConstructions[4] = {};
  void (*timerCallback)() = nullptr;
  unsigned int timerPeriodUs = 0;
}

TestSerial Serial;
TestScreen Screen;

static void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

struct E5 {
  SoftwareUart uart;
  std::string line;
  bool listening = false;
  std::vector<LoRaFrame::Message> transmissions;
  std::vector<unsigned long> transmitTimes;

  void reply(const std::string &text) {
    check(uart.write(text.data(), text.size()), "fake E5 response fits its UART queue");
  }
};

static E5 moduleA;
static E5 moduleB;
static bool deliverRequests = false;
static bool deliverReplies = false;
static bool corruptReply = false;

static std::string toHex(const uint8_t *bytes, size_t length) {
  const char *digits = "0123456789ABCDEF";
  std::string hex;
  for (size_t i = 0; i < length; i++) {
    hex += digits[bytes[i] >> 4];
    hex += digits[bytes[i] & 15];
  }
  return hex;
}

static void deliver(E5 &module, const uint8_t *bytes, size_t length) {
  module.reply("+TEST: LEN:" + std::to_string(length) + ", RSSI:-45, SNR:9\r\n+TEST: RX \"" + toHex(bytes, length) + "\"\r\n");
}

static void inject(E5 &module, LoRaFrame::Kind kind, uint32_t id, const std::string &text) {
  uint8_t bytes[LoRaFrame::kMaxSize];
  uint8_t length = 0;
  const std::vector<uint8_t> payload(text.begin(), text.end());
  check(LoRaFrame::encode(kind, id, payload.data(), payload.size(), bytes, length), "encode the deliberately injected test packet");
  deliver(module, bytes, length);
}

static void moduleCommands(E5 &module) {
  int byte = 0;
  while ((byte = module.uart.read()) >= 0) {
    if (byte == '\r') {
      continue;
    }
    if (byte != '\n') {
      module.line += static_cast<char>(byte);
      continue;
    }
    const std::string line = module.line;
    module.line.clear();
    if (line == "AT") {
      module.reply("+AT: OK\r\n");
    } else if (line == "AT+MODE=TEST") {
      module.reply("+MODE: TEST\r\n");
    } else if (line == "AT+TEST=STOP") {
      module.listening = false;
      module.reply("+TEST: STOP\r\n");
    } else if (line == "AT+TEST=RFCFG,868.1,SF7,125,12,15,10,ON,OFF,OFF") {
      module.reply("+TEST: RFCFG F:868100000, SF7, BW125K, TXPR:12, RXPR:15, POW:10dBm, CRC:ON, IQ:OFF, NET:OFF\r\n");
    } else if (line == "AT+TEST=RXLRPKT") {
      module.listening = true;
      module.reply("+TEST: RXLRPKT\r\n");
    } else if (line.find("AT+TEST=TXLRPKT,\"") == 0) {
      check(!module.listening, "RX must be stopped before issuing a transmit command");
      const size_t quote = line.find('"');
      check(line.back() == '"', "AT hex command closes its quote");
      const std::string hex = line.substr(quote + 1, line.size() - quote - 2);
      check(hex.size() % 2 == 0 && hex.size() <= LoRaFrame::kMaxSize * 2, "framed command has a bounded hexadecimal payload");
      std::vector<uint8_t> bytes;
      for (size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
      }
      LoRaFrame::Message message;
      check(LoRaFrame::decode(bytes.data(), bytes.size(), message), "every transmitted packet contains the application framing");
      module.transmissions.push_back(message);
      module.transmitTimes.push_back(millis());
      module.reply("+TEST: TXLRPKT\r\n+TEST: TX DONE\r\n");
      E5 &peer = &module == &moduleA ? moduleB : moduleA;
      check(peer.listening, "the peer must already be back in receive mode before the request or echo is sent");
      const bool deliveryEnabled = message.kind == LoRaFrame::Kind::Request ? deliverRequests : deliverReplies;
      if (deliveryEnabled) {
        if (message.kind == LoRaFrame::Kind::Reply && corruptReply) {
          bytes.back() ^= 1;
        }
        deliver(peer, bytes.data(), bytes.size());
      }
    } else {
      std::cerr << "Unexpected AT command: " << line << '\n';
      check(false, "bridge emits only the supported AT command sequence");
    }
  }
}

static void runBridge(unsigned int milliseconds) {
  const uint64_t until = FakeHardware::nowUs + static_cast<uint64_t>(milliseconds) * 1000;
  unsigned int ticks = 0;
  while (FakeHardware::nowUs < until) {
    FakeHardware::pinLevels[PB_0] = moduleA.uart.sample(FakeHardware::pinLevels[PB_14]);
    FakeHardware::pinLevels[PB_7] = moduleB.uart.sample(FakeHardware::pinLevels[PC_6]);
    FakeHardware::timerCallback();
    FakeHardware::nowUs += SoftwareUart::kSamplePeriodUs;
    moduleCommands(moduleA);
    moduleCommands(moduleB);
    if (++ticks % 40 == 0) {
      LoRaBridge::update();
    }
  }
  LoRaBridge::update();
}

static void console(const std::string &text) {
  Serial.input.insert(Serial.input.end(), text.begin(), text.end());
}

static bool contains(const std::string &text) {
  return Serial.output.find(text) != std::string::npos;
}

int main() {
  LoRaBridge::begin();
  if (!AppConfig::kLoRaEnabled) {
    LoRaBridge::update();
    check(FakeHardware::inputConstructions == 0 && FakeHardware::outputConstructions == 0, "inactive LoRa mode never configures Grove GPIOs");
    check(FakeHardware::timerCallback == nullptr, "inactive LoRa mode does not start its UART timer");
    std::cout << "PASS: LoRa GPIO/timer exclusion in non-LoRa firmware\n";
    return 0;
  }
  check(FakeHardware::inputConstructions == 2 && FakeHardware::outputConstructions == 2, "both E5 UARTs are constructed");
  check(FakeHardware::pinOutputConstructions[PB_0] == 0 && FakeHardware::pinOutputConstructions[PB_7] == 0, "P0/P2 remain inputs to avoid contention with module TX");
  check(FakeHardware::pinOutputConstructions[PB_14] == 1 && FakeHardware::pinOutputConstructions[PC_6] == 1, "P14/P16 supply commands to module RX");
  check(FakeHardware::timerPeriodUs == 26, "UART sampling interval matches the tested baud rate");
  runBridge(4000);
  check(contains("LoRa ready:") && moduleA.listening && moduleB.listening, "both transceivers start in receive mode");

  console("no local echo\r\n");
  runBridge(10000);
  check(moduleB.transmissions.empty(), "startup duty-cycle delay is enforced");
  runBridge(15000);
  check(moduleB.transmissions.size() == 1 && moduleA.transmissions.empty(), "a lost request must not cause a manufactured echo");
  check(contains("LoRa round trip 1 failed: forward reception timed out"), "TX DONE without reception is a failed round trip");
  check(Screen.lines[0] == "RF: waiting", "local input and transmit acknowledgement do not update the receive display");

  deliverRequests = deliverReplies = true;
  console("hello\n");
  runBridge(19000);
  check(contains("LoRa round trip 2 OK (A -> B -> A): hello"), "the next transaction alternates its initiating module");
  check(contains("LoRa A received: hello") && contains("LoRa B received: hello"), "both modules receive the actual payload");
  check(Screen.lines[0] == "RF: hello       " && Screen.lines[1] == "LoRa loopback OK", "received text and round-trip status are displayed");
  check(moduleA.listening && moduleB.listening, "both modules return to receive mode after transmission");

  console("abcdefghijklmnopqrst\n");
  runBridge(19000);
  check(contains("LoRa round trip 3 OK (B -> A -> B)"), "the following transaction uses the other direction");
  const std::string beforeScroll = Screen.lines[0];
  runBridge(300);
  check(Screen.lines[0] != beforeScroll, "long received messages continue scrolling");

  console(std::string(77, 'Z') + "\n");
  runBridge(19000);
  check(contains("LoRa round trip 4 OK (A -> B -> A): " + std::string(77, 'Z')), "all 77 user bytes survive both radio hops");
  for (E5 *module: {&moduleA, &moduleB}) {
    const LoRaFrame::Message &message = module->transmissions.back();
    check(message.id == 4 && message.length == 77 && std::string(message.payload, message.payload + message.length) == std::string(77, 'Z'), "framing does not reduce or alter the user payload");
  }

  deliverReplies = false;
  console("lost echo\n");
  runBridge(2500);
  inject(moduleB, LoRaFrame::Kind::Reply, 4, "lost echo");
  inject(moduleB, LoRaFrame::Kind::Request, 5, "lost echo");
  runBridge(1000);
  check(!contains("LoRa round trip 5 OK"), "stale or wrong-direction packets cannot count as the return echo");
  runBridge(9500);
  check(contains("LoRa round trip 5 failed: return echo timed out"), "a lost return hop fails instead of reporting success");

  deliverReplies = true;
  corruptReply = true;
  console("must match\n");
  runBridge(19000);
  check(contains("LoRa round trip 6 failed: received bytes differ"), "a correlated echo with altered bytes is rejected");
  check(!contains("LoRa round trip 6 OK"), "payload mismatch is not success");
  corruptReply = false;

  const size_t transmittedBefore = moduleA.transmissions.size() + moduleB.transmissions.size();
  inject(moduleA, LoRaFrame::Kind::Request, 999, "unsolicited");
  console(std::string(78, 'X') + "\n");
  runBridge(17000);
  check(moduleA.transmissions.size() + moduleB.transmissions.size() == transmittedBefore, "unsolicited frames and oversized lines do not trigger radio transmission");
  check(contains("line exceeds 77 bytes"), "overlength console input is reported");

  console("recovered\n");
  runBridge(19000);
  check(contains("LoRa round trip 7 OK (B -> A -> B): recovered"), "a new valid request can succeed after a failed transaction");
  for (E5 *module: {&moduleA, &moduleB}) {
    for (size_t i = 1; i < module->transmitTimes.size(); i++) {
      check(module->transmitTimes[i] - module->transmitTimes[i - 1] >= 16000, "each transceiver independently observes its duty-cycle interval");
    }
  }
  check(contains("passed=4 failed=3"), "round-trip successes and failures have distinct counters");
  check(!contains("LoRa A error:") && !contains("LoRa B error:"), "packet-loss tests do not create module-control errors");
  check(!Screen.invalidWrite, "OLED writes stay within the display dimensions");
  std::cout << "PASS: both modules TX/RX, alternating round trips, half-duplex handover, correlation, both-hop loss, changed-payload rejection, maximum text and independent duty pacing\n";
}
