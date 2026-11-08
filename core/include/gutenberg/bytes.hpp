#pragma once

#include "error.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gutenberg {

using Bytes = std::vector<std::uint8_t>;

class ByteView {
  public:
    explicit ByteView(std::span<const std::uint8_t> bytes, ErrorCode code = ErrorCode::verification)
        : bytes_(bytes), code_(code) {}
    [[nodiscard]] std::size_t size() const noexcept {
        return bytes_.size();
    }
    [[nodiscard]] std::uint8_t u8(std::size_t offset) const;
    [[nodiscard]] std::uint16_t u16(std::size_t offset) const;
    [[nodiscard]] std::uint32_t u32(std::size_t offset) const;
    [[nodiscard]] std::uint64_t u64(std::size_t offset) const;
    [[nodiscard]] std::span<const std::uint8_t> slice(std::size_t offset, std::size_t count) const;
    [[nodiscard]] std::string text(std::size_t offset, std::size_t count) const;
    [[nodiscard]] std::uint32_t uleb(std::size_t& cursor) const;
    [[nodiscard]] std::int32_t sleb(std::size_t& cursor) const;
    void require(std::size_t offset, std::size_t count) const;

  private:
    std::span<const std::uint8_t> bytes_;
    ErrorCode code_;
};

[[nodiscard]] Bytes readFile(const std::filesystem::path& path, std::size_t limit = 512 * 1024 * 1024);
[[nodiscard]] std::string utf8(std::u16string_view text);
[[nodiscard]] std::string utf8Replacing(std::u16string_view text, char16_t replacement = u'\ufffd');
[[nodiscard]] std::u16string utf16Replacing(std::string_view text);

} // namespace gutenberg
