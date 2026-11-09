#pragma once
#include <bit>
#include <cstdint>
#include <span>

namespace gutenberg {
struct NativeArgument {
    enum class Kind { integer, floating } kind;
    std::uint64_t bits;
    static NativeArgument integer(std::uint64_t value) {
        return {Kind::integer, value};
    }
    static NativeArgument floating(float value) {
        return {Kind::floating, std::bit_cast<std::uint32_t>(value)};
    }
    static NativeArgument floating(double value) {
        return {Kind::floating, std::bit_cast<std::uint64_t>(value)};
    }
};
struct NativeResult {
    std::uint64_t integer = 0, floating = 0;
};

// Scalar calls use Android's AAPCS64 or x86-64 System V ABI, including stack spill slots.
[[nodiscard]] NativeResult invokeNative(void* function, std::span<const NativeArgument> arguments);
} // namespace gutenberg
