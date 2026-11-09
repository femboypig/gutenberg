#include "gutenberg/native_call.hpp"
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
long noArguments(void* function) {
    require(function != nullptr, "missing native function");
    return std::bit_cast<long>(invokeNative(function, {}).integer);
}
long oneArgument(void* function, long value) {
    require(function != nullptr, "missing native function");
    const NativeArgument args[]{NativeArgument::integer(static_cast<std::uint64_t>(value))};
    return std::bit_cast<long>(invokeNative(function, args).integer);
}
long* address(void* function) {
    require(function != nullptr, "missing native pointer function");
    return reinterpret_cast<long*>(static_cast<std::uintptr_t>(invokeNative(function, {}).integer));
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
            std::vector<NativeArgument> mixed;
            for (unsigned n = 0; n < 10; ++n) {
                mixed.push_back(NativeArgument::integer(n + 1));
                mixed.push_back(n % 2 ? NativeArgument::floating(static_cast<float>(n) + 0.5f)
                                      : NativeArgument::floating(static_cast<double>(n) + 0.25));
            }
            const auto sum = invokeNative(image->symbol("native_mixed"), mixed);
            require(std::bit_cast<double>(sum.floating) == 433.75, "mixed register and stack argument lanes");
            const NativeArgument single[]{NativeArgument::floating(1.5f), NativeArgument::floating(2.f)};
            require(std::bit_cast<float>(static_cast<std::uint32_t>(
                        invokeNative(image->symbol("native_single"), single).floating)) == 3.25f,
                    "single precision native arguments and return");
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
