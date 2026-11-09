#include "gutenberg/native_image.hpp"
#include <iostream>
#include <stdexcept>

using namespace gutenberg;
namespace {
long factor = 2;
unsigned finalizations = 0;
unsigned initializations = 0;
long hostAdd(long left, long right) {
    if (left == -7)
        ++finalizations;
    if (left == 3 && right == 4)
        ++initializations;
    return left + right;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
long noArguments(void* function) {
    require(function != nullptr, "missing native function");
    return reinterpret_cast<long (*)()>(function)();
}
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
long oneArgument(void* function, long value) {
    require(function != nullptr, "missing native function");
    return reinterpret_cast<long (*)(long)>(function)(value);
}
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
long* address(void* function) {
    require(function != nullptr, "missing native pointer function");
    return reinterpret_cast<long* (*)()>(function)();
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected native fixture directory");
#if defined(__aarch64__) || defined(__arm64__)
        const std::string machine = "aarch64", other = "x86_64";
#else
        const std::string machine = "x86_64", other = "aarch64";
#endif
        const auto root = std::filesystem::path(argv[1]);
        for (const auto* variant : {"rela", "packed"}) {
            factor = 2;
            const auto before = finalizations;
            const auto beforeInitialization = initializations;
            const auto path = root / machine / ("libconformance-" + std::string(variant) + ".so");
            const auto file = std::make_shared<ElfFile>(readFile(path));
            bool rejected = false;
            try {
                (void)NativeImage::load(file);
            } catch (const Error& error) {
                rejected = error.code() == ErrorCode::native_link;
            }
            require(rejected, "unresolved strong imports must prevent native loading");
            auto image = NativeImage::load(file, [](std::string_view name) -> std::optional<std::uint64_t> {
                if (name == "gutenberg_host_add")
                    return reinterpret_cast<std::uintptr_t>(&hostAdd);
                if (name == "gutenberg_host_factor")
                    return reinterpret_cast<std::uintptr_t>(&factor);
                return std::nullopt;
            });
            require(!image->symbol("missing"), "missing export lookup");
            require(noArguments(image->symbol("native_value")) == 0, "BSS must be zero before constructors");
            image->initialize();
            image->initialize();
            require(initializations == beforeInitialization + 1, "native constructors must run once");
            require(noArguments(image->symbol("native_value")) == 7, "native constructor did not run");
            require(oneArgument(image->symbol("native_accumulate"), 3) == 20,
                    "PLT function and GOT data imports");
            factor = 3;
            require(oneArgument(image->symbol("native_accumulate"), 1) == 63, "writable native data");
            require(*address(image->symbol("native_address")) == 63, "internal native data address");
            require(address(image->symbol("native_external_address")) == &factor,
                    "external native data address");
            image->finalize();
            image->finalize();
            require(finalizations == before + 1, "native finalizers must run once");
            std::cout << "native " << machine << ' ' << variant << " execution passed\n";
        }
        bool mismatch = false;
        try {
            (void)NativeImage::load(
                std::make_shared<ElfFile>(readFile(root / other / "libconformance-rela.so")));
        } catch (const Error& error) {
            mismatch = error.code() == ErrorCode::unsupported;
        }
        require(mismatch, "foreign instructions must not execute on the host");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
