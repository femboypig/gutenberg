#include "gutenberg/jit.hpp"
#include "jit_internal.hpp"
#include <map>
#include <set>

namespace gutenberg {
namespace {
using jit::Instruction;
using jit::Operation;

std::optional<std::vector<Instruction>> lower(const Code& code) {
    if (!code.tries.empty() || code.words.size() > 16384 || code.registers > 4096)
        return std::nullopt;
    std::vector<Instruction> output;
    auto word = [&](std::size_t at) {
        if (at >= code.words.size())
            throw Error(ErrorCode::verification, "truncated jit instruction");
        return code.words[at];
    };
    for (std::size_t pc = 0; pc < code.words.size();) {
        const auto first = word(pc);
        const unsigned op = first & 0xff, aa = first >> 8, a = aa & 15, b = aa >> 4;
        Instruction ir{Operation::nop, pc};
        std::size_t size = 1;
        auto relative = [&](std::int32_t offset) {
            const auto target = static_cast<std::int64_t>(pc) + offset;
            if (target < 0 || static_cast<std::uint64_t>(target) >= code.words.size())
                throw Error(ErrorCode::verification, "jit branch outside method");
            return static_cast<std::size_t>(target);
        };
        if (op == 0 && first == 0) {
        } else if (op >= 1 && op <= 3) {
            ir.operation = Operation::move;
            size = op;
            ir.destination = op == 3 ? word(pc + 1) : op == 2 ? aa : a;
            ir.left = op == 3 ? word(pc + 2) : op == 2 ? word(pc + 1) : b;
        } else if (op == 0x0e)
            ir.operation = Operation::returnVoid;
        else if (op == 0x0f) {
            ir.operation = Operation::return_;
            ir.left = aa;
        } else if (op >= 0x12 && op <= 0x15) {
            ir.operation = Operation::constant;
            ir.destination = op == 0x12 ? a : aa;
            if (op == 0x12)
                ir.constant = static_cast<std::uint32_t>(b < 8 ? static_cast<std::int32_t>(b)
                                                               : static_cast<std::int32_t>(b) - 16);
            else if (op == 0x13) {
                size = 2;
                ir.constant =
                    static_cast<std::uint32_t>(std::int32_t(std::bit_cast<std::int16_t>(word(pc + 1))));
            } else if (op == 0x14) {
                size = 3;
                ir.constant = std::uint32_t(word(pc + 1)) | (std::uint32_t(word(pc + 2)) << 16);
            } else {
                size = 2;
                ir.constant = std::uint32_t(word(pc + 1)) << 16;
            }
        } else if (op >= 0x28 && op <= 0x2a) {
            ir.operation = Operation::jump;
            if (op == 0x28)
                ir.target = relative(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(aa)));
            else if (op == 0x29) {
                size = 2;
                ir.target = relative(std::bit_cast<std::int16_t>(word(pc + 1)));
            } else {
                size = 3;
                ir.target = relative(std::bit_cast<std::int32_t>(std::uint32_t(word(pc + 1)) |
                                                                 (std::uint32_t(word(pc + 2)) << 16)));
            }
        } else if (op >= 0x32 && op <= 0x3d) {
            const Operation comparisons[]{Operation::equal,        Operation::notEqual, Operation::less,
                                          Operation::greaterEqual, Operation::greater,  Operation::lessEqual};
            ir.operation = comparisons[(op - 0x32) % 6];
            size = 2;
            ir.left = op >= 0x38 ? aa : a;
            ir.right = b;
            ir.literal = op >= 0x38;
            ir.target = relative(std::bit_cast<std::int16_t>(word(pc + 1)));
        } else if (op == 0x7b || op == 0x7c) {
            ir.operation = op == 0x7b ? Operation::negate : Operation::complement;
            ir.destination = a;
            ir.left = b;
        } else if ((op >= 0x90 && op <= 0x9a) || (op >= 0xb0 && op <= 0xba) || (op >= 0xd0 && op <= 0xe2)) {
            const Operation operations[]{Operation::add,
                                         Operation::subtract,
                                         Operation::multiply,
                                         Operation::nop,
                                         Operation::nop,
                                         Operation::and_,
                                         Operation::or_,
                                         Operation::xor_,
                                         Operation::shiftLeft,
                                         Operation::shiftRight,
                                         Operation::unsignedShiftRight};
            unsigned arithmetic;
            if (op <= 0x9a) {
                size = 2;
                arithmetic = op - 0x90;
                ir.destination = aa;
                ir.left = word(pc + 1) & 255;
                ir.right = word(pc + 1) >> 8;
            } else if (op <= 0xba) {
                arithmetic = op - 0xb0;
                ir.destination = a;
                ir.left = a;
                ir.right = b;
            } else {
                size = 2;
                ir.literal = true;
                const bool small = op >= 0xd8;
                arithmetic = op - (small ? 0xd8 : 0xd0);
                ir.destination = small ? aa : a;
                ir.left = small ? word(pc + 1) & 255 : b;
                ir.constant = static_cast<std::uint32_t>(
                    small ? std::int32_t(
                                std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(word(pc + 1) >> 8)))
                          : std::int32_t(std::bit_cast<std::int16_t>(word(pc + 1))));
                ir.reverse = arithmetic == 1;
            }
            if (arithmetic == 3 || arithmetic == 4)
                return std::nullopt;
            ir.operation = operations[arithmetic];
        } else
            return std::nullopt;
        if (size > code.words.size() - pc || ir.destination >= code.registers || ir.left >= code.registers ||
            (!ir.literal && ir.right >= code.registers))
            return std::nullopt;
        output.push_back(ir);
        pc += size;
    }
    std::set<std::size_t> starts;
    for (const auto& ir : output)
        starts.insert(ir.pc);
    for (const auto& ir : output)
        if ((ir.operation == Operation::jump || ir.operation >= Operation::equal) &&
            !starts.contains(ir.target))
            throw Error(ErrorCode::verification, "jit branch into instruction");
    if (output.empty())
        return std::nullopt;
    // The interpreter reports fallthrough as invalid; don't emit a native fallthrough.
    const auto last = output.back().operation;
    if (last != Operation::return_ && last != Operation::returnVoid && last != Operation::jump)
        return std::nullopt;
    return output;
}

struct Frame {
    std::uint32_t* registers;
    bool (*poll)(Frame*) noexcept;
    Runtime* runtime;
    std::uint64_t result;
    std::exception_ptr error;
};
static_assert(offsetof(Frame, result) == 24);
bool poll(Frame* frame) noexcept {
    try {
        frame->runtime->tick();
        return true;
    } catch (...) {
        frame->error = std::current_exception();
        return false;
    }
}
#if defined(__clang__)
__attribute__((no_sanitize("function")))
#endif
int enter(void* address, Frame* frame) {
    using Function = int (*)(Frame*);
    return reinterpret_cast<Function>(address)(frame);
}
} // namespace

struct JitEngine::Implementation {
    std::map<const Code*, std::unique_ptr<jit::ExecutableMemory>> cache;
    JitStatistics statistics;
    bool available = true;
};
JitEngine::JitEngine() : implementation_(std::make_unique<Implementation>()) {
#if !defined(__aarch64__) && !defined(__x86_64__)
    implementation_->available = false;
#endif
}
JitEngine::~JitEngine() = default;
const JitStatistics& JitEngine::statistics() const noexcept {
    return implementation_->statistics;
}
bool JitEngine::available() const noexcept {
    return implementation_->available;
}

std::optional<Value> JitEngine::execute(Runtime& runtime, const Code& code, Arguments args) {
    auto& state = *implementation_;
    if (!state.available || code.registers < code.incoming || args.size() != code.incoming)
        return std::nullopt;
    for (const auto& value : args)
        if (value.reference)
            return std::nullopt;
    auto entry = state.cache.find(&code);
    if (entry == state.cache.end()) {
        const auto ir = lower(code);
        std::unique_ptr<jit::ExecutableMemory> memory;
        if (ir) {
            Bytes machine;
#if defined(__aarch64__)
            machine = jit::emitAarch64(*ir);
#elif defined(__x86_64__)
            machine = jit::emitX64(*ir);
#endif
            memory = jit::ExecutableMemory::create(machine);
            if (!memory)
                state.available = false;
        }
        if (memory)
            ++state.statistics.compiledMethods;
        else
            ++state.statistics.interpretedMethods;
        entry = state.cache.emplace(&code, std::move(memory)).first;
    }
    if (!entry->second)
        return std::nullopt;
    std::vector<std::uint32_t> registers(code.registers);
    for (std::size_t n = 0; n < args.size(); ++n)
        registers[code.registers - code.incoming + n] = static_cast<std::uint32_t>(args[n].bits);
    Frame frame{registers.data(), poll, &runtime, 0, {}};
    const auto result = enter(entry->second->address(), &frame);
    if (frame.error)
        std::rethrow_exception(frame.error);
    if (result)
        throw Error(ErrorCode::verification, "jit execution failed");
    ++state.statistics.executions;
    return Value{frame.result, false};
}

} // namespace gutenberg
