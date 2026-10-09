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

// Writes a campaign archive (version 1.00): the campaign name, then each
// scenario's identifier (its title), file name and SCX bytes.
struct CpxWriteEntry {
    std::string identifier;
    std::string filename;
    std::vector<uint8_t> data;
};
bool writeCpxBytes(const std::string &name, const std::vector<CpxWriteEntry> &entries,
                   std::vector<uint8_t> &out, std::string *err = nullptr);

} // namespace swgb
