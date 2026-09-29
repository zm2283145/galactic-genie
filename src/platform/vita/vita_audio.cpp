// SPDX-License-Identifier: GPL-3.0-or-later
#include "vita_audio.h"

#include <psp2/audioout.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

namespace swgb {

namespace {

constexpr int kBufferFrames = 1024;
constexpr size_t kMaxQueuedClips = 8;
constexpr size_t kMaxCachedEffects = 64;
constexpr size_t kMaxCachedAmbience = 6;
constexpr size_t kMaxActiveEffects = 8;
constexpr size_t kMaxPendingEffects = 16;
constexpr float kMusicGain = 0.34f;
constexpr float kDuckedMusicGain = 0.11f;
constexpr float kDialogueGain = 0.88f;
constexpr float kEffectGain = 0.72f;
constexpr float kAmbienceGain = 0.24f;

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

VitaAudio::VitaAudio(std::string scenarioSoundDir,
                     std::string musicDir,
                     std::string terrainSoundDir)
    : scenarioSoundDir_(std::move(scenarioSoundDir)),
      musicDir_(std::move(musicDir)),
      terrainSoundDir_(std::move(terrainSoundDir)) {}

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

float VitaAudio::playAmbient(const std::string &name) {
    const std::string key = normalizedName(name);
    if (key.empty() || !running_) return 0;

    auto found = ambientCache_.find(key);
    if (found == ambientCache_.end()) {
        auto clip = std::make_shared<AudioClip>();
        std::string err;
        const std::string path =
            terrainSoundDir_ + "/" + key + ".wav";
        if (!loadWav(path, *clip, &err)) {
            log(err);
            return 0;
        }
        if (ambientCache_.size() >= kMaxCachedAmbience)
            ambientCache_.clear();
        found = ambientCache_.emplace(key, std::move(clip)).first;
    }
    const float duration =
        found->second->frameCount() /
        (float)AudioClip::kSampleRate;
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (pendingEffects_.size() >= kMaxPendingEffects)
        pendingEffects_.pop_front();
    pendingEffects_.push_back(
        {found->second, kAmbienceGain});
    sceKernelUnlockMutex(mutex_, 1);
    log("playing ambience " + name);
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
    if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
    pendingEffects_.push_back(
        {found->second, kEffectGain});
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
    struct EffectVoice {
        std::shared_ptr<AudioClip> clip;
        size_t frame = 0;
        float gain = 1.0f;
    };
    std::shared_ptr<AudioClip> current;
    std::vector<EffectVoice> effects;
    OggVorbis_File music{};
    bool musicOpen = false;
    bool musicAvailable = true;
    size_t nextMusicTrack = 0;
    size_t frame = 0;
    size_t bufferIndex = 0;
    alignas(64) int16_t buffers[2][kBufferFrames * AudioClip::kChannels];
    alignas(64) int16_t musicBuffer[kBufferFrames * AudioClip::kChannels];

    const std::string musicTracks[] = {
        musicDir_ + "/track03.ogg",
        musicDir_ + "/track02.ogg",
    };
    auto closeMusic = [&]() {
        if (musicOpen) ov_clear(&music);
        musicOpen = false;
    };
    auto openNextMusic = [&]() {
        closeMusic();
        for (size_t attempt = 0;
             attempt < std::size(musicTracks);
             ++attempt) {
            const std::string &path =
                musicTracks[nextMusicTrack];
            nextMusicTrack =
                (nextMusicTrack + 1) %
                std::size(musicTracks);
            if (ov_fopen(path.c_str(), &music) != 0)
                continue;
            vorbis_info *info = ov_info(&music, -1);
            if (info && info->channels ==
                            (int)AudioClip::kChannels &&
                info->rate ==
                    (long)AudioClip::kSampleRate) {
                musicOpen = true;
                log("playing music " + path);
                return true;
            }
            ov_clear(&music);
        }
        musicAvailable = false;
        log("music tracks not found or unsupported");
        return false;
    };

    while (running_) {
        sceKernelLockMutex(mutex_, 1, nullptr);
        while (!pendingEffects_.empty()) {
            if (effects.size() >= kMaxActiveEffects) effects.erase(effects.begin());
            PendingVoice voice =
                std::move(pendingEffects_.front());
            effects.push_back(
                {std::move(voice.clip), 0, voice.gain});
            pendingEffects_.pop_front();
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
        effects.erase(std::remove_if(effects.begin(), effects.end(),
                                     [](const EffectVoice &effect) {
                                         return !effect.clip ||
                                                effect.frame >= effect.clip->frameCount();
                                     }),
                      effects.end());

        int16_t *buffer = buffers[bufferIndex];
        std::memset(buffer, 0, sizeof(buffers[bufferIndex]));
        std::memset(musicBuffer, 0, sizeof(musicBuffer));
        size_t musicBytes = 0;
        while (musicAvailable &&
               musicBytes < sizeof(musicBuffer)) {
            if (!musicOpen && !openNextMusic()) break;
            int bitstream = 0;
            const long read = ov_read(
                &music,
                reinterpret_cast<char *>(musicBuffer) +
                    musicBytes,
                (int)(sizeof(musicBuffer) - musicBytes),
                0, 2, 1, &bitstream);
            if (read > 0) {
                musicBytes += (size_t)read;
            } else if (read == OV_HOLE) {
                continue;
            } else {
                closeMusic();
            }
        }
        const float musicGain =
            current ? kDuckedMusicGain : kMusicGain;
        for (size_t sample = 0;
             sample < musicBytes / sizeof(int16_t);
             ++sample)
            buffer[sample] =
                (int16_t)std::lround(
                    musicBuffer[sample] * musicGain);
        if (current) {
            const size_t frames =
                std::min<size_t>(kBufferFrames, current->frameCount() - frame);
            for (size_t sample = 0;
                 sample <
                 frames * AudioClip::kChannels;
                 ++sample) {
                const int mixed =
                    (int)buffer[sample] +
                    (int)std::lround(
                        current->samples[
                            frame * AudioClip::kChannels +
                            sample] *
                        kDialogueGain);
                buffer[sample] =
                    (int16_t)std::max(
                        -32768, std::min(32767, mixed));
            }
            frame += frames;
        }
        const float effectDivisor =
            (float)std::max<int>(
                1, (int)effects.size());
        for (EffectVoice &effect : effects) {
            const size_t frames =
                std::min<size_t>(kBufferFrames, effect.clip->frameCount() - effect.frame);
            for (size_t sample = 0; sample < frames * AudioClip::kChannels; sample++) {
                const int mixed = (int)buffer[sample] +
                                  (int)std::lround(
                                      effect.clip->samples[
                                          effect.frame *
                                              AudioClip::kChannels +
                                          sample] *
                                      effect.gain /
                                      effectDivisor);
                buffer[sample] = (int16_t)std::max(-32768, std::min(32767, mixed));
            }
            effect.frame += frames;
        }
        const int result = sceAudioOutOutput(port_, buffer);
        if (result < 0) {
            log("sceAudioOutOutput failed: " + std::to_string(result));
            running_ = false;
            return result;
        }
        bufferIndex ^= 1;
    }
    closeMusic();
    return 0;
}

void VitaAudio::log(const std::string &message) const {
    if (log_) log_(message);
}

} // namespace swgb
