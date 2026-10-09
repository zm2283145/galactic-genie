// SPDX-License-Identifier: GPL-3.0-or-later
// PC build: no start-up movies (the Vita plays H.264 conversions through
// SceAvPlayer); the caller shows its fallback.
#include "../vita/vita_video.h"

namespace swgb {
bool playMovie(const std::string &, Renderer &, int, int, const std::function<void(const std::string &)> &) {
    return false;
}
} // namespace swgb
