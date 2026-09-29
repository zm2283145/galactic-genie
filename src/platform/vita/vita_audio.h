// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../../core/audio.h"

#include <psp2/kernel/threadmgr.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace swgb {

class VitaAudio {
public:
    explicit VitaAudio(std::string scenarioSoundDir);
    ~VitaAudio();

    bool start(std::string *err = nullptr);
    float play(const std::string &name);
    void setLogger(std::function<void(const std::string &)> logger) { log_ = std::move(logger); }

private:
    static int threadEntry(SceSize args, void *argp);
    int run();
    void log(const std::string &message) const;

    std::string scenarioSoundDir_;
    std::function<void(const std::string &)> log_;
    std::atomic<bool> running_{false};
    SceUID thread_ = -1;
    SceUID mutex_ = -1;
    int port_ = -1;
    std::deque<std::shared_ptr<AudioClip>> queue_;
};

} // namespace swgb
