#include "jit_internal.hpp"
#include <map>
#include <limits>

namespace gutenberg::jit {
namespace {
constexpr auto failure = std::numeric_limits<std::size_t>::max();
constexpr auto epilogue = failure - 1;
struct Emitter {
    Bytes bytes;
    std::map<std::size_t, std::size_t> labels;
    std::vector<std::pair<std::size_t, std::size_t>> patches;
    void emit(std::initializer_list<std::uint8_t> values) { bytes.insert(bytes.end(), values); }
    void u32(std::uint32_t n) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(n >> (i * 8))); }
    void load(unsigned reg, bool right = false) { emit({0x41, 0x8b, static_cast<std::uint8_t>(right ? 0x8d : 0x85)}); u32(reg * 4); }
    void store(unsigned reg) { emit({0x41, 0x89, 0x85}); u32(reg * 4); }
    void constant(std::uint32_t n, bool right = false) { emit({static_cast<std::uint8_t>(right ? 0xb9 : 0xb8)}); u32(n); }
    void jump(std::size_t target) { emit({0xe9}); patches.emplace_back(bytes.size(), target); u32(0); }
    void conditional(std::uint8_t condition, std::size_t target) { emit({0x0f, condition}); patches.emplace_back(bytes.size(), target); u32(0); }
    void label(std::size_t id) { labels[id] = bytes.size(); }
    void patch() {
        for (auto [at, target] : patches) {
            const auto destination = labels.at(target);
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(at + 4);
            if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max())
                throw Error(ErrorCode::limit_exceeded, "jit branch displacement overflow");
            const auto bits = static_cast<std::uint32_t>(delta);
            for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(bits >> (i * 8));
        }
    }
};
}

Bytes emitX64(const std::vector<Instruction>& instructions) {
    Emitter e;
    e.emit({0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xec, 0x08}); // preserve r12/r13; align stack for calls
    e.emit({0x49, 0x89, 0xfc, 0x4d, 0x8b, 0x2c, 0x24}); // frame in r12, guest registers in r13
    for (const auto& ir : instructions) {
        e.label(ir.pc);
        e.emit({0x4c, 0x89, 0xe7, 0x49, 0x8b, 0x44, 0x24, 0x08, 0xff, 0xd0, 0x84, 0xc0});
        e.conditional(0x84, failure);
        if (ir.operation == Operation::nop) continue;
        if (ir.operation == Operation::jump) { e.jump(ir.target); continue; }
        if (ir.operation == Operation::returnVoid || ir.operation == Operation::return_) {
            if (ir.operation == Operation::return_) e.load(ir.left); else e.emit({0x31, 0xc0});
            e.emit({0x49, 0x89, 0x44, 0x24, 0x18, 0x31, 0xc0}); e.jump(epilogue); continue;
        }
        if (ir.operation == Operation::constant) e.constant(ir.constant);
        else {
            e.load(ir.left);
            if (ir.operation != Operation::move && ir.operation != Operation::negate && ir.operation != Operation::complement) {
                if (ir.literal) e.constant(ir.constant, true); else e.load(ir.right, true);
            }
            switch (ir.operation) {
                case Operation::move: break;
                case Operation::negate: e.emit({0xf7, 0xd8}); break;
                case Operation::complement: e.emit({0xf7, 0xd0}); break;
                case Operation::add: e.emit({0x01, 0xc8}); break;
                case Operation::subtract:
                    if (ir.reverse) e.emit({0x91}); // xchg eax, ecx
                    e.emit({0x29, 0xc8}); break;
                case Operation::multiply: e.emit({0x0f, 0xaf, 0xc1}); break;
                case Operation::and_: e.emit({0x21, 0xc8}); break;
                case Operation::or_: e.emit({0x09, 0xc8}); break;
                case Operation::xor_: e.emit({0x31, 0xc8}); break;
                case Operation::shiftLeft: e.emit({0xd3, 0xe0}); break;
                case Operation::shiftRight: e.emit({0xd3, 0xf8}); break;
                case Operation::unsignedShiftRight: e.emit({0xd3, 0xe8}); break;
                default:
                    if (ir.operation >= Operation::equal) {
                        const std::uint8_t conditions[]{0x84, 0x85, 0x8c, 0x8d, 0x8f, 0x8e};
                        e.emit({0x39, 0xc8}); e.conditional(conditions[static_cast<unsigned>(ir.operation) - static_cast<unsigned>(Operation::equal)], ir.target); continue;
                    }
                    throw Error(ErrorCode::verification, "invalid x64 jit operation");
            }
        }
        e.store(ir.destination);
    }
    e.label(failure); e.constant(1);
    e.label(epilogue); e.emit({0x48, 0x83, 0xc4, 0x08, 0x41, 0x5d, 0x41, 0x5c, 0xc3});
    e.patch(); return std::move(e.bytes);
}

}
