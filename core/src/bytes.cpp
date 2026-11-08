#include "gutenberg/bytes.hpp"
#include <bit>
#include <fstream>
#include <limits>

namespace gutenberg {

void ByteView::require(std::size_t offset, std::size_t count) const {
    if (offset > size() || count > size() - offset)
        throw Error(code_, "truncated data at byte " + std::to_string(offset));
}

std::uint8_t ByteView::u8(std::size_t offset) const {
    require(offset, 1);
    return bytes_[offset];
}
std::uint16_t ByteView::u16(std::size_t offset) const {
    require(offset, 2);
    return static_cast<std::uint16_t>(bytes_[offset] | (static_cast<unsigned>(bytes_[offset + 1]) << 8));
}
std::uint32_t ByteView::u32(std::size_t offset) const {
    require(offset, 4);
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t(bytes_[offset + i]) << (8 * i);
    return value;
}
std::uint64_t ByteView::u64(std::size_t offset) const {
    return std::uint64_t(u32(offset)) | (std::uint64_t(u32(offset + 4)) << 32);
}
std::span<const std::uint8_t> ByteView::slice(std::size_t offset, std::size_t count) const {
    require(offset, count);
    return bytes_.subspan(offset, count);
}
std::string ByteView::text(std::size_t offset, std::size_t count) const {
    const auto data = slice(offset, count);
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}
std::uint32_t ByteView::uleb(std::size_t& cursor) const {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto byte = u8(cursor++);
        if (i == 4 && (byte & 0xf0))
            throw Error(code_, "overflowing uleb128");
        result |= std::uint32_t(byte & 0x7f) << (7 * i);
        if (!(byte & 0x80))
            return result;
    }
    throw Error(code_, "unterminated uleb128");
}
std::int32_t ByteView::sleb(std::size_t& cursor) const {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 5; ++i) {
        const auto byte = u8(cursor++);
        if (i == 4 && ((byte & 0x78) != 0 && (byte & 0x78) != 0x78))
            throw Error(code_, "overflowing sleb128");
        result |= std::uint32_t(byte & 0x7f) << (7 * i);
        if (!(byte & 0x80)) {
            if (i < 4 && (byte & 0x40))
                result |= ~std::uint32_t(0) << (7 * (i + 1));
            return std::bit_cast<std::int32_t>(result);
        }
    }
    throw Error(code_, "unterminated sleb128");
}

Bytes readFile(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw Error(ErrorCode::io, "cannot open " + path.filename().string());
    const auto end = file.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) > limit)
        throw Error(ErrorCode::limit_exceeded, "file exceeds size limit");
    Bytes bytes(static_cast<std::size_t>(end));
    file.seekg(0);
    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw Error(ErrorCode::io, "cannot read " + path.filename().string());
    return bytes;
}

std::string utf8(std::u16string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t c = text[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i >= text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                throw Error(ErrorCode::verification, "invalid utf-16 surrogate");
            c = 0x10000 + ((c - 0xd800) << 10) + (text[i] - 0xdc00);
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            throw Error(ErrorCode::verification, "unpaired utf-16 surrogate");
        }
        if (c < 0x80)
            result += static_cast<char>(c);
        else if (c < 0x800) {
            result += static_cast<char>(0xc0 | (c >> 6));
            result += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            result += static_cast<char>(0xe0 | (c >> 12));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            result += static_cast<char>(0xf0 | (c >> 18));
            result += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return result;
}

} // namespace gutenberg
