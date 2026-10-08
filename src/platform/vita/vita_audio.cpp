// SPDX-License-Identifier: GPL-3.0-or-later
#include "vita_audio.h"

#include <psp2/audioout.h>
#include <psp2/kernel/processmgr.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <iterator>
#include <utility>
#include <vector>

namespace swgb {

namespace {

constexpr int kBufferFrames = 1024;
constexpr size_t kMaxQueuedClips = 8;
constexpr size_t kMaxCachedEffects = 128;
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
                     std::string campaignSoundDir,
                     std::string musicDir,
                     std::string terrainSoundDir)
    : scenarioSoundDir_(std::move(scenarioSoundDir)),
      campaignSoundDir_(std::move(campaignSoundDir)),
      musicDir_(std::move(musicDir)),
      terrainSoundDir_(std::move(terrainSoundDir)) {}

VitaAudio::~VitaAudio() {
    running_ = false;
    if (decoder_ >= 0) {
        if (decodeSema_ >= 0) sceKernelSignalSema(decodeSema_, 1);
        sceKernelWaitThreadEnd(decoder_, nullptr, nullptr);
        sceKernelDeleteThread(decoder_);
    }
    if (decodeSema_ >= 0) sceKernelDeleteSema(decodeSema_);
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
    // Decoder on another core, below the game thread's priority.
    decodeSema_ = sceKernelCreateSema("swgb_audio_decode", 0, 0, 1 << 20, nullptr);
    if (decodeSema_ >= 0) {
        decoder_ = sceKernelCreateThread("swgb_audio_decode", decoderEntry, 0x10000100 + 20,
                                         256 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, nullptr);
        if (decoder_ >= 0 && sceKernelStartThread(decoder_, sizeof(self), &self) < 0) {
            sceKernelDeleteThread(decoder_);
            decoder_ = -1;
        }
        if (decoder_ >= 0) sceKernelSignalSema(decodeSema_, 1); // ambience header scan
    }
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
    // Fast path: read the file here, take the exact duration from the MP3
    // frame headers and decode on the worker thread. Anything unusual takes
    // the original synchronous path below.
    if (decoder_ >= 0) {
        std::vector<uint8_t> data;
        const uint64_t readStart = sceKernelGetProcessTimeWide();
        bool found = false;
        sceKernelLockMutex(mutex_, 1, nullptr);
        const uint64_t lockedUs = sceKernelGetProcessTimeWide() - readStart;
        auto prefetched = voiceFiles_.find(key);
        const int prefetchState = prefetched == voiceFiles_.end() ? 0 : prefetched->second.empty() ? 1 : 2;
        const std::string workerState = "; prefetched " + std::to_string(voiceFetchesDone_) + ", queued " +
                                        std::to_string(voiceFetches_.size()) + ", effect jobs " +
                                        std::to_string(effectJobs_.size()) + ", effects done " +
                                        std::to_string(effectJobsDone_);
        if (prefetched != voiceFiles_.end()) {
            data = prefetched->second;
            found = !data.empty();
        }
        sceKernelUnlockMutex(mutex_, 1);
        if (!found) {
            std::string path = scenarioSoundDir_ + "/" + key + ".mp3";
            found = readAudioFile(path, data);
            if (!found) {
                path = campaignSoundDir_ + "/" + key + ".mp3";
                found = readAudioFile(path, data);
            }
        }
        const uint64_t readUs = sceKernelGetProcessTimeWide() - readStart;
        uint64_t frames = 0;
        const bool counted = found && mp3OutputFrameCount(data, frames);
        const uint64_t countUs = sceKernelGetProcessTimeWide() - readStart - readUs;
        if (readUs + countUs > 20000)
            log("voice " + name + ": read " + std::to_string(readUs) + " us (" +
                std::to_string(data.size()) + " bytes, lock " + std::to_string(lockedUs) +
                " us, prefetch " + (prefetchState == 2 ? "hit" : prefetchState == 1 ? "pending" : "none") +
                "), header scan " + std::to_string(countUs) + " us" + workerState);
        if (counted) {
            const float duration = frames / (float)AudioClip::kSampleRate;
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (queue_.size() >= kMaxQueuedClips) {
                sceKernelUnlockMutex(mutex_, 1);
                log("audio queue full; dropped " + name);
                return 0;
            }
            decoding_.push_back(clip.get());
            decodeJobs_.push_back({clip, std::move(data), name});
            queue_.push_back(std::move(clip));
            sceKernelUnlockMutex(mutex_, 1);
            sceKernelSignalSema(decodeSema_, 1);
            log("playing sound " + name);
            return duration;
        }
    }
    std::string path = scenarioSoundDir_ + "/" + key + ".mp3";
    if (!loadMp3(path, *clip, &err)) {
        path = campaignSoundDir_ + "/" + key + ".mp3";
        err.clear();
    }
    if (clip->samples.empty() &&
        !loadMp3(path, *clip, &err)) {
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

int VitaAudio::decoderEntry(SceSize args, void *argp) {
    if (args != sizeof(VitaAudio *) || !argp) return -1;
    VitaAudio *self = *static_cast<VitaAudio **>(argp);
    return self ? self->runDecoder() : -1;
}

int VitaAudio::runDecoder() {
    while (running_) {
        sceKernelWaitSema(decodeSema_, 1, nullptr);
        for (;;) {
            sceKernelLockMutex(mutex_, 1, nullptr);
            // Voice prefetches alternate with effect/ambient jobs so a battle's
            // steady stream of effect loads cannot starve them.
            const bool voiceTurn = preferVoiceFetch_ && decodeJobs_.empty() && !voiceFetches_.empty();
            if (!voiceTurn && !ambientJobs_.empty()) {
                preferVoiceFetch_ = true;
                AmbientJob job = std::move(ambientJobs_.front());
                ambientJobs_.pop_front();
                sceKernelUnlockMutex(mutex_, 1);
                auto clip = std::make_shared<AudioClip>();
                std::string err;
                const bool loaded = loadWav(job.path, *clip, &err);
                sceKernelLockMutex(mutex_, 1, nullptr);
                if (!loaded) {
                    workerLogs_.push_back(err);
                } else {
                    auto found = ambientCache_.find(job.key);
                    if (found == ambientCache_.end()) {
                        if (ambientCache_.size() >= kMaxCachedAmbience)
                            ambientCache_.clear();
                        found = ambientCache_.emplace(job.key, std::move(clip)).first;
                    }
                    if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
                    pendingEffects_.push_back({found->second, kAmbienceGain});
                }
                sceKernelUnlockMutex(mutex_, 1);
                continue;
            }
            if (!voiceTurn && !effectJobs_.empty()) {
                preferVoiceFetch_ = true;
                EffectJob job = std::move(effectJobs_.front());
                effectJobs_.pop_front();
                effectJobsDone_++;
                const bool cached = effectCache_.count(job.resourceId) != 0;
                sceKernelUnlockMutex(mutex_, 1);
                auto clip = std::make_shared<AudioClip>();
                std::string err;
                std::vector<uint8_t> loaded;
                bool haveData = cached || job.data;
                if (!haveData && effectLoader_) haveData = effectLoader_(job.resourceId, job.interfaceSound, loaded);
                const bool decoded =
                    cached || (haveData && decodeWav(job.data ? *job.data : loaded, *clip, &err));
                if (!haveData) err = "not found";
                sceKernelLockMutex(mutex_, 1, nullptr);
                auto pending = std::find(decodingEffects_.begin(), decodingEffects_.end(), job.resourceId);
                if (pending != decodingEffects_.end()) decodingEffects_.erase(pending);
                if (!decoded) {
                    workerLogs_.push_back("could not decode effect " + std::to_string(job.resourceId) +
                                          ": " + err);
                } else {
                    auto found = effectCache_.find(job.resourceId);
                    if (found == effectCache_.end()) {
                        if (effectCache_.size() >= kMaxCachedEffects)
                            effectCache_.erase(effectCache_.begin());
                        found = effectCache_.emplace(job.resourceId, std::move(clip)).first;
                    }
                    if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
                    pendingEffects_.push_back({found->second, kEffectGain});
                }
                sceKernelUnlockMutex(mutex_, 1);
                continue;
            }
            if (decodeJobs_.empty() && !voiceFetches_.empty()) {
                preferVoiceFetch_ = false;
                const std::string key = voiceFetches_.front();
                voiceFetches_.pop_front();
                voiceFetchesDone_++;
                sceKernelUnlockMutex(mutex_, 1);
                std::vector<uint8_t> bytes;
                if (!readAudioFile(scenarioSoundDir_ + "/" + key + ".mp3", bytes))
                    readAudioFile(campaignSoundDir_ + "/" + key + ".mp3", bytes);
                sceKernelLockMutex(mutex_, 1, nullptr);
                if (bytes.empty()) workerLogs_.push_back("voice prefetch missed " + key);
                voiceFiles_[key] = std::move(bytes);
                if (voiceFetches_.empty()) workerLogs_.push_back("voice prefetch done");
                sceKernelUnlockMutex(mutex_, 1);
                continue;
            }
            if (decodeJobs_.empty() && !ambientScanListed_) {
                ambientScanListed_ = true;
                sceKernelUnlockMutex(mutex_, 1);
                std::vector<std::string> keys;
                if (DIR *dir = opendir(terrainSoundDir_.c_str())) {
                    while (const dirent *entry = readdir(dir)) {
                        const std::string fileName = entry->d_name;
                        if (fileName.size() > 4) {
                            std::string extension = fileName.substr(fileName.size() - 4);
                            for (char &c : extension) c = (char)std::tolower((unsigned char)c);
                            if (extension == ".wav") keys.push_back(normalizedName(fileName));
                        }
                    }
                    closedir(dir);
                }
                sceKernelLockMutex(mutex_, 1, nullptr);
                for (const std::string &key : keys)
                    if (!key.empty()) ambientHeaderScan_.push_back(key);
                sceKernelUnlockMutex(mutex_, 1);
                continue;
            }
            if (decodeJobs_.empty() && !ambientHeaderScan_.empty()) {
                const std::string key = ambientHeaderScan_.front();
                ambientHeaderScan_.pop_front();
                const bool known = ambientFrames_.count(key) != 0;
                sceKernelUnlockMutex(mutex_, 1);
                uint64_t frames = 0;
                if (!known && wavFileOutputFrameCount(terrainSoundDir_ + "/" + key + ".wav", frames)) {
                    sceKernelLockMutex(mutex_, 1, nullptr);
                    ambientFrames_.emplace(key, frames);
                    if (ambientHeaderScan_.empty())
                        workerLogs_.push_back("ambience headers read: " + std::to_string(ambientFrames_.size()));
                    sceKernelUnlockMutex(mutex_, 1);
                }
                continue;
            }
            if (decodeJobs_.empty()) {
                sceKernelUnlockMutex(mutex_, 1);
                break;
            }
            DecodeJob job = std::move(decodeJobs_.front());
            decodeJobs_.pop_front();
            sceKernelUnlockMutex(mutex_, 1);
            AudioClip decoded;
            std::string err;
            const bool ok = decodeMp3(job.data, decoded, &err);
            sceKernelLockMutex(mutex_, 1, nullptr);
            if (!ok) workerLogs_.push_back("could not decode " + job.name + ": " + err);
            job.clip->samples = std::move(decoded.samples);
            decoding_.erase(std::remove(decoding_.begin(), decoding_.end(), job.clip.get()),
                            decoding_.end());
            sceKernelUnlockMutex(mutex_, 1);
        }
    }
    return 0;
}

std::vector<std::string> VitaAudio::campaignBriefing(
    const std::string &prefix) const {
    std::vector<std::string> result;
    const std::string key = normalizedName(prefix);
    if (key.empty()) return result;
    DIR *directory =
        opendir(campaignSoundDir_.c_str());
    if (!directory) {
        log(
            "campaign narration directory unavailable: " +
            campaignSoundDir_);
        return result;
    }
    while (dirent *entry = readdir(directory)) {
        const std::string name =
            normalizedName(entry->d_name);
        if (name.size() <= key.size() ||
            name.compare(0, key.size(), key) != 0 ||
            name[key.size()] != '_')
            continue;
        result.push_back(name);
    }
    closedir(directory);
    std::sort(result.begin(), result.end());
    log(
        "campaign narration " + prefix + ": " +
        std::to_string(result.size()) + " clips");
    return result;
}

float VitaAudio::playAmbient(const std::string &name) {
    const std::string key = normalizedName(name);
    if (key.empty() || !running_) return 0;
    flushWorkerLogs();

    sceKernelLockMutex(mutex_, 1, nullptr);
    auto found = ambientCache_.find(key);
    if (found != ambientCache_.end()) {
        const float duration =
            found->second->frameCount() /
            (float)AudioClip::kSampleRate;
        if (pendingEffects_.size() >= kMaxPendingEffects)
            pendingEffects_.pop_front();
        pendingEffects_.push_back(
            {found->second, kAmbienceGain});
        sceKernelUnlockMutex(mutex_, 1);
        return duration;
    }
    const std::string path =
        terrainSoundDir_ + "/" + key + ".wav";
    // Ambience WAVs are large (~330 KB): read and resample them on the
    // worker. The duration the game schedules with comes from the RIFF
    // headers and equals the decoded clip's.
    auto knownFrames = ambientFrames_.find(key);
    if (decoder_ >= 0 && knownFrames != ambientFrames_.end()) {
        const uint64_t frames = knownFrames->second;
        ambientJobs_.push_back({key, path});
        sceKernelUnlockMutex(mutex_, 1);
        sceKernelSignalSema(decodeSema_, 1);
        return frames / (float)AudioClip::kSampleRate;
    }
    sceKernelUnlockMutex(mutex_, 1);
    uint64_t frames = 0;
    if (decoder_ >= 0 && wavFileOutputFrameCount(path, frames)) {
        sceKernelLockMutex(mutex_, 1, nullptr);
        ambientFrames_.emplace(key, frames);
        ambientJobs_.push_back({key, path});
        sceKernelUnlockMutex(mutex_, 1);
        sceKernelSignalSema(decodeSema_, 1);
        return frames / (float)AudioClip::kSampleRate;
    }
    auto clip = std::make_shared<AudioClip>();
    std::string err;
    if (!loadWav(path, *clip, &err)) {
        log(err);
        return 0;
    }
    const float duration =
        clip->frameCount() /
        (float)AudioClip::kSampleRate;
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (ambientCache_.size() >= kMaxCachedAmbience)
        ambientCache_.clear();
    found = ambientCache_.emplace(key, std::move(clip)).first;
    if (pendingEffects_.size() >= kMaxPendingEffects)
        pendingEffects_.pop_front();
    pendingEffects_.push_back(
        {found->second, kAmbienceGain});
    sceKernelUnlockMutex(mutex_, 1);
    return duration;
}

bool VitaAudio::playEffect(int resourceId, const std::vector<uint8_t> &data) {
    if (resourceId < 0 || data.empty() || !running_) return false;
    return playEffect(resourceId, std::make_shared<const std::vector<uint8_t>>(data));
}

bool VitaAudio::playEffect(int resourceId, std::shared_ptr<const std::vector<uint8_t>> data) {
    if (resourceId < 0 || !data || data->empty() || !running_) return false;
    flushWorkerLogs();
    sceKernelLockMutex(mutex_, 1, nullptr);
    auto found = effectCache_.find(resourceId);
    if (found != effectCache_.end()) {
        if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
        pendingEffects_.push_back({found->second, kEffectGain});
        sceKernelUnlockMutex(mutex_, 1);
        return true;
    }
    if (decoder_ >= 0) {
        // Decode on the worker (resampling a WAV took tens of ms on the game
        // thread); a request while that clip is decoding plays it once ready.
        effectJobs_.push_back({resourceId, std::move(data)});
        decodingEffects_.push_back(resourceId);
        sceKernelUnlockMutex(mutex_, 1);
        sceKernelSignalSema(decodeSema_, 1);
        return true;
    }
    sceKernelUnlockMutex(mutex_, 1);
    auto clip = std::make_shared<AudioClip>();
    std::string err;
    if (!decodeWav(*data, *clip, &err)) {
        log("could not decode effect " + std::to_string(resourceId) + ": " + err);
        return false;
    }
    sceKernelLockMutex(mutex_, 1, nullptr);
    // Evict one decoded clip rather than all of them (a full clear made
    // battles re-decode every sound on the game thread).
    if (effectCache_.size() >= kMaxCachedEffects) effectCache_.erase(effectCache_.begin());
    found = effectCache_.emplace(resourceId, std::move(clip)).first;
    if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
    pendingEffects_.push_back({found->second, kEffectGain});
    sceKernelUnlockMutex(mutex_, 1);
    return true;
}

bool VitaAudio::playEffectById(int resourceId, bool interfaceSound) {
    if (resourceId < 0 || !running_) return false;
    flushWorkerLogs();
    sceKernelLockMutex(mutex_, 1, nullptr);
    auto found = effectCache_.find(resourceId);
    if (found != effectCache_.end()) {
        if (pendingEffects_.size() >= kMaxPendingEffects) pendingEffects_.pop_front();
        pendingEffects_.push_back({found->second, kEffectGain});
        sceKernelUnlockMutex(mutex_, 1);
        return true;
    }
    if (decoder_ < 0 || !effectLoader_) {
        sceKernelUnlockMutex(mutex_, 1);
        return false;
    }
    effectJobs_.push_back({resourceId, nullptr, interfaceSound});
    sceKernelUnlockMutex(mutex_, 1);
    sceKernelSignalSema(decodeSema_, 1);
    return true;
}

void VitaAudio::prefetchVoices(const std::vector<std::string> &names) {
    if (decoder_ < 0) return;
    sceKernelLockMutex(mutex_, 1, nullptr);
    for (const std::string &name : names) {
        const std::string key = normalizedName(name);
        if (!key.empty() && !voiceFiles_.count(key)) {
            voiceFiles_[key];
            voiceFetches_.push_back(key);
        }
    }
    sceKernelUnlockMutex(mutex_, 1);
    sceKernelSignalSema(decodeSema_, 1);
}

void VitaAudio::setMusic(Music music) {
    if (mutex_ < 0) return;
    if (musicMode_.load() == (int)music) return;
    std::string path;
    bool loop = false;
    // stream/ sits beside the campaign sound folder (Sound/Campaign).
    if (streamDir_.empty()) {
        streamDir_ = campaignSoundDir_;
        const size_t slash = streamDir_.find_last_of('/');
        streamDir_ = (slash == std::string::npos ? std::string(".") : streamDir_.substr(0, slash)) +
                     "/Stream";
    }
    switch (music) {
    case Music::Menu: path = streamDir_ + "/open.mp3"; loop = true; break;
    case Music::Victory: path = streamDir_ + "/won1.mp3"; break;
    case Music::Defeat: path = streamDir_ + "/lost.mp3"; break;
    default: break;
    }
    sceKernelLockMutex(mutex_, 1, nullptr);
    musicMode_ = (int)music;
    streamMusicPath_ = path;
    streamMusicLoop_ = loop;
    streamMusicGeneration_++;
    sceKernelUnlockMutex(mutex_, 1);
}

std::string VitaAudio::workerState() {
    if (mutex_ < 0) return "off";
    flushWorkerLogs();
    sceKernelLockMutex(mutex_, 1, nullptr);
    const std::string state =
        "effects " + std::to_string(effectJobsDone_) + " done/" + std::to_string(effectJobs_.size()) +
        " queued, voices " + std::to_string(voiceFetchesDone_) + "/" + std::to_string(voiceFetches_.size()) +
        ", decodes queued " + std::to_string(decodeJobs_.size()) + ", ambience queued " +
        std::to_string(ambientJobs_.size()) + ", headers " + std::to_string(ambientFrames_.size()) + "/" +
        std::to_string(ambientHeaderScan_.size()) + ", worker affinity 0x" +
        [](int mask) { char text[16]; snprintf(text, sizeof text, "%x", mask); return std::string(text); }(
            decoder_ >= 0 ? sceKernelGetThreadCpuAffinityMask(decoder_) : 0);
    sceKernelUnlockMutex(mutex_, 1);
    return state;
}

void VitaAudio::flushWorkerLogs() {
    if (mutex_ < 0) return;
    std::vector<std::string> messages;
    sceKernelLockMutex(mutex_, 1, nullptr);
    messages.swap(workerLogs_);
    sceKernelUnlockMutex(mutex_, 1);
    for (const std::string &message : messages) log(message);
}

void VitaAudio::playOiiaEffect() {
    if (!running_) return;
    if (!oiiaClip_) {
        oiiaClip_ = std::make_shared<AudioClip>();
        constexpr float duration = 1.05f;
        const size_t frames =
            (size_t)(AudioClip::kSampleRate * duration);
        oiiaClip_->samples.resize(
            frames * AudioClip::kChannels);
        const float notes[] = {
            220.0f, 330.0f, 392.0f, 330.0f,
            247.0f, 370.0f, 440.0f, 370.0f};
        for (size_t frame = 0;
             frame < frames; ++frame) {
            const float time =
                frame / (float)AudioClip::kSampleRate;
            const int syllable =
                std::min(
                    7,
                    (int)(time / (duration / 8.0f)));
            const float local =
                std::fmod(
                    time, duration / 8.0f) /
                (duration / 8.0f);
            const float envelope =
                std::min(1.0f, local * 10.0f) *
                std::min(1.0f, (1.0f - local) * 7.0f);
            const float vibrato =
                1.0f + 0.018f *
                           std::sin(
                               2.0f * 3.14159265f *
                               7.0f * time);
            const float phase =
                2.0f * 3.14159265f *
                notes[syllable] * vibrato * time;
            const float vowel =
                std::sin(phase) * 0.58f +
                std::sin(phase * 2.0f) * 0.25f +
                std::sin(phase * 3.0f) * 0.10f +
                std::sin(phase * 5.0f) * 0.07f;
            const float sample =
                std::max(
                    -1.0f,
                    std::min(
                        1.0f,
                        vowel * envelope * 0.72f));
            const int16_t pcm =
                (int16_t)(sample * 32767.0f);
            oiiaClip_->samples[
                frame * AudioClip::kChannels] = pcm;
            oiiaClip_->samples[
                frame * AudioClip::kChannels + 1] = pcm;
        }
    }
    sceKernelLockMutex(mutex_, 1, nullptr);
    if (pendingEffects_.size() >=
        kMaxPendingEffects)
        pendingEffects_.pop_front();
    pendingEffects_.push_back(
        {oiiaClip_, kEffectGain});
    sceKernelUnlockMutex(mutex_, 1);
    log("playing original oiia effect");
}

void VitaAudio::resetSession() {
    if (!running_) return;
    sceKernelLockMutex(mutex_, 1, nullptr);
    queue_.clear();
    pendingEffects_.clear();
    voiceFetches_.clear();
    voiceFiles_.clear();
    ++resetGeneration_;
    sceKernelUnlockMutex(mutex_, 1);
}

void VitaAudio::setVolumes(
    int master, int music,
    int dialogue, int effects) {
    const auto volume = [](int value) {
        return std::max(
                   0, std::min(100, value)) /
               100.0f;
    };
    masterVolume_ = volume(master);
    musicVolume_ = volume(music);
    dialogueVolume_ = volume(dialogue);
    effectsVolume_ = volume(effects);
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
    uint32_t resetGeneration =
        resetGeneration_.load();
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

    // Menu / outcome music, decoded from the MP3 as it plays.
    Mp3Stream stream;
    bool streamLoop = false;
    uint32_t streamGeneration = 0;
    std::string streamPath;
    while (running_) {
        sceKernelLockMutex(mutex_, 1, nullptr);
        const bool streamChanged = streamMusicGeneration_ != streamGeneration;
        if (streamChanged) {
            streamGeneration = streamMusicGeneration_;
            streamPath = streamMusicPath_;
            streamLoop = streamMusicLoop_;
        }
        const uint32_t requestedReset =
            resetGeneration_.load();
        if (requestedReset != resetGeneration) {
            current.reset();
            effects.clear();
            frame = 0;
            resetGeneration = requestedReset;
        }
        while (!pendingEffects_.empty()) {
            if (effects.size() >= kMaxActiveEffects) effects.erase(effects.begin());
            PendingVoice voice =
                std::move(pendingEffects_.front());
            effects.push_back(
                {std::move(voice.clip), 0, voice.gain});
            pendingEffects_.pop_front();
        }
        if ((!current || frame >= current->frameCount()) && !queue_.empty() &&
            std::find(decoding_.begin(), decoding_.end(), queue_.front().get()) ==
                decoding_.end()) {
            current = std::move(queue_.front());
            queue_.pop_front();
            frame = 0;
        }
        sceKernelUnlockMutex(mutex_, 1);
        if (streamChanged) {
            stream.close();
            if (!streamPath.empty() && !stream.open(streamPath))
                log("music " + streamPath + " unavailable");
        }
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
        const bool gameMusic = musicMode_.load() == (int)Music::Game;
        if (!gameMusic && stream.isOpen()) {
            size_t written = stream.read(musicBuffer, kBufferFrames);
            if (written < (size_t)kBufferFrames && streamLoop && stream.rewind())
                written += stream.read(musicBuffer + written * AudioClip::kChannels,
                                       kBufferFrames - written);
            if (written == 0 && !streamLoop) stream.close();
            musicBytes = written * AudioClip::kChannels * sizeof(int16_t);
        }
        while (gameMusic && musicAvailable &&
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
        const float masterGain =
            masterVolume_.load();
        const float musicGain =
            (current ? kDuckedMusicGain
                     : kMusicGain) *
            masterGain * musicVolume_.load();
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
                        kDialogueGain * masterGain *
                        dialogueVolume_.load());
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
                                      effectDivisor *
                                      masterGain *
                                      effectsVolume_.load());
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
