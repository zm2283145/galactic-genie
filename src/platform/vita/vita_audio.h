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
              std::string campaignSoundDir,
              std::string musicDir,
              std::string terrainSoundDir);
    ~VitaAudio();

    bool start(std::string *err = nullptr);
    float play(const std::string &name);
    void playTaunt(int number);
    void setTauntDir(std::string dir) { tauntDir_ = std::move(dir); }
    std::vector<std::string> campaignBriefing(
        const std::string &prefix) const;
    float playAmbient(const std::string &name);
    bool playEffect(int resourceId, const std::vector<uint8_t> &data);
    bool playEffect(int resourceId, std::shared_ptr<const std::vector<uint8_t>> data);
    // Plays a cached effect at once, otherwise has the worker read it (with
    // the loader below, on the worker thread), decode and play it.
    bool playEffectById(int resourceId, bool interfaceSound);
    void setEffectLoader(std::function<bool(int, bool, std::vector<uint8_t> &)> loader) {
        effectLoader_ = std::move(loader);
    }
    // Reads these voice files on the worker ahead of time (memory card reads
    // took 40-80 ms each on the game thread).
    void prefetchVoices(const std::vector<std::string> &names);
    void playOiiaEffect();
    void resetSession();
    void setVolumes(
        int master, int music,
        int dialogue, int effects);
    void setLogger(std::function<void(const std::string &)> logger) { log_ = std::move(logger); }
    // Music by screen, as the original: the menus loop stream/open.mp3,
    // the victory and defeat screens play won1.mp3 / lost.mp3 once, and the
    // CD tracks (Music/track02/03.ogg) play in game.
    enum class Music { None, Menu, Game, Victory, Defeat };
    void setMusic(Music music);
    // Worker progress for the frame log.
    std::string workerState();

private:
    static int threadEntry(SceSize args, void *argp);
    static int decoderEntry(SceSize args, void *argp);
    int run();
    int runDecoder();
    void log(const std::string &message) const;

    struct PendingVoice {
        std::shared_ptr<AudioClip> clip;
        float gain = 1.0f;
    };

    std::string scenarioSoundDir_;
    std::string tauntDir_;
    std::string campaignSoundDir_;
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
    // Dialogue MP3s decode on a worker thread; play() still returns the exact
    // duration (from the frame headers). A queued clip still in this set is
    // not started until its samples are ready.
    struct DecodeJob {
        std::shared_ptr<AudioClip> clip;
        std::vector<uint8_t> data;
        std::string name;
    };
    std::deque<DecodeJob> decodeJobs_;
    std::vector<const AudioClip *> decoding_;
    // Sound effects decode on the same worker; the clip is cached and played
    // when ready. Worker messages are logged from the game thread.
    struct EffectJob {
        int resourceId;
        std::shared_ptr<const std::vector<uint8_t>> data; // null: use effectLoader_
        bool interfaceSound = false;
    };
    std::function<bool(int, bool, std::vector<uint8_t> &)> effectLoader_;
    std::deque<std::string> voiceFetches_;
    bool preferVoiceFetch_ = false;
    uint64_t voiceFetchesDone_ = 0, effectJobsDone_ = 0;
    std::map<std::string, std::vector<uint8_t>> voiceFiles_;
    std::deque<EffectJob> effectJobs_;
    struct AmbientJob {
        std::string key, path;
    };
    std::deque<AmbientJob> ambientJobs_;
    std::atomic<int> musicMode_{(int)Music::None};
    std::string streamMusicPath_;          // requested stream file ("" none)
    bool streamMusicLoop_ = false;
    uint32_t streamMusicGeneration_ = 0;
    std::string streamDir_;
    // Ambience durations (RIFF header frame counts) by key. The worker reads
    // the headers of every terrain WAV at start-up, lowest priority, so
    // playAmbient() does not open the file on the game thread (20-60 ms on
    // the memory card).
    std::map<std::string, uint64_t> ambientFrames_;
    std::deque<std::string> ambientHeaderScan_;
    bool ambientScanListed_ = false;
    std::vector<int> decodingEffects_;
    std::vector<std::string> workerLogs_;
    void flushWorkerLogs();
    SceUID decoder_ = -1;
    SceUID decodeSema_ = -1;
};

} // namespace swgb
