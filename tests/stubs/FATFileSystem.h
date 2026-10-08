#pragma once

#include <sys/stat.h>
#include "SFlashBlockDevice.h"

class FATFileSystem {
  public:
  explicit FATFileSystem(const char *name);
  int mount(SFlashBlockDevice *device, bool force);
  int unmount();
  int stat(const char *path, struct stat *info);
  int remove(const char *path);
  static int format(SFlashBlockDevice *device);
};
