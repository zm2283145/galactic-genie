// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace swgb {

enum class StartupStage : uint8_t {
    ValidateStorage,
    InitializeData,
    Logo,
    Intro,
    Title,
    LoadProfile,
    Ready,
    Error,
    Exit,
};

class StartupFlow {
public:
    void validationFinished(
        bool success,
        const std::string &error = {});
    void dataFinished(
        bool success,
        const std::string &error = {});
    void profileFinished(
        bool success,
        const std::string &warning = {});
    void update(float seconds, bool skip);
    void retry();
    void exit();

    StartupStage stage() const { return stage_; }
    const std::string &message() const {
        return message_;
    }
    float progress() const;

private:
    StartupStage stage_ =
        StartupStage::ValidateStorage;
    StartupStage failedStage_ =
        StartupStage::ValidateStorage;
    std::string message_ = "VALIDATING ORIGINAL DATA";
    float elapsed_ = 0;
};

} // namespace swgb
