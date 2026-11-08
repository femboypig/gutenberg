#include "gutenberg/archive.hpp"
#include <algorithm>
#include <limits>
#include <zlib.h>

namespace gutenberg {
namespace {
void validPath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != path.npos || path.find('\0') != path.npos)
        throw Error(ErrorCode::malformed_archive, "invalid archive path");
    std::size_t start = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == path.npos ? path.size() - start : end - start);
        if (part == ".." || part == "." || part.find(':') != part.npos)
            throw Error(ErrorCode::malformed_archive, "unsafe archive path");
        if (end == path.npos)
            break;
        start = end + 1;
    }
}
} // namespace

Archive::Archive(Bytes bytes, ArchiveLimits limits) : bytes_(std::move(bytes)), limits_(limits) {
    const ByteView data(bytes_, ErrorCode::malformed_archive);
    if (data.size() < 22)
        throw Error(ErrorCode::malformed_archive, "missing zip directory");
    std::size_t eocd = data.size() - 22;
    const auto lower = data.size() > 65557 ? data.size() - 65557 : 0;
    for (;;) {
        if (data.u32(eocd) == 0x06054b50 && data.u16(eocd + 20) == data.size() - eocd - 22)
            break;
        if (eocd == lower)
            throw Error(ErrorCode::malformed_archive, "missing zip directory");
        --eocd;
    }
    if (data.u16(eocd + 4) || data.u16(eocd + 6) || data.u16(eocd + 8) != data.u16(eocd + 10))
        throw Error(ErrorCode::unsupported, "multi-disk zip is unsupported");
    const auto count = data.u16(eocd + 10);
    const auto directorySize = data.u32(eocd + 12);
    const auto directoryOffset = data.u32(eocd + 16);
    if (count == 0xffff || directoryOffset == 0xffffffff || directorySize == 0xffffffff)
        throw Error(ErrorCode::unsupported, "zip64 is unsupported");
    if (count > limits_.maxEntries)
        throw Error(ErrorCode::limit_exceeded, "too many archive entries");
    data.require(directoryOffset, directorySize);
    if (directoryOffset > eocd || directorySize > eocd - directoryOffset)
        throw Error(ErrorCode::malformed_archive, "overlapping zip directory");
    std::size_t cursor = directoryOffset;
    std::uint64_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        data.require(cursor, 46);
        if (data.u32(cursor) != 0x02014b50)
            throw Error(ErrorCode::malformed_archive, "invalid zip entry");
        const auto nameSize = data.u16(cursor + 28);
        const auto extraSize = data.u16(cursor + 30);
        const auto commentSize = data.u16(cursor + 32);
        ArchiveEntry entry{data.text(cursor + 46, nameSize),
                           data.u16(cursor + 10),
                           data.u16(cursor + 8),
                           data.u32(cursor + 16),
                           data.u32(cursor + 20),
                           data.u32(cursor + 24),
                           data.u32(cursor + 42)};
        validPath(entry.name);
        if (entry.flags & 1)
            throw Error(ErrorCode::unsupported, "encrypted zip entry");
        if (entry.method != 0 && entry.method != 8)
            throw Error(ErrorCode::unsupported, "unsupported zip compression");
        if (entry.size > limits_.maxEntryBytes || (total += entry.size) > limits_.maxTotalBytes)
            throw Error(ErrorCode::limit_exceeded, "archive expansion limit exceeded");
        if (!entries_.emplace(entry.name, entry).second)
            throw Error(ErrorCode::malformed_archive, "duplicate zip entry");
        cursor += 46 + std::size_t(nameSize) + extraSize + commentSize;
        if (cursor > std::size_t(directoryOffset) + directorySize)
            throw Error(ErrorCode::malformed_archive, "zip entry exceeds directory");
    }
    if (cursor != std::size_t(directoryOffset) + directorySize)
        throw Error(ErrorCode::malformed_archive, "inconsistent zip directory size");
}

Bytes Archive::read(std::string_view name) const {
    const auto found = entries_.find(name);
    if (found == entries_.end())
        throw Error(ErrorCode::io, "missing archive entry: " + std::string(name));
    const auto& entry = found->second;
    const ByteView data(bytes_, ErrorCode::malformed_archive);
    const std::size_t local = entry.localOffset;
    data.require(local, 30);
    if (data.u32(local) != 0x04034b50 || data.u16(local + 8) != entry.method ||
        data.u16(local + 6) != entry.flags)
        throw Error(ErrorCode::malformed_archive, "inconsistent local zip header");
    const auto nameSize = data.u16(local + 26);
    if (data.text(local + 30, nameSize) != entry.name)
        throw Error(ErrorCode::malformed_archive, "inconsistent local zip filename");
    const auto source = data.slice(local + 30 + nameSize + data.u16(local + 28), entry.compressedSize);
    Bytes output(entry.size);
    if (entry.method == 0) {
        if (source.size() != output.size())
            throw Error(ErrorCode::malformed_archive, "invalid stored entry size");
        std::copy(source.begin(), source.end(), output.begin());
    } else {
        z_stream stream{};
        stream.next_in = const_cast<Bytef*>(source.data());
        stream.avail_in = static_cast<uInt>(source.size());
        // zlib still needs an output byte for a valid empty stream.
        std::uint8_t empty = 0;
        stream.next_out = output.empty() ? &empty : output.data();
        stream.avail_out = output.empty() ? 1 : static_cast<uInt>(output.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            throw Error(ErrorCode::io, "cannot initialize inflate");
        const auto status = inflate(&stream, Z_FINISH);
        const bool valid =
            status == Z_STREAM_END && stream.total_out == output.size() && stream.total_in == source.size();
        inflateEnd(&stream);
        if (!valid)
            throw Error(ErrorCode::malformed_archive, "invalid deflate stream");
    }
    const auto crc = crc32(0, output.data(), static_cast<uInt>(output.size()));
    if (crc != entry.crc)
        throw Error(ErrorCode::malformed_archive, "zip checksum mismatch: " + entry.name);
    return output;
}

} // namespace gutenberg
