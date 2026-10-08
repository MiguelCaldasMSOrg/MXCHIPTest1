#pragma once

#include <stddef.h>
#include <sys/types.h>
#include "FATFileSystem.h"

namespace mbed {
  class File {
    public:
    int open(FATFileSystem *filesystem, const char *path, int flags);
    int close();
    ssize_t read(void *buffer, size_t length);
    ssize_t write(const void *buffer, size_t length);
    int sync();

    private:
    size_t position = 0;
  };
}
