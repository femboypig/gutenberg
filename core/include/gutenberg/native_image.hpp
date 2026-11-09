#pragma once
#include "elf.hpp"
#include <memory>
#include <unordered_set>

namespace gutenberg {
class NativeImage {
  public:
    [[nodiscard]] static std::unique_ptr<NativeImage> load(std::shared_ptr<const ElfFile> file,
                                                           const ElfFile::Resolver& resolver = {});
    ~NativeImage();
    NativeImage(const NativeImage&) = delete;
    NativeImage& operator=(const NativeImage&) = delete;
    [[nodiscard]] void* symbol(std::string_view name) const;
    [[nodiscard]] std::uint64_t loadBias() const noexcept {
        return loadBias_;
    }
    void initialize();
    // Finalize while the owning runtime's guest services and dependencies remain alive.
    void finalize();

  private:
    NativeImage() = default;
    void call(std::uint64_t address);
    void callArray(std::pair<std::uint64_t, std::uint64_t> array, bool reverse);
    std::shared_ptr<const ElfFile> file_;
    std::unordered_set<std::uint64_t> importedFunctions_;
    void* memory_ = nullptr;
    std::size_t size_ = 0;
    std::uint64_t loadBias_ = 0;
    bool initialized_ = false, finalized_ = false;
};
} // namespace gutenberg
