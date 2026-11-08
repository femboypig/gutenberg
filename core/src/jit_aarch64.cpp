#include "jit_internal.hpp"
#include <limits>
#include <map>

namespace gutenberg::jit {
namespace {
constexpr auto failure = std::numeric_limits<std::size_t>::max();
constexpr auto epilogue = failure - 1;
struct Patch {
    std::size_t at, target;
    unsigned bits, shift;
};
struct Emitter {
    std::vector<std::uint32_t> words;
    std::map<std::size_t, std::size_t> labels;
    std::vector<Patch> patches;
    void emit(std::uint32_t word) {
        words.push_back(word);
    }
    void constant(std::uint32_t value, unsigned reg = 0) {
        emit(0x52800000 | ((value & 0xffff) << 5) | reg);
        if (value >> 16)
            emit(0x72a00000 | ((value >> 16) << 5) | reg);
    }
    void load(unsigned guest, unsigned native = 0) {
        emit(0xb9400000 | (guest << 10) | (19 << 5) | native);
    }
    void store(unsigned guest) {
        emit(0xb9000000 | (guest << 10) | (19 << 5));
    }
    void label(std::size_t id) {
        labels[id] = words.size();
    }
    void jump(std::size_t target) {
        patches.push_back({words.size(), target, 26, 0});
        emit(0x14000000);
    }
    void conditional(unsigned condition, std::size_t target) {
        patches.push_back({words.size(), target, 19, 5});
        emit(0x54000000 | condition);
    }
    void failed() {
        patches.push_back({words.size(), failure, 19, 5});
        emit(0x34000000);
    }
    Bytes finish() {
        for (const auto& patch : patches) {
            const auto delta =
                static_cast<std::int64_t>(labels.at(patch.target)) - static_cast<std::int64_t>(patch.at);
            const auto bound = std::int64_t(1) << (patch.bits - 1);
            if (delta < -bound || delta >= bound)
                throw Error(ErrorCode::limit_exceeded, "aarch64 jit branch displacement overflow");
            const auto mask = (std::uint32_t(1) << patch.bits) - 1;
            words[patch.at] |= (static_cast<std::uint32_t>(delta) & mask) << patch.shift;
        }
        Bytes bytes;
        bytes.reserve(words.size() * 4);
        for (auto word : words)
            for (unsigned i = 0; i < 4; ++i)
                bytes.push_back(static_cast<std::uint8_t>(word >> (i * 8)));
        return bytes;
    }
};
} // namespace

Bytes emitAarch64(const std::vector<Instruction>& instructions) {
    Emitter e;
    e.emit(0xa9be7bfd);
    e.emit(0xa90153f3);
    e.emit(0x910003fd); // save frame, link, x19, x20
    e.emit(0xaa0003f4);
    e.emit(0xf9400293); // frame in x20, guest registers in x19
    for (const auto& ir : instructions) {
        e.label(ir.pc);
        e.emit(0xaa1403e0);
        e.emit(0xf9400690);
        e.emit(0xd63f0200);
        e.failed();
        if (ir.operation == Operation::nop)
            continue;
        if (ir.operation == Operation::jump) {
            e.jump(ir.target);
            continue;
        }
        if (ir.operation == Operation::returnVoid || ir.operation == Operation::return_) {
            if (ir.operation == Operation::return_)
                e.load(ir.left);
            else
                e.constant(0);
            e.emit(0xf9000e80);
            e.constant(0);
            e.jump(epilogue);
            continue;
        }
        if (ir.operation == Operation::constant)
            e.constant(ir.constant);
        else {
            e.load(ir.left);
            if (ir.operation != Operation::move && ir.operation != Operation::negate &&
                ir.operation != Operation::complement) {
                if (ir.literal)
                    e.constant(ir.constant, 1);
                else
                    e.load(ir.right, 1);
            }
            switch (ir.operation) {
            case Operation::move:
                break;
            case Operation::negate:
                e.emit(0x4b0003e0);
                break;
            case Operation::complement:
                e.emit(0x2a2003e0);
                break;
            case Operation::add:
                e.emit(0x0b010000);
                break;
            case Operation::subtract:
                e.emit(ir.reverse ? 0x4b000020 : 0x4b010000);
                break;
            case Operation::multiply:
                e.emit(0x1b017c00);
                break;
            case Operation::and_:
                e.emit(0x0a010000);
                break;
            case Operation::or_:
                e.emit(0x2a010000);
                break;
            case Operation::xor_:
                e.emit(0x4a010000);
                break;
            case Operation::shiftLeft:
                e.emit(0x1ac12000);
                break;
            case Operation::shiftRight:
                e.emit(0x1ac12800);
                break;
            case Operation::unsignedShiftRight:
                e.emit(0x1ac12400);
                break;
            default:
                if (ir.operation >= Operation::equal) {
                    const unsigned conditions[]{0, 1, 11, 10, 12, 13};
                    e.emit(0x6b01001f);
                    e.conditional(conditions[static_cast<unsigned>(ir.operation) -
                                             static_cast<unsigned>(Operation::equal)],
                                  ir.target);
                    continue;
                }
                throw Error(ErrorCode::verification, "invalid aarch64 jit operation");
            }
        }
        e.store(ir.destination);
    }
    e.label(failure);
    e.constant(1);
    e.label(epilogue);
    e.emit(0xa94153f3);
    e.emit(0xa8c27bfd);
    e.emit(0xd65f03c0);
    return e.finish();
}

} // namespace gutenberg::jit
