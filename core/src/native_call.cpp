#include "gutenberg/native_call.hpp"
#include "gutenberg/error.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace gutenberg {
namespace {
struct CallFrame {
    std::array<std::uint64_t, 8> general{}, floating{};
    const std::uint64_t* stack;
    std::size_t stackCount;
    NativeResult result;
};
static_assert(offsetof(CallFrame, floating) == 64);
static_assert(offsetof(CallFrame, stack) == 128);
static_assert(offsetof(CallFrame, stackCount) == 136);
static_assert(offsetof(CallFrame, result) == 144);
extern "C" void gutenberg_call_native(void* function, CallFrame* frame);
} // namespace

NativeResult invokeNative(void* function, std::span<const NativeArgument> arguments) {
    if (!function)
        throw Error(ErrorCode::native_link, "cannot call a null native function");
    if (arguments.size() > 512)
        throw Error(ErrorCode::limit_exceeded, "native argument budget exceeded");
#if defined(__aarch64__) || defined(__arm64__)
    constexpr std::size_t generalCount = 8;
#elif defined(__x86_64__)
    constexpr std::size_t generalCount = 6;
#else
    throw Error(ErrorCode::unsupported, "native scalar calls are unavailable on this host");
    constexpr std::size_t generalCount = 0;
#endif
    CallFrame frame;
    std::vector<std::uint64_t> stack;
    std::size_t general = 0, floating = 0;
    for (const auto& argument : arguments) {
        if (argument.kind == NativeArgument::Kind::floating) {
            if (floating < frame.floating.size())
                frame.floating[floating++] = argument.bits;
            else
                stack.push_back(argument.bits);
        } else if (general < generalCount)
            frame.general[general++] = argument.bits;
        else
            stack.push_back(argument.bits);
    }
    frame.stack = stack.data();
    frame.stackCount = stack.size();
#if defined(__aarch64__) || defined(__arm64__) || defined(__x86_64__)
    gutenberg_call_native(function, &frame);
#endif
    return frame.result;
}
} // namespace gutenberg
