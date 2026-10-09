#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace NfcProtocol {
  constexpr size_t kFrameBytes = 72;
  constexpr uint8_t kTestBlock = 4;
  enum class Type {
    Ntag213,
    Classic1k,
    Ntag215216
  };

  inline size_t encode(const uint8_t *command, size_t size, uint8_t *frame) {
    if (command == nullptr || size == 0 || size > kFrameBytes - 8) {
      return 0;
    }
    const uint8_t length = static_cast<uint8_t>(size + 1);
    frame[0] = frame[1] = 0;
    frame[2] = 0xFF;
    frame[3] = length;
    frame[4] = static_cast<uint8_t>(-length);
    frame[5] = 0xD4;
    uint8_t sum = 0xD4;
    for (size_t i = 0; i < size; ++i) {
      frame[6 + i] = command[i];
      sum += command[i];
    }
    frame[6 + size] = static_cast<uint8_t>(-sum);
    frame[7 + size] = 0;
    return size + 8;
  }

  inline bool decode(const uint8_t *frame, size_t size, uint8_t command, uint8_t *data, size_t capacity, size_t &length) {
    length = 0;
    if (size < 9 || size > kFrameBytes || frame[0] != 0 || frame[1] != 0 || frame[2] != 255 || frame[3] < 2 || static_cast<uint8_t>(frame[3] + frame[4]) != 0 || size != static_cast<size_t>(frame[3]) + 7 || frame[5] != 0xD5 || frame[6] != static_cast<uint8_t>(command + 1) || frame[size - 1] != 0 ||
      frame[3] - 2U > capacity) {
      return false;
    }
    uint8_t sum = 0;
    for (size_t i = 5; i < size - 1; ++i) {
      sum += frame[i];
    }
    if (sum != 0) {
      return false;
    }
    length = frame[3] - 2;
    if (length) {
      memcpy(data, frame + 7, length);
    }
    return true;
  }

  struct Tag {
    uint8_t uid[10] = {};
    uint8_t uidLength = 0;
    uint8_t sak = 0;
    uint16_t atqa = 0;
    Type type = Type::Classic1k;
    uint8_t storageCode = 0;
    bool classic1k() const {
      return sak == 0x08 && (uidLength == 4 || uidLength == 7);
    }
  };

  class Link {
    public:
    virtual ~Link() {}
    virtual bool exchange(const uint8_t *command, size_t size, uint8_t *response, size_t capacity, size_t &length) = 0;
  };

  class Reader {
    public:
    explicit Reader(Link &transport): link(transport) {}
    bool initialize(uint8_t *version) {
      const uint8_t firmware[] = {0x02};
      size_t length = 0;
      if (!link.exchange(firmware, sizeof(firmware), version, 4, length) || length != 4 || version[0] != 0x32) {
        return false;
      }
      const uint8_t sam[] = {0x14, 1, 0x14, 0};
      const uint8_t retries[] = {0x32, 5, 0xFF, 1, 0};
      uint8_t reply[4];
      return link.exchange(sam, sizeof(sam), reply, sizeof(reply), length) && length == 0 && link.exchange(retries, sizeof(retries), reply, sizeof(reply), length) && length == 0;
    }
    bool scan(Tag &tag, bool &present) {
      tag = {};
      present = false;
      const uint8_t command[] = {0x4A, 1, 0};
      uint8_t reply[40];
      size_t length = 0;
      if (!link.exchange(command, sizeof(command), reply, sizeof(reply), length) || length < 1) {
        return false;
      }
      if (reply[0] == 0) {
        return length == 1;
      }
      if (reply[0] != 1 || length < 6 || reply[1] != 1 || (reply[5] != 4 && reply[5] != 7 && reply[5] != 10) || length < static_cast<size_t>(6 + reply[5])) {
        return false;
      }
      tag.atqa = static_cast<uint16_t>((reply[2] << 8) | reply[3]);
      tag.sak = reply[4];
      tag.uidLength = reply[5];
      memcpy(tag.uid, reply + 6, tag.uidLength);
      present = true;
      return true;
    }
    bool select(const Tag &expected) {
      Tag actual;
      bool present = false;
      return scan(actual, present) && present && actual.sak == expected.sak && actual.uidLength == expected.uidLength && memcmp(actual.uid, expected.uid, expected.uidLength) == 0 && identify(actual, expected.type) && actual.storageCode == expected.storageCode;
    }
    bool identify(Tag &tag, Type type) {
      tag.type = type;
      if (type == Type::Classic1k) {
        return tag.classic1k();
      }
      if (tag.sak != 0 || tag.uidLength != 7) {
        return false;
      }
      uint8_t version[8];
      if (!ntagVersion(version)) {
        return false;
      }
      tag.storageCode = version[6];
      return type == Type::Ntag213 ? version[6] == 0x0F : (version[6] == 0x11 || version[6] == 0x13);
    }
    bool readSelected(const Tag &tag, uint8_t *data) {
      return (tag.type != Type::Classic1k || authenticate(tag)) && read(kTestBlock, data);
    }
    bool writable(const Tag &tag) {
      uint8_t metadata[16];
      if (tag.type == Type::Classic1k) {
        return authenticate(tag) && read(7, metadata) && metadata[6] == 0xFF && metadata[7] == 0x07 && metadata[8] == 0x80;
      }
      uint8_t version[8];
      if (!ntagVersion(version) || !read(2, metadata) || metadata[2] != 0 || metadata[3] != 0) {
        return false;
      }
      const uint8_t lockPage = version[6] == 0x0F ? 40 : (version[6] == 0x11 ? 130 : 226);
      // NTAG READ returns four pages; password readback is masked by the chip and never exposed.
      if (!read(lockPage, metadata)) {
        return false;
      }
      return metadata[0] == 0 && metadata[1] == 0 && metadata[2] == 0 && metadata[7] == 0xFF && (metadata[8] & 0x40) == 0 && (metadata[4] & 0xF0) == 0;
    }
    bool authenticate(const Tag &tag) {
      if (!tag.classic1k()) {
        return false;
      }
      uint8_t command[14] = {0x40, 1, 0x60, kTestBlock, 255, 255, 255, 255, 255, 255};
      memcpy(command + 10, tag.uid + tag.uidLength - 4, 4);
      uint8_t reply[1];
      size_t length = 0;
      return link.exchange(command, sizeof(command), reply, sizeof(reply), length) && length == 1 && reply[0] == 0;
    }
    bool read(uint8_t block, uint8_t *data) {
      const uint8_t command[] = {0x40, 1, 0x30, block};
      uint8_t reply[17];
      size_t length = 0;
      if (!link.exchange(command, sizeof(command), reply, sizeof(reply), length) || length != sizeof(reply) || reply[0] != 0) {
        return false;
      }
      memcpy(data, reply + 1, 16);
      return true;
    }
    bool write(Type type, const uint8_t *data) {
      uint8_t command[20] = {0x40, 1, 0xA0, kTestBlock};
      const size_t size = type == Type::Classic1k ? 16 : 4;
      if (type != Type::Classic1k) {
        command[2] = 0xA2;
      }
      memcpy(command + 4, data, size);
      uint8_t reply[1];
      size_t length = 0;
      return link.exchange(command, size + 4, reply, sizeof(reply), length) && length == 1 && reply[0] == 0;
    }

    private:
    Link &link;
    bool ntagVersion(uint8_t *version) {
      const uint8_t command[] = {0x42, 0x60};
      uint8_t reply[9];
      size_t length = 0;
      const uint8_t prefix[] = {0, 4, 4, 2, 1, 0};
      if (!link.exchange(command, sizeof(command), reply, sizeof(reply), length) || length != 9 || reply[0] != 0 || memcmp(reply + 1, prefix, sizeof(prefix)) != 0 || reply[8] != 3 || (reply[7] != 0x0F && reply[7] != 0x11 && reply[7] != 0x13)) {
        return false;
      }
      memcpy(version, reply + 1, 8);
      return true;
    }
  };

  enum class WriteResult {
    Passed,
    Rejected,
    TestFailedRestored,
    RestoreRequired
  };

  class WriteTest {
    public:
    bool pending() const {
      return saved;
    }
    const Tag &tag() const {
      return originalTag;
    }
    WriteResult run(Reader &reader, const Tag &expected) {
      if (saved || !reader.select(expected) || !reader.writable(expected)) {
        return WriteResult::Rejected;
      }
      uint8_t second[16];
      if (!reader.read(kTestBlock, original) || !reader.read(kTestBlock, second) || memcmp(original, second, 16) != 0) {
        return WriteResult::Rejected;
      }
      originalTag = expected;
      saved = true;
      uint8_t pattern[16];
      memcpy(pattern, original, 16);
      const size_t size = expected.type == Type::Classic1k ? 16 : 4;
      for (size_t i = 0; i < size; ++i) {
        pattern[i] = static_cast<uint8_t>(original[i] ^ (0xA5 + i));
      }
      const bool written = reader.write(expected.type, pattern);
      const bool verified = written && reader.read(kTestBlock, second) && memcmp(pattern, second, 16) == 0;
      if (!restore(reader)) {
        return WriteResult::RestoreRequired;
      }
      return verified ? WriteResult::Passed : WriteResult::TestFailedRestored;
    }
    bool restore(Reader &reader) {
      if (!saved || !reader.select(originalTag) || !reader.writable(originalTag)) {
        return false;
      }
      uint8_t current[16];
      if (!reader.read(kTestBlock, current)) {
        return false;
      }
      if (memcmp(current, original, 16) != 0) {
        const size_t size = originalTag.type == Type::Classic1k ? 16 : 4;
        // Adjacent NTAG pages must remain unchanged; never rewrite them to hide a mismatch.
        if (memcmp(current + size, original + size, 16 - size) != 0 || !reader.write(originalTag.type, original) || !reader.read(kTestBlock, current) || memcmp(current, original, 16) != 0) {
          return false;
        }
      }
      memset(original, 0, sizeof(original));
      saved = false;
      return true;
    }

    private:
    bool saved = false;
    uint8_t original[16] = {};
    Tag originalTag;
  };
}
