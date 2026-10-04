#include "LoRaE5.h"
#include "SoftwareUart.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

static void feedUart(SoftwareUart &uart, uint8_t byte, bool validStop = true, double bitUs = 104.1666667, unsigned int phaseUs = 0) {
  for (unsigned int i = 0; i < 5; i++) {
    uart.sample(true);
  }
  const unsigned int ticks = static_cast<unsigned int>(std::ceil((phaseUs + 10 * bitUs) / SoftwareUart::kSamplePeriodUs));
  for (unsigned int tick = 0; tick < ticks; tick++) {
    const double time = tick * SoftwareUart::kSamplePeriodUs - static_cast<double>(phaseUs);
    const int bit = static_cast<int>(std::floor(time / bitUs));
    const bool high = bit < 0 || bit >= 10 || (bit == 9 && validStop) || (bit > 0 && bit < 9 && (byte & (1U << (bit - 1))) != 0);
    uart.sample(high);
  }
  for (unsigned int i = 0; i < 5; i++) {
    uart.sample(true);
  }
}

static void testUart() {
  for (unsigned int phase = 0; phase < SoftwareUart::kSamplePeriodUs; phase++) {
    for (double period: {102.5, 104.1666667, 105.8}) {
      SoftwareUart uart;
      for (unsigned int byte = 0; byte < 256; byte++) {
        feedUart(uart, static_cast<uint8_t>(byte), true, period, phase);
        check(uart.read() == static_cast<int>(byte), "UART decodes every byte over sampling phases and clock tolerances");
      }
      check(uart.errors() == 0, "valid UART frames do not report errors");
    }
  }
  {
    SoftwareUart uart;
    const char bytes[] = {0, static_cast<char>(0xFF), 0x55, static_cast<char>(0xAA)};
    check(uart.write(bytes, sizeof(bytes)), "UART accepts TX bytes");
    for (uint8_t byte: bytes) {
      for (unsigned int bit = 0; bit < 10; bit++) {
        const bool expected = bit == 9 || (bit > 0 && (byte & (1U << (bit - 1))) != 0);
        for (unsigned int sample = 0; sample < SoftwareUart::kSamplesPerBit; sample++) {
          check(uart.sample(true) == expected, "UART emits exact 8N1 timing and bit order");
        }
      }
    }
    check(!uart.transmitting() && uart.sample(true), "UART returns to high idle");
  }
  {
    SoftwareUart uart;
    std::string full(SoftwareUart::kTxCapacity, 'X');
    check(uart.write(full.data(), full.size()), "UART accepts exact TX queue capacity");
    check(!uart.write("Y", 1), "UART rejects TX overflow without truncation");
    uart.discardTransmit();
    check(uart.sample(true) && !uart.transmitting(), "cancelled TX stops driving a partial command");
    feedUart(uart, 'X', false);
    check(uart.errors() == 1 && uart.read() == -1, "invalid stop bit is reported and rejected");
    feedUart(uart, 'Z');
    check(uart.read() == 'Z', "UART recovers after a framing error");
    for (size_t i = 0; i <= SoftwareUart::kRxCapacity; i++) {
      feedUart(uart, 'A');
    }
    check(uart.errors() == 2, "UART RX overflow is counted");
    for (size_t i = 0; i < SoftwareUart::kRxCapacity; i++) {
      check(uart.read() == 'A', "RX overflow does not overwrite unread bytes");
    }
    check(uart.read() == -1, "RX capacity is bounded");
  }
  std::cout << "PASS: 9600-8N1 UART framing, phase/timing tolerance, finite queues and explicit errors\n";
}

static void reply(LoRaE5 &modem, const std::string &text, uint32_t now = 2500) {
  for (unsigned char byte: text) {
    modem.input(byte, now);
  }
}

static void expectCommand(LoRaE5 &modem, const char *command, const char *response, uint32_t now = 2500) {
  check(modem.command() != nullptr && strcmp(modem.command(), command) == 0, "expected AT command is pending");
  modem.commandSent(now);
  check(modem.command() == nullptr, "a sent command is not resent while awaiting its response");
  reply(modem, response, now + 10);
  check(modem.error() == nullptr, "valid AT response is accepted");
}

static void configure(LoRaE5 &modem, const char *config = "+TEST: RFCFG F:868100000, SF7, BW125K, TXPR:12, RXPR:15, POW:10dBm, CRC:ON, IQ:OFF, NET:OFF\r\n") {
  modem.begin(0);
  modem.update(1999);
  check(modem.command() == nullptr, "modules get a startup settling interval");
  modem.update(2000);
  expectCommand(modem, "AT\r\n", "+AT: OK\r\n");
  expectCommand(modem, "AT+MODE=TEST\r\n", "+MODE: TEST\r\n");
  expectCommand(modem, "AT+TEST=STOP\r\n", "+TEST: STOP\r\n");
  expectCommand(modem, "AT+TEST=RFCFG,868.1,SF7,125,12,15,10,ON,OFF,OFF\r\n", config);
  expectCommand(modem, "AT+TEST=RXLRPKT\r\n", "+TEST: RXLRPKT\r\n");
  check(modem.ready(), "module becomes ready only after verified configuration");
}

static void testModem() {
  {
    LoRaE5 tx;
    configure(tx);
    const uint8_t text[] = {'h', '"', '\\', ' ', '\t'};
    check(tx.transmit(text, sizeof(text)), "TX accepts supported byte data");
    expectCommand(tx, "AT+TEST=STOP\r\n", "+TEST: STOP\r\n");
    check(strcmp(tx.command(), "AT+TEST=TXLRPKT,\"68225C2009\"\r\n") == 0, "text is hex encoded, not interpolated into AT syntax");
    check(!tx.transmit(text, sizeof(text)), "busy TX is not overwritten");
    tx.commandSent(3000);
    reply(tx, "+TEST: TXLRPKT \"68225C2009\"\r\n", 3010);
    check(tx.transmitting(), "TX echo is not completion");
    uint8_t data[LoRaE5::kMaxPayloadLength];
    uint8_t length = 0;
    check(!tx.receive(data, length), "TX echo never becomes received text");
    reply(tx, "+TEST: TX DONE\r\n", 3200);
    check(!tx.ready() && tx.completedTransmissions() == 1, "TX DONE is not equivalent to being ready to receive the echo");
    expectCommand(tx, "AT+TEST=RXLRPKT\r\n", "+TEST: RXLRPKT\r\n", 3300);
    check(tx.ready(), "sender becomes ready after returning to receive mode");
    reply(tx, "+TEST: RX \"6869\"\r\n");
    check(tx.receive(data, length) && length == 2 && memcmp(data, "hi", 2) == 0, "a transmitting modem can subsequently receive");
    check(!tx.transmit(nullptr, 1) && !tx.transmit(text, 0), "invalid TX arguments are rejected");
    std::vector<uint8_t> maximum(LoRaE5::kMaxPayloadLength, 0xFF);
    check(tx.transmit(maximum.data(), maximum.size()), "maximum payload fits the AT command");
    expectCommand(tx, "AT+TEST=STOP\r\n", "+TEST: STOP\r\n");
    check(strlen(tx.command()) < SoftwareUart::kTxCapacity, "longest command fits the serial TX buffer");
  }
  {
    LoRaE5 rx;
    configure(rx, "+TEST: RFCFG F:868100000, SF7, BW125K, TXPR:12, RXPR:15, POW:10dBm, CRC:ON, IQ:OFF, PNET:OFF\r\n");
    reply(rx, "+TEST: LEN:5, RSSI:-32, SNR:10\r\n+TEST: RX \"68656c6c6f\"\r\n");
    uint8_t data[LoRaE5::kMaxPayloadLength];
    uint8_t length = 0;
    check(rx.receive(data, length) && length == 5 && memcmp(data, "hello", 5) == 0, "receiver decodes quoted lower-case hex and metadata");
    reply(rx, "+TEST: RX 41 42 43\r\n");
    check(rx.receive(data, length) && length == 3 && memcmp(data, "ABC", 3) == 0, "unquoted spaced hex is accepted");
    reply(rx, "+TEST: RX \"41\"\r\n+TEST: RX \"42\"\r\n");
    check(rx.droppedPackets() == 1, "unread packet overflow is counted");
    check(rx.receive(data, length) && length == 1 && data[0] == 'A', "unread packet is not overwritten");
    check(rx.transmit(data, length), "a receiving modem can subsequently transmit");
    check(strcmp(rx.command(), "AT+TEST=STOP\r\n") == 0, "receive mode is stopped before switching to TX");
  }
  const std::vector<std::string> invalidPackets = {"+TEST: RX \"A\"\r\n", "+TEST: RX \"GG\"\r\n", "+TEST: LEN:2\r\n+TEST: RX \"41\"\r\n", "+TEST: RX \"" + std::string((LoRaE5::kMaxPayloadLength + 1) * 2, 'A') + "\"\r\n"};
  for (const std::string &invalid: invalidPackets) {
    LoRaE5 rx;
    configure(rx);
    reply(rx, invalid);
    check(rx.error() != nullptr, "invalid RX text fails explicitly instead of displaying partial data");
  }
  {
    LoRaE5 modem;
    modem.begin(0xFFFFF000);
    modem.update(0xFFFFF000 + 2000);
    modem.commandSent(0xFFFFFF00);
    modem.update(0xFFFFFF00U + LoRaE5::kCommandTimeoutMs);
    check(modem.error() != nullptr && modem.command() == nullptr, "AT timeout handles millis wraparound and does not retry silently");
  }
  {
    LoRaE5 tx;
    configure(tx);
    const uint8_t value = 'X';
    check(tx.transmit(&value, 1), "TX begins for timeout test");
    expectCommand(tx, "AT+TEST=STOP\r\n", "+TEST: STOP\r\n", 9900);
    tx.commandSent(10000);
    tx.update(14999);
    check(tx.error() == nullptr, "TX timeout does not expire early");
    tx.update(15000);
    check(tx.error() != nullptr && tx.completedTransmissions() == 0, "missing TX DONE is not claimed as success");
  }
  for (const char *response: {"+TEST: ERROR(-12)\r\n", "+INFO: Input timeout\r\n"}) {
    LoRaE5 modem;
    configure(modem);
    reply(modem, response);
    check(modem.error() != nullptr, "module-reported errors are explicit");
  }
  for (const char *response: {"+TEST: LEN:1BAD\r\n", "+TEST: LEN:99999999999999999999999\r\n"}) {
    LoRaE5 modem;
    configure(modem);
    reply(modem, response);
    check(modem.error() != nullptr, "malformed and overflowing lengths cannot become accepted metadata");
  }
  {
    LoRaE5 modem;
    modem.begin(0);
    modem.update(2000);
    expectCommand(modem, "AT\r\n", "+AT: OK\r\n");
    expectCommand(modem, "AT+MODE=TEST\r\n", "+MODE: TEST\r\n");
    expectCommand(modem, "AT+TEST=STOP\r\n", "+TEST: STOP\r\n");
    modem.commandSent(2500);
    reply(modem, "+TEST: RFCFG F:868300000, SF7, BW125K, TXPR:12, RXPR:15, POW:10dBm, CRC:ON, IQ:OFF, NET:OFF\r\n");
    check(modem.error() != nullptr && !modem.ready(), "incorrect radio configuration must block transmission");
  }
  {
    LoRaE5 modem;
    configure(modem);
    reply(modem, std::string(300, 'X'));
    check(modem.error() != nullptr, "oversized AT lines are rejected");
  }
  std::cout << "PASS: E5 configuration, strict replies, hex framing, receive-only display source, failures and timeouts\n";
}

static void testFrames() {
  for (LoRaFrame::Kind kind: {LoRaFrame::Kind::Request, LoRaFrame::Kind::Reply}) {
    for (size_t size = 1; size <= SerialRadioInput::kMaxPayloadLength; size++) {
      std::vector<uint8_t> payload(size, 'X');
      uint8_t frame[LoRaFrame::kMaxSize];
      uint8_t length = 0;
      check(LoRaFrame::encode(kind, 0x12345678, payload.data(), size, frame, length), "round-trip frame encodes every user payload length");
      const uint8_t header[] = {'M', 'X', 1, static_cast<uint8_t>(kind), 0x78, 0x56, 0x34, 0x12};
      check(memcmp(frame, header, sizeof(header)) == 0 && length == size + 8, "on-air header has the defined version, type and little-endian transaction ID");
      LoRaFrame::Message message;
      check(LoRaFrame::decode(frame, length, message), "round-trip frame decodes");
      check(message.id == 0x12345678 && message.kind == kind && message.length == size && memcmp(message.payload, payload.data(), size) == 0, "frame retains the exact ID, direction and payload");
      frame[2] = 2;
      check(!LoRaFrame::decode(frame, length, message), "unknown frame versions are rejected");
    }
  }
  uint8_t frame[LoRaFrame::kMaxSize] = {};
  uint8_t length = 0;
  const uint8_t payload = 'A';
  check(!LoRaFrame::encode(LoRaFrame::Kind::Request, 0, &payload, 1, frame, length), "zero transaction IDs are rejected");
  check(!LoRaFrame::encode(LoRaFrame::Kind::Request, 1, &payload, 78, frame, length), "framing does not accept overlength user data");
  check(LoRaFrame::encode(LoRaFrame::Kind::Reply, 1, &payload, 1, frame, length), "one-byte reply frame");
  LoRaFrame::Message message;
  check(!LoRaFrame::decode(frame, 8, message), "header without a payload is rejected");
  check(!LoRaFrame::decode(frame, LoRaFrame::kMaxSize + 1, message), "oversize frames are rejected before reading payload");
  frame[3] = 7;
  check(!LoRaFrame::decode(frame, length, message), "unknown request/reply types are rejected");
  std::cout << "PASS: correlated request/reply headers, IDs and all 77 user payload lengths\n";
}

int main() {
  testUart();
  testModem();
  testFrames();
}
