// SPDX-License-Identifier: GPL-3.0-or-later
// Full-screen movie playback for the start-up videos (the original's
// xlogo1.avi and xintro.avi, converted once to H.264/AAC MP4 because the
// Vita has no Indeo 5 decoder). Uses the system SceAvPlayer: the hardware
// decodes, the frames are drawn 4:3 centred, the sound goes to its own
// audio port.
#pragma once

#include "../../render/renderer.h"

#include <functional>
#include <string>

namespace swgb {

// Plays path to the end or until START is pressed. Returns false when the
// file can't be opened (the caller shows its fallback).
bool playMovie(const std::string &path, Renderer &renderer, int screenW, int screenH,
               const std::function<void(const std::string &)> &log = {});

} // namespace swgb
