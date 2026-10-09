// SPDX-License-Identifier: GPL-3.0-or-later
// PS Vita entry point: vitaGL init, input, main loop.
#include "../../core/cpx.h"
#include "../../core/scenario.h"
#include "../../engine/assets.h"
#include "../../engine/campaign.h"
#include "../../engine/campaign_layout.h"
#include "../../engine/campaign_scene.h"
#include "../../engine/editor.h"
#include "../../engine/frontend.h"
#include "../../engine/game.h"
#include "../../engine/settings.h"
#include "../../engine/startup.h"
#include "../../engine/ui_text.h"
#include "../../render/gl_renderer.h"
#include "vita_audio.h"
#include "vita_video.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <atomic>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

// Leave enough of the 256 MiB game partition for the executable, runtime
// modules and allocations made before main(). A 256 MiB newlib heap exhausts
// the partition during vitaGL's static initialization.
int _newlib_heap_size_user = 192 * 1024 * 1024;

namespace swgb { extern void (*g_glTrace)(const char *); extern bool g_skipGlClear; }

namespace {

const char *kRoot = "ux0:data/swgb";
const char *kDataDir = "ux0:data/swgb/Data";
const char *kCampaignDir = "ux0:data/swgb/Campaign";
const char *kScenarioSoundDir = "ux0:data/swgb/Sound/Scenario";
const char *kCampaignSoundDir = "ux0:data/swgb/Sound/Campaign";
const char *kMusicDir = "ux0:data/swgb/Music";
const char *kTerrainSoundDir = "ux0:data/swgb/Sound/Terrain";
const char *kSettingsPath = "ux0:data/swgb/settings.bin";
const char *kProfilePath = "ux0:data/swgb/campaign.profile";
const char *kSavePath = "ux0:data/swgb/skirmish.save";
const char *kPlaytestSavePath =
    "ux0:data/swgb/Scenarios/playtest.save";
const char *kScenarioRoot =
    "ux0:data/swgb/Scenarios";
const char *kScenarioImport =
    "ux0:data/swgb/Scenarios/Import";
const int kScreenW = 960, kScreenH = 544;

FILE *g_log = nullptr;
// Vita3K only writes host files out when they are closed, so in Vita3K mode
// every line reopens the log; on hardware lines are buffered and flushed with
// the 5-second frame stats (a flush per line cost ~1 ms on the memory card).
bool g_logReopen = false;
std::atomic<bool> g_quitRequested{false};

void logf(const char *fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (g_logReopen) sceClibPrintf("swgb: %s\n", line);
    if (!g_log) return;
    fprintf(g_log, "[%7.3f] ", sceKernelGetProcessTimeWide() / 1e6);
    fputs(line, g_log);
    fputc('\n', g_log);
    if (g_logReopen) {
        fclose(g_log);
        g_log = fopen("ux0:data/swgb/swgb.log", "a");
    }
}

void flushLog() {
    if (g_log && !g_logReopen) fflush(g_log);
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

std::string campaignNarrationPrefix(
    const swgb::CampaignMission *mission) {
    if (!mission) return {};
    std::string archive = mission->archiveName;
    std::transform(
        archive.begin(), archive.end(),
        archive.begin(),
        [](unsigned char c) {
            return (char)std::tolower(c);
        });
    const char *prefix = nullptr;
    if (archive == "xcam1") prefix = "TF";
    else if (archive == "xcam2") prefix = "GN";
    else if (archive == "xcam3") prefix = "GE";
    else if (archive == "xcam4") prefix = "RA";
    else if (archive == "xcam5") prefix = "WK";
    else if (archive == "xcam8") prefix = "TU";
    return prefix
               ? std::string(prefix) +
                     std::to_string(mission->entry + 1)
               : std::string();
}

void startupCard(
    swgb::Renderer &renderer,
    const std::string &title,
    const std::string &message,
    uint8_t red = 2, uint8_t green = 6,
    uint8_t blue = 15) {
    static int cardTrace = 0;
    const bool trace = cardTrace++ < 2;
    if (trace) logf("startupCard: begin '%s'", title.c_str());
    renderer.beginFrame(
        kScreenW, kScreenH, 1.0f, red, green, blue);
    if (trace) logf("startupCard: beginFrame done");
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
    if (trace) logf("startupCard: text queued");
    renderer.endFrame();
    if (trace) logf("startupCard: endFrame done");
    vglSwapBuffers(GL_FALSE);
    if (trace) logf("startupCard: swap done");
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
    // The original start-up movies (xlogo1.avi, then xintro.avi), converted
    // to MP4 in ux0:data/swgb/Video; START skips each one. Without them the
    // title cards below stand in.
    auto movieLog = [](const std::string &message) {
        logf("%s", message.c_str());
        flushLog();
    };
    // Dev: encoding tests ux0:data/swgb/Video/test/t1.mp4 ... t9.mp4.
    for (int i = 1; i <= 9; ++i) {
        const std::string test = "ux0:data/swgb/Video/test/t" + std::to_string(i) + ".mp4";
        if (fileExists(test.c_str())) swgb::playMovie(test, renderer, kScreenW, kScreenH, movieLog);
    }
    if (swgb::playMovie("ux0:data/swgb/Video/xlogo1.mp4", renderer, kScreenW, kScreenH, movieLog)) {
        swgb::playMovie("ux0:data/swgb/Video/xintro.mp4", renderer, kScreenW, kScreenH, movieLog);
        return;
    }
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
    sceIoMkdir(kScenarioRoot, 0777);
    sceIoMkdir(kScenarioImport, 0777);
    sceIoMkdir(
        "ux0:data/swgb/Scenarios/scenarios", 0777);
    sceIoMkdir(
        "ux0:data/swgb/Scenarios/recent", 0777);
    sceIoMkdir(
        "ux0:data/swgb/Scenarios/autosave", 0777);
    sceIoMkdir(
        "ux0:data/swgb/Scenarios/recovery", 0777);
    g_log = fopen("ux0:data/swgb/swgb.log", "w");
    logf("swgb-vita starting");
    {
        // Remote close for test runs: a file uploaded over FTP as
        // ux0:data/swgb/quit.txt ends the app within about a second, whatever
        // screen it is on (vitacompanion's destroy does not close it).
        sceIoRemove("ux0:data/swgb/quit.txt");
        const SceUID thread = sceKernelCreateThread(
            "swgb_quit_watch",
            [](SceSize, void *) -> int {
                for (;;) {
                    sceKernelDelayThread(1000000);
                    SceIoStat stat{};
                    if (sceIoGetstat("ux0:data/swgb/quit.txt", &stat) >= 0) {
                        sceIoRemove("ux0:data/swgb/quit.txt");
                        // The game thread exits at the top of its next frame;
                        // exiting from here while it is drawing crashes.
                        g_quitRequested = true;
                        sceKernelDelayThread(10000000);
                        sceKernelExitProcess(0); // game thread stuck
                    }
                }
                return 0;
            },
            0x10000100, 0x2000, 0, 0, nullptr);
        if (thread >= 0) sceKernelStartThread(thread, 0, nullptr);
    }
    {
        // Keep the game thread on core 0: the audio and sprite-sheet workers
        // run below its priority on cores 1 and 2, and would be starved
        // whenever the scheduler placed the busy game thread on their core.
        const SceUID self = sceKernelGetThreadId();
        const int before = sceKernelGetThreadCpuAffinityMask(self);
        const int result = sceKernelChangeThreadCpuAffinityMask(self, SCE_KERNEL_CPU_MASK_USER_0);
        logf("main thread affinity 0x%x -> core 0 (result %d)", before, result);
    }

    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // As vglInitExtended, but physically contiguous RAM is left to the
    // system until the start-up movies have played: SceAvPlayer's decoder
    // allocates from it. vitaGL takes it afterwards (vglPhycontMemLazyInit).
    GLboolean fallback = vglInitWithCustomThreshold(0, kScreenW, kScreenH, 0x20000, 256 * 1024,
                                                    0x7FFFFFFF, 0, SCE_GXM_MULTISAMPLE_NONE);
    logf("vglInitExtended done (resolution fallback=%d)", (int)fallback);
    logf("vitaGL memory total vram/ram/phy/all=%.1f/%.1f/%.1f/%.1fMB free all=%.1fMB",
         vglMemTotal(VGL_MEM_VRAM) / 1048576.0, vglMemTotal(VGL_MEM_RAM) / 1048576.0,
         vglMemTotal(VGL_MEM_PHYCONT) / 1048576.0, vglMemTotal(VGL_MEM_ALL) / 1048576.0,
         vglMemFree(VGL_MEM_ALL) / 1048576.0);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    {
        swgb::g_glTrace = [](const char *what) { logf("gl: %s", what); };
        // Development runs under Vita3K: ux0:data/swgb/vita3k.txt
        if (FILE *marker = fopen("ux0:data/swgb/vita3k.txt", "r")) {
            g_logReopen = true;
            // The marker's number is a watchdog in seconds: the emulator only
            // flushes its own log and our files when the app exits by itself.
            int watchdogSeconds = 0;
            if (fscanf(marker, "%d", &watchdogSeconds) != 1) watchdogSeconds = 0;
            fclose(marker);
            if (watchdogSeconds > 0) {
                static int watchdogDelay = 0;
                watchdogDelay = watchdogSeconds;
                const SceUID thread = sceKernelCreateThread(
                    "swgb_watchdog",
                    [](SceSize, void *) -> int {
                        sceKernelDelayThread((SceUInt)watchdogDelay * 1000000u);
                        logf("watchdog: exiting after %d s", watchdogDelay);
                        sceKernelExitProcess(0);
                        return 0;
                    },
                    0x10000100, 0x4000, 0, 0, nullptr);
                if (thread >= 0) sceKernelStartThread(thread, 0, nullptr);
                logf("vita3k watchdog: %d s", watchdogSeconds);
            }
            swgb::g_skipGlClear = true;
            logf("vita3k mode: glClear replaced by full-frame fills");
        }
        logf("creating renderer");
        swgb::GlRenderer renderer;
        logf("renderer created");
        {
            // Cached terrain layer (render-to-texture). ux0:data/swgb/nolayer.txt
            // turns it off; layerflip.txt samples the layer the other way up.
            bool layers = true, flipped = true;
            if (FILE *f = fopen("ux0:data/swgb/nolayer.txt", "r")) { fclose(f); layers = false; }
            if (FILE *f = fopen("ux0:data/swgb/layerflip.txt", "r")) { fclose(f); flipped = false; }
            renderer.setLayersEnabled(layers);
            renderer.setLayerFlipped(flipped);
            logf("terrain layer cache: %s%s", layers ? "on" : "off", flipped ? "" : " (unflipped)");
        }
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
                // The original campaigns must all be there; the Clone
                // Campaigns (1CAM1/2.CP1) are optional.
                size_t originalMissions = 0;
                for (const swgb::CampaignInfo &campaign : catalog.campaigns())
                    if (!campaign.expansion && !campaign.custom)
                        originalMissions += campaign.missions.size();
                if (valid && originalMissions != 43) {
                    err =
                        "expected 43 original XCAM missions; found " +
                        std::to_string(originalMissions) +
                        " (copy XCAM1/2/3/4/5/8.CPX)";
                    valid = false;
                }
            }
            if (valid) {
                flushLog();
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
        // Unattended test runs (autostart.txt) skip the intro movies.
        if (!fileExists("ux0:data/swgb/autostart.txt"))
            runStartupPresentation(renderer);
        {
            SceKernelFreeMemorySizeInfo info{};
            info.size = sizeof info;
            sceKernelGetFreeMemorySize(&info);
            const int reserve = 1024 * 1024;
            if (info.size_phycont > reserve) vglPhycontMemLazyInit((size_t)(info.size_phycont - reserve));
            logf("phycont pool after movies: %.1f MB", (info.size_phycont - reserve) / 1048576.0);
        }

        swgb::Game game(assets);
        swgb::Frontend frontend;
        // The original goes from the intro movie straight to the main menu.
        frontend.showMainMenu();
        swgb::ScenarioEditor editor(
            assets, kScenarioRoot, kScenarioImport);
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
        // Opening scene of the selected campaign mission (Campaign/Media).
        swgb::CampaignScene campaignScene;
        frontend.setCampaignScene(&campaignScene);
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
        const bool expandingFrontsMenu =
            assets.interfaceFrame(
                53233, 0, 53237) != nullptr;
        const int mainMenuSlp =
            expandingFrontsMenu ? 53233 : 50189;
        const int mainMenuPalette =
            expandingFrontsMenu ? 53237 : 50589;
        frontend.setExpandingFrontsMenu(
            expandingFrontsMenu);
        frontend.setOriginalMenuDecoration(
            assets.interfaceFrame(
                mainMenuSlp, 49,
                mainMenuPalette),
            assets.interfaceFrame(50688, 4, 50589),
            assets.interfaceFrame(50688, 6, 50589));
        for (size_t campaign = 0;
             campaign < catalog.campaigns().size();
             ++campaign) {
            if (catalog.campaigns()[campaign].expansion) {
                // Clone Campaigns screen 53222: frames 1 + 4 * (n - 1).
                const int number = catalog.campaigns()[campaign].originalNumber;
                if (number < 1 || number > 2) continue;
                const size_t cloneFrame = 1 + 4 * (size_t)(number - 1);
                frontend.setOriginalCampaignIcon(
                    campaign, assets.interfaceFrame(53222, cloneFrame, 53220),
                    assets.interfaceFrame(53222, cloneFrame + 1, 53220));
                continue;
            }
            size_t frame = 0;
            switch (
                catalog.campaigns()[campaign]
                    .originalNumber) {
            case 1: frame = 1; break;
            case 2: frame = 5; break;
            case 3: frame = 9; break;
            case 4: frame = 13; break;
            case 5: frame = 17; break;
            case 8: frame = 29; break;
            default: continue;
            }
            frontend.setOriginalCampaignIcon(
                campaign,
                assets.interfaceFrame(
                    53014, frame, 53016),
                assets.interfaceFrame(
                    53014, frame + 1, 53016));
        }
        const std::array<size_t, 8>
            mainHotspotFrames =
                expandingFrontsMenu
                    ? std::array<size_t, 8>{{
                          34, 22, 18, 30,
                          10, 14, 26, 46,
                      }}
                    : std::array<size_t, 8>{{
                          30, 14, 18, 10,
                          22, 26, 34, 46,
                      }};
        for (size_t hotspot = 0;
             hotspot < mainHotspotFrames.size();
             ++hotspot) {
            const size_t frame =
                mainHotspotFrames[hotspot];
            frontend.setOriginalMainHotspot(
                hotspot,
                assets.interfaceFrame(
                    mainMenuSlp, frame,
                    mainMenuPalette),
                assets.interfaceFrame(
                    mainMenuSlp, frame + 1,
                    mainMenuPalette),
                assets.interfaceFrame(
                    mainMenuSlp,
                    frame +
                        (expandingFrontsMenu &&
                                 hotspot == 7
                             ? 2
                             : 3),
                    mainMenuPalette));
        }
        int frontendBackgroundSlp = -1;
        int frontendBackgroundPalette = -1;
        int frontendDialogSlp = -1;
        int frontendDialogPalette = -1;
        int frontendMissionTheme = -1;
        auto campaignThemeNumber = [&]() {
            if (frontend.selectedCampaign() >=
                catalog.campaigns().size())
                return 1;
            if (catalog.campaigns()[frontend.selectedCampaign()].custom)
                return -1; // no original screens: the list layout
            if (catalog.campaigns()[frontend.selectedCampaign()].expansion)
                return 100 + catalog.campaigns()[frontend.selectedCampaign()].originalNumber;
            const std::string &archive =
                catalog.campaigns()[
                    frontend.selectedCampaign()]
                    .archiveName;
            for (int number :
                 {1, 2, 3, 4, 5, 8})
                if (archive.find(
                        std::to_string(number)) !=
                    std::string::npos)
                    return number;
            return 1;
        };
        auto refreshOriginalFrontendArt = [&]() {
            int backgroundSlp = -1;
            int backgroundPalette = -1;
            int dialogSlp = -1;
            int dialogPalette = -1;
            switch (frontend.screen()) {
            case swgb::FrontendScreen::Title:
            case swgb::FrontendScreen::MainMenu:
            case swgb::FrontendScreen::SinglePlayer:
            case swgb::FrontendScreen::Options:
            case swgb::FrontendScreen::DataStatus:
            case swgb::FrontendScreen::Confirm:
            case swgb::FrontendScreen::Outcome:
                backgroundSlp = mainMenuSlp;
                backgroundPalette =
                    mainMenuPalette;
                break;
            case swgb::FrontendScreen::Achievements:
                // Screen info 50061: scr10B with the scr_ach palette.
                backgroundSlp = 50149;
                backgroundPalette = 50531;
                break;
            case swgb::FrontendScreen::CampaignBrowser:
                // Original campaigns 53014, Clone Campaigns 53222; the Custom
                // Campaigns list sits on the main menu art.
                backgroundSlp = frontend.customCampaignsShown() ? mainMenuSlp
                                : frontend.cloneCampaignsShown() ? 53222 : 53014;
                backgroundPalette = frontend.customCampaignsShown() ? mainMenuPalette
                                    : frontend.cloneCampaignsShown() ? 53220 : 53016;
                break;
            case swgb::FrontendScreen::CampaignMissions:
            case swgb::FrontendScreen::CampaignBriefing: {
                const int theme =
                    campaignThemeNumber();
                // Custom campaigns (theme -1) use the first campaign's art.
                backgroundSlp = swgb::missionBackgroundSlp(theme < 0 ? 1 : theme);
                backgroundPalette = swgb::missionPalette(theme < 0 ? 1 : theme);
                if (frontend.screen() ==
                    swgb::FrontendScreen::
                        CampaignBriefing) {
                    // Clone Campaigns dialogs: 53271 / 53281 (53270, 53280).
                    dialogSlp =
                        theme < 0 ? 53161
                        : theme == 101 ? 53271
                        : theme == 102 ? 53281
                        : theme == 8
                            ? 53164
                            : 53160 + theme;
                    dialogPalette =
                        theme == 8
                            ? 53114
                            : backgroundPalette;
                }
                break;
            }
            default:
                break;
            }
            const bool missionNodesVisible =
                frontend.screen() ==
                swgb::FrontendScreen::
                    CampaignMissions;
            const int missionTheme =
                missionNodesVisible
                    ? campaignThemeNumber()
                    : -1;
            if (missionTheme != frontendMissionTheme) {
                if (frontendMissionTheme >= 0) {
                    const int oldSlp =
                        swgb::missionBackgroundSlp(frontendMissionTheme);
                    const int oldPalette =
                        swgb::missionPalette(frontendMissionTheme);
                    const size_t oldMissionCount =
                        frontendMissionTheme == 4
                            ? 8
                            : 7;
                    frontend.clearOriginalMissionNodes();
                    for (size_t frame = 1;
                         frame <=
                         oldMissionCount * 4;
                         ++frame)
                        assets.releaseInterfaceFrame(
                            oldSlp, frame,
                            oldPalette);
                }
                frontendMissionTheme = missionTheme;
                if (const swgb::OriginalMissionLayout *layout =
                        swgb::originalMissionLayout(missionTheme))
                    swgb::applyOriginalMissionLayout(frontend, assets, *layout);
            }
            if (backgroundSlp !=
                    frontendBackgroundSlp ||
                backgroundPalette !=
                    frontendBackgroundPalette) {
                const swgb::SpriteFrame *next =
                    backgroundSlp >= 0
                        ? assets.interfaceFrame(
                              backgroundSlp, 0,
                              backgroundPalette)
                        : nullptr;
                if (next || backgroundSlp < 0) {
                    frontend.setOriginalMenuBackground(next);
                    if (frontendBackgroundSlp >= 0)
                        assets.releaseInterfaceFrame(
                            frontendBackgroundSlp, 0,
                            frontendBackgroundPalette);
                    frontendBackgroundSlp = backgroundSlp;
                    frontendBackgroundPalette =
                        backgroundPalette;
                }
            }
            if (dialogSlp != frontendDialogSlp ||
                dialogPalette !=
                    frontendDialogPalette) {
                const swgb::SpriteFrame *next =
                    dialogSlp >= 0
                        ? assets.interfaceFrame(
                              dialogSlp, 0,
                              dialogPalette)
                        : nullptr;
                if (next || dialogSlp < 0) {
                    frontend.setOriginalBriefingDialog(next);
                    if (frontendDialogSlp >= 0)
                        assets.releaseInterfaceFrame(
                            frontendDialogSlp, 0,
                            frontendDialogPalette);
                    frontendDialogSlp = dialogSlp;
                    frontendDialogPalette = dialogPalette;
                }
            }
        };
        refreshOriginalFrontendArt();
        logf(
            "original widescreen frontend art initialized");
        assets.setBuildsPerFrame(3);
        {
            // Unit sprite atlases decode on a worker core; syncsheets.txt
            // turns that off for comparison runs.
            bool asyncSheets = true;
            if (FILE *f = fopen("ux0:data/swgb/syncsheets.txt", "r")) { fclose(f); asyncSheets = false; }
            assets.setAsyncSheetBuilds(asyncSheets);
            logf("sprite sheet builds: %s", asyncSheets ? "worker thread" : "main thread");
        }
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
            kScenarioSoundDir, kCampaignSoundDir,
            kMusicDir, kTerrainSoundDir);
        audio.setLogger([](const std::string &s) { logf("audio: %s", s.c_str()); });
        audio.setTauntDir(std::string(kRoot) + "/Taunt");
        std::string audioError;
        if (!audio.start(&audioError)) logf("audio disabled: %s", audioError.c_str());
        audio.setVolumes(
            userSettings.masterVolume,
            userSettings.musicVolume,
            userSettings.dialogueVolume,
            userSettings.effectsVolume);
        // Time spent in the game's sound callbacks (read + decode), logged
        // with the frame stats.
        uint64_t soundUs = 0, soundMaxUs = 0;
        struct SoundTimer {
            uint64_t start;
            uint64_t &total, &maximum;
            const char *kind = "";
            std::string name;
            ~SoundTimer() {
                const uint64_t us = sceKernelGetProcessTimeWide() - start;
                total += us;
                if (us > maximum) maximum = us;
                if (us > 20000)
                    logf("slow sound %s %s: %llu us", kind, name.c_str(), (unsigned long long)us);
            }
        };
        game.setTauntPlayer([&](int number) { audio.playTaunt(number); });
        game.setSoundPlayer([&](const std::string &name) {
            SoundTimer timer{sceKernelGetProcessTimeWide(), soundUs, soundMaxUs, "voice", name};
            return audio.play(name);
        });
        game.setAmbientSoundPlayer(
            [&](const std::string &name) {
                SoundTimer timer{sceKernelGetProcessTimeWide(), soundUs, soundMaxUs, "ambient", name};
                return audio.playAmbient(name);
            });
        // The audio worker reads uncached effects from its own DRS handles.
        auto workerSounds = std::make_shared<swgb::ResourceSet>();
        auto workerInterface = std::make_shared<swgb::ResourceSet>();
        for (const std::string &path : assets.soundArchivePaths()) workerSounds->add(path);
        for (const std::string &path : assets.interfaceArchivePaths()) workerInterface->add(path);
        audio.setEffectLoader(
            [workerSounds, workerInterface](int resourceId, bool interfaceSound,
                                            std::vector<uint8_t> &data) {
                // Same lookup order as Assets::readSoundResource / readSound.
                if (interfaceSound && workerInterface->read(resourceId, data)) return true;
                return workerSounds->read(resourceId, data);
            });
        const auto playInterfaceSound =
            [&](int resourceId) {
            SoundTimer timer{sceKernelGetProcessTimeWide(), soundUs, soundMaxUs, "interface",
                             std::to_string(resourceId)};
            if (audio.playEffectById(resourceId, true)) return;
            std::vector<uint8_t> data;
            if (!assets.readSoundResource(resourceId, data)) {
                logf("interface sound %d not found", resourceId);
                return;
            }
            audio.playEffect(resourceId, data);
        };
        game.setInterfaceSoundPlayer(
            playInterfaceSound);
        frontend.setSoundPlayer(
            playInterfaceSound);
        uint32_t unitSoundChoice = 0;
        game.setUnitSoundPlayer([&](int soundId, int civilization) {
            SoundTimer timer{sceKernelGetProcessTimeWide(), soundUs, soundMaxUs, "unit",
                             std::to_string(soundId)};
            int resourceId = -1;
            std::string fileName;
            // Choose the clip exactly as readSound would, then let the audio
            // worker read and decode it if it is not cached yet.
            if (!assets.selectSound(soundId, civilization, unitSoundChoice++,
                                    &resourceId, &fileName)) {
                logf("unit sound %d not found", soundId);
                return;
            }
            if (audio.playEffectById(resourceId, false)) return;
            std::shared_ptr<const std::vector<uint8_t>> data;
            if (!assets.readSoundShared(soundId, civilization, unitSoundChoice - 1, data,
                                        &resourceId, &fileName) ||
                !audio.playEffect(resourceId, data))
                logf("unit sound %s (%d) could not play", fileName.c_str(), resourceId);
        });
        game.setOiiaSoundPlayer(
            [&audio]() {
                audio.playOiiaEffect();
            });
        bool campaignMatch = false;
        swgb::Scenario campaignScenario;
        swgb::Scenario playtestScenario;
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
            logf(
                "campaign load begin: %s entry %u",
                mission->archiveName.c_str(),
                (unsigned)mission->entry + 1);
            const uint64_t campaignLoadStart =
                sceKernelGetProcessTimeWide();
            if (!catalog.loadScenario(
                    frontend.selectedCampaign(),
                    frontend.selectedMission(),
                    campaignScenario, &err)) {
                logf(
                    "campaign archive load failed: %s",
                    err.c_str());
                return false;
            }
            logf(
                "campaign archive decoded in %llu ms: %ux%u, %u units, %u triggers",
                (unsigned long long)(
                    (sceKernelGetProcessTimeWide() -
                     campaignLoadStart) /
                    1000),
                (unsigned)campaignScenario.map.width,
                (unsigned)campaignScenario.map.height,
                (unsigned)campaignScenario.units.size(),
                (unsigned)campaignScenario.triggers.size());
            const uint64_t scenarioInitStart =
                sceKernelGetProcessTimeWide();
            if (!game.initScenario(
                    campaignScenario, &err,
                    mission->archiveName,
                    mission->entry,
                    campaignProfile.difficulty,
                    "ux0:data/swgb/AI")) {
                logf(
                    "campaign simulation initialization failed after %llu ms: %s",
                    (unsigned long long)(
                        (sceKernelGetProcessTimeWide() -
                         scenarioInitStart) /
                        1000),
                    err.c_str());
                return false;
            }
            campaignMatch = true;
            frontend.setCampaignMatch(true);
            {
                // Read the mission's voice lines in the background.
                std::vector<std::string> voices;
                for (const swgb::ScenarioTrigger &trigger : campaignScenario.triggers)
                    for (const swgb::ScenarioEffect &effect : trigger.effects)
                        if (!effect.sound.empty()) voices.push_back(effect.sound);
                audio.prefetchVoices(voices);
                logf("prefetching %u voice clips", (unsigned)voices.size());
            }
            logf(
                "campaign %s entry %u loaded in %llu ms: %s, %ux%u, %u units",
                mission->archiveName.c_str(),
                (unsigned)mission->entry + 1,
                (unsigned long long)(
                    (sceKernelGetProcessTimeWide() -
                     campaignLoadStart) /
                    1000),
                campaignScenario.originalFilename.c_str(),
                (unsigned)campaignScenario.map.width,
                (unsigned)campaignScenario.map.height,
                (unsigned)campaignScenario.units.size());
            return true;
        };
        const auto startEditorPlaytest = [&]() {
            audio.resetSession();
            err.clear();
            if (!editor.buildPlaytestScenario(
                    playtestScenario, &err))
                return false;
            if (!game.initScenario(
                    playtestScenario, &err,
                    std::string(), 0,
                    editor.playtestDifficulty(),
                    "ux0:data/swgb/AI"))
                return false;
            campaignMatch = false;
            frontend.setCampaignMatch(false);
            frontend.setPlaytestMatch(true);
            frontend.setPlaytestObjectives(
                editor.document().messages.objectives.empty()
                    ? editor.document()
                          .messages.instructions
                    : editor.document()
                          .messages.objectives);
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
        swgb::UpdateStats updateTotals{};
        swgb::UpdateStats maxUpdateStats{};
        swgb::RenderStats renderTotals{};
        swgb::RenderStats maxRenderStats{};
        uint64_t maxUpdateUs = 0;
        uint64_t maxRenderUs = 0;
        uint32_t gameplayUpdates = 0;
        uint32_t matchRenders = 0;
        size_t lastBuilds = 0;
        uint64_t statT = last;
        int frames = 0;
        swgb::FrontendScreen previousFrontendScreen =
            frontend.screen();
        // Development autostart (ux0:data/swgb/autostart.txt) for unattended
        // runs on hardware or in Vita3K:
        //   campaign <archive substring> <scenario entry>   e.g. "campaign XCAM3 2"
        //   exit_after <seconds of gameplay>                quit after logging
        std::string autostartArchive;
        int autostartEntry = -1;
        int autostartSimRate = -1;
        bool autostartScene = false;
        float autostartQuitAfter = 0.0f; // wall seconds after start-up (menu tests)
        float autostartExitAfter = 0.0f;
        // achievements 1: at exit_after, show each Achievements tab for 1.5 s
        // first.
        bool autostartAchievements = false;
        uint64_t autostartAchievementsStart = 0;
        size_t autostartAchievementsTab = SIZE_MAX;
        int autostartBattle = 0, autostartBattleEnemy = 5;
        bool autostartPending = false;
        uint64_t autostartGameplayStart = 0;
        bool autostartLaunched = false;
        if (FILE *autostart = fopen("ux0:data/swgb/autostart.txt", "r")) {
            char key[64] = {}, value[128] = {};
            int number = 0;
            char line[256];
            while (fgets(line, sizeof(line), autostart)) {
                //   battle <units per side> <enemy player>          spawn a test fight
                if (sscanf(line, "battle %d %d", &autostartBattle, &autostartBattleEnemy) >= 1)
                    continue;
                if (sscanf(line, "campaign %127s %d", value, &number) == 2) {
                    autostartArchive = value;
                    autostartEntry = number;
                    autostartPending = true;
                } else if (sscanf(line, "%63s %127s", key, value) == 2) {
                    if (std::string(key) == "exit_after")
                        autostartExitAfter = (float)atof(value);
                    else if (std::string(key) == "simrate")
                        autostartSimRate = atoi(value);
                    else if (std::string(key) == "scene")
                        autostartScene = atoi(value) != 0;
                    else if (std::string(key) == "quit_after")
                        autostartQuitAfter = (float)atof(value);
                    else if (std::string(key) == "achievements")
                        autostartAchievements = atoi(value) != 0;
                }
            }
            fclose(autostart);
            logf("autostart: campaign %s entry %d exit_after %.0f s",
                 autostartArchive.c_str(), autostartEntry, autostartExitAfter);
        }
        // Simulation rate: fixed steps per second (ux0:data/swgb/simrate.txt,
        // or "simrate N" in autostart.txt); 0 = one variable step per frame
        // as before.
        int simHz = 15;
        if (FILE *rate = fopen("ux0:data/swgb/simrate.txt", "r")) {
            int value = -1;
            if (fscanf(rate, "%d", &value) == 1 && value >= 0 && value <= 60) simHz = value;
            fclose(rate);
        }
        if (autostartSimRate >= 0 && autostartSimRate <= 60) simHz = autostartSimRate;
        float simAccumulator = 0.0f;
        int simChunk = 0;                 // next part of the step in progress
        uint64_t simStepUs = 0;
        uint64_t simChunkUs[3] = {3000, 12000, 9000};
        // Frame pacing: whole frames of 1/fpsCap s (ux0:data/swgb/fpscap.txt,
        // default 30; 0 = as fast as vsync allows); the simulation gets what
        // the frame has left after drawing.
        int fpsCap = 30;
        if (FILE *cap = fopen("ux0:data/swgb/fpscap.txt", "r")) {
            int value = -1;
            if (fscanf(cap, "%d", &value) == 1 && value >= 0 && value <= 60) fpsCap = value;
            fclose(cap);
        }
        uint64_t simBudgetUs = fpsCap > 0 ? std::max<int64_t>(6000, 1000000 / fpsCap - 19000) : 12000;
        uint64_t frameStartUs = sceKernelGetProcessTimeWide();
        logf("frame pacing: %d fps cap, simulation budget %llu us", fpsCap,
             (unsigned long long)simBudgetUs);
        logf("simulation: %s", simHz > 0 ? (std::to_string(simHz) + " Hz fixed steps").c_str()
                                          : "variable step per frame");
        std::vector<std::string> campaignNarration;
        size_t campaignNarrationIndex = 0;
        float campaignNarrationRemaining = 0;

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

            swgb::FrontendAction action =
                swgb::FrontendAction::None;
            swgb::EditorAction editorAction =
                swgb::EditorAction::None;
            if (frontend.screen() ==
                swgb::FrontendScreen::ScenarioEditor)
                editorAction = editor.update(dt, in);
            else
                action = frontend.update(
                    in, game.victoryStateForTesting());
            if (autostartPending &&
                frontend.screen() == swgb::FrontendScreen::Title)
                frontend.showMainMenu();
            if (autostartPending &&
                frontend.screen() == swgb::FrontendScreen::MainMenu) {
                autostartPending = false;
                bool found = false;
                for (size_t c = 0; c < catalog.campaigns().size() && !found; ++c) {
                    const swgb::CampaignInfo &campaign = catalog.campaigns()[c];
                    if (campaign.archiveName.find(autostartArchive) == std::string::npos)
                        continue;
                    for (size_t m = 0; m < campaign.missions.size(); ++m)
                        if ((int)campaign.missions[m].entry == autostartEntry) {
                            frontend.selectCampaignMission(c, m);
                            found = true;
                            break;
                        }
                }
                logf("autostart: mission %s", found ? "found" : "NOT FOUND");
                if (found && autostartScene) {
                    // Through the mission's opening scene, which starts it.
                    frontend.showScreenForTesting(swgb::FrontendScreen::CampaignBriefing, 0);
                    autostartLaunched = true;
                } else if (found) {
                    action = swgb::FrontendAction::StartCampaign;
                    autostartLaunched = true;
                }
            }
            // Skip the opening objectives pane so the mission runs unattended.
            if (autostartLaunched && !autostartGameplayStart &&
                frontend.screen() == swgb::FrontendScreen::Objectives) {
                logf("autostart: dismissing objectives");
                frontend.showGameplay();
            }
            if (g_quitRequested) {
                logf("remote quit requested");
                flushLog();
                sceKernelExitProcess(0);
            }
            if (autostartQuitAfter > 0.0f && now / 1e6f >= autostartQuitAfter) {
                logf("autostart: quitting after %.0f s", autostartQuitAfter);
                flushLog();
                sceKernelExitProcess(0);
            }
            if (autostartExitAfter > 0.0f &&
                frontend.screen() == swgb::FrontendScreen::Gameplay) {
                if (!autostartGameplayStart && autostartBattle > 0) {
                    // Performance test fight at the map centre (dev only).
                    const float centre = game.mapSizeForTesting() * 0.5f;
                    int spawned = 0;
                    for (int side = 0; side < 2; side++) {
                        const int player = side == 0 ? 1 : autostartBattleEnemy;
                        const int civ = game.civilizationForPlayerForTesting(player);
                        for (int i = 0; i < autostartBattle; i++) {
                            const float x = centre + (side == 0 ? -3.0f : 3.0f) +
                                            (i / 8) * (side == 0 ? -0.8f : 0.8f);
                            const float y = centre - 3.0f + (i % 8) * 0.8f;
                            if (game.spawnObjectForTesting(civ, 460, player, x, y)) spawned++;
                        }
                    }
                    game.lookAt(centre, centre);
                    logf("autostart: battle spawned %d units vs player %d", spawned, autostartBattleEnemy);
                }
                if (!autostartGameplayStart) autostartGameplayStart = now;
                if ((now - autostartGameplayStart) / 1e6f >= autostartExitAfter) {
                    if (autostartAchievements && !autostartAchievementsStart) {
                        autostartAchievementsStart = now;
                    } else {
                        logf("autostart: exiting after %.0f s of gameplay", autostartExitAfter);
                        if (g_log) fflush(g_log);
                        sceKernelExitProcess(0);
                    }
                }
            }
            if (autostartAchievementsStart) {
                // Six Achievements tabs, then the Diplomacy dialog and a taunt
                // (taunt 39) sent to everyone.
                const size_t tab = (size_t)((now - autostartAchievementsStart) / 1500000ull);
                if (tab >= 9) {
                    logf("autostart: achievements shown, exiting");
                    if (g_log) fflush(g_log);
                    sceKernelExitProcess(0);
                }
                if (tab != autostartAchievementsTab) {
                    autostartAchievementsTab = tab;
                    if (tab < 6) {
                        frontend.showAchievementsForTesting(tab);
                        logf("autostart: achievements tab %u", (unsigned)tab);
                    } else if (tab == 6) {
                        std::array<const swgb::SpriteFrame *, 4> icons{};
                        for (size_t i = 0; i < icons.size(); ++i)
                            icons[i] = assets.interfaceFrame(50732, i, 50500);
                        frontend.setDiplomacyArt(assets.interfaceFrame(50221, 0, 50500), icons);
                        frontend.setDiplomacy(game.diplomacyData());
                        frontend.showDiplomacyForTesting();
                        logf("autostart: diplomacy dialog, %u rows",
                             (unsigned)game.diplomacyData().rows.size());
                    } else if (tab == 7) {
                        std::vector<int> everyone;
                        for (const swgb::DiplomacyRow &row : game.diplomacyData().rows)
                            if (!row.local) everyone.push_back(row.player);
                        game.sendChat(game.localPlayerForTesting(), everyone, "39");
                        logf("autostart: sent taunt 39");
                    }
                }
            }
            const swgb::FrontendScreen currentFrontendScreen =
                frontend.screen();
            // Music by screen (see VitaAudio::setMusic).
            {
                using Screen = swgb::FrontendScreen;
                swgb::VitaAudio::Music music = swgb::VitaAudio::Music::Menu;
                static bool matchMusic = false;
                if (currentFrontendScreen == Screen::Gameplay ||
                    currentFrontendScreen == Screen::Objectives ||
                    currentFrontendScreen == Screen::Loading ||
                    currentFrontendScreen == Screen::ScenarioEditor)
                    matchMusic = true;
                else if (currentFrontendScreen != Screen::Pause &&
                         currentFrontendScreen != Screen::Options &&
                         currentFrontendScreen != Screen::Diplomacy &&
                         currentFrontendScreen != Screen::Chat &&
                         currentFrontendScreen != Screen::Confirm)
                    matchMusic = false; // the in-game menus keep the game's music
                if (matchMusic)
                    music = swgb::VitaAudio::Music::Game;
                else if (currentFrontendScreen == Screen::CampaignBriefing ||
                         currentFrontendScreen == Screen::CampaignEpilogue)
                    music = swgb::VitaAudio::Music::None; // the scene's narration
                else if (currentFrontendScreen == Screen::Outcome ||
                         currentFrontendScreen == Screen::Achievements)
                    music = game.victoryStateForTesting() == 1 ? swgb::VitaAudio::Music::Victory
                                                               : swgb::VitaAudio::Music::Defeat;
                audio.setMusic(music);
            }
            if (currentFrontendScreen !=
                previousFrontendScreen) {
                if (currentFrontendScreen == swgb::FrontendScreen::Achievements) {
                    // Tabs (sat_tabs 50765) and name banners (PNBnr1 50762),
                    // and this game's statistics.
                    std::array<const swgb::SpriteFrame *, 12> tabs{};
                    std::array<const swgb::SpriteFrame *, 8> banners{};
                    for (size_t i = 0; i < tabs.size(); ++i)
                        tabs[i] = assets.interfaceFrame(50765, i, 50531);
                    for (size_t i = 0; i < banners.size(); ++i)
                        banners[i] = assets.interfaceFrame(50762, i, 50531);
                    frontend.setAchievementsArt(tabs, banners);
                    swgb::AchievementsData data;
                    data.players = game.achievementsPlayers();
                    data.elapsedSeconds = game.elapsedGameTime();
                    for (const swgb::AchievementsPlayer &row : data.players)
                        logf("achievements: player %d '%s' score %d", row.player,
                             row.name.c_str(), row.total);
                    frontend.setAchievements(std::move(data));
                } else if (previousFrontendScreen == swgb::FrontendScreen::Achievements) {
                    frontend.setAchievementsArt({}, {});
                    for (size_t i = 0; i < 12; ++i) assets.releaseInterfaceFrame(50765, i, 50531);
                    for (size_t i = 0; i < 8; ++i) assets.releaseInterfaceFrame(50762, i, 50531);
                }
                if (currentFrontendScreen ==
                    swgb::FrontendScreen::CampaignBriefing) {
                    audio.resetSession();
                    std::string sceneError;
                    if (campaignScene.load(assets, std::string(kRoot) + "/Campaign/Media",
                                           campaignThemeNumber(),
                                           (int)frontend.selectedMission() + 1, true,
                                           &sceneError)) {
                        // The scene's SND lines start the narration on time;
                        // read the files ahead on the audio worker.
                        campaignNarration.clear();
                        audio.prefetchVoices(campaignScene.soundNames());
                        logf("campaign scene: campaign %d mission %d loaded",
                             campaignThemeNumber(), (int)frontend.selectedMission() + 1);
                    } else {
                        logf("campaign scene unavailable (%s); narration only",
                             sceneError.c_str());
                        const std::string prefix =
                            campaignNarrationPrefix(
                                frontend.selectedCampaignMission());
                        campaignNarration =
                            audio.campaignBriefing(prefix);
                    }
                    campaignNarrationIndex = 0;
                    campaignNarrationRemaining = 0;
                } else if (currentFrontendScreen ==
                           swgb::FrontendScreen::CampaignEpilogue) {
                    // The won mission's closing scene (xc<c>s<m>_end.mm).
                    audio.resetSession();
                    campaignNarration.clear();
                    std::string sceneError;
                    if (campaignScene.load(assets, std::string(kRoot) + "/Campaign/Media",
                                           campaignThemeNumber(),
                                           (int)frontend.selectedMission() + 1, false,
                                           &sceneError)) {
                        audio.prefetchVoices(campaignScene.soundNames());
                        logf("campaign closing scene: campaign %d mission %d loaded",
                             campaignThemeNumber(), (int)frontend.selectedMission() + 1);
                    } else {
                        logf("campaign closing scene unavailable (%s)", sceneError.c_str());
                    }
                } else if (previousFrontendScreen ==
                               swgb::FrontendScreen::CampaignBriefing ||
                           previousFrontendScreen ==
                               swgb::FrontendScreen::CampaignEpilogue) {
                    audio.resetSession();
                    campaignNarration.clear();
                    campaignScene.release(assets);
                }
                previousFrontendScreen =
                    currentFrontendScreen;
            }
            if ((currentFrontendScreen ==
                     swgb::FrontendScreen::CampaignBriefing ||
                 currentFrontendScreen == swgb::FrontendScreen::CampaignEpilogue) &&
                campaignScene.loaded()) {
                campaignScene.advance(dt);
                for (const std::string &sound : campaignScene.takeDueSounds())
                    audio.play(sound);
            }
            if (currentFrontendScreen ==
                    swgb::FrontendScreen::CampaignBriefing &&
                campaignNarrationIndex <
                    campaignNarration.size()) {
                campaignNarrationRemaining -= dt;
                if (campaignNarrationRemaining <= 0) {
                    campaignNarrationRemaining =
                        audio.play(
                            campaignNarration[
                                campaignNarrationIndex++]);
                    if (campaignNarrationRemaining <= 0)
                        campaignNarrationRemaining = 0.1f;
                }
            }
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
                swgb::FrontendAction::OpenScenarioEditor) {
                audio.resetSession();
                game.clearMatch();
                frontend.setPlaytestMatch(false);
                editor.enter();
            } else if (
                editorAction ==
                swgb::EditorAction::Close) {
                frontend.showMainMenu();
                frontend.setPlaytestMatch(false);
            } else if (
                editorAction ==
                swgb::EditorAction::Playtest) {
                editor.render(
                    renderer, kScreenW, kScreenH);
                vglSwapBuffers(GL_FALSE);
                const bool started =
                    startEditorPlaytest();
                if (started) {
                    frontend.showGameplay();
                } else {
                    editor.playtestFinished(
                        false,
                        "PLAYTEST FAILED: " + err);
                    frontend.showEditor();
                    game.clearMatch();
                }
                touching = false;
                touchMoved = false;
                touchBox = false;
                stickBoxArmed = false;
                stickBoxMoved = false;
            } else if (action ==
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
                        frontend.playtestMatch()
                            ? startEditorPlaytest()
                        : campaignMatch
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
            } else if (action == swgb::FrontendAction::OpenDiplomacy) {
                // dlg_dip 50221 and the tribute icons tradicon 50732.
                std::array<const swgb::SpriteFrame *, 4> icons{};
                for (size_t i = 0; i < icons.size(); ++i)
                    icons[i] = assets.interfaceFrame(50732, i, 50500);
                frontend.setDiplomacyArt(assets.interfaceFrame(50221, 0, 50500), icons);
                frontend.setDiplomacy(game.diplomacyData());
            } else if (action == swgb::FrontendAction::OpenChat) {
                // Other players by name, and the taunt list (Taunt/taunts.txt:
                // "number|text" lines).
                std::vector<std::pair<int, std::string>> players;
                for (const swgb::DiplomacyRow &row : game.diplomacyData().rows)
                    if (!row.local && !row.defeated) players.push_back({row.player, row.name});
                frontend.setChatPlayers(std::move(players));
                static std::vector<std::pair<int, std::string>> taunts;
                if (taunts.empty())
                    if (FILE *file = fopen((std::string(kRoot) + "/Taunt/taunts.txt").c_str(), "r")) {
                        char line[256];
                        while (fgets(line, sizeof line, file)) {
                            char *bar = strchr(line, '|');
                            if (!bar) continue;
                            *bar = 0;
                            std::string name = bar + 1;
                            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
                                name.pop_back();
                            taunts.push_back({atoi(line), name});
                        }
                        fclose(file);
                    }
                if (taunts.empty())
                    for (int number = 1; number <= 42; ++number) taunts.push_back({number, ""});
                frontend.setTaunts(taunts);
            } else if (action == swgb::FrontendAction::SendChat) {
                const int local = game.localPlayerForTesting();
                const int to = frontend.chatRecipient();
                std::vector<int> recipients;
                for (const swgb::DiplomacyRow &row : game.diplomacyData().rows) {
                    if (row.local || row.defeated) continue;
                    if (to == 0 || (to == 1 && row.ourStance == 0) || to == row.player)
                        recipients.push_back(row.player);
                }
                game.sendChat(local, recipients, std::to_string(frontend.chatTaunt()));
            } else if (action == swgb::FrontendAction::ApplyDiplomacy) {
                const swgb::DiplomacyResult &result = frontend.diplomacyResult();
                const int local = game.localPlayerForTesting();
                for (const auto &[player, stance] : result.stances)
                    game.setStance(local, player, stance);
                const float fee = game.diplomacyData().fee;
                for (const auto &[player, amounts] : result.tributes)
                    for (int type = 0; type < 4; ++type)
                        if (amounts[(size_t)type] > 0)
                            game.payTribute(local, player, type, (float)amounts[(size_t)type], fee);
                game.setAlliedVictory(local, result.alliedVictory);
                logf("diplomacy: %u stance changes, %u tributes", (unsigned)result.stances.size(),
                     (unsigned)result.tributes.size());
            } else if (
                action ==
                swgb::FrontendAction::SaveMatch) {
                err.clear();
                const bool saved =
                    game.saveMatch(
                        frontend.playtestMatch()
                            ? kPlaytestSavePath
                            : kSavePath,
                        &err);
                frontend.actionFinished(
                    action, saved,
                    saved ? std::string() : err);
                if (saved &&
                    !frontend.playtestMatch())
                    frontend.setContinueAvailable(true);
            } else if (
                action ==
                    swgb::FrontendAction::ReturnToEditor) {
                audio.resetSession();
                game.clearMatch();
                editor.playtestFinished(
                    false,
                    "PLAYTEST RETURNED - EDITS PRESERVED");
                frontend.showEditor();
                unitSoundChoice = 0;
                touching = false;
                touchMoved = false;
                touchBox = false;
                stickBoxArmed = false;
                stickBoxMoved = false;
            } else if (
                action ==
                    swgb::FrontendAction::ReturnToMainMenu ||
                action ==
                    swgb::FrontendAction::ReturnToCampaignBrowser) {
                audio.resetSession();
                game.clearMatch();
                if (frontend.playtestMatch()) {
                    editor.playtestFinished(
                        true,
                        "PLAYTEST ENDED - EDITS PRESERVED");
                    frontend.setPlaytestMatch(false);
                }
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
            refreshOriginalFrontendArt();
            const uint64_t t0 = sceKernelGetProcessTimeWide();
            const bool renderMatch =
                frontend.screen() ==
                    swgb::FrontendScreen::Gameplay ||
                frontend.screen() ==
                    swgb::FrontendScreen::Objectives ||
                frontend.screen() == swgb::FrontendScreen::Diplomacy ||
                frontend.screen() == swgb::FrontendScreen::Chat;
            // Simulation: either one variable step per frame (simrate 0, the
            // original update), or fixed steps of 1/simHz with input handled
            // every frame and moving objects drawn between steps.
            auto recordUpdate = [&](uint64_t stepFrom, uint64_t stepTo) {
                const swgb::UpdateStats &update =
                    game.updateStats();
                updateTotals.inputUs +=
                    update.inputUs;
                updateTotals.triggersUs +=
                    update.triggersUs;
                updateTotals.aiUs += update.aiUs;
                updateTotals.worldUs +=
                    update.worldUs;
                updateTotals.worldProductionUs +=
                    update.worldProductionUs;
                updateTotals.worldConstructionUs +=
                    update.worldConstructionUs;
                updateTotals.worldShieldsUs +=
                    update.worldShieldsUs;
                updateTotals.worldOccupancyUs +=
                    update.worldOccupancyUs;
                updateTotals.worldWorkersUs +=
                    update.worldWorkersUs;
                updateTotals.worldMaintenanceUs +=
                    update.worldMaintenanceUs;
                updateTotals.worldLivestockUs +=
                    update.worldLivestockUs;
                updateTotals.worldGatheringUs +=
                    update.worldGatheringUs;
                updateTotals.worldRepairingUs +=
                    update.worldRepairingUs;
                updateTotals.worldConversionUs +=
                    update.worldConversionUs;
                updateTotals.worldHolocronsUs +=
                    update.worldHolocronsUs;
                updateTotals.movementUs +=
                    update.movementUs;
                updateTotals.finalUs +=
                    update.finalUs;
                gameplayUpdates++;
                if (stepTo - stepFrom > 80000)
                    logf("spike update %llu us: in/trg/ai/world/move/final=%llu/%llu/%llu/%llu/%llu/%llu",
                         (unsigned long long)(stepTo - stepFrom), (unsigned long long)update.inputUs,
                         (unsigned long long)update.triggersUs, (unsigned long long)update.aiUs,
                         (unsigned long long)update.worldUs, (unsigned long long)update.movementUs,
                         (unsigned long long)update.finalUs);
                if (stepTo - stepFrom > maxUpdateUs) {
                    maxUpdateUs = stepTo - stepFrom;
                    maxUpdateStats = update;
                }
                        };
            if (frontend.screen() ==
                swgb::FrontendScreen::Gameplay) {
                if (simHz <= 0) {
                    game.update(dt, in);
                    recordUpdate(t0, sceKernelGetProcessTimeWide());
                } else {
                    // Steps run in parts (Game::simulatePart) within a time
                    // budget per frame, so the frame time stays even; the
                    // moving objects are drawn between the last two steps.
                    const float simStep = 1.0f / simHz;
                    game.updateInput(dt, in);
                    simAccumulator += dt;
                    const uint64_t budgetStart = sceKernelGetProcessTimeWide();
                    for (;;) {
                        if (simChunk == 0) {
                            if (simAccumulator < simStep || !game.simulationActive()) break;
                            simAccumulator -= simStep;
                            game.recordPreviousPositions();
                            simStepUs = 0;
                        }
                        const uint64_t partStart = sceKernelGetProcessTimeWide();
                        game.simulatePart(simStep, simChunk);
                        const uint64_t partEnd = sceKernelGetProcessTimeWide();
                        simStepUs += partEnd - partStart;
                        simChunkUs[simChunk] = (simChunkUs[simChunk] * 3 + (partEnd - partStart)) / 4;
                        simChunk = (simChunk + 1) % swgb::Game::kSimulationChunks;
                        if (simChunk == 0) recordUpdate(0, simStepUs);
                        // Next part only if it fits in this frame's budget.
                        const uint64_t spent = partEnd - budgetStart;
                        if (spent + simChunkUs[simChunk] > simBudgetUs) break;
                    }
                    // Far behind (a long hitch): slow the game down rather
                    // than catch up in a burst.
                    if (simAccumulator > 2.0f * simStep) simAccumulator = 2.0f * simStep;
                    game.setRenderInterpolation(
                        simChunk == 0 ? std::min(1.0f, simAccumulator / simStep) : 1.0f);
                }
            } else {
                simAccumulator = 0.0f;
                // A new match starts with a whole step (a paused match keeps
                // its step in progress).
                if (frontend.screen() == swgb::FrontendScreen::Loading) simChunk = 0;
            }
            const uint64_t t1 = sceKernelGetProcessTimeWide();
            if (renderMatch) {
                game.render(
                    renderer, kScreenW, kScreenH);
                if (frontend.screen() ==
                    swgb::FrontendScreen::Objectives)
                    frontend.renderObjectivesOverlay(
                        renderer, kScreenW,
                        kScreenH);
                else if (frontend.screen() == swgb::FrontendScreen::Diplomacy)
                    frontend.renderDiplomacyOverlay(renderer, kScreenW, kScreenH);
                else if (frontend.screen() == swgb::FrontendScreen::Chat)
                    frontend.renderChatOverlay(renderer, kScreenW, kScreenH);
            } else if (frontend.screen() ==
                     swgb::FrontendScreen::ScenarioEditor)
                editor.render(
                    renderer, kScreenW, kScreenH);
            else
                frontend.render(
                    renderer, kScreenW, kScreenH);
            const uint64_t t2 = sceKernelGetProcessTimeWide();
            if (renderMatch) {
                const swgb::RenderStats &render =
                    game.renderStats();
                renderTotals.prepareTerrainUs +=
                    render.prepareTerrainUs;
                renderTotals.prepareObjectsUs +=
                    render.prepareObjectsUs;
                renderTotals.beginFrameUs +=
                    render.beginFrameUs;
                renderTotals.drawTerrainUs +=
                    render.drawTerrainUs;
                renderTotals.drawWorldUs +=
                    render.drawWorldUs;
                renderTotals.drawUiUs +=
                    render.drawUiUs;
                renderTotals.endFrameUs +=
                    render.endFrameUs;
                matchRenders++;
                if (t2 - t1 > maxRenderUs) {
                    maxRenderUs = t2 - t1;
                    maxRenderStats = render;
                }
            }
            if (fpsCap > 0 && simHz > 0 && renderMatch) {
                // Hold the frame to 1/fpsCap: sleep until just before that
                // vblank, then the swap lands on it.
                const uint64_t period = 1000000 / fpsCap;
                const uint64_t due = frameStartUs + period - 2500;
                const uint64_t nowUs = sceKernelGetProcessTimeWide();
                if (nowUs < due) sceKernelDelayThread((SceUInt)(due - nowUs));
            }
            vglSwapBuffers(GL_FALSE);
            const uint64_t t3 = sceKernelGetProcessTimeWide();
            frameStartUs = t3;
            updateUs += t1 - t0;
            renderUs += t2 - t1;
            swapUs += t3 - t2;
            frames++;
            if (now - statT >= 5000000) {
                const uint64_t updateDivisor =
                    std::max<uint32_t>(
                        1, gameplayUpdates);
                const uint64_t renderDivisor =
                    std::max<uint32_t>(
                        1, matchRenders);
                logf("fps=%.1f ms upd/rnd/swap=%.1f/%.1f/%.1f phase-avg-us=%llu/%llu/%llu/%llu/%llu/%llu max-upd-us=%llu max-phase-us=%llu/%llu/%llu/%llu/%llu/%llu max-ai=%llu/%llu/%llu/%llu/%llu/%llu p%d:%llu max-final=%llu/%llu/%llu/%llu/%llu draws=%d quads=%d sprites=%d vis=%d sheets=%u tex=%.1fMB "
                     "builds=%u free vram/ram/phy=%.1f/%.1f/%.1fMB menu=%d sel=%u zoom=%.2f",
                     frames * 1e6 / (double)(now - statT),
                     frames ? updateUs / 1000.0 / frames : 0.0, frames ? renderUs / 1000.0 / frames : 0.0,
                     frames ? swapUs / 1000.0 / frames : 0.0,
                     (unsigned long long)(updateTotals.inputUs / updateDivisor),
                     (unsigned long long)(updateTotals.triggersUs / updateDivisor),
                     (unsigned long long)(updateTotals.aiUs / updateDivisor),
                     (unsigned long long)(updateTotals.worldUs / updateDivisor),
                     (unsigned long long)(updateTotals.movementUs / updateDivisor),
                     (unsigned long long)(updateTotals.finalUs / updateDivisor),
                     (unsigned long long)maxUpdateUs,
                     (unsigned long long)maxUpdateStats.inputUs,
                     (unsigned long long)maxUpdateStats.triggersUs,
                     (unsigned long long)maxUpdateStats.aiUs,
                     (unsigned long long)maxUpdateStats.worldUs,
                     (unsigned long long)maxUpdateStats.movementUs,
                     (unsigned long long)maxUpdateStats.finalUs,
                     (unsigned long long)maxUpdateStats.aiRulesUs,
                     (unsigned long long)maxUpdateStats.aiEconomyUs,
                     (unsigned long long)maxUpdateStats.aiDefenseUs,
                     (unsigned long long)maxUpdateStats.aiStrategyUs,
                     (unsigned long long)maxUpdateStats.aiMilitaryUs,
                     (unsigned long long)maxUpdateStats.aiScoutingUs,
                     maxUpdateStats.aiMaxPlayer,
                     (unsigned long long)maxUpdateStats.aiMaxPlayerUs,
                     (unsigned long long)maxUpdateStats.finalGarrisonUs,
                     (unsigned long long)maxUpdateStats.finalProjectilesUs,
                     (unsigned long long)maxUpdateStats.finalRemainsUs,
                     (unsigned long long)maxUpdateStats.finalVisibilityUs,
                     (unsigned long long)maxUpdateStats.finalVictoryUs,
                     renderer.drawCalls(), renderer.quads(),
                     game.stats().sprites,
                     game.stats().visibilityChecks,
                     (unsigned)assets.sheetCount(),
                     assets.textureBytes() / 1048576.0,
                     (unsigned)(assets.buildCount() - lastBuilds),
                     vglMemFree(VGL_MEM_VRAM) / 1048576.0, vglMemFree(VGL_MEM_RAM) / 1048576.0,
                     vglMemFree(VGL_MEM_PHYCONT) / 1048576.0, (int)game.actionMenuOpenForTesting(),
                     (unsigned)game.selectedObjectIds().size(), game.zoom());
                {
                    const auto build = assets.takeBuildTime();
                    logf("audio worker: %s", audio.workerState().c_str());
                    const swgb::Game::PathStats paths = game.takePathStats();
                    logf("sheet-build-us=%llu/%llu sound-us=%llu/%llu path searches=%u/%llu us grid-builds=%u/%llu us "
                         "(signature/blocked/tables/finder %llu/%llu/%llu/%llu)",
                         (unsigned long long)build.first, (unsigned long long)build.second,
                         (unsigned long long)soundUs, (unsigned long long)soundMaxUs,
                         paths.searches, (unsigned long long)paths.findUs, paths.builds,
                         (unsigned long long)paths.buildUs, (unsigned long long)paths.phaseUs[0],
                         (unsigned long long)paths.phaseUs[1], (unsigned long long)paths.phaseUs[2],
                         (unsigned long long)paths.phaseUs[3]);
                    soundUs = soundMaxUs = 0;
                }
                logf(
                    "world-phase-avg-us=%llu/%llu/%llu/%llu/%llu/%llu max-world-phase-us=%llu/%llu/%llu/%llu/%llu/%llu",
                    (unsigned long long)(updateTotals.worldProductionUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldConstructionUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldShieldsUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldOccupancyUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldWorkersUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldMaintenanceUs / updateDivisor),
                    (unsigned long long)maxUpdateStats.worldProductionUs,
                    (unsigned long long)maxUpdateStats.worldConstructionUs,
                    (unsigned long long)maxUpdateStats.worldShieldsUs,
                    (unsigned long long)maxUpdateStats.worldOccupancyUs,
                    (unsigned long long)maxUpdateStats.worldWorkersUs,
                    (unsigned long long)maxUpdateStats.worldMaintenanceUs);
                logf(
                    "worker-phase-avg-us=%llu/%llu/%llu/%llu/%llu max-worker-phase-us=%llu/%llu/%llu/%llu/%llu",
                    (unsigned long long)(updateTotals.worldLivestockUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldGatheringUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldRepairingUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldConversionUs / updateDivisor),
                    (unsigned long long)(updateTotals.worldHolocronsUs / updateDivisor),
                    (unsigned long long)maxUpdateStats.worldLivestockUs,
                    (unsigned long long)maxUpdateStats.worldGatheringUs,
                    (unsigned long long)maxUpdateStats.worldRepairingUs,
                    (unsigned long long)maxUpdateStats.worldConversionUs,
                    (unsigned long long)maxUpdateStats.worldHolocronsUs);
                logf(
                    "render-phase-avg-us=%llu/%llu/%llu/%llu/%llu/%llu/%llu max-render-us=%llu max-render-phase-us=%llu/%llu/%llu/%llu/%llu/%llu/%llu",
                    (unsigned long long)(renderTotals.prepareTerrainUs / renderDivisor),
                    (unsigned long long)(renderTotals.prepareObjectsUs / renderDivisor),
                    (unsigned long long)(renderTotals.beginFrameUs / renderDivisor),
                    (unsigned long long)(renderTotals.drawTerrainUs / renderDivisor),
                    (unsigned long long)(renderTotals.drawWorldUs / renderDivisor),
                    (unsigned long long)(renderTotals.drawUiUs / renderDivisor),
                    (unsigned long long)(renderTotals.endFrameUs / renderDivisor),
                    (unsigned long long)maxRenderUs,
                    (unsigned long long)maxRenderStats.prepareTerrainUs,
                    (unsigned long long)maxRenderStats.prepareObjectsUs,
                    (unsigned long long)maxRenderStats.beginFrameUs,
                    (unsigned long long)maxRenderStats.drawTerrainUs,
                    (unsigned long long)maxRenderStats.drawWorldUs,
                    (unsigned long long)maxRenderStats.drawUiUs,
                    (unsigned long long)maxRenderStats.endFrameUs);
                flushLog();
                updateUs = renderUs = swapUs = 0;
                updateTotals = {};
                maxUpdateStats = {};
                renderTotals = {};
                maxRenderStats = {};
                maxUpdateUs = 0;
                maxRenderUs = 0;
                gameplayUpdates = 0;
                matchRenders = 0;
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
