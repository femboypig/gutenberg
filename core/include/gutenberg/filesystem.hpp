#pragma once

#include "bytes.hpp"

namespace gutenberg {

class FileSandbox {
  public:
    FileSandbox(std::filesystem::path root, std::string package);
    [[nodiscard]] std::string normalize(std::string_view path) const;
    [[nodiscard]] std::filesystem::path resolve(std::string_view path) const;
    [[nodiscard]] std::string filesDirectory() const {
        return guestRoot_ + "/files";
    }
    [[nodiscard]] std::string cacheDirectory() const {
        return guestRoot_ + "/cache";
    }
    [[nodiscard]] Bytes read(std::string_view path, std::size_t limit = 16 * 1024 * 1024) const;
    void write(std::string_view path, std::span<const std::uint8_t> bytes, bool append = false) const;
    [[nodiscard]] bool exists(std::string_view path) const;
    [[nodiscard]] bool isDirectory(std::string_view path) const;
    [[nodiscard]] std::uint64_t length(std::string_view path) const;
    [[nodiscard]] bool createFile(std::string_view path) const;
    [[nodiscard]] bool createDirectories(std::string_view path) const;
    [[nodiscard]] bool remove(std::string_view path) const;
    [[nodiscard]] bool rename(std::string_view from, std::string_view to) const;
    [[nodiscard]] std::vector<std::string> list(std::string_view path) const;

  private:
    std::filesystem::path root_;
    std::string guestRoot_;
    std::string legacyRoot_;
};

} // namespace gutenberg
