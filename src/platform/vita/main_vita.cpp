// SPDX-License-Identifier: GPL-3.0-or-later
// PS Vita entry point: vitaGL init, input, main loop.
#include "../../engine/assets.h"
#include "../../engine/game.h"
#include "../../render/gl_renderer.h"

#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>

// Leave enough of the 256 MiB game partition for the executable, runtime
// modules and allocations made before main(). A 256 MiB newlib heap exhausts
// the partition during vitaGL's static initialization.
int _newlib_heap_size_user = 192 * 1024 * 1024;

namespace {

const char *kRoot = "ux0:data/swgb";
const char *kDataDir = "ux0:data/swgb/Data";
const int kScreenW = 960, kScreenH = 544;

FILE *g_log = nullptr;

void logf(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

float axis(uint8_t v) {
    float f = (v - 128) / 127.0f;
    const float dead = 0.25f;
    if (f > -dead && f < dead) return 0;
    return f > 0 ? (f - dead) / (1 - dead) : (f + dead) / (1 - dead);
}

// Solid-colour screen used for fatal errors (no font renderer yet).
void errorScreen(const std::string &msg) {
    logf("FATAL: %s", msg.c_str());
    SceCtrlData pad{};
    for (;;) {
        glClearColor(0.5f, 0.05f, 0.05f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;
    }
}

} // namespace

int main() {
    sceIoMkdir(kRoot, 0777);
    g_log = fopen("ux0:data/swgb/swgb.log", "w");
    logf("swgb-vita starting");

    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    GLboolean fallback = vglInitExtended(0, kScreenW, kScreenH, 0x20000, SCE_GXM_MULTISAMPLE_NONE);
    logf("vglInitExtended done (resolution fallback=%d)", (int)fallback);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    {
        swgb::GlRenderer renderer;
        swgb::Assets assets(&renderer);
        assets.setLogger([](const std::string &s) { logf("%s", s.c_str()); });

        // Loading screen: dark blue while the dat is parsed.
        glClearColor(0.02f, 0.03f, 0.08f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);

        std::string err;
        uint64_t t0 = sceKernelGetProcessTimeWide();
        if (!assets.init(kDataDir, &err)) {
            errorScreen(err + " (copy the game's Data folder to ux0:data/swgb/Data)");
            sceKernelExitProcess(0);
            return 0;
        }
        logf("assets loaded in %llu ms", (unsigned long long)((sceKernelGetProcessTimeWide() - t0) / 1000));

        swgb::Game game(assets);
        if (!game.init((uint32_t)sceKernelGetProcessTimeLow(), 64, &err)) {
            errorScreen(err);
            sceKernelExitProcess(0);
            return 0;
        }

        SceCtrlData pad{}, prev{};
        SceTouchData touch{};
        bool touching = false;
        float lastTx = 0, lastTy = 0;
        uint64_t last = sceKernelGetProcessTimeWide();
        uint64_t statT = last;
        int frames = 0;

        for (;;) {
            uint64_t now = sceKernelGetProcessTimeWide();
            float dt = (now - last) / 1e6f;
            last = now;
            if (dt > 0.1f) dt = 0.1f;

            sceCtrlPeekBufferPositive(0, &pad, 1);
            uint32_t pressed = pad.buttons & ~prev.buttons;
            prev = pad;
            if (pad.buttons & SCE_CTRL_START) break;

            swgb::InputState in;
            in.scrollX = axis(pad.lx);
            in.scrollY = axis(pad.ly);
            if (pad.buttons & SCE_CTRL_LEFT) in.scrollX = -1;
            if (pad.buttons & SCE_CTRL_RIGHT) in.scrollX = 1;
            if (pad.buttons & SCE_CTRL_UP) in.scrollY = -1;
            if (pad.buttons & SCE_CTRL_DOWN) in.scrollY = 1;
            if (pressed & SCE_CTRL_RTRIGGER) in.zoomStep = 1;
            if (pressed & SCE_CTRL_LTRIGGER) in.zoomStep = -1;
            if (pressed & SCE_CTRL_SELECT) in.toggleDebug = true;

            sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
            if (touch.reportNum > 0) {
                float tx = touch.report[0].x / 2.0f, ty = touch.report[0].y / 2.0f;
                if (touching) {
                    in.dragX = tx - lastTx;
                    in.dragY = ty - lastTy;
                }
                lastTx = tx;
                lastTy = ty;
                touching = true;
            } else {
                touching = false;
            }

            game.update(dt, in);
            game.render(renderer, kScreenW, kScreenH);
            vglSwapBuffers(GL_FALSE);

            frames++;
            if (now - statT >= 5000000) {
                logf("fps=%.1f draws=%d quads=%d tiles=%d sprites=%d sheets=%u tex=%.1fMB zoom=%.2f",
                     frames * 1e6 / (double)(now - statT), renderer.drawCalls(), renderer.quads(),
                     game.stats().tiles, game.stats().sprites, (unsigned)assets.sheetCount(),
                     assets.textureBytes() / 1048576.0, game.zoom());
                frames = 0;
                statT = now;
            }
        }
    }

    logf("exit");
    if (g_log) fclose(g_log);
    sceKernelExitProcess(0);
    return 0;
}
