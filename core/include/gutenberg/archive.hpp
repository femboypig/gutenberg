#pragma once

#include "bytes.hpp"
#include <map>
#include <memory>
#include <string_view>

namespace gutenberg {

struct ArchiveLimits {
    std::size_t maxEntryBytes = 256 * 1024 * 1024;
    std::uint64_t maxTotalBytes = 1024ULL * 1024 * 1024;
    std::size_t maxEntries = 100000;
};

struct ArchiveEntry {
    std::string name;
    std::uint16_t method;
    std::uint16_t flags;
    std::uint32_t crc;
    std::uint32_t compressedSize;
    std::uint32_t size;
    std::uint32_t localOffset;
};

class Archive {
  public:
    explicit Archive(Bytes bytes, ArchiveLimits limits = {});
    [[nodiscard]] const std::map<std::string, ArchiveEntry, std::less<>>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] bool contains(std::string_view name) const {
        return entries_.contains(name);
    }
    [[nodiscard]] Bytes read(std::string_view name) const;

  private:
    Bytes bytes_;
    ArchiveLimits limits_;
    std::map<std::string, ArchiveEntry, std::less<>> entries_;
};

} // namespace gutenberg
