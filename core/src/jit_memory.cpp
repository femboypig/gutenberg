#include "jit_internal.hpp"
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#endif

namespace gutenberg::jit {

ExecutableMemory::~ExecutableMemory() {
    if (address_)
        munmap(address_, size_);
}

std::unique_ptr<ExecutableMemory> ExecutableMemory::create(std::span<const std::uint8_t> code) {
    if (code.empty())
        return nullptr;
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return nullptr;
    auto result = std::unique_ptr<ExecutableMemory>(new ExecutableMemory);
    const auto page = static_cast<std::size_t>(pageSize);
    result->size_ = ((code.size() + page - 1) / page) * page;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    int protection = PROT_READ | PROT_WRITE;
    bool appleJit = false;
#if defined(__APPLE__)
#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
    // Sideloading does not imply executable-memory permission. A JIT enabler
    // must have marked this process as debugged before allocation is attempted.
    using Csops = int (*)(pid_t, unsigned int, void*, std::size_t);
    const auto csops = reinterpret_cast<Csops>(dlsym(RTLD_DEFAULT, "csops"));
    std::uint32_t status = 0;
    if (!csops || csops(getpid(), 0, &status, sizeof(status)) != 0 || !(status & 0x10000000U))
        return nullptr;
#endif
#ifdef MAP_JIT
    flags |= MAP_JIT;
    protection |= PROT_EXEC;
    appleJit = true;
#endif
#endif
    void* memory = mmap(nullptr, result->size_, protection, flags, -1, 0);
    if (memory == MAP_FAILED)
        return nullptr;
    result->address_ = memory;
#if defined(__APPLE__)
    using WriteProtect = void (*)(int);
    const auto writeProtect =
        reinterpret_cast<WriteProtect>(dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
    if (appleJit && writeProtect)
        writeProtect(0);
#endif
    std::memcpy(memory, code.data(), code.size());
#if defined(__APPLE__)
    sys_icache_invalidate(memory, code.size());
    if (appleJit && writeProtect)
        writeProtect(1);
#else
    __builtin___clear_cache(static_cast<char*>(memory), static_cast<char*>(memory) + code.size());
#endif
    if (!appleJit && mprotect(memory, result->size_, PROT_READ | PROT_EXEC) != 0)
        return nullptr;
    return result;
}

} // namespace gutenberg::jit
