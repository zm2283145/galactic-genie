// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../core/audio.h"

#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace swgb {

class VitaAudio {
public:
    VitaAudio(std::string scenarioSoundDir,
              std::string musicDir,
              std::string terrainSoundDir);
    ~VitaAudio();

    bool start(std::string *err = nullptr);
    float play(const std::string &name);
    float playAmbient(const std::string &name);
    bool playEffect(int resourceId, const std::vector<uint8_t> &data);
    void playOiiaEffect();
    void resetSession();
    void setVolumes(
        int master, int music,
        int dialogue, int effects);
    void setLogger(std::function<void(const std::string &)> logger) { log_ = std::move(logger); }

private:
    static int threadEntry(SceSize args, void *argp);
    int run();
    void log(const std::string &message) const;

    struct PendingVoice {
        std::shared_ptr<AudioClip> clip;
        float gain = 1.0f;
    };

    std::string scenarioSoundDir_;
    std::string musicDir_;
    std::string terrainSoundDir_;
    std::function<void(const std::string &)> log_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> resetGeneration_{0};
    std::atomic<float> masterVolume_{1.0f};
    std::atomic<float> musicVolume_{0.7f};
    std::atomic<float> dialogueVolume_{1.0f};
    std::atomic<float> effectsVolume_{0.85f};
    SceUID thread_ = -1;
    SceUID mutex_ = -1;
    int port_ = -1;
    std::deque<std::shared_ptr<AudioClip>> queue_;
    std::map<int, std::shared_ptr<AudioClip>> effectCache_;
    std::map<std::string, std::shared_ptr<AudioClip>> ambientCache_;
    std::shared_ptr<AudioClip> oiiaClip_;
    std::deque<PendingVoice> pendingEffects_;
};

} // namespace swgb
