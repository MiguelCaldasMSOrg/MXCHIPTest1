#pragma once

#include <string.h>
#include "SerialRadioInput.h"

namespace LoRaFrame {
  constexpr size_t kHeaderSize = 8;
  constexpr size_t kMaxSize = kHeaderSize + SerialRadioInput::kMaxPayloadLength;

  enum class Kind: uint8_t {
    Request = 1,
    Reply = 2
  };

  struct Message {
    Kind kind;
    uint32_t id;
    uint8_t length;
    uint8_t payload[SerialRadioInput::kMaxPayloadLength];
  };

  inline bool encode(Kind kind, uint32_t id, const uint8_t *payload, size_t length, uint8_t (&frame)[kMaxSize], uint8_t &frameLength) {
    frameLength = 0;
    if ((kind != Kind::Request && kind != Kind::Reply) || id == 0 || payload == nullptr || length == 0 || length > SerialRadioInput::kMaxPayloadLength) {
      return false;
    }
    frame[0] = 'M';
    frame[1] = 'X';
    frame[2] = 1;
    frame[3] = static_cast<uint8_t>(kind);
    for (unsigned int byte = 0; byte < 4; byte++) {
      frame[4 + byte] = static_cast<uint8_t>(id >> (byte * 8));
    }
    memcpy(frame + kHeaderSize, payload, length);
    frameLength = static_cast<uint8_t>(kHeaderSize + length);
    return true;
  }

  inline bool decode(const uint8_t *frame, size_t length, Message &message) {
    if (frame == nullptr || length <= kHeaderSize || length > kMaxSize || frame[0] != 'M' || frame[1] != 'X' || frame[2] != 1 || (frame[3] != static_cast<uint8_t>(Kind::Request) && frame[3] != static_cast<uint8_t>(Kind::Reply))) {
      return false;
    }
    uint32_t id = 0;
    for (unsigned int byte = 0; byte < 4; byte++) {
      id |= static_cast<uint32_t>(frame[4 + byte]) << (byte * 8);
    }
    if (id == 0) {
      return false;
    }
    message.kind = static_cast<Kind>(frame[3]);
    message.id = id;
    message.length = static_cast<uint8_t>(length - kHeaderSize);
    memcpy(message.payload, frame + kHeaderSize, message.length);
    return true;
  }
}
