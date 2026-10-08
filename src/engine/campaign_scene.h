// SPDX-License-Identifier: GPL-3.0-or-later
// Campaign mission scenes: the original "Multimedia Screen" (battlegrounds_x1
// 0x524350 / 0x525240 / 0x525800) that plays before (and after) a campaign
// mission. A scene is a script in Campaign/Media/xc<campaign>s<mission>_beg.mm
// (_end.mm after a victory) of timed items over the campaign's background
// (media/xbackgrd<campaign>.slp with its .pal):
//
//   ID TYPE X Y WIDTH HEIGHT STARTTIME DISPLAYTIME FRAME STR R G B
//
//   TEXT  word-wrapped text in the box; STR is a language string id (or the
//         text itself), FRAME the font, R G B the colour.
//   PICT  frame FRAME of xc<c>s<m>_beg.slp drawn at X Y (its own size).
//   SND   campaign voice file STR, started at STARTTIME.
//   WND   the scene length (DISPLAYTIME); the scene ends after it.
//
// Times are milliseconds from the start of the scene. Text and pictures fade
// in over 400 ms from STARTTIME and fade out over 400 ms after
// STARTTIME + DISPLAYTIME (0x525240 with the 400 ms blend at +0xd14).
#pragma once

#include "assets.h"

#include <functional>
#include <string>
#include <vector>

namespace swgb {

struct MediaItem {
    enum Type { Text = 0, Picture = 1, Sound = 2, Window = 3, Custom = 4, Animation = 5 };
    int id = 0;
    int type = -1;
    int x = 0, y = 0, width = 0, height = 0;
    int start = 0, display = 0;
    int frame = 0;
    std::string text;
    uint8_t red = 255, green = 255, blue = 255;
};

// Parses a .mm script. windowMs is the WND length, or -1 when there is none.
bool parseMediaScript(const std::string &script, std::vector<MediaItem> &items, int &windowMs);

class CampaignScene {
public:
    // campaign: the campaign's number (1-5, 8); mission: 1-based.
    bool load(Assets &assets, const std::string &mediaDir, int campaign, int mission,
              bool beginning, std::string *err = nullptr);
    void release(Assets &assets);
    bool loaded() const { return loaded_; }
    void restart();
    void advance(float seconds);
    // Voice files whose start time has passed since the last call.
    std::vector<std::string> takeDueSounds();
    // Every voice file the scene plays (for prefetching).
    std::vector<std::string> soundNames() const;
    bool finished() const;
    float elapsedMs() const { return elapsedMs_; }
    // Draws the scene into an 800x600 logical screen scaled to screenW x screenH.
    void render(Renderer &renderer, int screenW, int screenH,
                const std::function<std::string(int)> &strings) const;

private:
    bool loaded_ = false;
    std::vector<MediaItem> items_;
    std::vector<const SpriteFrame *> pictures_; // per item (pictures only)
    std::vector<bool> soundStarted_;
    const SpriteFrame *background_ = nullptr;
    int windowMs_ = -1;
    float elapsedMs_ = 0.0f;
};

} // namespace swgb
