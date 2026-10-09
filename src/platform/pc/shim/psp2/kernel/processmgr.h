// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../types_shim.h"

SceUInt64 sceKernelGetProcessTimeWide();
int sceKernelExitProcess(int status);
int sceKernelDelayThread(SceUInt microseconds);
