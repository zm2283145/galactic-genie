// SPDX-License-Identifier: GPL-3.0-or-later
#include "vita_audio.h"

#include <psp2/audioout.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace swgb {

namespace {

constexpr int kBufferFrames = 1024;
constexpr size_t kMaxQueuedClips = 8;

std::string normalizedName(const std::string &name) {
    const size_t slash = name.find_last_of("/\\");
    const size_t first = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = name.find('.', first);
    std::string result;
    result.reserve((dot == std::string::npos ? name.size() : dot) - first);
    for (size_t i = first; i < (dot == std::string::npos ? name.size() : dot); i++) {
        const unsigned char c = (unsigned char)name[i];
        if (std::isalnum(c) || c == '_' || c == '-') result.push_back((char)std::tolower(c));
    }
    return result;
}

} // namespace

VitaAudio::VitaAudio(std::string scenarioSoundDir)
    : scenarioSoundDir_(std::move(scenarioSoundDir)) {}

VitaAudio::~VitaAudio() {
    running_ = false;
    if (thread_ >= 0) {
        sceKernelWaitThreadEnd(thread_, nullptr, nullptr);
        sceKernelDeleteThread(thread_);
    }
    if (port_ >= 0) sceAudioOutReleasePort(port_);
    if (mutex_ >= 0) sceKernelDeleteMutex(mutex_);
}

bool VitaAudio::start(std::string *err) {
    port_ = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, kBufferFrames,
                                AudioClip::kSampleRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port_ < 0) {
        if (err) *err = "sceAudioOutOpenPort failed: " + std::to_string(port_);
        return false;
    }
    mutex_ = sceKernelCreateMutex("swgb_audio_queue", 0, 1, nullptr);
    if (mutex_ < 0) {
        if (err) *err = "sceKernelCreateMutex failed: " + std::to_string(mutex_);
        sceAudioOutReleasePort(port_);
        port_ = -1;
        return false;
    }
    thread_ = sceKernelCreateThread("swgb_audio", threadEntry, 0x10000100, 64 * 1024, 0, 0, nullptr);
    if (thread_ < 0) {
        if (err) *err = "sceKernelCreateThread failed: " + std::to_string(thread_);
        sceKernelDeleteMutex(mutex_);
        sceAudioOutReleasePort(port_);
        mutex_ = port_ = -1;
        return false;
    }
    running_ = true;
    VitaAudio *self = this;
    const int result = sceKernelStartThread(thread_, sizeof(self), &self);
    if (result < 0) {
        if (err) *err = "sceKernelStartThread failed: " + std::to_string(result);
        running_ = false;
        sceKernelDeleteThread(thread_);
        sceKernelDeleteMutex(mutex_);
        sceAudioOutReleasePort(port_);
        thread_ = mutex_ = port_ = -1;
        return false;
    }
    return true;
}

bool VitaAudio::play(const std::string &name) {
    const std::string key = normalizedName(name);
    if (key.empty() || !running_) return false;

    auto clip = std::make_shared<AudioClip>();
    std::string err;
    const std::string path = scenarioSoundDir_ + "/" + key + ".mp3";
    if (!loadMp3(path, *clip, &err)) {
        log(err);
        return false;
    }

    sceKernelLockMutex(mutex_, 1, nullptr);
    if (queue_.size() >= kMaxQueuedClips) {
        sceKernelUnlockMutex(mutex_, 1);
        log("audio queue full; dropped " + name);
        return false;
    }
    queue_.push_back(std::move(clip));
    sceKernelUnlockMutex(mutex_, 1);
    log("playing sound " + name);
    return true;
}

int VitaAudio::threadEntry(SceSize args, void *argp) {
    if (args != sizeof(VitaAudio *) || !argp) return -1;
    VitaAudio *self = *static_cast<VitaAudio **>(argp);
    return self ? self->run() : -1;
}

int VitaAudio::run() {
    std::shared_ptr<AudioClip> current;
    size_t frame = 0;
    int16_t buffer[kBufferFrames * AudioClip::kChannels];

    while (running_) {
        if (!current || frame >= current->frameCount()) {
            current.reset();
            frame = 0;
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (!queue_.empty()) {
                current = std::move(queue_.front());
                queue_.pop_front();
            }
            sceKernelUnlockMutex(mutex_, 1);
        }

        std::memset(buffer, 0, sizeof(buffer));
        if (current) {
            const size_t frames =
                std::min<size_t>(kBufferFrames, current->frameCount() - frame);
            std::memcpy(buffer, &current->samples[frame * AudioClip::kChannels],
                        frames * AudioClip::kChannels * sizeof(int16_t));
            frame += frames;
        }
        const int result = sceAudioOutOutput(port_, buffer);
        if (result < 0) {
            log("sceAudioOutOutput failed: " + std::to_string(result));
            running_ = false;
            return result;
        }
    }
    return 0;
}

void VitaAudio::log(const std::string &message) const {
    if (log_) log_(message);
}

} // namespace swgb
