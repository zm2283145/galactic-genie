// SPDX-License-Identifier: GPL-3.0-or-later
// PC shim: an audio port is an SDL audio device fed by blocking writes.
#pragma once
#include "types_shim.h"

enum { SCE_AUDIO_OUT_PORT_TYPE_MAIN = 0, SCE_AUDIO_OUT_PORT_TYPE_BGM = 1, SCE_AUDIO_OUT_PORT_TYPE_VOICE = 2 };
enum { SCE_AUDIO_OUT_MODE_MONO = 0, SCE_AUDIO_OUT_MODE_STEREO = 1 };

int sceAudioOutOpenPort(int type, int grain, int frequency, int mode);
int sceAudioOutOutput(int port, const void *buffer);
int sceAudioOutReleasePort(int port);
