#include "stubs/DiagnosticHardware.h"
#include <FATFileSystem.h>
#include <File.h>
#include <errno.h>
#include <fcntl.h>
#include "AppConfig.h"
#include "DiagnosticChecks.h"
#include "OnboardTests.h"
#include <algorithm>
#include <vector>

namespace {
  bool exists = false;
  bool mounted = false;
  unsigned int mounts = 0;
  unsigned int writes = 0;
  unsigned int deletes = 0;
  unsigned int formats = 0;
  std::vector<uint8_t> data;
  std::string failure;
}

FATFileSystem::FATFileSystem(const char *name) {
  check(std::string(name) == "diagfs", "dedicated filesystem mount name");
}

int FATFileSystem::mount(SFlashBlockDevice *, bool force) {
  ++mounts;
  check(force && !mounted, "force a real mount, without formatting or mounting twice");
  mounted = true;
  if (failure == "mount") {
    return -EIO;
  }
  return 0;
}

int FATFileSystem::unmount() {
  check(mounted, "unmount mounted filesystem");
  mounted = false;
  return 0;
}

int FATFileSystem::stat(const char *path, struct stat *) {
  check(mounted && std::string(path) == "MXCTEST.BIN", "only the dedicated test file is inspected");
  if (failure == "stat") {
    return -EIO;
  }
  return exists ? 0 : -ENOENT;
}

int FATFileSystem::remove(const char *path) {
  check(std::string(path) == "MXCTEST.BIN", "only the named test file can be removed");
  ++deletes;
  if (failure == "remove") {
    return -EIO;
  }
  exists = false;
  data.clear();
  return 0;
}

int FATFileSystem::format(SFlashBlockDevice *) {
  ++formats;
  exists = false;
  data.clear();
  return 0;
}

int mbed::File::open(FATFileSystem *, const char *path, int flags) {
  check(mounted && std::string(path) == "MXCTEST.BIN", "only dedicated file is opened");
  position = 0;
  if ((flags & O_CREAT) != 0) {
    check((flags & O_EXCL) != 0 && (flags & O_TRUNC) == 0, "exclusive creation, no truncation");
    if (exists) {
      return -EEXIST;
    }
    exists = true;
    data.clear();
    return 0;
  }
  return exists ? 0 : -ENOENT;
}

int mbed::File::close() {
  return failure == "close" ? -EIO : 0;
}

ssize_t mbed::File::read(void *buffer, size_t length) {
  if (failure == "read") {
    return -EIO;
  }
  const size_t size = std::min(std::min(length, static_cast<size_t>(19)), data.size() - position);
  if (size != 0) {
    memcpy(buffer, data.data() + position, size);
  }
  position += size;
  return static_cast<ssize_t>(size);
}

ssize_t mbed::File::write(const void *buffer, size_t length) {
  ++writes;
  if (failure == "write") {
    return 0;
  }
  const size_t size = std::min(length, static_cast<size_t>(13));
  const uint8_t *bytes = static_cast<const uint8_t *>(buffer);
  data.insert(data.end(), bytes, bytes + size);
  position += size;
  return static_cast<ssize_t>(size);
}

int mbed::File::sync() {
  return failure == "sync" ? -EIO : 0;
}

int main() {
  FileSystemTests::run();
  if (!AppConfig::kFileSystemEnabled) {
    FileSystemTests::verify();
    FileSystemTests::cleanup();
    FileSystemTests::format();
    check(mounts == 0 && writes == 0 && deletes == 0 && formats == 0, "inactive filesystem mode has no storage side effects");
    std::cout << "PASS: filesystem mode exclusion\n";
    return 0;
  }
  check(exists && data.size() == 4096 && mounts == 2 && !mounted, "exclusive write, sync, unmount and remounted readback");
  for (size_t index = 0; index < data.size(); ++index) {
    check(data[index] == DiagnosticChecks::fileByte(index), "exact persistent record shape");
  }
  const auto original = data;
  const unsigned int previousWrites = writes;
  FileSystemTests::run();
  check(writes == previousWrites && data == original, "pre-existing record is never rewritten");
  data[2000] ^= 1;
  FileSystemTests::run();
  FileSystemTests::cleanup();
  check(writes == previousWrites && deletes == 0 && exists, "corrupt/colliding file is neither overwritten nor deleted");
  data = original;
  data.push_back(0);
  FileSystemTests::verify();
  check(Serial.output.find("exactly 4096 bytes") != std::string::npos, "trailing data rejected");
  data = original;
  data.resize(100);
  FileSystemTests::verify();
  check(Serial.output.find("short/failed read") != std::string::npos, "truncated file rejected");
  data = original;
  for (const char *stage: {"read", "close", "stat", "mount"}) {
    failure = stage;
    Serial.output.clear();
    FileSystemTests::run();
    check(Serial.output.find("FAIL") != std::string::npos && Serial.output.find("File verify OK") == std::string::npos, "I/O failures are explicit");
    check(data == original && writes == previousWrites && deletes == 0, "I/O errors never trigger destructive recovery");
  }
  failure = "remove";
  FileSystemTests::cleanup();
  check(exists && data == original, "failed cleanup retains data");
  failure.clear();
  FileSystemTests::cleanup();
  check(!exists && !mounted && Serial.output.find("absence confirmed after remount") != std::string::npos, "cleanup checks absence after remount");
  for (const char *stage: {"write", "sync"}) {
    failure = stage;
    exists = false;
    data.clear();
    Serial.output.clear();
    FileSystemTests::run();
    check(exists && Serial.output.find("FAIL") != std::string::npos, "partial/unsynced record retained, not hidden by deletion");
  }
  check(formats == 0, "normal operations and I/O failures never format automatically");
  failure.clear();
  FileSystemTests::format();
  check(formats == 1 && exists && data == original && !mounted, "explicit formatting initializes then verifies a new test record");
  check(!Screen.invalidWrite, "filesystem status rows fit OLED");
  std::cout << "PASS: exact 4096-byte record, partial I/O, remount/persistence, exclusive creation, corruption, cleanup and non-destructive failure handling\n";
}
