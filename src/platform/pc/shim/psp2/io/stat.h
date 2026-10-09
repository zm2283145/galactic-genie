// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../types_shim.h"

struct SceIoStat {
    SceOff st_size;
    unsigned int st_mode;
};
int sceIoMkdir(const char *path, int mode);
int sceIoGetstat(const char *path, SceIoStat *stat);
