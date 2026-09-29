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
constexpr size_t kMaxCachedEffects = 64;

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
    port_ = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, kBufferFrames,
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
    thread_ = sceKernelCreateThread("swgb_audio", threadEntry, 0x40, 64 * 1024, 0, 0, nullptr);
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

float VitaAudio::play(const std::string &name) {
    const std::string key = normalizedName(name);
    if (key.empty() || !running_) return 0;

    auto clip = std::make_shared<AudioClip>();
    std::string err;
    const std::string path = scenarioSoundDir_ + "/" + key + ".mp3";
    if (!loadMp3(path, *clip, &err)) {
        log(err);
        return 0;
    }
    const float duration = clip->frameCount() / (float)AudioClip::kSampleRate;

    sceKernelLockMutex(mutex_, 1, nullptr);
    if (queue_.size() >= kMaxQueuedClips) {
        sceKernelUnlockMutex(mutex_, 1);
        log("audio queue full; dropped " + name);
        return 0;
    }
    queue_.push_back(std::move(clip));
    sceKernelUnlockMutex(mutex_, 1);
    log("playing sound " + name);
    return duration;
}

bool VitaAudio::playEffect(int resourceId, const std::vector<uint8_t> &data) {
    if (resourceId < 0 || data.empty() || !running_) return false;
    auto found = effectCache_.find(resourceId);
    if (found == effectCache_.end()) {
        auto clip = std::make_shared<AudioClip>();
        std::string err;
        if (!decodeWav(data, *clip, &err)) {
            log("could not decode effect " + std::to_string(resourceId) + ": " + err);
            return false;
        }
        if (effectCache_.size() >= kMaxCachedEffects) effectCache_.clear();
        found = effectCache_.emplace(resourceId, std::move(clip)).first;
    }
    sceKernelLockMutex(mutex_, 1, nullptr);
    pendingEffect_ = found->second;
    sceKernelUnlockMutex(mutex_, 1);
    log("playing effect " + std::to_string(resourceId));
    return true;
}

int VitaAudio::threadEntry(SceSize args, void *argp) {
    if (args != sizeof(VitaAudio *) || !argp) return -1;
    VitaAudio *self = *static_cast<VitaAudio **>(argp);
    return self ? self->run() : -1;
}

int VitaAudio::run() {
    std::shared_ptr<AudioClip> current;
    std::shared_ptr<AudioClip> effect;
    size_t frame = 0;
    size_t effectFrame = 0;
    size_t bufferIndex = 0;
    alignas(64) int16_t buffers[2][kBufferFrames * AudioClip::kChannels];

    while (running_) {
        sceKernelLockMutex(mutex_, 1, nullptr);
        if (pendingEffect_) {
            effect = std::move(pendingEffect_);
            effectFrame = 0;
        }
        if ((!current || frame >= current->frameCount()) && !queue_.empty()) {
            current = std::move(queue_.front());
            queue_.pop_front();
            frame = 0;
        }
        sceKernelUnlockMutex(mutex_, 1);
        if (current && frame >= current->frameCount()) {
            current.reset();
            frame = 0;
        }
        if (effect && effectFrame >= effect->frameCount()) {
            effect.reset();
            effectFrame = 0;
        }

        int16_t *buffer = buffers[bufferIndex];
        std::memset(buffer, 0, sizeof(buffers[bufferIndex]));
        if (current) {
            const size_t frames =
                std::min<size_t>(kBufferFrames, current->frameCount() - frame);
            std::memcpy(buffer, &current->samples[frame * AudioClip::kChannels],
                        frames * AudioClip::kChannels * sizeof(int16_t));
            frame += frames;
        }
        if (effect) {
            const size_t frames =
                std::min<size_t>(kBufferFrames, effect->frameCount() - effectFrame);
            for (size_t sample = 0; sample < frames * AudioClip::kChannels; sample++) {
                const int mixed = (int)buffer[sample] +
                                  (int)effect->samples[effectFrame * AudioClip::kChannels + sample];
                buffer[sample] = (int16_t)std::max(-32768, std::min(32767, mixed));
            }
            effectFrame += frames;
        }
        const int result = sceAudioOutOutput(port_, buffer);
        if (result < 0) {
            log("sceAudioOutOutput failed: " + std::to_string(result));
            running_ = false;
            return result;
        }
        bufferIndex ^= 1;
    }
    return 0;
}

void VitaAudio::log(const std::string &message) const {
    if (log_) log_(message);
}

} // namespace swgb
