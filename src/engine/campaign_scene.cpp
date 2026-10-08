// SPDX-License-Identifier: GPL-3.0-or-later
#include "campaign_scene.h"

#include "ui_text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>

namespace swgb {

namespace {

bool readTextFile(const std::string &path, std::string &text) {
    text.clear();
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    char buffer[4096];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, file)) > 0) text.append(buffer, got);
    fclose(file);
    return true;
}

// dir/name, matching the name without regard to case (one listing per call).
std::string mediaPathNoCase(const std::string &dir, const std::string &name) {
    std::string wanted = name;
    for (char &c : wanted) c = (char)std::tolower((unsigned char)c);
    if (DIR *listing = opendir(dir.c_str())) {
        while (const dirent *entry = readdir(listing)) {
            std::string candidate = entry->d_name;
            for (char &c : candidate) c = (char)std::tolower((unsigned char)c);
            if (candidate == wanted) {
                const std::string path = dir + "/" + entry->d_name;
                closedir(listing);
                return path;
            }
        }
        closedir(listing);
    }
    return dir + "/" + name;
}

int typeFromName(std::string name) {
    for (char &c : name) c = (char)std::toupper((unsigned char)c);
    if (name == "TEXT") return MediaItem::Text;
    if (name == "PICT") return MediaItem::Picture;
    if (name == "SND") return MediaItem::Sound;
    if (name == "WND") return MediaItem::Window;
    if (name == "MM") return MediaItem::Custom;
    if (name == "ANIM") return MediaItem::Animation;
    return -1;
}

constexpr float kFadeMs = 400.0f; // 0x524350: +0xd14 = 400

// Fade level 0..1 of a text or picture at time t (0x525240 / 0x525800).
float itemLevel(const MediaItem &item, float t) {
    if (t < item.start) return 0.0f;
    const float end = (float)item.start + (float)item.display;
    if (t < end) return std::min(1.0f, (t - item.start) / kFadeMs);
    return std::max(0.0f, 1.0f - (t - end) / kFadeMs);
}

// Word wrap to a pixel width (DrawText DT_WORDBREAK).
std::vector<std::string> wrapText(const std::string &text, float width, float scale) {
    std::vector<std::string> lines;
    std::string paragraph;
    auto flushParagraph = [&](const std::string &value) {
        std::string line;
        size_t position = 0;
        while (position <= value.size()) {
            size_t space = value.find(' ', position);
            if (space == std::string::npos) space = value.size();
            const std::string word = value.substr(position, space - position);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && uiTextWidth(candidate, scale) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            position = space + 1;
        }
        lines.push_back(line);
    };
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string part = text.substr(start, end - start);
        if (!part.empty() && part.back() == '\r') part.pop_back();
        flushParagraph(part);
        start = end + 1;
    }
    return lines;
}

// Scene fonts (FUN_004285d0 ids 0x0f, 0x10, 0x14, 0x15, 0x17) as UI text
// scales; the stock scripts use font 3 for all narration.
float fontScale(int font) {
    switch (font) {
    case 1: return 0.72f;
    case 2: return 0.85f;
    case 3: return 0.8f;
    case 4: return 1.0f;
    default: return 0.7f;
    }
}

} // namespace

bool parseMediaScript(const std::string &script, std::vector<MediaItem> &items, int &windowMs) {
    items.clear();
    windowMs = -1;
    size_t lineStart = 0;
    while (lineStart < script.size()) {
        size_t lineEnd = script.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = script.size();
        const std::string line = script.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        std::vector<std::string> tokens;
        for (size_t i = 0; i < line.size();) {
            const char c = line[i];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
                continue;
            }
            if (c == '"') {
                const size_t close = line.find('"', i + 1);
                const size_t stop = close == std::string::npos ? line.size() : close;
                tokens.push_back(line.substr(i + 1, stop - i - 1));
                i = stop + 1;
                continue;
            }
            size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
            tokens.push_back(line.substr(i, j - i));
            i = j;
        }
        // Item lines start with their numeric id (the header line does not).
        if (tokens.size() < 9 || !std::isdigit((unsigned char)tokens[0][0])) continue;
        MediaItem item;
        item.id = std::atoi(tokens[0].c_str());
        item.type = typeFromName(tokens[1]);
        if (item.type < 0) continue;
        item.x = std::atoi(tokens[2].c_str());
        item.y = std::atoi(tokens[3].c_str());
        item.width = std::atoi(tokens[4].c_str());
        item.height = std::atoi(tokens[5].c_str());
        item.start = std::atoi(tokens[6].c_str());
        item.display = std::atoi(tokens[7].c_str());
        item.frame = std::atoi(tokens[8].c_str());
        if (tokens.size() > 9) item.text = tokens[9];
        if (tokens.size() > 12) {
            item.red = (uint8_t)std::atoi(tokens[10].c_str());
            item.green = (uint8_t)std::atoi(tokens[11].c_str());
            item.blue = (uint8_t)std::atoi(tokens[12].c_str());
        }
        if (item.type == MediaItem::Window) windowMs = item.display;
        items.push_back(std::move(item));
    }
    return !items.empty();
}

bool CampaignScene::load(Assets &assets, const std::string &mediaDir, int campaign, int mission,
                         bool beginning, std::string *err) {
    release(assets);
    const std::string base = mediaDir + "/xc" + std::to_string(campaign) + "s" +
                             std::to_string(mission) + (beginning ? "_beg" : "_end");
    std::string script;
    if (!readTextFile(mediaPathNoCase(mediaDir, base.substr(mediaDir.size() + 1) + ".mm"), script)) {
        if (err) *err = base + ".mm not found";
        return false;
    }
    if (!parseMediaScript(script, items_, windowMs_)) {
        if (err) *err = base + ".mm has no items";
        return false;
    }
    // File name case varies between installs (xbackgrd3.SLP, xc3s2_beg.SLP).
    auto mediaFile = [&](const std::string &name) {
        return mediaPathNoCase(mediaDir, name);
    };
    const std::string backgroundName = "xbackgrd" + std::to_string(campaign);
    const std::string palette = mediaFile(backgroundName + ".pal");
    background_ = assets.fileFrame(mediaFile(backgroundName + ".slp"), 0, palette);
    const std::string pictures = mediaFile(base.substr(mediaDir.size() + 1) + ".slp");
    pictures_.assign(items_.size(), nullptr);
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].type == MediaItem::Picture)
            pictures_[i] = assets.fileFrame(pictures, (size_t)std::max(0, items_[i].frame),
                                            palette);
    soundStarted_.assign(items_.size(), false);
    elapsedMs_ = 0.0f;
    loaded_ = true;
    return true;
}

void CampaignScene::release(Assets &assets) {
    if (loaded_ || background_) assets.releaseFileFrames();
    loaded_ = false;
    items_.clear();
    pictures_.clear();
    soundStarted_.clear();
    background_ = nullptr;
    windowMs_ = -1;
    elapsedMs_ = 0.0f;
}

void CampaignScene::restart() {
    elapsedMs_ = 0.0f;
    soundStarted_.assign(items_.size(), false);
}

void CampaignScene::advance(float seconds) {
    if (loaded_) elapsedMs_ += seconds * 1000.0f;
}

std::vector<std::string> CampaignScene::takeDueSounds() {
    std::vector<std::string> due;
    for (size_t i = 0; i < items_.size(); ++i) {
        const MediaItem &item = items_[i];
        if (item.type != MediaItem::Sound || soundStarted_[i] || elapsedMs_ < item.start) continue;
        soundStarted_[i] = true;
        if (!item.text.empty()) due.push_back(item.text);
    }
    return due;
}

std::vector<std::string> CampaignScene::soundNames() const {
    std::vector<std::string> names;
    for (const MediaItem &item : items_)
        if (item.type == MediaItem::Sound && !item.text.empty()) names.push_back(item.text);
    return names;
}

bool CampaignScene::finished() const {
    if (!loaded_) return true;
    if (windowMs_ >= 0) return elapsedMs_ >= windowMs_;
    // No WND line: end once every text and picture has faded out.
    int end = 0;
    for (const MediaItem &item : items_)
        if (item.type == MediaItem::Text || item.type == MediaItem::Picture)
            end = std::max(end, item.start + item.display + (int)kFadeMs);
    return elapsedMs_ >= end;
}

void CampaignScene::render(Renderer &renderer, int screenW, int screenH,
                           const std::function<std::string(int)> &strings) const {
    if (!loaded_) return;
    const float sx = screenW / 800.0f, sy = screenH / 600.0f;
    auto drawFrame = [&](const SpriteFrame *frame, float x, float y, uint8_t alpha) {
        if (!frame || !frame->tex) return;
        const Quad quad{x * sx, y * sy, frame->w * sx, frame->h * sy,
                        frame->u, frame->v, frame->u + frame->w, frame->v + frame->h};
        if (alpha >= 255) renderer.draw(frame->tex, quad);
        else renderer.drawTinted(frame->tex, quad, 255, 255, 255, alpha);
    };
    drawFrame(background_, 0.0f, 0.0f, 255);
    // Pictures first, then text (0x525800 draws them in that order).
    for (size_t i = 0; i < items_.size(); ++i) {
        const MediaItem &item = items_[i];
        if (item.type != MediaItem::Picture) continue;
        const float level = itemLevel(item, elapsedMs_);
        if (level <= 0.0f) continue;
        drawFrame(pictures_[i], (float)item.x, (float)item.y, (uint8_t)(level * 255.0f + 0.5f));
    }
    for (const MediaItem &item : items_) {
        if (item.type != MediaItem::Text) continue;
        const float level = itemLevel(item, elapsedMs_);
        if (level <= 0.0f) continue;
        std::string text = item.text;
        if (!text.empty() && std::isdigit((unsigned char)text[0]) && strings) {
            const std::string localized = strings(std::atoi(text.c_str()));
            if (!localized.empty()) text = localized;
        }
        const float scale = fontScale(item.frame) * sy;
        const std::vector<std::string> lines = wrapText(text, item.width * sx, scale);
        drawUiText(renderer, lines, item.x * sx, item.y * sy, scale, item.red, item.green,
                   item.blue, (uint8_t)(level * 255.0f + 0.5f));
    }
}

} // namespace swgb
