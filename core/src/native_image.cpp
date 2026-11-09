#include "gutenberg/native_image.hpp"
#include "gutenberg/native_call.hpp"
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#endif

namespace gutenberg {
namespace {
[[noreturn]] void mappingError(const char* action) {
    throw Error(ErrorCode::native_link, std::string(action) + ": " + std::strerror(errno));
}
} // namespace
std::unique_ptr<NativeImage> NativeImage::load(std::shared_ptr<const ElfFile> file,
                                               const ElfFile::Resolver& resolver) {
    if (!file)
        throw Error(ErrorCode::native_link, "missing ELF image");
#if defined(__aarch64__) || defined(__arm64__)
    constexpr auto machine = ElfMachine::aarch64;
#elif defined(__x86_64__)
    constexpr auto machine = ElfMachine::x86_64;
#else
    throw Error(ErrorCode::unsupported, "native image execution is unavailable on this host");
    constexpr auto machine = static_cast<ElfMachine>(0);
#endif
    if (file->machine() != machine)
        throw Error(ErrorCode::unsupported, "ELF machine differs from the host instruction set");
    if (file->hasTls())
        throw Error(ErrorCode::unsupported, "ELF thread-local storage requires a guest TLS allocator");
#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
    using Csops = int (*)(pid_t, unsigned int, void*, std::size_t);
    const auto csops = reinterpret_cast<Csops>(dlsym(RTLD_DEFAULT, "csops"));
    std::uint32_t status = 0;
    if (!csops || csops(getpid(), 0, &status, sizeof(status)) != 0 || !(status & 0x10000000U))
        throw Error(ErrorCode::native_link,
                    "native ELF execution requires JIT to be enabled for this process");
#endif
    const auto pageResult = sysconf(_SC_PAGESIZE);
    if (pageResult <= 0)
        throw Error(ErrorCode::native_link, "cannot determine native memory page size");
    const auto page = static_cast<std::uint64_t>(pageResult);
    const auto start = file->imageStart() / page * page;
    const auto end = file->imageStart() + file->imageSize();
    if (end > UINT64_MAX - (page - 1))
        throw Error(ErrorCode::native_link, "native image page alignment overflows");
    const auto size = ((end + page - 1) / page * page) - start;
    if (!size || size > 512 * 1024 * 1024)
        throw Error(ErrorCode::limit_exceeded, "native image mapping budget exceeded");
    auto result = std::unique_ptr<NativeImage>(new NativeImage);
    result->file_ = std::move(file);
    result->size_ = static_cast<std::size_t>(size);
    void* memory = mmap(nullptr, result->size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED)
        mappingError("cannot reserve native image memory");
    result->memory_ = memory;
    result->loadBias_ = reinterpret_cast<std::uintptr_t>(memory) - start;
    const auto offset = static_cast<std::size_t>(result->file_->imageStart() - start);
    auto image = std::span(static_cast<std::uint8_t*>(memory), result->size_).subspan(offset);
    result->file_->map(image);
    std::unordered_map<std::string, std::uint64_t> resolved;
    result->file_->relocate(
        image, result->loadBias_, [&](std::string_view name) -> std::optional<std::uint64_t> {
            if (const auto found = resolved.find(std::string(name)); found != resolved.end())
                return found->second;
            const auto value = resolver ? resolver(name) : std::nullopt;
            if (value)
                resolved.emplace(name, *value);
            return value;
        });
    for (const auto& symbol : result->file_->symbols())
        if (symbol.type == 2)
            if (const auto found = resolved.find(symbol.name); found != resolved.end())
                result->importedFunctions_.insert(found->second);
    std::vector<int> protections(result->size_ / static_cast<std::size_t>(page), PROT_NONE);
    for (const auto& segment : result->file_->segments()) {
        const auto first = (segment.address - start) / page;
        const auto last = (segment.address + segment.memorySize - 1 - start) / page;
        const int flags = ((segment.flags & 4) ? PROT_READ : 0) | ((segment.flags & 2) ? PROT_WRITE : 0) |
                          ((segment.flags & 1) ? PROT_EXEC : 0);
        for (auto n = first; n <= last; ++n)
            protections[static_cast<std::size_t>(n)] |= flags;
    }
#if defined(__APPLE__)
    sys_icache_invalidate(memory, result->size_);
#else
    __builtin___clear_cache(static_cast<char*>(memory), static_cast<char*>(memory) + result->size_);
#endif
    for (std::size_t first = 0; first < protections.size();) {
        const auto flags = protections[first];
        if ((flags & PROT_WRITE) && (flags & PROT_EXEC))
            throw Error(ErrorCode::unsupported, "ELF code and writable data share a host memory page");
        auto last = first + 1;
        while (last < protections.size() && protections[last] == flags)
            ++last;
        if (mprotect(static_cast<std::uint8_t*>(memory) + first * page,
                     (last - first) * static_cast<std::size_t>(page), flags) != 0)
            mappingError("cannot apply native image memory protection");
        first = last;
    }
    return result;
}
NativeImage::~NativeImage() {
    if (memory_)
        munmap(memory_, size_);
}
void* NativeImage::symbol(std::string_view name) const {
    if (finalized_)
        throw Error(ErrorCode::native_link, "native image has been finalized");
    const auto* symbol = file_->exported(name);
    if (!symbol)
        return nullptr;
    if (symbol->type == 6 || symbol->type == 10 || symbol->section == 0xfff2)
        throw Error(ErrorCode::unsupported, "native symbol requires TLS or indirect resolution");
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(symbol->section == 0xfff1 ? symbol->value : loadBias_ + symbol->value));
}
void NativeImage::call(std::uint64_t address) {
    if (!address || address == UINT64_MAX)
        return;
    if (!file_->contains(address - loadBias_, 1, 1) && !importedFunctions_.contains(address))
        throw Error(ErrorCode::native_link, "native constructor is outside executable modules");
    (void)invokeNative(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), {});
}
void NativeImage::callArray(std::pair<std::uint64_t, std::uint64_t> array, bool reverse) {
    if (!array.second)
        return;
    const auto* bytes =
        reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(loadBias_ + array.first));
    const ByteView pointers(std::span(bytes, static_cast<std::size_t>(array.second)), ErrorCode::native_link);
    const auto count = static_cast<std::size_t>(array.second / 8);
    for (std::size_t n = 0; n < count; ++n)
        call(pointers.u64((reverse ? count - n - 1 : n) * 8));
}
void NativeImage::initialize() {
    if (finalized_)
        throw Error(ErrorCode::native_link, "cannot initialize an unloaded native image");
    if (initialized_)
        return;
    initialized_ = true;
    if (file_->initializer())
        call(loadBias_ + file_->initializer());
    callArray(file_->initializerArray(), false);
}
void NativeImage::finalize() {
    if (finalized_ || !initialized_)
        return;
    finalized_ = true;
    callArray(file_->finalizerArray(), true);
    if (file_->finalizer())
        call(loadBias_ + file_->finalizer());
}
} // namespace gutenberg
