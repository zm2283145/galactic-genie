// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpx.h"

#include "bytes.h"

#include <cerrno>
#include <cstring>

namespace swgb {

namespace {

bool readAt(FILE *fp, uint32_t offset, void *dst, size_t size) {
    return fseek(fp, (long)offset, SEEK_SET) == 0 && fread(dst, 1, size, fp) == size;
}

} // namespace

CpxArchive::~CpxArchive() {
    if (fp_) fclose(fp_);
}

std::unique_ptr<CpxArchive> CpxArchive::open(const std::string &path, std::string *err) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        if (err) *err = "cannot open " + path + ": " + std::strerror(errno);
        return nullptr;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        if (err) *err = "cannot determine CPX size: " + path;
        fclose(fp);
        return nullptr;
    }
    const long fileSize = ftell(fp);
    if (fileSize < 264 || fseek(fp, 0, SEEK_SET) != 0) {
        if (err) *err = "short CPX header: " + path;
        fclose(fp);
        return nullptr;
    }

    std::vector<uint8_t> header(264);
    if (fread(header.data(), 1, header.size(), fp) != header.size()) {
        if (err) *err = "short CPX header: " + path;
        fclose(fp);
        return nullptr;
    }

    std::unique_ptr<CpxArchive> archive(new CpxArchive());
    archive->path_ = path;
    archive->fp_ = fp;
    archive->fileSize_ = (uint64_t)fileSize;
    try {
        ByteReader reader(header);
        archive->version_ = reader.fixedStr(4);
        archive->name_ = reader.fixedStr(256);
        const uint32_t count = reader.u32();
        if (archive->version_ != "1.00")
            throw FormatError("unsupported CPX version '" + archive->version_ + "'");
        if (count == 0 || count > 10000) throw FormatError("invalid CPX entry count");
        const uint64_t tableEnd = 264ull + (uint64_t)count * 520ull;
        if (tableEnd > archive->fileSize_) throw FormatError("CPX entry table exceeds file size");

        std::vector<uint8_t> table((size_t)count * 520);
        if (!readAt(fp, 264, table.data(), table.size())) throw FormatError("short CPX entry table");
        ByteReader entries(table);
        archive->entries_.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            CpxEntry entry;
            entry.size = entries.u32();
            entry.offset = entries.u32();
            entry.identifier = entries.fixedStr(255);
            entry.filename = entries.fixedStr(257);
            const uint64_t end = (uint64_t)entry.offset + entry.size;
            if (entry.offset < tableEnd || end > archive->fileSize_ || end < entry.offset)
                throw FormatError("CPX entry " + std::to_string(i + 1) + " has an invalid data range");
            archive->entries_.push_back(std::move(entry));
        }
    } catch (const std::exception &e) {
        if (err) *err = std::string("invalid CPX: ") + e.what();
        return nullptr;
    }
    return archive;
}

bool CpxArchive::read(size_t index, std::vector<uint8_t> &out, std::string *err) const {
    if (index >= entries_.size()) {
        if (err) *err = "CPX entry index out of range";
        return false;
    }
    const CpxEntry &entry = entries_[index];
    out.resize(entry.size);
    if (!out.empty() && !readAt(fp_, entry.offset, out.data(), out.size())) {
        if (err) *err = "failed to read CPX entry " + std::to_string(index + 1);
        out.clear();
        return false;
    }
    return true;
}

} // namespace swgb
