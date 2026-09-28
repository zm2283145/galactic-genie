// SPDX-License-Identifier: GPL-3.0-or-later
// Genie campaign archive reader.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace swgb {

struct CpxEntry {
    uint32_t size = 0;
    uint32_t offset = 0;
    std::string identifier;
    std::string filename;
};

class CpxArchive {
public:
    ~CpxArchive();

    static std::unique_ptr<CpxArchive> open(const std::string &path, std::string *err = nullptr);

    const std::string &path() const { return path_; }
    const std::string &version() const { return version_; }
    const std::string &name() const { return name_; }
    const std::vector<CpxEntry> &entries() const { return entries_; }
    bool read(size_t index, std::vector<uint8_t> &out, std::string *err = nullptr) const;

private:
    CpxArchive() = default;

    std::string path_;
    std::string version_;
    std::string name_;
    FILE *fp_ = nullptr;
    uint64_t fileSize_ = 0;
    std::vector<CpxEntry> entries_;
};

} // namespace swgb
