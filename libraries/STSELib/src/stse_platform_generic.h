#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define PLAT_UI8 uint8_t
#define PLAT_UI16 uint16_t
#define PLAT_UI32 uint32_t
#define PLAT_UI64 uint64_t
#define PLAT_I8 int8_t
#define PLAT_I16 int16_t
#define PLAT_I32 int32_t
#define PLAT_PACKED_STRUCT __attribute__((packed))
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
