// SPDX-License-Identifier: GPL-3.0-or-later
// PC shim: kernel threads, mutexes and semaphores on the C++ library.
#pragma once
#include "../types_shim.h"

typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);
enum {
    SCE_KERNEL_CPU_MASK_USER_0 = 0x10000,
    SCE_KERNEL_CPU_MASK_USER_1 = 0x20000,
    SCE_KERNEL_CPU_MASK_USER_2 = 0x40000,
    SCE_KERNEL_MUTEX_ATTR_RECURSIVE = 0x02,
};

SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority, SceSize stack,
                             SceUInt attr, int cpuMask, const void *option);
int sceKernelStartThread(SceUID thread, SceSize argSize, void *argp);
int sceKernelWaitThreadEnd(SceUID thread, int *status, SceUInt *timeout);
int sceKernelDeleteThread(SceUID thread);
SceUID sceKernelGetThreadId();
int sceKernelGetThreadCpuAffinityMask(SceUID thread);
int sceKernelChangeThreadCpuAffinityMask(SceUID thread, int mask);

SceUID sceKernelCreateMutex(const char *name, SceUInt attr, int initCount, const void *option);
int sceKernelLockMutex(SceUID mutex, int count, SceUInt *timeout);
int sceKernelUnlockMutex(SceUID mutex, int count);
int sceKernelDeleteMutex(SceUID mutex);

SceUID sceKernelCreateSema(const char *name, SceUInt attr, int initCount, int maxCount, const void *option);
int sceKernelWaitSema(SceUID sema, int need, SceUInt *timeout);
int sceKernelSignalSema(SceUID sema, int count);
int sceKernelDeleteSema(SceUID sema);
