#pragma once
#include "bytes.hpp"
#include <functional>
#include <optional>
#include <unordered_map>

namespace gutenberg {
enum class ElfMachine : std::uint16_t { x86_64 = 62, aarch64 = 183 };
struct ElfSegment {
    std::uint64_t address, offset, fileSize, memorySize, alignment;
    std::uint32_t flags;
};
struct ElfSymbol {
    std::string name;
    std::uint64_t value, size;
    std::uint16_t section;
    std::uint8_t binding, type, visibility;
};
struct ElfRelocation {
    std::uint64_t address;
    std::uint32_t type, symbol;
    std::int64_t addend;
    bool implicitAddend = false;
};
class ElfFile {
  public:
    using Resolver = std::function<std::optional<std::uint64_t>(std::string_view)>;
    explicit ElfFile(Bytes bytes);
    [[nodiscard]] ElfMachine machine() const noexcept {
        return machine_;
    }
    [[nodiscard]] const auto& segments() const noexcept {
        return segments_;
    }
    [[nodiscard]] const auto& symbols() const noexcept {
        return symbols_;
    }
    [[nodiscard]] const auto& relocations() const noexcept {
        return relocations_;
    }
    [[nodiscard]] const auto& dependencies() const noexcept {
        return dependencies_;
    }
    [[nodiscard]] const std::string& soname() const noexcept {
        return soname_;
    }
    [[nodiscard]] std::uint64_t imageStart() const noexcept {
        return imageStart_;
    }
    [[nodiscard]] std::uint64_t imageSize() const noexcept {
        return imageEnd_ - imageStart_;
    }
    [[nodiscard]] bool hasTls() const noexcept {
        return hasTls_;
    }
    [[nodiscard]] std::uint64_t initializer() const noexcept {
        return initializer_;
    }
    [[nodiscard]] std::uint64_t finalizer() const noexcept {
        return finalizer_;
    }
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> initializerArray() const noexcept {
        return initializerArray_;
    }
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> finalizerArray() const noexcept {
        return finalizerArray_;
    }
    [[nodiscard]] const ElfSymbol* exported(std::string_view name) const;
    [[nodiscard]] bool contains(std::uint64_t address, std::size_t count, std::uint32_t flags = 0) const;
    void map(std::span<std::uint8_t> image) const;
    void relocate(std::span<std::uint8_t> image, std::uint64_t loadBias, const Resolver& resolver) const;

  private:
    [[nodiscard]] std::size_t fileOffset(std::uint64_t address, std::size_t count) const;
    Bytes bytes_;
    ElfMachine machine_;
    std::vector<ElfSegment> segments_;
    std::vector<ElfSymbol> symbols_;
    std::unordered_map<std::string, std::size_t> exports_;
    std::vector<ElfRelocation> relocations_;
    std::vector<std::string> dependencies_;
    std::string soname_;
    std::uint64_t imageStart_ = 0, imageEnd_ = 0, initializer_ = 0, finalizer_ = 0;
    std::pair<std::uint64_t, std::uint64_t> initializerArray_{}, finalizerArray_{};
    bool hasTls_ = false, symbolic_ = false;
};
} // namespace gutenberg
