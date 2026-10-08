// SPDX-License-Identifier: GPL-3.0-or-later
#include "vita_video.h"

#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <string>
#include <vector>

namespace swgb {

namespace {

std::atomic<int> gGeneralAllocs{0}, gGeneralFailures{0}, gGeneralFromGl{0};
std::atomic<uint32_t> gGeneralBytes{0};
// Blocks that came from vitaGL's RAM pool (the newlib heap ran short).
void *gGlBlocks[256];

void *allocateGeneral(void *, uint32_t alignment, uint32_t size) {
    alignment = std::max<uint32_t>(alignment, 16);
    ++gGeneralAllocs;
    gGeneralBytes += size;
    if (void *pointer = memalign(alignment, size)) return pointer;
    void *pointer = vglMemalign(alignment, size);
    if (!pointer) {
        ++gGeneralFailures;
        return nullptr;
    }
    for (void *&slot : gGlBlocks) {
        if (!slot) {
            slot = pointer;
            ++gGeneralFromGl;
            return pointer;
        }
    }
    vglFree(pointer);
    ++gGeneralFailures;
    return nullptr;
}

void freeGeneral(void *, void *pointer) {
    if (!pointer) return;
    for (void *&slot : gGlBlocks) {
        if (slot == pointer) {
            vglFree(pointer);
            slot = nullptr;
            return;
        }
    }
    free(pointer);
}

std::atomic<int> gFrameAllocs{0}, gFrameAllocFailures{0}, gLastAllocError{0};
std::atomic<int> gEvents[8];
std::atomic<int> gEventCount{0};

// The decoder's frame memory: its own physically contiguous memory block
// (vitaGL leaves that heap to the system until the start-up movies are
// over), mapped for the GPU.
struct FrameBlock {
    void *base;
    SceUID block;
};
FrameBlock gFrameBlocks[16];

void *allocateFrame(void *, uint32_t alignment, uint32_t size) {
    const uint32_t total = (size + 0xFFFFF) & ~0xFFFFFu;
    (void)alignment; // 1 MiB blocks satisfy any alignment the player asks for
    const SceUID block = sceKernelAllocMemBlock(
        "swgb_movie", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, total, nullptr);
    if (block < 0) {
        ++gFrameAllocFailures;
        gLastAllocError = block;
        return nullptr;
    }
    void *base = nullptr;
    sceKernelGetMemBlockBase(block, &base);
    sceGxmMapMemory(base, total, (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ |
                                                           SCE_GXM_MEMORY_ATTRIB_WRITE));
    for (FrameBlock &slot : gFrameBlocks) {
        if (!slot.base) {
            slot = {base, block};
            ++gFrameAllocs;
            return base;
        }
    }
    sceGxmUnmapMemory(base);
    sceKernelFreeMemBlock(block);
    ++gFrameAllocFailures;
    return nullptr;
}

void freeFrame(void *, void *pointer) {
    if (!pointer) return;
    for (FrameBlock &slot : gFrameBlocks) {
        if (slot.base == pointer) {
            sceGxmUnmapMemory(pointer);
            sceKernelFreeMemBlock(slot.block);
            slot = {nullptr, -1};
            return;
        }
    }
}

// The movie is read through sceIo rather than the player's own reader.
SceUID gMovieFile = -1;
uint64_t gMovieSize = 0;

int openMovieFile(void *, const char *filename) {
    gMovieFile = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (gMovieFile < 0) return -1;
    gMovieSize = (uint64_t)sceIoLseek(gMovieFile, 0, SCE_SEEK_END);
    sceIoLseek(gMovieFile, 0, SCE_SEEK_SET);
    return 0;
}

int closeMovieFile(void *) {
    if (gMovieFile >= 0) sceIoClose(gMovieFile);
    gMovieFile = -1;
    return 0;
}

std::atomic<int> gMovieReads{0};
std::atomic<uint32_t> gMovieMaxEnd{0};

int readMovieFile(void *, uint8_t *buffer, uint64_t position, uint32_t length) {
    if (gMovieFile < 0) return -1;
    ++gMovieReads;
    if (position + length > gMovieMaxEnd) gMovieMaxEnd = (uint32_t)(position + length);
    sceIoLseek(gMovieFile, (SceOff)position, SCE_SEEK_SET);
    return sceIoRead(gMovieFile, buffer, length);
}

uint64_t movieFileSize(void *) { return gMovieSize; }

void onPlayerEvent(void *, int32_t event, int32_t, void *data) {
    const int slot = gEventCount.fetch_add(1);
    if (slot < 8) gEvents[slot] = (event == 0x20 && data) ? *(int32_t *)data : event;
}

struct AudioPump {
    SceAvPlayerHandle player = -1;
    std::atomic<bool> running{false};
    SceUID thread = -1;
};

int audioEntry(SceSize, void *argument) {
    AudioPump *pump = *static_cast<AudioPump **>(argument);
    int port = -1;
    uint32_t rate = 0, grain = 0;
    while (pump->running) {
        SceAvPlayerFrameInfo frame{};
        if (!sceAvPlayerGetAudioData(pump->player, &frame) || !frame.pData) {
            sceKernelDelayThread(2000);
            continue;
        }
        const uint32_t channels = std::max<uint32_t>(1, frame.details.audio.channelCount);
        const uint32_t samples = frame.details.audio.size / (2 * channels);
        if (samples == 0) continue;
        if (port < 0 || rate != frame.details.audio.sampleRate || grain != samples) {
            if (port >= 0) sceAudioOutReleasePort(port);
            rate = frame.details.audio.sampleRate;
            grain = samples;
            port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, (int)grain, (int)rate,
                                       channels == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                                     : SCE_AUDIO_OUT_MODE_STEREO);
            if (port < 0) {
                sceKernelDelayThread(5000);
                continue;
            }
        }
        sceAudioOutOutput(port, frame.pData); // blocks: paces the pump
    }
    if (port >= 0) sceAudioOutReleasePort(port);
    return 0;
}

inline uint8_t clampByte(int value) { return (uint8_t)std::min(255, std::max(0, value)); }

// NV12 (Y plane, then interleaved UV at half resolution), BT.601 video range.
void nv12ToRgba(const uint8_t *data, int width, int height, int stride, uint8_t *rgba) {
    const uint8_t *chroma = data + (size_t)stride * height;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = data + (size_t)y * stride;
        const uint8_t *uv = chroma + (size_t)(y / 2) * stride;
        uint8_t *out = rgba + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x) {
            const int c = 298 * (row[x] - 16);
            const int u = uv[(x & ~1)] - 128;
            const int v = uv[(x & ~1) + 1] - 128;
            out[x * 4 + 0] = clampByte((c + 409 * v + 128) >> 8);
            out[x * 4 + 1] = clampByte((c - 100 * u - 208 * v + 128) >> 8);
            out[x * 4 + 2] = clampByte((c + 516 * u + 128) >> 8);
            out[x * 4 + 3] = 255;
        }
    }
}

} // namespace

bool playMovie(const std::string &path, Renderer &renderer, int screenW, int screenH,
               const std::function<void(const std::string &)> &log) {
    auto note = [&](const std::string &message) {
        if (log) log("movie " + path + ": " + message);
    };
    if (FILE *probe = fopen(path.c_str(), "rb")) fclose(probe);
    else {
        note("not found");
        return false;
    }
    static bool moduleLoaded = false;
    if (!moduleLoaded) {
        const int loaded = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
        note("avplayer module " + std::to_string(loaded));
        moduleLoaded = true;
    }
    SceAvPlayerInitData init;
    std::memset(&init, 0, sizeof init);
    init.memoryReplacement.allocate = allocateGeneral;
    init.memoryReplacement.deallocate = freeGeneral;
    init.memoryReplacement.allocateTexture = allocateFrame;
    init.memoryReplacement.deallocateTexture = freeFrame;
    init.fileReplacement.objectPointer = nullptr;
    init.fileReplacement.open = openMovieFile;
    init.fileReplacement.close = closeMovieFile;
    init.fileReplacement.readOffset = readMovieFile;
    init.fileReplacement.size = movieFileSize;
    init.eventReplacement.objectPointer = nullptr;
    init.eventReplacement.eventCallback = onPlayerEvent;
    gFrameAllocs = 0;
    gFrameAllocFailures = 0;
    gLastAllocError = 0;
    gEventCount = 0;
    gMovieSize = 0;
    gGeneralAllocs = 0;
    gGeneralFailures = 0;
    gGeneralFromGl = 0;
    gGeneralBytes = 0;
    gMovieReads = 0;
    gMovieMaxEnd = 0;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = SCE_TRUE;
    const SceAvPlayerHandle player = sceAvPlayerInit(&init);
    // The handle is a user-space address (often above 0x80000000), so it
    // is only an error when it is 0 or an SceAvPlayer error code.
    if (player == 0 || ((uint32_t)player & 0xFFFF0000u) == 0x806A0000u) {
        note("sceAvPlayerInit failed " + std::to_string(player));
        return false;
    }
    const int added = sceAvPlayerAddSource(player, path.c_str());
    if (added < 0) {
        note("sceAvPlayerAddSource failed " + std::to_string(added));
        sceAvPlayerClose(player);
        return false;
    }
    AudioPump pump;
    pump.player = player;
    pump.running = true;
    AudioPump *pumpPointer = &pump;
    pump.thread = sceKernelCreateThread("swgb_movie_audio", audioEntry, 0x10000100 - 10, 0x10000, 0,
                                        SCE_KERNEL_CPU_MASK_USER_1, nullptr);
    if (pump.thread >= 0) sceKernelStartThread(pump.thread, sizeof pumpPointer, &pumpPointer);

    Texture *texture = nullptr;
    int textureW = 0, textureH = 0;
    std::vector<uint8_t> rgba, cached;
    SceCtrlData pad{}, previous{};
    sceCtrlPeekBufferPositive(0, &previous, 1);
    bool shown = false;
    // The player reports active only once the source has been opened; give
    // it up to 3 s to get there.
    for (int wait = 0; wait < 300 && !sceAvPlayerIsActive(player); ++wait)
        sceKernelDelayThread(10000);
    if (!sceAvPlayerIsActive(player)) note("player never became active");
    int loops = 0, videoCalls = 0;
    const uint64_t loopStart = sceKernelGetProcessTimeWide();
    int inactiveLoops = 0;
    for (;;) {
        // The player can report inactive for a moment while it starts; only
        // stop once it has been inactive past the first 1.5 s.
        if (!sceAvPlayerIsActive(player)) {
            ++inactiveLoops;
            if (sceKernelGetProcessTimeWide() - loopStart > 1500000) break;
        }
        ++loops;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const uint32_t pressed = pad.buttons & ~previous.buttons;
        previous = pad;
        if (pressed & SCE_CTRL_START) break;
        SceAvPlayerFrameInfo frame{};
        if (sceAvPlayerGetVideoData(player, &frame) && frame.pData) {
            ++videoCalls;
            const int width = (int)frame.details.video.width;
            const int height = (int)frame.details.video.height;
            if (width > 0 && height > 0) {
                if (!texture || width != textureW || height != textureH) {
                    if (texture) renderer.destroyTexture(texture);
                    textureW = width;
                    textureH = height;
                    rgba.assign((size_t)width * height * 4, 0);
                    texture = renderer.createTexture(width, height, rgba.data());
                }
                // The decoder writes uncached memory: copy the frame out in
                // one burst before reading it a byte at a time.
                const int stride = (width + 15) & ~15;
                const size_t frameBytes = (size_t)stride * height * 3 / 2;
                if (cached.size() < frameBytes) cached.resize(frameBytes);
                std::memcpy(cached.data(), frame.pData, frameBytes);
                nv12ToRgba(cached.data(), width, height, stride, rgba.data());
                if (texture) renderer.updateTexture(texture, rgba.data());
                shown = true;
            }
        }
        renderer.beginFrame(screenW, screenH, 1.0f, 0, 0, 0);
        renderer.fillRect(0, 0, (float)screenW, (float)screenH, 0, 0, 0, 255);
        if (texture && shown) {
            // 4:3 picture, as tall as the screen, centred.
            const float drawH = (float)screenH;
            const float w = std::min((float)screenW, drawH * 4.0f / 3.0f);
            renderer.draw(texture, Quad{(screenW - w) * 0.5f, 0.0f, w, drawH, 0.0f, 0.0f,
                                        (float)textureW, (float)textureH});
        }
        renderer.endFrame();
        vglSwapBuffers(GL_FALSE);
    }
    pump.running = false;
    sceAvPlayerStop(player);
    if (pump.thread >= 0) {
        sceKernelWaitThreadEnd(pump.thread, nullptr, nullptr);
        sceKernelDeleteThread(pump.thread);
    }
    sceAvPlayerClose(player);
    if (texture) renderer.destroyTexture(texture);
    {
        char buffer[160];
        std::snprintf(buffer, sizeof buffer, " (file %llu bytes, frame blocks %d, failed %d, last error 0x%08X, events",
                      (unsigned long long)gMovieSize, gFrameAllocs.load(), gFrameAllocFailures.load(), (unsigned)gLastAllocError.load());
        std::string detail = buffer;
        std::snprintf(buffer, sizeof buffer, " reads %d to %u, loops %d (%d inactive) over %llu ms, frames %d;",
                      gMovieReads.load(), (unsigned)gMovieMaxEnd.load(), loops, inactiveLoops,
                      (unsigned long long)((sceKernelGetProcessTimeWide() - loopStart) / 1000), videoCalls);
        detail = buffer + detail;
        std::snprintf(buffer, sizeof buffer, " general allocs %d (%u KB), from vitaGL %d, failed %d;",
                      gGeneralAllocs.load(), (unsigned)(gGeneralBytes.load() / 1024),
                      gGeneralFromGl.load(), gGeneralFailures.load());
        detail = buffer + detail;
        const int events = std::min(8, gEventCount.load());
        for (int i = 0; i < events; ++i) detail += " " + std::to_string(gEvents[i].load());
        note(std::string(shown ? "played" : "no video frames decoded") + detail + ")");
    }
    return true;
}

} // namespace swgb
