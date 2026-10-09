// SPDX-License-Identifier: GPL-3.0-or-later
// PC shim: the mouse is not reported as touch (the PC build reads the mouse
// directly, see pc_platform.h); this keeps the touch code compiling.
#pragma once
#include "types_shim.h"

enum { SCE_TOUCH_PORT_FRONT = 0, SCE_TOUCH_PORT_BACK = 1 };
enum { SCE_TOUCH_SAMPLING_STATE_STOP = 0, SCE_TOUCH_SAMPLING_STATE_START = 1 };

struct SceTouchReport {
    uint8_t id, force;
    uint16_t x, y;
    int8_t reserved[8];
    uint16_t info;
};
struct SceTouchData {
    uint64_t timeStamp;
    unsigned int status;
    unsigned int reportNum;
    SceTouchReport report[8];
};

int sceTouchSetSamplingState(int port, int state);
int sceTouchPeek(int port, SceTouchData *data, int count);
