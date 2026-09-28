// SPDX-License-Identifier: GPL-3.0-or-later
// Little-endian, bounds-checked byte reader used by every format loader.
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace swgb {

class FormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ByteReader {
public:
    ByteReader() = default;
    ByteReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const std::vector<uint8_t> &v) : data_(v.data()), size_(v.size()) {}

    size_t pos() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return size_ - pos_; }
    const uint8_t *data() const { return data_; }

    void seek(size_t p) {
        if (p > size_) throw FormatError("seek out of range");
        pos_ = p;
    }
    void skip(size_t n) { need(n); pos_ += n; }

    template <typename T> T get() {
        need(sizeof(T));
        T v;
        std::memcpy(&v, data_ + pos_, sizeof(T)); // host is little-endian (x86, ARM)
        pos_ += sizeof(T);
        return v;
    }
    uint8_t u8() { return get<uint8_t>(); }
    int8_t i8() { return get<int8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    int16_t i16() { return get<int16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    int32_t i32() { return get<int32_t>(); }
    float f32() { return get<float>(); }

    template <typename T> void array(T *out, size_t n) {
        for (size_t i = 0; i < n; i++) out[i] = get<T>();
    }
    template <typename T> std::vector<T> vec(size_t n) {
        std::vector<T> v(n);
        for (size_t i = 0; i < n; i++) v[i] = get<T>();
        return v;
    }

    // Fixed-size, NUL-padded string field.
    std::string fixedStr(size_t n) {
        need(n);
        const char *s = reinterpret_cast<const char *>(data_ + pos_);
        size_t len = strnlen(s, n);
        pos_ += n;
        return std::string(s, len);
    }

    const uint8_t *ptr(size_t n) {
        need(n);
        const uint8_t *p = data_ + pos_;
        pos_ += n;
        return p;
    }

private:
    void need(size_t n) const {
        if (n > size_ - pos_) throw FormatError("unexpected end of data");
    }

    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
};

} // namespace swgb
