// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../types_shim.h"

struct SceKernelFreeMemorySizeInfo {
    SceSize size;
    SceSize size_user;
    SceSize size_cdram;
    SceSize size_phycont;
};
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info);
