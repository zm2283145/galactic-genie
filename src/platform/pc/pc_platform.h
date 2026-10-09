// SPDX-License-Identifier: GPL-3.0-or-later
// PC build: what the shared main loop (main_vita.cpp) reads beyond the
// Vita-style buttons: the mouse in game coordinates and a few keys.
#pragma once

namespace swgb_pc {

struct Mouse {
    float x = 0, y = 0;            // game-screen coordinates
    bool left = false, right = false;
    bool leftPressed = false, leftReleased = false, rightPressed = false;
    int wheel = 0;                 // notches this frame (+ away from the user)
    bool inside = false;           // over the window
};

// The state gathered by the last sceCtrlPeekBufferPositive call.
const Mouse &mouse();
// Keys pressed this frame (SDL scancodes).
bool keyPressed(int scancode);
bool keyDown(int scancode);

} // namespace swgb_pc
