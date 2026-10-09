// SPDX-License-Identifier: GPL-3.0-or-later
// PC build: the few Vita SDK types and calls the game uses, implemented on
// SDL2 (src/platform/pc/pc_shim.cpp). Not a general Vita SDK replacement.
#pragma once

#include <cstddef>
#include <cstdint>

typedef int SceUID;
typedef unsigned int SceSize;
typedef unsigned int SceUInt;
typedef int64_t SceOff;
typedef uint64_t SceUInt64;
typedef int SceInt;
typedef int SceBool;
#define SCE_TRUE 1
#define SCE_FALSE 0
