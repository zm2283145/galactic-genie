// SPDX-License-Identifier: GPL-3.0-or-later
// PS Vita entry point: vitaGL init, input, main loop.
#include "../../core/cpx.h"
#include "../../core/scenario.h"
#include "../../engine/assets.h"
#include "../../engine/campaign.h"
#include "../../engine/frontend.h"
#include "../../engine/game.h"
#include "../../engine/settings.h"
#include "../../engine/startup.h"
#include "../../engine/ui_text.h"
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
const char *kCampaignDir = "ux0:data/swgb/Campaign";
const char *kScenarioSoundDir = "ux0:data/swgb/Sound/Scenario";
const char *kMusicDir = "ux0:data/swgb/Music";
const char *kTerrainSoundDir = "ux0:data/swgb/Sound/Terrain";
const char *kSettingsPath = "ux0:data/swgb/settings.bin";
const char *kProfilePath = "ux0:data/swgb/campaign.profile";
const char *kSavePath = "ux0:data/swgb/skirmish.save";
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

bool fileExists(const char *path) {
    FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

void startupCard(
    swgb::Renderer &renderer,
    const std::string &title,
    const std::string &message,
    uint8_t red = 2, uint8_t green = 6,
    uint8_t blue = 15) {
    renderer.beginFrame(
        kScreenW, kScreenH, 1.0f, red, green, blue);
    renderer.fillRect(
        0, 0, kScreenW, kScreenH,
        red, green, blue, 255);
    swgb::drawUiText(
        renderer, {title},
        (kScreenW -
         swgb::uiTextWidth(title, 2.5f)) *
            0.5f,
        190, 2.5f, 235, 213, 145);
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < message.size() &&
           lines.size() < 4) {
        size_t end =
            std::min(message.size(), start + 92);
        if (end < message.size()) {
            const size_t space =
                message.rfind(' ', end);
            if (space != std::string::npos &&
                space > start)
                end = space;
        }
        lines.push_back(
            message.substr(start, end - start));
        start = end;
        while (start < message.size() &&
               message[start] == ' ')
            ++start;
    }
    swgb::drawUiText(
        renderer, lines, 55, 275, 1.05f,
        209, 222, 232);
    renderer.endFrame();
    vglSwapBuffers(GL_FALSE);
}

bool startupErrorScreen(
    swgb::Renderer &renderer,
    const std::string &msg) {
    logf("FATAL: %s", msg.c_str());
    SceCtrlData pad{}, previous{};
    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const uint32_t pressed =
            pad.buttons & ~previous.buttons;
        previous = pad;
        startupCard(
            renderer, "ORIGINAL DATA ERROR",
            msg + "   X: RETRY   O: EXIT",
            50, 6, 9);
        if (pressed & SCE_CTRL_CROSS) return true;
        if (pressed & SCE_CTRL_CIRCLE) return false;
        sceKernelDelayThread(16000);
    }
}

void runStartupPresentation(
    swgb::Renderer &renderer) {
    swgb::StartupFlow flow;
    flow.validationFinished(true);
    flow.dataFinished(true);
    SceCtrlData pad{}, previous{};
    uint64_t last = sceKernelGetProcessTimeWide();
    while (flow.stage() == swgb::StartupStage::Logo ||
           flow.stage() == swgb::StartupStage::Intro) {
        const uint64_t now =
            sceKernelGetProcessTimeWide();
        const float dt = (now - last) / 1000000.0f;
        last = now;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const uint32_t pressed =
            pad.buttons & ~previous.buttons;
        previous = pad;
        const bool skip =
            (pressed &
             (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE |
              SCE_CTRL_START)) != 0;
        startupCard(
            renderer,
            flow.stage() == swgb::StartupStage::Logo
                ? "LUCASARTS"
                : "STAR WARS",
            flow.stage() == swgb::StartupStage::Logo
                ? "ORIGINAL GAME DATA INITIALIZED   X: SKIP"
                : "GALACTIC BATTLEGROUNDS: CLONE CAMPAIGNS   X: SKIP");
        flow.update(dt, skip);
        sceKernelDelayThread(16000);
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
        std::string err;
        std::unique_ptr<swgb::Assets> assetOwner;
        swgb::CampaignCatalog catalog;
        for (;;) {
            startupCard(
                renderer, "LOADING ORIGINAL DATA",
                "VALIDATING DATA, LANGUAGE, CAMPAIGNS, AND PROFILE");
            assetOwner.reset(new swgb::Assets(&renderer));
            assetOwner->setLogger(
                [](const std::string &s) { logf("%s", s.c_str()); });
            const uint64_t t0 =
                sceKernelGetProcessTimeWide();
            err.clear();
            bool valid = assetOwner->init(kDataDir, &err);
            if (valid) {
                valid = catalog.discover(
                    kCampaignDir,
                    [&](int id, const std::string &fallback) {
                        const std::string &localized =
                            assetOwner->localizedString(id);
                        return localized.empty()
                                   ? fallback
                                   : localized;
                    },
                    &err);
                if (valid && catalog.missionCount() != 43) {
                    err =
                        "expected 43 original XCAM missions; found " +
                        std::to_string(catalog.missionCount()) +
                        " (copy XCAM1/2/3/4/5/8.CPX)";
                    valid = false;
                }
            }
            if (valid) {
                logf(
                    "original data loaded in %llu ms; %u campaigns, %u missions",
                    (unsigned long long)(
                        (sceKernelGetProcessTimeWide() - t0) /
                        1000),
                    (unsigned)catalog.campaigns().size(),
                    (unsigned)catalog.missionCount());
                break;
            }
            if (!startupErrorScreen(
                    renderer,
                    err +
                        " (copy the original game data to ux0:data/swgb)")) {
                sceKernelExitProcess(0);
                return 0;
            }
        }
        swgb::Assets &assets = *assetOwner;
        runStartupPresentation(renderer);

        swgb::Game game(assets);
        swgb::Frontend frontend;
        frontend.setStringLookup(
            [&](int id, const std::string &fallback) {
                const std::string &localized =
                    assets.localizedString(id);
                return localized.empty() ? fallback
                                         : localized;
            });
        swgb::CampaignProfile campaignProfile;
        std::string profileError;
        if (!swgb::loadCampaignProfile(
                kProfilePath, campaignProfile,
                &profileError)) {
            logf(
                "campaign profile recovery: %s",
                profileError.c_str());
            campaignProfile = swgb::CampaignProfile{};
            frontend.reportMessage(
                "CAMPAIGN PROFILE WAS CORRUPT; DEFAULTS RESTORED");
        }
        frontend.setCampaignData(
            &catalog, &campaignProfile);
        frontend.setDataStatus(
            catalog.campaigns().size(),
            catalog.missionCount(),
            fileExists("ux0:data/swgb/xlogo1.avi") ||
                fileExists("ux0:data/swgb/xintro.avi"));
        swgb::UserSettings userSettings;
        std::string settingsError;
        if (!swgb::loadSettings(
                kSettingsPath, userSettings,
                &settingsError)) {
            logf(
                "settings recovery: %s; using defaults",
                settingsError.c_str());
            userSettings = swgb::UserSettings{};
            frontend.reportMessage(
                "SETTINGS WERE CORRUPT; DEFAULTS RESTORED");
        }
        frontend.setUserSettings(userSettings);
        swgb::MatchSaveMetadata savedMetadata;
        std::string saveProbeError;
        const bool saveAvailable =
            swgb::Game::readSaveMetadata(
                kSavePath, savedMetadata,
                &saveProbeError);
        frontend.setContinueAvailable(saveAvailable);
        if (saveAvailable)
            frontend.setContinueKind(savedMetadata.kind);
        if (!saveProbeError.empty())
            logf("save probe: %s", saveProbeError.c_str());
        game.setLogger([](const std::string &s) { logf("%s", s.c_str()); });
        swgb::VitaAudio audio(
            kScenarioSoundDir, kMusicDir, kTerrainSoundDir);
        audio.setLogger([](const std::string &s) { logf("audio: %s", s.c_str()); });
        std::string audioError;
        if (!audio.start(&audioError)) logf("audio disabled: %s", audioError.c_str());
        audio.setVolumes(
            userSettings.masterVolume,
            userSettings.musicVolume,
            userSettings.dialogueVolume,
            userSettings.effectsVolume);
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
        const auto startSkirmish = [&]() {
            audio.resetSession();
            const swgb::SkirmishSettings &settings =
                frontend.settings();
            if (!game.initSkirmish(settings, &err))
                return false;
            static constexpr const char *difficulties[] = {
                "DIFFICULTY-HARDEST",
                "DIFFICULTY-HARD",
                "DIFFICULTY-MODERATE",
                "DIFFICULTY-EASY",
                "DIFFICULTY-EASIEST",
            };
            for (int slot = 0;
                 slot < swgb::kMaxSkirmishSlots;
                 ++slot) {
                const swgb::SkirmishSlot &player =
                    settings.slots[(size_t)slot];
                if (player.type !=
                    swgb::SkirmishSlotType::Computer)
                    continue;
                const char *personality =
                    player.personality ==
                            swgb::AiPersonality::Classic
                        ? "Computer Classic.per"
                        : "Computer Expanded.per";
                const std::string aiPath =
                    std::string(
                        "ux0:data/swgb/AI/") +
                    personality;
                std::unordered_set<std::string> defines{
                    difficulties[std::max(
                        0, std::min(
                               4,
                               (int)player
                                   .difficulty))],
                    "POPULATION-CAP-" +
                        std::to_string(
                            settings.populationCap),
                };
                if (settings.victory ==
                    swgb::SkirmishVictory::Standard)
                    defines.insert(
                        "VICTORY-STANDARD");
                else if (settings.victory ==
                         swgb::SkirmishVictory::Conquest)
                    defines.insert(
                        "VICTORY-CONQUEST");
                if (settings.mapStyle ==
                        swgb::SkirmishMapStyle::Archipelago ||
                    settings.mapStyle ==
                        swgb::SkirmishMapStyle::CompactIslands)
                    defines.insert(
                        player.team > 0
                            ? "TEAM-LAND-SATELLITES-MAP"
                            : "LAND-SATELLITES-MAP");
                if (!game.loadAiScript(
                        slot + 1, aiPath,
                        defines, &err)) {
                    err =
                        "AI personality for slot " +
                        std::to_string(slot + 1) +
                        " could not be loaded: " +
                        err +
                        " (copy the original AI folder)";
                    return false;
                }
            }
            campaignMatch = false;
            frontend.setCampaignMatch(false);
            return true;
        };
        const auto startCampaign = [&]() {
            audio.resetSession();
            const swgb::CampaignMission *mission =
                frontend.selectedCampaignMission();
            if (!mission) {
                err = "no campaign mission is selected";
                return false;
            }
            if (!catalog.loadScenario(
                    frontend.selectedCampaign(),
                    frontend.selectedMission(),
                    campaignScenario, &err))
                return false;
            if (!game.initScenario(
                    campaignScenario, &err,
                    mission->archiveName,
                    mission->entry,
                    campaignProfile.difficulty,
                    "ux0:data/swgb/AI"))
                return false;
            campaignMatch = true;
            frontend.setCampaignMatch(true);
            logf(
                "campaign %s entry %u loaded: %s, %ux%u, %u units",
                mission->archiveName.c_str(),
                (unsigned)mission->entry + 1,
                campaignScenario.originalFilename.c_str(),
                (unsigned)campaignScenario.map.width,
                (unsigned)campaignScenario.map.height,
                (unsigned)campaignScenario.units.size());
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
            const bool leftHanded =
                frontend.userSettings().controls ==
                swgb::ControlPreset::LeftHanded;
            in.scrollX =
                axis(leftHanded ? pad.rx : pad.lx);
            in.scrollY =
                axis(leftHanded ? pad.ry : pad.ly);
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
            const float cursorMoveX =
                axis(leftHanded ? pad.lx : pad.rx) *
                520.0f * dt;
            const float cursorMoveY =
                axis(leftHanded ? pad.ly : pad.ry) *
                520.0f * dt;
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
            if (pressed &
                (leftHanded ? SCE_CTRL_CIRCLE
                            : SCE_CTRL_CROSS))
                in.selectPressed = true;
            if (pressed &
                (leftHanded ? SCE_CTRL_CROSS
                            : SCE_CTRL_CIRCLE))
                in.commandPressed = true;
            if (pressed & SCE_CTRL_TRIANGLE) in.cycleAttackMode = true;
            if ((pad.buttons & SCE_CTRL_SELECT) &&
                !cheatChord) {
                if (pressed & SCE_CTRL_UP)
                    in.controlGroup = 0;
                else if (pressed & SCE_CTRL_RIGHT)
                    in.controlGroup = 1;
                else if (pressed & SCE_CTRL_DOWN)
                    in.controlGroup = 2;
                else if (pressed & SCE_CTRL_LEFT)
                    in.controlGroup = 3;
                if (in.controlGroup >= 0) {
                    in.controlGroupAssign =
                        (pad.buttons &
                         SCE_CTRL_SQUARE) != 0;
                    if (in.controlGroupAssign) {
                        stickBoxArmed = false;
                        stickBoxMoved = false;
                        in.boxSelectActive = false;
                        in.boxSelectCommit = false;
                    }
                    in.scrollX = in.scrollY = 0.0f;
                    in.menuUp = in.menuDown =
                        in.menuLeft =
                            in.menuRight = false;
                }
            }

            sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
            if (touch.reportNum > 0) {
                float tx = touch.report[0].x / 2.0f, ty = touch.report[0].y / 2.0f;
                in.pointerX = tx;
                in.pointerY = ty;
                in.pointerDown = true;
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
            if (frontend.takeSettingsChanged()) {
                userSettings =
                    frontend.userSettings();
                audio.setVolumes(
                    userSettings.masterVolume,
                    userSettings.musicVolume,
                    userSettings.dialogueVolume,
                    userSettings.effectsVolume);
                std::string saveError;
                if (!swgb::saveSettings(
                        kSettingsPath,
                        userSettings, &saveError)) {
                    logf(
                        "settings save failed: %s",
                        saveError.c_str());
                    frontend.reportMessage(
                        "SETTINGS COULD NOT BE SAVED: " +
                        saveError);
                }
            }
            if (frontend.takeProfileChanged()) {
                std::string saveError;
                if (!swgb::saveCampaignProfile(
                        kProfilePath, campaignProfile,
                        &saveError)) {
                    logf(
                        "campaign profile save failed: %s",
                        saveError.c_str());
                    frontend.reportMessage(
                        "CAMPAIGN PROGRESS COULD NOT BE SAVED: " +
                        saveError);
                }
            }
            if (action ==
                    swgb::FrontendAction::StartSkirmish ||
                action ==
                    swgb::FrontendAction::StartCampaign ||
                action ==
                    swgb::FrontendAction::RestartMatch ||
                action ==
                    swgb::FrontendAction::LoadMatch) {
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
                else if (action ==
                         swgb::FrontendAction::RestartMatch)
                    started =
                        campaignMatch
                            ? startCampaign()
                            : startSkirmish();
                else {
                    swgb::MatchSaveMetadata metadata;
                    started =
                        swgb::Game::readSaveMetadata(
                            kSavePath, metadata, &err);
                    if (started &&
                        metadata.kind ==
                            swgb::MatchSaveKind::Skirmish) {
                        frontend.settingsForTesting() =
                            metadata.skirmish;
                        started = startSkirmish();
                    } else if (started) {
                        bool found = false;
                        for (size_t c = 0;
                             c < catalog.campaigns().size() &&
                             !found;
                             ++c) {
                            const swgb::CampaignInfo &campaign =
                                catalog.campaigns()[c];
                            if (campaign.archiveName !=
                                metadata.campaignArchive)
                                continue;
                            for (size_t m = 0;
                                 m < campaign.missions.size();
                                 ++m) {
                                if (campaign.missions[m].entry !=
                                    metadata.campaignEntry)
                                    continue;
                                frontend.selectCampaignMission(c, m);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            err =
                                "campaign save references an unavailable mission";
                            started = false;
                        } else {
                            started = startCampaign();
                        }
                    }
                    if (started)
                        started = game.loadMatch(
                            kSavePath, &err);
                }
                frontend.actionFinished(
                    action, started,
                    started ? std::string() : err);
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
                swgb::FrontendAction::SaveMatch) {
                err.clear();
                const bool saved =
                    game.saveMatch(kSavePath, &err);
                frontend.actionFinished(
                    action, saved,
                    saved ? std::string() : err);
                if (saved)
                    frontend.setContinueAvailable(true);
            } else if (
                action ==
                    swgb::FrontendAction::ReturnToMainMenu ||
                action ==
                    swgb::FrontendAction::ReturnToCampaignBrowser) {
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
