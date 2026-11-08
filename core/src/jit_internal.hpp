#pragma once
#include "gutenberg/bytes.hpp"
#include <memory>
#include <optional>

namespace gutenberg::jit {

enum class Operation {
    nop,
    constant,
    move,
    negate,
    complement,
    add,
    subtract,
    multiply,
    and_,
    or_,
    xor_,
    shiftLeft,
    shiftRight,
    unsignedShiftRight,
    return_,
    returnVoid,
    jump,
    equal,
    notEqual,
    less,
    greaterEqual,
    greater,
    lessEqual
};

struct Instruction {
    Operation operation;
    std::size_t pc;
    unsigned destination = 0, left = 0, right = 0;
    bool literal = false, reverse = false;
    std::uint32_t constant = 0;
    std::size_t target = 0;
};

[[nodiscard]] Bytes emitAarch64(const std::vector<Instruction>& instructions);
[[nodiscard]] Bytes emitX64(const std::vector<Instruction>& instructions);

class ExecutableMemory {
  public:
    ~ExecutableMemory();
    static std::unique_ptr<ExecutableMemory> create(std::span<const std::uint8_t> code);
    [[nodiscard]] void* address() const noexcept {
        return address_;
    }

  private:
    void* address_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace gutenberg::jit
