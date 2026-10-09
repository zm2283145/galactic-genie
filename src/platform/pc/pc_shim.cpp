// SPDX-License-Identifier: GPL-3.0-or-later
// PC build: the Vita SDK calls the game uses, on SDL2 and the C++ library.
#include "pc_platform.h"
#include "shim/psp2/audioout.h"
#include "shim/psp2/ctrl.h"
#include "shim/psp2/io/fcntl.h"
#include "shim/psp2/io/stat.h"
#include "shim/psp2/kernel/processmgr.h"
#include "shim/psp2/kernel/sysmem.h"
#include "shim/psp2/kernel/threadmgr.h"
#include "shim/psp2/touch.h"
#include "shim/vitaGL.h"

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#endif

namespace {

SDL_Window *g_window = nullptr;
SDL_GLContext g_context = nullptr;
SDL_GameController *g_controller = nullptr;
int g_gameW = 960, g_gameH = 544;
swgb_pc::Mouse g_mouse;
std::set<int> g_pressed;
const auto g_start = std::chrono::steady_clock::now();

template <typename F> F load(const char *name) {
    return reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
}

// Window pixels -> game coordinates (the game is drawn letterboxed).
void toGame(int wx, int wy, float &gx, float &gy) {
    int w = 1, h = 1;
    SDL_GL_GetDrawableSize(g_window, &w, &h);
    int ww = 1, wh = 1;
    SDL_GetWindowSize(g_window, &ww, &wh);
    const float px = wx * (float)w / std::max(1, ww), py = wy * (float)h / std::max(1, wh);
    const float scale = std::min(w / (float)g_gameW, h / (float)g_gameH);
    const float ox = (w - g_gameW * scale) * 0.5f, oy = (h - g_gameH * scale) * 0.5f;
    gx = (px - ox) / scale;
    gy = (py - oy) / scale;
}

// SWGB_PC_KEYS="<frame>:<scancode>,<frame>:<scancode>..." presses keys
// (headless checks).
std::set<int> g_injected;
void injectKeys() {
    static int frame = 0;
    ++frame;
    g_injected.clear();
    const char *keys = std::getenv("SWGB_PC_KEYS");
    if (!keys) return;
    for (const char *at = keys; *at;) {
        const int when = std::atoi(at);
        const char *colon = std::strchr(at, ':');
        if (!colon) break;
        const int code = std::atoi(colon + 1);
        if (when == frame) {
            g_injected.insert(code);
            g_pressed.insert(code);
        }
        const char *comma = std::strchr(colon, ',');
        if (!comma) break;
        at = comma + 1;
    }
}

void pumpEvents() {
    g_pressed.clear();
    g_mouse.leftPressed = g_mouse.leftReleased = g_mouse.rightPressed = false;
    g_mouse.wheel = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            std::exit(0);
        case SDL_KEYDOWN:
            if (!event.key.repeat) g_pressed.insert(event.key.keysym.scancode);
            if (event.key.keysym.scancode == SDL_SCANCODE_RETURN && (event.key.keysym.mod & KMOD_ALT))
                SDL_SetWindowFullscreen(g_window, (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                                      ? 0
                                                      : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) g_mouse.leftPressed = g_mouse.left = true;
            if (event.button.button == SDL_BUTTON_RIGHT) g_mouse.rightPressed = g_mouse.right = true;
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                g_mouse.left = false;
                g_mouse.leftReleased = true;
            }
            if (event.button.button == SDL_BUTTON_RIGHT) g_mouse.right = false;
            break;
        case SDL_MOUSEWHEEL:
            g_mouse.wheel += event.wheel.y;
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_ENTER) g_mouse.inside = true;
            if (event.window.event == SDL_WINDOWEVENT_LEAVE) g_mouse.inside = false;
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!g_controller) g_controller = SDL_GameControllerOpen(event.cdevice.which);
            break;
        default:
            break;
        }
    }
    injectKeys();
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    g_mouse.inside = SDL_GetMouseFocus() == g_window;
    toGame(mx, my, g_mouse.x, g_mouse.y);
    // SWGB_PC_MOUSE="<frame>:<x>:<y>:<button>;..." puts the pointer at game
    // coordinates from that frame on; button 1 clicks left, 2 right
    // (headless checks).
    if (const char *script = std::getenv("SWGB_PC_MOUSE")) {
        static int frame = 0;
        static float heldX = -1, heldY = -1;
        static int release = 0;
        ++frame;
        if (release == frame) {
            g_mouse.left = false;
            g_mouse.leftReleased = true;
        }
        for (const char *at = script; *at;) {
            int when = 0, button = 0;
            float x = 0, y = 0;
            if (std::sscanf(at, "%d:%f:%f:%d", &when, &x, &y, &button) != 4) break;
            if (when == frame) {
                heldX = x;
                heldY = y;
                if (button == 1) {
                    g_mouse.left = g_mouse.leftPressed = true;
                    release = frame + 1;
                }
                if (button == 2) g_mouse.rightPressed = true;
            }
            const char *semi = std::strchr(at, ';');
            if (!semi) break;
            at = semi + 1;
        }
        if (heldX >= 0) {
            g_mouse.x = heldX;
            g_mouse.y = heldY;
            g_mouse.inside = true;
        }
    }
    g_mouse.x = std::max(0.0f, std::min((float)g_gameW - 1, g_mouse.x));
    g_mouse.y = std::max(0.0f, std::min((float)g_gameH - 1, g_mouse.y));
}

struct Thread {
    SceKernelThreadEntry entry = nullptr;
    std::thread thread;
};
struct Sema {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0, max = 0;
};
std::mutex g_objectsMutex;
std::map<SceUID, std::shared_ptr<Thread>> g_threads;
std::map<SceUID, std::shared_ptr<std::recursive_mutex>> g_mutexes;
std::map<SceUID, std::shared_ptr<Sema>> g_semas;
std::atomic<SceUID> g_nextUid{0x1000};

template <typename T> std::shared_ptr<T> find(std::map<SceUID, std::shared_ptr<T>> &map, SceUID uid) {
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    const auto it = map.find(uid);
    return it == map.end() ? nullptr : it->second;
}

struct Port {
    SDL_AudioDeviceID device = 0;
    int bytes = 0; // one grain
};
std::map<int, Port> g_ports;
int g_nextPort = 1;

} // namespace

namespace swgb_pc {
const Mouse &mouse() { return g_mouse; }
bool keyPressed(int scancode) { return g_pressed.count(scancode) != 0; }
bool keyDown(int scancode) {
    if (g_injected.count(scancode)) return true;
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    return keys && scancode >= 0 && scancode < SDL_NUM_SCANCODES && keys[scancode];
}
} // namespace swgb_pc

// --- Input --------------------------------------------------------------------

int sceCtrlSetSamplingMode(int) { return 0; }

// Keyboard: arrows = D-pad, Enter/Space = Cross, Escape/Backspace = Circle,
// Q = Triangle, Shift = Square (box select with the stick), Tab = Select,
// F10 = Start, Page Up / Page Down = L / R. A game controller maps directly.
int sceCtrlPeekBufferPositive(int, SceCtrlData *data, int) {
    pumpEvents();
    std::memset(data, 0, sizeof *data);
    data->lx = data->ly = data->rx = data->ry = 128;
    unsigned int buttons = 0;
    const struct {
        SDL_Scancode key;
        unsigned int button;
    } keys[] = {
        {SDL_SCANCODE_UP, SCE_CTRL_UP},           {SDL_SCANCODE_DOWN, SCE_CTRL_DOWN},
        {SDL_SCANCODE_LEFT, SCE_CTRL_LEFT},       {SDL_SCANCODE_RIGHT, SCE_CTRL_RIGHT},
        {SDL_SCANCODE_RETURN, SCE_CTRL_CROSS},    {SDL_SCANCODE_KP_ENTER, SCE_CTRL_CROSS},
        {SDL_SCANCODE_SPACE, SCE_CTRL_CROSS},     {SDL_SCANCODE_ESCAPE, SCE_CTRL_CIRCLE},
        {SDL_SCANCODE_BACKSPACE, SCE_CTRL_CIRCLE}, {SDL_SCANCODE_Q, SCE_CTRL_TRIANGLE},
        {SDL_SCANCODE_LSHIFT, SCE_CTRL_SQUARE},   {SDL_SCANCODE_TAB, SCE_CTRL_SELECT},
        {SDL_SCANCODE_F10, SCE_CTRL_START},       {SDL_SCANCODE_PAGEUP, SCE_CTRL_LTRIGGER},
        {SDL_SCANCODE_PAGEDOWN, SCE_CTRL_RTRIGGER},
    };
    for (const auto &key : keys)
        if (swgb_pc::keyDown(key.key)) buttons |= key.button;
    if (g_controller) {
        const struct {
            SDL_GameControllerButton button;
            unsigned int vita;
        } pads[] = {
            {SDL_CONTROLLER_BUTTON_DPAD_UP, SCE_CTRL_UP},       {SDL_CONTROLLER_BUTTON_DPAD_DOWN, SCE_CTRL_DOWN},
            {SDL_CONTROLLER_BUTTON_DPAD_LEFT, SCE_CTRL_LEFT},   {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SCE_CTRL_RIGHT},
            {SDL_CONTROLLER_BUTTON_A, SCE_CTRL_CROSS},          {SDL_CONTROLLER_BUTTON_B, SCE_CTRL_CIRCLE},
            {SDL_CONTROLLER_BUTTON_X, SCE_CTRL_SQUARE},         {SDL_CONTROLLER_BUTTON_Y, SCE_CTRL_TRIANGLE},
            {SDL_CONTROLLER_BUTTON_BACK, SCE_CTRL_SELECT},      {SDL_CONTROLLER_BUTTON_START, SCE_CTRL_START},
            {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SCE_CTRL_LTRIGGER},
            {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SCE_CTRL_RTRIGGER},
        };
        for (const auto &pad : pads)
            if (SDL_GameControllerGetButton(g_controller, pad.button)) buttons |= pad.vita;
        auto axis = [&](SDL_GameControllerAxis a) {
            return (unsigned char)std::max(0, std::min(255, SDL_GameControllerGetAxis(g_controller, a) / 256 + 128));
        };
        data->lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
        data->ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
        data->rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
        data->ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    }
    data->buttons = buttons;
    data->timeStamp = sceKernelGetProcessTimeWide();
    return 1;
}

int sceTouchSetSamplingState(int, int) { return 0; }
int sceTouchPeek(int, SceTouchData *data, int) {
    std::memset(data, 0, sizeof *data);
    return 0;
}

// --- Kernel ------------------------------------------------------------------

SceUInt64 sceKernelGetProcessTimeWide() {
    return (SceUInt64)std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - g_start)
        .count();
}
int sceKernelExitProcess(int status) {
    std::fflush(nullptr);
    std::_Exit(status);
}
int sceKernelDelayThread(SceUInt microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}

SceUID sceKernelCreateThread(const char *, SceKernelThreadEntry entry, int, SceSize, SceUInt, int,
                             const void *) {
    auto thread = std::make_shared<Thread>();
    thread->entry = entry;
    const SceUID uid = g_nextUid++;
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_threads[uid] = thread;
    return uid;
}
int sceKernelStartThread(SceUID uid, SceSize argSize, void *argp) {
    auto thread = find(g_threads, uid);
    if (!thread) return -1;
    std::vector<uint8_t> args(argSize);
    if (argSize && argp) std::memcpy(args.data(), argp, argSize);
    SceKernelThreadEntry entry = thread->entry;
    thread->thread = std::thread([entry, args]() mutable {
        entry((SceSize)args.size(), args.empty() ? nullptr : args.data());
    });
    return 0;
}
int sceKernelWaitThreadEnd(SceUID uid, int *status, SceUInt *) {
    auto thread = find(g_threads, uid);
    if (!thread) return -1;
    if (thread->thread.joinable()) thread->thread.join();
    if (status) *status = 0;
    return 0;
}
int sceKernelDeleteThread(SceUID uid) {
    auto thread = find(g_threads, uid);
    if (thread && thread->thread.joinable()) thread->thread.detach();
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_threads.erase(uid);
    return 0;
}
SceUID sceKernelGetThreadId() { return 1; }
int sceKernelGetThreadCpuAffinityMask(SceUID) { return 0; }
int sceKernelChangeThreadCpuAffinityMask(SceUID, int) { return 0; }

SceUID sceKernelCreateMutex(const char *, SceUInt, int initCount, const void *) {
    auto mutex = std::make_shared<std::recursive_mutex>();
    for (int i = 0; i < initCount; ++i) mutex->lock();
    const SceUID uid = g_nextUid++;
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_mutexes[uid] = mutex;
    return uid;
}
int sceKernelLockMutex(SceUID uid, int count, SceUInt *) {
    auto mutex = find(g_mutexes, uid);
    if (!mutex) return -1;
    for (int i = 0; i < count; ++i) mutex->lock();
    return 0;
}
int sceKernelUnlockMutex(SceUID uid, int count) {
    auto mutex = find(g_mutexes, uid);
    if (!mutex) return -1;
    for (int i = 0; i < count; ++i) mutex->unlock();
    return 0;
}
int sceKernelDeleteMutex(SceUID uid) {
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_mutexes.erase(uid);
    return 0;
}

SceUID sceKernelCreateSema(const char *, SceUInt, int initCount, int maxCount, const void *) {
    auto sema = std::make_shared<Sema>();
    sema->count = initCount;
    sema->max = maxCount;
    const SceUID uid = g_nextUid++;
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_semas[uid] = sema;
    return uid;
}
int sceKernelWaitSema(SceUID uid, int need, SceUInt *timeout) {
    auto sema = find(g_semas, uid);
    if (!sema) return -1;
    std::unique_lock<std::mutex> lock(sema->mutex);
    if (timeout) {
        if (!sema->cv.wait_for(lock, std::chrono::microseconds(*timeout),
                               [&] { return sema->count >= need; }))
            return -1;
    } else {
        sema->cv.wait(lock, [&] { return sema->count >= need; });
    }
    sema->count -= need;
    return 0;
}
int sceKernelSignalSema(SceUID uid, int count) {
    auto sema = find(g_semas, uid);
    if (!sema) return -1;
    {
        std::lock_guard<std::mutex> lock(sema->mutex);
        sema->count = std::min(sema->max > 0 ? sema->max : 1 << 30, sema->count + count);
    }
    sema->cv.notify_all();
    return 0;
}
int sceKernelDeleteSema(SceUID uid) {
    std::lock_guard<std::mutex> lock(g_objectsMutex);
    g_semas.erase(uid);
    return 0;
}

int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info) {
    info->size_user = 256u << 20;
    info->size_cdram = 128u << 20;
    info->size_phycont = 0;
    return 0;
}

// --- Files --------------------------------------------------------------------

int sceIoRemove(const char *path) { return std::remove(path) == 0 ? 0 : -1; }
int sceIoMkdir(const char *path, int mode) {
#ifdef _WIN32
    (void)mode;
    return _mkdir(path) == 0 ? 0 : -1;
#else
    return mkdir(path, (mode_t)mode) == 0 ? 0 : -1;
#endif
}
int sceIoGetstat(const char *path, SceIoStat *stat) {
    struct stat info {};
    if (::stat(path, &info) != 0) return -1;
    if (stat) {
        stat->st_size = (SceOff)info.st_size;
        stat->st_mode = (unsigned)info.st_mode;
    }
    return 0;
}

// --- Audio --------------------------------------------------------------------

int sceAudioOutOpenPort(int, int grain, int frequency, int mode) {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return -1;
    SDL_AudioSpec want{}, have{};
    want.freq = frequency;
    want.format = AUDIO_S16SYS;
    want.channels = mode == SCE_AUDIO_OUT_MODE_STEREO ? 2 : 1;
    want.samples = (Uint16)std::max(256, grain);
    const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device) return -1;
    SDL_PauseAudioDevice(device, 0);
    const int port = g_nextPort++;
    g_ports[port] = {device, grain * want.channels * 2};
    return port;
}
// Blocks like the Vita's port: about two grains stay queued.
int sceAudioOutOutput(int port, const void *buffer) {
    const auto it = g_ports.find(port);
    if (it == g_ports.end()) return -1;
    if (!buffer) return 0;
    SDL_QueueAudio(it->second.device, buffer, (Uint32)it->second.bytes);
    while (SDL_GetQueuedAudioSize(it->second.device) > (Uint32)it->second.bytes * 2)
        SDL_Delay(1);
    return 0;
}
int sceAudioOutReleasePort(int port) {
    const auto it = g_ports.find(port);
    if (it == g_ports.end()) return -1;
    SDL_CloseAudioDevice(it->second.device);
    g_ports.erase(it);
    return 0;
}

// --- Graphics -----------------------------------------------------------------

void (*pcglActiveTexture)(GLenum) = nullptr;
void (*pcglClientActiveTexture)(GLenum) = nullptr;
void (*pcglGenFramebuffers)(GLsizei, GLuint *) = nullptr;
void (*pcglBindFramebuffer)(GLenum, GLuint) = nullptr;
void (*pcglFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
void (*pcglDeleteFramebuffers)(GLsizei, const GLuint *) = nullptr;

namespace {
void noFramebuffers(GLsizei n, GLuint *ids) {
    for (GLsizei i = 0; i < n; ++i) ids[i] = 0;
}
} // namespace

GLboolean vglInitWithCustomThreshold(int, int width, int height, int, int, int, int, int) {
    g_gameW = width;
    g_gameH = height;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        std::exit(1);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const char *scaleText = std::getenv("SWGB_WINDOW_SCALE");
    const int scale = scaleText ? std::max(1, std::atoi(scaleText)) : 2;
    g_window = SDL_CreateWindow("Star Wars Galactic Battlegrounds", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, width * scale, height * scale,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_window || !(g_context = SDL_GL_CreateContext(g_window))) {
        std::fprintf(stderr, "window/context: %s\n", SDL_GetError());
        std::exit(1);
    }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE); // the game draws its own cursor
    pcglActiveTexture = load<void (*)(GLenum)>("glActiveTexture");
    pcglClientActiveTexture = load<void (*)(GLenum)>("glClientActiveTexture");
    pcglGenFramebuffers = load<void (*)(GLsizei, GLuint *)>("glGenFramebuffers");
    pcglBindFramebuffer = load<void (*)(GLenum, GLuint)>("glBindFramebuffer");
    pcglFramebufferTexture2D = load<void (*)(GLenum, GLenum, GLenum, GLuint, GLint)>("glFramebufferTexture2D");
    pcglDeleteFramebuffers = load<void (*)(GLsizei, const GLuint *)>("glDeleteFramebuffers");
    if (!pcglGenFramebuffers) pcglGenFramebuffers = noFramebuffers;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i) && !g_controller) g_controller = SDL_GameControllerOpen(i);
    return GL_FALSE;
}
GLboolean vglInitExtended(int pool, int width, int height, int ramThreshold, int msaa) {
    return vglInitWithCustomThreshold(pool, width, height, ramThreshold, 0, 0, 0, msaa);
}
// SWGB_PC_SHOT="<frame>:<file.ppm>" saves that frame (headless tests).
void vglSwapBuffers(GLboolean) {
    static int frame = 0;
    static int shotFrame = -1;
    static std::string shotPath;
    if (frame == 0)
        if (const char *shot = std::getenv("SWGB_PC_SHOT")) {
            shotFrame = std::atoi(shot);
            const char *colon = std::strchr(shot, ':');
            if (colon) shotPath = colon + 1;
        }
    if (++frame == shotFrame && !shotPath.empty()) {
        int w = 1, h = 1;
        SDL_GL_GetDrawableSize(g_window, &w, &h);
        std::vector<uint8_t> pixels((size_t)w * h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        if (FILE *file = std::fopen(shotPath.c_str(), "wb")) {
            std::fprintf(file, "P6\n%d %d\n255\n", w, h);
            for (int y = h - 1; y >= 0; --y) std::fwrite(pixels.data() + (size_t)y * w * 3, 1, (size_t)w * 3, file);
            std::fclose(file);
        }
    }
    SDL_GL_SwapWindow(g_window);
}
size_t vglMemFree(int type) { return type == VGL_MEM_VRAM ? (size_t)192 << 20 : (size_t)64 << 20; }
size_t vglMemTotal(int type) { return vglMemFree(type); }
void vglPhycontMemLazyInit(size_t) {}

// The renderer draws in game coordinates: map them onto the window,
// letterboxed (GlRenderer calls glViewport with the game size).
extern "C" int swgbPcViewport(int *x, int *y, int *w, int *h) {
    int dw = 1, dh = 1;
    SDL_GL_GetDrawableSize(g_window, &dw, &dh);
    const float scale = std::min(dw / (float)g_gameW, dh / (float)g_gameH);
    *w = (int)(g_gameW * scale);
    *h = (int)(g_gameH * scale);
    *x = (dw - *w) / 2;
    *y = (dh - *h) / 2;
    return 1;
}
