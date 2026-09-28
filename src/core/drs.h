// SPDX-License-Identifier: GPL-3.0-or-later
// DRS archive reader (Genie engine resource container).
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace swgb {

enum class DrsType : uint32_t {
    // Four-char codes as stored in the file (reversed, space padded).
    Slp = 0x736c7020,  // "slp "
    Wav = 0x77617620,  // "wav "
    Bina = 0x62696e61, // "bina"
    Unknown = 0,
};

struct DrsEntry {
    DrsType type;
    int32_t id;
    uint32_t offset;
    uint32_t size;
};

class DrsArchive {
public:
    ~DrsArchive();
    // Returns nullptr (and sets err) on failure.
    static std::unique_ptr<DrsArchive> open(const std::string &path, std::string *err = nullptr);

    const std::string &path() const { return path_; }
    const std::vector<DrsEntry> &entries() const { return entries_; }
    const DrsEntry *find(int32_t id) const;
    bool read(const DrsEntry &e, std::vector<uint8_t> &out);

private:
    DrsArchive() = default;
    std::string path_;
    FILE *fp_ = nullptr;
    std::vector<DrsEntry> entries_;
    std::unordered_map<int32_t, size_t> index_;
};

// Ordered set of archives. Archives added first win (add expansion archives
// like graphics_x1.drs before their base graphics.drs).
class ResourceSet {
public:
    bool add(const std::string &path, std::string *err = nullptr);
    bool has(int32_t id) const;
    bool read(int32_t id, std::vector<uint8_t> &out);
    const DrsEntry *find(int32_t id, DrsArchive **owner = nullptr) const;
    const std::vector<std::unique_ptr<DrsArchive>> &archives() const { return archives_; }

private:
    std::vector<std::unique_ptr<DrsArchive>> archives_;
};

} // namespace swgb
