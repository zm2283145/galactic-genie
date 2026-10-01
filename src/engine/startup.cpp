// SPDX-License-Identifier: GPL-3.0-or-later
#include "startup.h"

namespace swgb {

void StartupFlow::validationFinished(
    bool success, const std::string &error) {
    if (!success) {
        failedStage_ = StartupStage::ValidateStorage;
        stage_ = StartupStage::Error;
        message_ = error;
        return;
    }
    stage_ = StartupStage::InitializeData;
    message_ = "INITIALIZING ORIGINAL DATA";
}

void StartupFlow::dataFinished(
    bool success, const std::string &error) {
    if (!success) {
        failedStage_ = StartupStage::InitializeData;
        stage_ = StartupStage::Error;
        message_ = error;
        return;
    }
    stage_ = StartupStage::Logo;
    message_ = "LUCASARTS";
    elapsed_ = 0;
}

void StartupFlow::profileFinished(
    bool success, const std::string &warning) {
    stage_ = StartupStage::Ready;
    message_ = success ? warning
                       : "PROFILE RECOVERED WITH DEFAULTS: " +
                             warning;
}

void StartupFlow::update(
    float seconds, bool skip) {
    elapsed_ += seconds;
    if (stage_ == StartupStage::Logo &&
        (skip || elapsed_ >= 1.4f)) {
        stage_ = StartupStage::Intro;
        message_ = "A LONG TIME AGO...";
        elapsed_ = 0;
    } else if (stage_ == StartupStage::Intro &&
               (skip || elapsed_ >= 1.8f)) {
        stage_ = StartupStage::Title;
        message_ = "PRESS X OR TAP TO CONTINUE";
        elapsed_ = 0;
    } else if (stage_ == StartupStage::Title &&
               skip) {
        stage_ = StartupStage::LoadProfile;
        message_ = "LOADING PROFILE AND PROGRESS";
    }
}

void StartupFlow::retry() {
    if (stage_ != StartupStage::Error) return;
    stage_ = failedStage_;
    elapsed_ = 0;
    message_ =
        stage_ == StartupStage::ValidateStorage
            ? "VALIDATING ORIGINAL DATA"
            : "INITIALIZING ORIGINAL DATA";
}

void StartupFlow::exit() {
    stage_ = StartupStage::Exit;
}

float StartupFlow::progress() const {
    switch (stage_) {
    case StartupStage::ValidateStorage: return 0.1f;
    case StartupStage::InitializeData: return 0.35f;
    case StartupStage::Logo: return 0.55f;
    case StartupStage::Intro: return 0.7f;
    case StartupStage::Title: return 0.85f;
    case StartupStage::LoadProfile: return 0.95f;
    case StartupStage::Ready: return 1.0f;
    default: return 0;
    }
}

} // namespace swgb
