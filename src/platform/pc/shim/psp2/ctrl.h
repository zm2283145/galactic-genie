// SPDX-License-Identifier: GPL-3.0-or-later
// PC shim: the keyboard and a game controller read as the Vita's buttons.
#pragma once
#include "types_shim.h"

enum {
    SCE_CTRL_SELECT = 0x00000001,
    SCE_CTRL_START = 0x00000008,
    SCE_CTRL_UP = 0x00000010,
    SCE_CTRL_RIGHT = 0x00000020,
    SCE_CTRL_DOWN = 0x00000040,
    SCE_CTRL_LEFT = 0x00000080,
    SCE_CTRL_LTRIGGER = 0x00000100,
    SCE_CTRL_RTRIGGER = 0x00000200,
    SCE_CTRL_TRIANGLE = 0x00001000,
    SCE_CTRL_CIRCLE = 0x00002000,
    SCE_CTRL_CROSS = 0x00004000,
    SCE_CTRL_SQUARE = 0x00008000,
};
enum { SCE_CTRL_MODE_DIGITAL = 0, SCE_CTRL_MODE_ANALOG = 1, SCE_CTRL_MODE_ANALOG_WIDE = 2 };

struct SceCtrlData {
    uint64_t timeStamp;
    unsigned int buttons;
    unsigned char lx, ly, rx, ry;
    uint8_t reserved[16];
};

int sceCtrlSetSamplingMode(int mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *data, int count);
