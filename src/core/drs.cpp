// SPDX-License-Identifier: GPL-3.0-or-later
#include "drs.h"

#include "bytes.h"

#include <cstring>

namespace swgb {

DrsArchive::~DrsArchive() {
    if (fp_) fclose(fp_);
}

static bool readAt(FILE *fp, uint32_t off, void *dst, size_t n) {
    if (fseek(fp, (long)off, SEEK_SET) != 0) return false;
    return fread(dst, 1, n, fp) == n;
}

std::unique_ptr<DrsArchive> DrsArchive::open(const std::string &path, std::string *err) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        if (err) *err = "cannot open " + path;
        return nullptr;
    }
    std::unique_ptr<DrsArchive> a(new DrsArchive());
    a->path_ = path;
    a->fp_ = fp;

    // AoE/AoK use a 40-byte copyright string, SWGB uses 60. Detect by looking
    // for the version string ("1.00") right after the copyright.
    uint8_t hdr[84];
    if (!readAt(fp, 0, hdr, sizeof(hdr))) {
        if (err) *err = "short DRS header: " + path;
        return nullptr;
    }
    size_t copyLen = 0;
    if (memcmp(hdr + 60, "1.00", 4) == 0) copyLen = 60;
    else if (memcmp(hdr + 40, "1.00", 4) == 0) copyLen = 40;
    else {
        if (err) *err = "unrecognised DRS header: " + path;
        return nullptr;
    }
    ByteReader h(hdr + copyLen, sizeof(hdr) - copyLen);
    h.skip(4 + 12); // version, file type ("tribe")
    int32_t tableCount = h.i32();
    h.i32(); // offset of first file
    if (tableCount <= 0 || tableCount > 64) {
        if (err) *err = "bad DRS table count: " + path;
        return nullptr;
    }

    std::vector<uint8_t> tables(tableCount * 12);
    if (!readAt(fp, (uint32_t)(copyLen + 4 + 12 + 8), tables.data(), tables.size())) {
        if (err) *err = "short DRS table list: " + path;
        return nullptr;
    }
    ByteReader t(tables);
    for (int32_t i = 0; i < tableCount; i++) {
        uint32_t ext = t.u32();
        uint32_t off = t.u32();
        uint32_t count = t.u32();
        if (count > 200000) {
            if (err) *err = "bad DRS table size: " + path;
            return nullptr;
        }
        std::vector<uint8_t> files(count * 12);
        if (count && !readAt(fp, off, files.data(), files.size())) {
            if (err) *err = "short DRS file table: " + path;
            return nullptr;
        }
        ByteReader f(files);
        DrsType type = DrsType::Unknown;
        if (ext == (uint32_t)DrsType::Slp || ext == (uint32_t)DrsType::Wav || ext == (uint32_t)DrsType::Bina)
            type = (DrsType)ext;
        for (uint32_t j = 0; j < count; j++) {
            DrsEntry e;
            e.type = type;
            e.id = f.i32();
            e.offset = f.u32();
            e.size = f.u32();
            a->index_.emplace(e.id, a->entries_.size());
            a->entries_.push_back(e);
        }
    }
    return a;
}

const DrsEntry *DrsArchive::find(int32_t id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &entries_[it->second];
}

bool DrsArchive::read(const DrsEntry &e, std::vector<uint8_t> &out) {
    out.resize(e.size);
    return readAt(fp_, e.offset, out.data(), e.size);
}

bool ResourceSet::add(const std::string &path, std::string *err) {
    auto a = DrsArchive::open(path, err);
    if (!a) return false;
    archives_.push_back(std::move(a));
    return true;
}

const DrsEntry *ResourceSet::find(int32_t id, DrsArchive **owner) const {
    for (auto &a : archives_) {
        if (const DrsEntry *e = a->find(id)) {
            if (owner) *owner = a.get();
            return e;
        }
    }
    return nullptr;
}

bool ResourceSet::has(int32_t id) const { return find(id) != nullptr; }

bool ResourceSet::read(int32_t id, std::vector<uint8_t> &out) {
    DrsArchive *owner = nullptr;
    const DrsEntry *e = find(id, &owner);
    return e && owner->read(*e, out);
}

bool ResourceSet::readLatest(int32_t id, std::vector<uint8_t> &out) {
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
        if (const DrsEntry *e = (*it)->find(id)) return (*it)->read(*e, out);
    return false;
}

} // namespace swgb
