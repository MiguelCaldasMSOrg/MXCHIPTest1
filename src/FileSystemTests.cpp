#include <Arduino.h>
#include <FATFileSystem.h>
#include <File.h>
#include <SFlashBlockDevice.h>
#include <errno.h>
#include <fcntl.h>
#include "AppConfig.h"
#include "DiagnosticChecks.h"
#include "OnboardTests.h"

namespace {
  constexpr const char *kFileName = "MXCTEST.BIN";
  bool mounted = false;
  bool createdThisBoot = false;

  SFlashBlockDevice &device() {
    static SFlashBlockDevice value;
    return value;
  }

  FATFileSystem &filesystem() {
    static FATFileSystem value("diagfs");
    return value;
  }

  void fail(const char *stage, int code) {
    Screen.print(1, "Filesystem FAIL");
    Screen.print(2, "See USB serial");
    Serial.print(F("Filesystem FAIL: "));
    Serial.print(stage);
    Serial.print(F(" ("));
    Serial.print(code);
    Serial.println(F("). No automatic format or deletion was attempted."));
  }

  bool mount() {
    if (mounted) {
      fail("previous unmount failed; reset before retrying", -EBUSY);
      return false;
    }
    const int result = filesystem().mount(&device(), true);
    if (result != 0) {
      fail("mount; an unformatted volume requires separate, explicitly approved formatting", result);
      // Core 2.0.0 reserves its sole drive slot even when f_mount rejects the volume.
      const int released = filesystem().unmount();
      if (released != 0 && released != -EINVAL) {
        mounted = true;
        fail("releasing a failed mount; reset required", released);
      }
      return false;
    }
    mounted = true;
    return true;
  }

  bool unmount() {
    const int result = filesystem().unmount();
    if (result != 0) {
      fail("unmount", result);
      return false;
    }
    mounted = false;
    return true;
  }

  bool close(mbed::File &file) {
    const int result = file.close();
    if (result != 0) {
      fail("close", result);
      return false;
    }
    return true;
  }

  bool verifyMounted() {
    mbed::File file;
    const int result = file.open(&filesystem(), kFileName, O_RDONLY);
    if (result != 0) {
      fail("open existing MXCTEST.BIN", result);
      return false;
    }
    bool valid = true;
    uint8_t bytes[128];
    size_t offset = 0;
    uint32_t hash = 2166136261UL;
    while (valid && offset < DiagnosticChecks::kFileBytes) {
      const size_t remaining = DiagnosticChecks::kFileBytes - offset;
      const size_t requested = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
      const ssize_t count = file.read(bytes, requested);
      if (count <= 0 || static_cast<size_t>(count) > requested) {
        fail("short/failed read; existing file left untouched", static_cast<int>(count));
        valid = false;
        break;
      }
      for (ssize_t index = 0; index < count; ++index) {
        if (bytes[index] != DiagnosticChecks::fileByte(offset + static_cast<size_t>(index))) {
          fail("signature/pattern mismatch; existing file left untouched", static_cast<int>(offset + index));
          valid = false;
          break;
        }
        hash = (hash ^ bytes[index]) * 16777619UL;
      }
      offset += static_cast<size_t>(count);
    }
    if (valid) {
      const ssize_t extra = file.read(bytes, 1);
      if (extra != 0) {
        fail("file must contain exactly 4096 bytes; existing file left untouched", static_cast<int>(extra));
        valid = false;
      }
    }
    const bool closed = close(file);
    if (valid && closed) {
      char text[96];
      snprintf(text, sizeof(text), "PASS: MXCTEST.BIN has all 4096 expected bytes; FNV-1a=%08lX.", static_cast<unsigned long>(hash));
      Serial.println(text);
      return true;
    }
    return false;
  }

  bool createMounted() {
    mbed::File file;
    const int result = file.open(&filesystem(), kFileName, O_WRONLY | O_CREAT | O_EXCL);
    if (result != 0) {
      fail("exclusive create; will not overwrite MXCTEST.BIN", result);
      return false;
    }
    bool valid = true;
    uint8_t bytes[128];
    size_t offset = 0;
    while (valid && offset < DiagnosticChecks::kFileBytes) {
      for (size_t index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = DiagnosticChecks::fileByte(offset + index);
      }
      size_t written = 0;
      while (written < sizeof(bytes)) {
        const ssize_t count = file.write(bytes + written, sizeof(bytes) - written);
        if (count <= 0 || static_cast<size_t>(count) > sizeof(bytes) - written) {
          fail("write; partial test file retained for inspection", static_cast<int>(count));
          valid = false;
          break;
        }
        written += static_cast<size_t>(count);
      }
      offset += written;
    }
    const int synced = file.sync();
    if (synced != 0) {
      fail("sync", synced);
      valid = false;
    }
    const bool closed = close(file);
    createdThisBoot = valid && closed;
    return createdThisBoot;
  }

  void title() {
    Screen.print(0, "QSPI filesystem");
    Screen.print(1, "Checking file");
    Screen.print(2, "MXCTEST.BIN");
    Screen.print(3, "A:run B:verify");
  }
}

namespace FileSystemTests {
  void format() {
    if (!AppConfig::kFileSystemEnabled) {
      return;
    }
    if (mounted) {
      fail("cannot format after an unmount failure; reset required", -EBUSY);
      return;
    }
    Screen.print(1, "Formatting QSPI");
    Serial.println(F("Explicit FORMAT FILESYS confirmed: erasing ONLY the SDK QSPI filesystem partition. Firmware and STSAFE are not touched."));
    const int initialized = device().init();
    if (initialized != 0) {
      fail("filesystem block-device initialization", initialized);
      return;
    }
    const int result = FATFileSystem::format(&device());
    if (result != 0) {
      fail("explicit filesystem format", result);
      return;
    }
    createdThisBoot = false;
    Serial.println(F("Filesystem formatted successfully; running the file integrity test."));
    run();
  }

  void run() {
    if (!AppConfig::kFileSystemEnabled) {
      return;
    }
    title();
    if (!mount()) {
      return;
    }
    struct stat info = {};
    const int found = filesystem().stat(kFileName, &info);
    bool valid = false;
    if (found == 0) {
      valid = verifyMounted();
    } else if (found == -ENOENT) {
      valid = createMounted();
    } else {
      fail("stat; not treating an I/O error as an empty volume", found);
    }
    if (!unmount() || !valid) {
      return;
    }
    // Reopen after unmount so a successful write is not just a cached read.
    verify();
  }

  void verify() {
    if (!AppConfig::kFileSystemEnabled) {
      return;
    }
    title();
    if (!mount()) {
      return;
    }
    const bool valid = verifyMounted();
    const bool unmounted = unmount();
    if (!valid || !unmounted) {
      return;
    }
    Screen.print(1, "File verify OK");
    Screen.print(2, createdThisBoot ? "Reset: retention" : "Retained file OK");
    Serial.println(createdThisBoot ? F("Readback/remount passed. Reset or power-cycle now to check persistence; file remains on QSPI.") : F("PASS: pre-existing test file verified after startup; no rewrite was needed."));
  }

  void cleanup() {
    if (!AppConfig::kFileSystemEnabled) {
      return;
    }
    if (!mount()) {
      return;
    }
    bool valid = verifyMounted();
    if (valid) {
      const int result = filesystem().remove(kFileName);
      if (result != 0) {
        fail("remove verified test file", result);
        valid = false;
      }
    }
    if (!unmount() || !valid || !mount()) {
      return;
    }
    struct stat info = {};
    const int found = filesystem().stat(kFileName, &info);
    const bool unmounted = unmount();
    if (found != -ENOENT) {
      fail("remounted cleanup verification", found);
      return;
    }
    if (unmounted) {
      createdThisBoot = false;
      Screen.print(1, "Test file gone");
      Screen.print(2, "Other files kept");
      Serial.println(F("PASS: removed only the verified MXCTEST.BIN; absence confirmed after remount."));
    }
  }
}
