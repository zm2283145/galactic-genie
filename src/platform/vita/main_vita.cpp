// SPDX-License-Identifier: GPL-3.0-or-later
// PS Vita entry point: vitaGL init, input, main loop.
#include "../../core/cpx.h"
#include "../../core/scenario.h"
#include "../../engine/assets.h"
#include "../../engine/frontend.h"
#include "../../engine/game.h"
#include "../../render/gl_renderer.h"
#include "vita_audio.h"

#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_set>

// Leave enough of the 256 MiB game partition for the executable, runtime
// modules and allocations made before main(). A 256 MiB newlib heap exhausts
// the partition during vitaGL's static initialization.
int _newlib_heap_size_user = 192 * 1024 * 1024;

namespace {

const char *kRoot = "ux0:data/swgb";
const char *kDataDir = "ux0:data/swgb/Data";
const char *kCampaignPath = "ux0:data/swgb/Campaign/xcam3.cpx";
const char *kScenarioSoundDir = "ux0:data/swgb/Sound/Scenario";
const char *kMusicDir = "ux0:data/swgb/Music";
const char *kTerrainSoundDir = "ux0:data/swgb/Sound/Terrain";
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
    logf("vitaGL memory total vram/ram/phy/all=%.1f/%.1f/%.1f/%.1fMB free all=%.1fMB",
         vglMemTotal(VGL_MEM_VRAM) / 1048576.0, vglMemTotal(VGL_MEM_RAM) / 1048576.0,
         vglMemTotal(VGL_MEM_PHYCONT) / 1048576.0, vglMemTotal(VGL_MEM_ALL) / 1048576.0,
         vglMemFree(VGL_MEM_ALL) / 1048576.0);

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
        swgb::Frontend frontend;
        game.setLogger([](const std::string &s) { logf("%s", s.c_str()); });
        swgb::VitaAudio audio(
            kScenarioSoundDir, kMusicDir, kTerrainSoundDir);
        audio.setLogger([](const std::string &s) { logf("audio: %s", s.c_str()); });
        std::string audioError;
        if (!audio.start(&audioError)) logf("audio disabled: %s", audioError.c_str());
        game.setSoundPlayer([&audio](const std::string &name) { return audio.play(name); });
        game.setAmbientSoundPlayer(
            [&audio](const std::string &name) {
                return audio.playAmbient(name);
            });
        game.setInterfaceSoundPlayer([&](int resourceId) {
            std::vector<uint8_t> data;
            if (!assets.readSoundResource(resourceId, data)) {
                logf("interface sound %d not found", resourceId);
                return;
            }
            audio.playEffect(resourceId, data);
        });
        uint32_t unitSoundChoice = 0;
        game.setUnitSoundPlayer([&](int soundId, int civilization) {
            std::vector<uint8_t> data;
            int resourceId = -1;
            std::string fileName;
            if (!assets.readSound(soundId, civilization, unitSoundChoice++, data,
                                  &resourceId, &fileName)) {
                logf("unit sound %d not found", soundId);
                return;
            }
            if (!audio.playEffect(resourceId, data))
                logf("unit sound %s (%d) could not play", fileName.c_str(), resourceId);
        });
        game.setOiiaSoundPlayer(
            [&audio]() {
                audio.playOiiaEffect();
            });
        bool campaignMatch = false;
        swgb::Scenario campaignScenario;
        bool campaignLoaded = false;

        const auto loadCampaign = [&]() {
            if (campaignLoaded) return true;
            auto campaign = swgb::CpxArchive::open(
                kCampaignPath, &err);
            if (!campaign) {
                err +=
                    " (copy XCAM3.CPX to ux0:data/swgb/Campaign)";
                return false;
            }
            std::vector<uint8_t> scx;
            if (!campaign->read(1, scx, &err) ||
                !campaignScenario.load(scx, &err))
                return false;
            campaignLoaded = true;
            logf(
                "scenario %s loaded: %ux%u, %u units",
                campaignScenario.originalFilename.c_str(),
                (unsigned)campaignScenario.map.width,
                (unsigned)campaignScenario.map.height,
                (unsigned)campaignScenario.units.size());
            return true;
        };
        const auto startSkirmish = [&]() {
            audio.resetSession();
            const swgb::SkirmishSettings &settings =
                frontend.settings();
            if (!game.initSkirmish(settings, &err))
                return false;
            const char *personality =
                settings.personality ==
                        swgb::AiPersonality::Classic
                    ? "Computer Classic.per"
                    : "Computer Expanded.per";
            const std::string aiPath =
                std::string("ux0:data/swgb/AI/") +
                personality;
            static constexpr const char *difficulties[] = {
                "DIFFICULTY-HARDEST",
                "DIFFICULTY-HARD",
                "DIFFICULTY-MODERATE",
                "DIFFICULTY-EASY",
                "DIFFICULTY-EASIEST",
            };
            std::unordered_set<std::string> defines{
                difficulties[std::max(
                    0, std::min(4, settings.difficulty))],
                "POPULATION-CAP-" +
                    std::to_string(settings.populationCap),
            };
            if (settings.victory ==
                swgb::SkirmishVictory::Conquest)
                defines.insert("VICTORY-CONQUEST");
            if (settings.mapStyle ==
                    swgb::SkirmishMapStyle::Archipelago ||
                settings.mapStyle ==
                    swgb::SkirmishMapStyle::CompactIslands)
                defines.insert(
                    settings.allied
                        ? "TEAM-LAND-SATELLITES-MAP"
                        : "LAND-SATELLITES-MAP");
            if (!game.loadAiScript(
                    2, aiPath, defines, &err)) {
                err =
                    "AI personality could not be loaded: " +
                    err +
                    " (copy the original AI folder)";
                return false;
            }
            campaignMatch = false;
            return true;
        };
        const auto startCampaign = [&]() {
            audio.resetSession();
            if (!loadCampaign())
                return false;
            if (!game.initScenario(
                    campaignScenario, &err))
                return false;
            campaignMatch = true;
            return true;
        };

        SceCtrlData pad{}, prev{};
        SceTouchData touch{};
        bool touching = false;
        bool touchMoved = false, touchBox = false;
        bool stickBoxArmed = false, stickBoxMoved = false;
        float touchStartX = 0, touchStartY = 0, lastTx = 0, lastTy = 0;
        float stickBoxStartX = 0, stickBoxStartY = 0;
        float cursorX = kScreenW * 0.5f, cursorY = kScreenH * 0.5f;
        int menuStickY = 0;
        uint64_t last = sceKernelGetProcessTimeWide();
        uint64_t budgetT = 0;
        uint64_t updateUs = 0, renderUs = 0, swapUs = 0;
        size_t lastBuilds = 0;
        uint64_t statT = last;
        int frames = 0;

        for (;;) {
            uint64_t now = sceKernelGetProcessTimeWide();
            float dt = (now - last) / 1e6f;
            last = now;
            if (dt > 0.1f) dt = 0.1f;

            sceCtrlPeekBufferPositive(0, &pad, 1);
            uint32_t pressed = pad.buttons & ~prev.buttons;
            uint32_t released = prev.buttons & ~pad.buttons;
            prev = pad;
            swgb::InputState in;
            in.screenW = kScreenW;
            in.screenH = kScreenH;
            const bool cheatChord =
                (pressed & SCE_CTRL_SELECT) &&
                (pad.buttons & SCE_CTRL_LTRIGGER) &&
                (pad.buttons & SCE_CTRL_RTRIGGER);
            in.toggleCheatMenu = cheatChord;
            in.menuUp = (pressed & SCE_CTRL_UP) != 0;
            in.menuDown = (pressed & SCE_CTRL_DOWN) != 0;
            in.menuLeft = (pressed & SCE_CTRL_LEFT) != 0;
            in.menuRight = (pressed & SCE_CTRL_RIGHT) != 0;
            in.menuActivate = (pressed & SCE_CTRL_CROSS) != 0;
            in.menuBack = (pressed & SCE_CTRL_CIRCLE) != 0;
            in.pausePressed =
                (pressed & SCE_CTRL_START) != 0;
            in.actionTabLeft =
                (pressed & SCE_CTRL_LTRIGGER) != 0;
            in.actionTabRight =
                (pressed & SCE_CTRL_RTRIGGER) != 0;
            const int currentMenuStickY =
                pad.ly < 64 ? -1 : pad.ly > 192 ? 1 : 0;
            if (currentMenuStickY < 0 && menuStickY >= 0)
                in.menuUp = true;
            if (currentMenuStickY > 0 && menuStickY <= 0)
                in.menuDown = true;
            menuStickY = currentMenuStickY;
            in.scrollX = axis(pad.lx);
            in.scrollY = axis(pad.ly);
            if (pad.buttons & SCE_CTRL_LEFT) in.scrollX = -1;
            if (pad.buttons & SCE_CTRL_RIGHT) in.scrollX = 1;
            if (pad.buttons & SCE_CTRL_UP) in.scrollY = -1;
            if (pad.buttons & SCE_CTRL_DOWN) in.scrollY = 1;
            if (pressed & SCE_CTRL_RTRIGGER) in.zoomStep = 1;
            if (pressed & SCE_CTRL_LTRIGGER) in.zoomStep = -1;
            if (pressed & SCE_CTRL_SQUARE) {
                stickBoxArmed = true;
                stickBoxMoved = false;
                stickBoxStartX = cursorX;
                stickBoxStartY = cursorY;
            }
            const float cursorMoveX = axis(pad.rx) * 520.0f * dt;
            const float cursorMoveY = axis(pad.ry) * 520.0f * dt;
            cursorX += cursorMoveX;
            cursorY += cursorMoveY;
            cursorX = std::max(0.0f, std::min((float)kScreenW, cursorX));
            cursorY = std::max(0.0f, std::min((float)kScreenH, cursorY));
            if (stickBoxArmed) {
                const float dx = cursorX - stickBoxStartX, dy = cursorY - stickBoxStartY;
                if (dx * dx + dy * dy > 16.0f) stickBoxMoved = true;
                if (pad.buttons & SCE_CTRL_SQUARE) {
                    in.boxSelectActive = stickBoxMoved;
                    in.boxStartX = stickBoxStartX;
                    in.boxStartY = stickBoxStartY;
                    in.boxEndX = cursorX;
                    in.boxEndY = cursorY;
                }
                if (released & SCE_CTRL_SQUARE) {
                    in.boxSelectCommit = stickBoxMoved;
                    in.boxStartX = stickBoxStartX;
                    in.boxStartY = stickBoxStartY;
                    in.boxEndX = cursorX;
                    in.boxEndY = cursorY;
                    stickBoxArmed = false;
                    stickBoxMoved = false;
                }
            }
            in.pointerX = cursorX;
            in.pointerY = cursorY;
            in.cursorVisible = true;
            if (pressed & SCE_CTRL_CROSS) in.selectPressed = true;
            if (pressed & SCE_CTRL_CIRCLE) in.commandPressed = true;
            if (pressed & SCE_CTRL_TRIANGLE) in.cycleAttackMode = true;

            sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
            if (touch.reportNum > 0) {
                float tx = touch.report[0].x / 2.0f, ty = touch.report[0].y / 2.0f;
                if (!touching) {
                    touchStartX = lastTx = tx;
                    touchStartY = lastTy = ty;
                    touchMoved = false;
                    touchBox = (pad.buttons & SCE_CTRL_SQUARE) != 0;
                    if (touchBox) {
                        stickBoxArmed = false;
                        stickBoxMoved = false;
                    }
                } else {
                    const float totalX = tx - touchStartX, totalY = ty - touchStartY;
                    if (totalX * totalX + totalY * totalY > 100.0f) touchMoved = true;
                    if (!touchBox && touchMoved) {
                        in.dragX = tx - lastTx;
                        in.dragY = ty - lastTy;
                    }
                }
                if (touchBox) {
                    in.boxSelectActive = true;
                    in.boxStartX = touchStartX;
                    in.boxStartY = touchStartY;
                    in.boxEndX = tx;
                    in.boxEndY = ty;
                }
                lastTx = tx;
                lastTy = ty;
                touching = true;
            } else if (touching) {
                in.pointerX = lastTx;
                in.pointerY = lastTy;
                if (touchBox && touchMoved) {
                    in.boxSelectCommit = true;
                    in.boxStartX = touchStartX;
                    in.boxStartY = touchStartY;
                    in.boxEndX = lastTx;
                    in.boxEndY = lastTy;
                } else if (!touchMoved) {
                    in.pointerTap = true;
                }
                touching = false;
            }

            const swgb::FrontendAction action =
                frontend.update(
                    in, game.victoryStateForTesting());
            if (action ==
                    swgb::FrontendAction::StartSkirmish ||
                action ==
                    swgb::FrontendAction::StartCampaign ||
                action ==
                    swgb::FrontendAction::RestartMatch) {
                frontend.render(
                    renderer, kScreenW, kScreenH);
                vglSwapBuffers(GL_FALSE);
                err.clear();
                bool started = false;
                if (action ==
                    swgb::FrontendAction::StartSkirmish)
                    started = startSkirmish();
                else if (action ==
                         swgb::FrontendAction::StartCampaign)
                    started = startCampaign();
                else
                    started =
                        campaignMatch
                            ? startCampaign()
                            : startSkirmish();
                frontend.loadingFinished(
                    started, started ? std::string() : err);
                unitSoundChoice = 0;
                touching = false;
                touchMoved = false;
                touchBox = false;
                stickBoxArmed = false;
                stickBoxMoved = false;
                if (!started)
                    game.clearMatch();
            } else if (
                action ==
                swgb::FrontendAction::ReturnToMainMenu) {
                audio.resetSession();
                game.clearMatch();
                unitSoundChoice = 0;
                touching = false;
                touchMoved = false;
                touchBox = false;
                stickBoxArmed = false;
                stickBoxMoved = false;
            } else if (
                action ==
                swgb::FrontendAction::Quit) {
                break;
            }

            // Sprite cache budget: vglMemFree(VGL_MEM_ALL) reports 0 here, so
            // use the per-pool figures; keep ~24 MB of GPU memory spare.
            if (frontend.screen() ==
                    swgb::FrontendScreen::Gameplay &&
                now - budgetT >= 1000000) {
                budgetT = now;
                const size_t freeBytes = vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM) +
                                         vglMemFree(VGL_MEM_PHYCONT);
                const size_t reserve = 40u * 1024u * 1024u;
                const size_t used = assets.textureBytes();
                size_t budget = freeBytes >= reserve ? used + (freeBytes - reserve)
                                                     : (used > reserve - freeBytes ? used - (reserve - freeBytes) : 0);
                budget = std::max<size_t>(32u * 1024u * 1024u, std::min<size_t>(budget, 160u * 1024u * 1024u));
                game.setTextureBudget(budget);
            }
            const uint64_t t0 = sceKernelGetProcessTimeWide();
            if (frontend.screen() ==
                swgb::FrontendScreen::Gameplay)
                game.update(dt, in);
            const uint64_t t1 = sceKernelGetProcessTimeWide();
            if (frontend.screen() ==
                swgb::FrontendScreen::Gameplay)
                game.render(
                    renderer, kScreenW, kScreenH);
            else
                frontend.render(
                    renderer, kScreenW, kScreenH);
            const uint64_t t2 = sceKernelGetProcessTimeWide();
            vglSwapBuffers(GL_FALSE);
            const uint64_t t3 = sceKernelGetProcessTimeWide();
            updateUs += t1 - t0;
            renderUs += t2 - t1;
            swapUs += t3 - t2;
            frames++;
            if (now - statT >= 5000000) {
                logf("fps=%.1f ms upd/rnd/swap=%.1f/%.1f/%.1f draws=%d quads=%d sprites=%d sheets=%u tex=%.1fMB "
                     "builds=%u free vram/ram/phy=%.1f/%.1f/%.1fMB menu=%d sel=%u zoom=%.2f",
                     frames * 1e6 / (double)(now - statT),
                     frames ? updateUs / 1000.0 / frames : 0.0, frames ? renderUs / 1000.0 / frames : 0.0,
                     frames ? swapUs / 1000.0 / frames : 0.0, renderer.drawCalls(), renderer.quads(),
                     game.stats().sprites, (unsigned)assets.sheetCount(),
                     assets.textureBytes() / 1048576.0,
                     (unsigned)(assets.buildCount() - lastBuilds),
                     vglMemFree(VGL_MEM_VRAM) / 1048576.0, vglMemFree(VGL_MEM_RAM) / 1048576.0,
                     vglMemFree(VGL_MEM_PHYCONT) / 1048576.0, (int)game.actionMenuOpenForTesting(),
                     (unsigned)game.selectedObjectIds().size(), game.zoom());
                updateUs = renderUs = swapUs = 0;
                lastBuilds = assets.buildCount();
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
