#include "gutenberg/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace gutenberg {
namespace {
[[noreturn]] void invalid(std::string message) {
    throw Error(ErrorCode::verification, std::move(message));
}
std::int32_t signed32(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}
std::int64_t signed64(std::uint64_t value) {
    return std::bit_cast<std::int64_t>(value);
}
template <class I, class F> I javaIntegral(F value) {
    if (std::isnan(value))
        return 0;
    if (value >= static_cast<F>(std::numeric_limits<I>::max()))
        return std::numeric_limits<I>::max();
    if (value <= static_cast<F>(std::numeric_limits<I>::min()))
        return std::numeric_limits<I>::min();
    return static_cast<I>(value);
}

std::size_t width(const std::vector<std::uint16_t>& code, std::size_t at) {
    auto word = [&](std::size_t n) {
        if (at + n >= code.size())
            invalid("truncated instruction");
        return code[at + n];
    };
    const auto op = word(0) & 0xff;
    if (op == 0 && word(0) != 0) {
        if (word(0) == 0x100)
            return 4 + std::size_t(word(1)) * 2;
        if (word(0) == 0x200)
            return 2 + std::size_t(word(1)) * 4;
        if (word(0) == 0x300) {
            const auto count = std::uint32_t(word(2)) | (std::uint32_t(word(3)) << 16);
            const auto bytes = std::uint64_t(count) * word(1);
            if (bytes > code.size() * 2)
                invalid("array payload exceeds method");
            return 4 + static_cast<std::size_t>((bytes + 1) / 2);
        }
        invalid("invalid instruction payload");
    }
    if (op == 0x18)
        return 5;
    if (op == 0xfa || op == 0xfb)
        return 4;
    if (op == 3 || op == 6 || op == 9 || op == 0x14 || op == 0x17 || op == 0x1b ||
        (op >= 0x24 && op <= 0x26) || (op >= 0x2a && op <= 0x2c) || (op >= 0x6e && op <= 0x72) ||
        (op >= 0x74 && op <= 0x78) || op == 0xfc || op == 0xfd)
        return 3;
    if (op == 2 || op == 5 || op == 8 || op == 0x13 || op == 0x15 || op == 0x16 || op == 0x19 || op == 0x1a ||
        (op >= 0x1c && op <= 0x20 && op != 0x1d && op != 0x1e) || op == 0x22 || op == 0x23 || op == 0x29 ||
        (op >= 0x2d && op <= 0x3d) || (op >= 0x44 && op <= 0x6d) || (op >= 0x90 && op <= 0xaf) ||
        (op >= 0xd0 && op <= 0xe2) || op == 0xfe || op == 0xff)
        return 2;
    return 1;
}

std::vector<bool> boundaries(const Code& code) {
    std::vector<bool> valid(code.words.size(), false);
    for (std::size_t pc = 0; pc < code.words.size();) {
        const auto size = width(code.words, pc);
        if (size > code.words.size() - pc)
            invalid("instruction exceeds method");
        valid[pc] = code.words[pc] < 0x100 || (code.words[pc] & 0xff) != 0;
        pc += size;
    }
    return valid;
}
} // namespace

Value Runtime::execute(const DexFile& dex, const Code& code, Arguments arguments, std::string_view context) {
    if (arguments.size() != code.incoming || code.incoming > code.registers)
        invalid("invalid frame arguments");
    const auto starts = boundaries(code);
    std::vector<Value> registers(code.registers);
    std::copy(arguments.begin(), arguments.end(), registers.end() - code.incoming);
    auto reg = [&](std::size_t index) -> Value& {
        if (index >= registers.size())
            invalid("register index outside frame");
        return registers[index];
    };
    auto wide = [&](std::size_t index) {
        return (reg(index).bits & 0xffffffff) | ((reg(index + 1).bits & 0xffffffff) << 32);
    };
    auto putWide = [&](std::size_t index, std::uint64_t bits) {
        reg(index) = {bits & 0xffffffff, false};
        reg(index + 1) = {bits >> 32, false};
    };
    auto word = [&](std::size_t at) -> std::uint16_t {
        if (at >= code.words.size())
            invalid("program counter outside method");
        return code.words[at];
    };
    auto u32 = [&](std::size_t at) { return std::uint32_t(word(at)) | (std::uint32_t(word(at + 1)) << 16); };
    auto target = [&](std::size_t base, std::int32_t relative, bool executable = true) {
        const auto destination = static_cast<std::int64_t>(base) + relative;
        if (destination < 0 || static_cast<std::uint64_t>(destination) >= code.words.size())
            invalid("branch outside method");
        const auto address = static_cast<std::size_t>(destination);
        if (executable && !starts[address])
            invalid("branch into instruction or payload");
        return address;
    };
    Value result, exception;
    bool resultReady = false;
    std::size_t pc = 0;
    for (;;) {
        tick();
        const auto start = pc;
        if (pc >= starts.size() || !starts[pc])
            invalid("execution reached non-instruction");
        const auto first = word(pc);
        const unsigned op = first & 0xff, aa = first >> 8, a = aa & 15, b = aa >> 4;
        const auto size = width(code.words, pc);
        pc += size;
        const bool previousResult = resultReady;
        resultReady = false;
        try {
            switch (op) {
            case 0x00:
                break;
            case 0x01:
            case 0x07:
                reg(a) = reg(b);
                break;
            case 0x02:
            case 0x08:
                reg(aa) = reg(word(start + 1));
                break;
            case 0x03:
            case 0x09:
                reg(word(start + 1)) = reg(word(start + 2));
                break;
            case 0x04:
                putWide(a, wide(b));
                break;
            case 0x05:
                putWide(aa, wide(word(start + 1)));
                break;
            case 0x06:
                putWide(word(start + 1), wide(word(start + 2)));
                break;
            case 0x0a:
            case 0x0c:
                if (!previousResult)
                    invalid("move-result without invocation");
                reg(aa) = result;
                break;
            case 0x0b:
                if (!previousResult)
                    invalid("move-result-wide without invocation");
                putWide(aa, result.bits);
                break;
            case 0x0d:
                if (exception.isNull())
                    invalid("move-exception outside handler");
                reg(aa) = exception;
                exception = {};
                break;
            case 0x0e:
                return {};
            case 0x0f:
            case 0x11:
                return reg(aa);
            case 0x10:
                return {wide(aa), false};
            case 0x12:
                reg(a) =
                    Value::integer(b < 8 ? static_cast<std::int32_t>(b) : static_cast<std::int32_t>(b) - 16);
                break;
            case 0x13:
                reg(aa) = Value::integer(std::bit_cast<std::int16_t>(word(start + 1)));
                break;
            case 0x14:
                reg(aa) = {u32(start + 1), false};
                break;
            case 0x15:
                reg(aa) = {std::uint32_t(word(start + 1)) << 16, false};
                break;
            case 0x16:
                putWide(aa, std::bit_cast<std::uint64_t>(
                                std::int64_t(std::bit_cast<std::int16_t>(word(start + 1)))));
                break;
            case 0x17:
                putWide(aa, std::bit_cast<std::uint64_t>(std::int64_t(signed32(u32(start + 1)))));
                break;
            case 0x18:
                putWide(aa, std::uint64_t(u32(start + 1)) | (std::uint64_t(u32(start + 3)) << 32));
                break;
            case 0x19:
                putWide(aa, std::uint64_t(word(start + 1)) << 48);
                break;
            case 0x1a:
                reg(aa) = string(dex.string16(word(start + 1)), true);
                break;
            case 0x1b:
                reg(aa) = string(dex.string16(u32(start + 1)), true);
                break;
            case 0x1c:
                reg(aa) = classObject(dex.type(word(start + 1)));
                break;
            case 0x1d:
            case 0x1e:
                (void)object(reg(aa));
                break;
            case 0x1f:
                if (!reg(aa).isNull() && !assignable(object(reg(aa)).type, dex.type(word(start + 1))))
                    raise("Ljava/lang/ClassCastException;", dex.type(word(start + 1)));
                break;
            case 0x20:
                reg(a) = Value::integer(!reg(b).isNull() &&
                                        assignable(object(reg(b)).type, dex.type(word(start + 1))));
                break;
            case 0x21: {
                const auto& obj = object(reg(b));
                if (obj.type.empty() || obj.type[0] != '[')
                    invalid("array-length on non-array");
                reg(a) = Value::integer(static_cast<std::int32_t>(obj.elements.size()));
                break;
            }
            case 0x22: {
                const auto& type = dex.type(word(start + 1));
                initialize(type);
                reg(aa) = allocate(type);
                break;
            }
            case 0x23: {
                const auto count = reg(b).i32();
                reg(a) = array(dex.type(word(start + 1)), count);
                break;
            }
            case 0x24:
            case 0x25: {
                const auto& type = dex.type(word(start + 1));
                if (type == "[J" || type == "[D")
                    invalid("wide filled-new-array");
                const auto count = op == 0x24 ? b : aa;
                result = array(type, static_cast<std::int32_t>(count));
                for (unsigned n = 0; n < count; ++n) {
                    if (op == 0x24 && count > 5)
                        invalid("too many filled-array registers");
                    const auto index = op == 0x25 ? word(start + 2) + n
                                       : n == 4   ? a
                                                  : (word(start + 2) >> (n * 4)) & 15;
                    object(result).elements[n] = reg(index);
                }
                resultReady = true;
                break;
            }
            case 0x26: {
                auto& obj = object(reg(aa));
                const auto payload = target(start, signed32(u32(start + 1)), false);
                if (word(payload) != 0x300)
                    invalid("invalid array-data payload");
                const auto stride = word(payload + 1);
                const auto count = u32(payload + 2);
                if ((stride != 1 && stride != 2 && stride != 4 && stride != 8) || count > obj.elements.size())
                    invalid("invalid array-data dimensions");
                for (std::uint32_t n = 0; n < count; ++n) {
                    std::uint64_t bits = 0;
                    for (unsigned j = 0; j < stride; ++j) {
                        const auto byte = std::size_t(n) * stride + j;
                        bits |= std::uint64_t((word(payload + 4 + byte / 2) >> ((byte % 2) * 8)) & 0xff)
                                << (j * 8);
                    }
                    if (obj.type == "[B")
                        bits = std::bit_cast<std::uint64_t>(
                            std::int64_t(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits))));
                    if (obj.type == "[S")
                        bits = std::bit_cast<std::uint64_t>(
                            std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits))));
                    obj.elements[n] = {bits, false};
                }
                break;
            }
            case 0x27:
                if (reg(aa).isNull())
                    raise("Ljava/lang/NullPointerException;", "throw null");
                throw GuestException(reg(aa), object(reg(aa)).type + ": " + text(reg(aa)));
            case 0x28:
                pc = target(start, std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(aa)));
                break;
            case 0x29:
                pc = target(start, std::bit_cast<std::int16_t>(word(start + 1)));
                break;
            case 0x2a:
                pc = target(start, signed32(u32(start + 1)));
                break;
            case 0x2b:
            case 0x2c: {
                const auto payload = target(start, signed32(u32(start + 1)), false);
                const auto count = word(payload + 1);
                if (word(payload) != (op == 0x2b ? 0x100 : 0x200))
                    invalid("invalid switch payload");
                const auto needle = reg(aa).i32();
                for (unsigned n = 0; n < count; ++n) {
                    const auto key =
                        op == 0x2b ? signed32(u32(payload + 2) + n) : signed32(u32(payload + 2 + n * 2));
                    if (key == needle) {
                        pc = target(
                            start,
                            signed32(u32(payload + (op == 0x2b ? 4 : 2 + std::size_t(count) * 2) + n * 2)));
                        break;
                    }
                }
                break;
            }
            default: {
                if (op >= 0x2d && op <= 0x31) {
                    const auto second = word(start + 1);
                    const unsigned x = second & 0xff, y = second >> 8;
                    std::int32_t comparison;
                    if (op == 0x31) {
                        const auto l = signed64(wide(x)), r = signed64(wide(y));
                        comparison = l < r ? -1 : l > r ? 1 : 0;
                    } else {
                        const double l = op <= 0x2e ? double(reg(x).f32()) : std::bit_cast<double>(wide(x));
                        const double r = op <= 0x2e ? double(reg(y).f32()) : std::bit_cast<double>(wide(y));
                        comparison = std::isnan(l) || std::isnan(r) ? ((op == 0x2d || op == 0x2f) ? -1 : 1)
                                     : l < r                        ? -1
                                     : l > r                        ? 1
                                                                    : 0;
                    }
                    reg(aa) = Value::integer(comparison);
                } else if (op >= 0x32 && op <= 0x3d) {
                    const bool zero = op >= 0x38;
                    const auto x = reg(zero ? aa : a), y = zero ? Value{} : reg(b);
                    const auto condition = (op - 0x32) % 6;
                    const bool take = condition == 0   ? x == y
                                      : condition == 1 ? !(x == y)
                                      : condition == 2 ? x.i32() < y.i32()
                                      : condition == 3 ? x.i32() >= y.i32()
                                      : condition == 4 ? x.i32() > y.i32()
                                                       : x.i32() <= y.i32();
                    if (take)
                        pc = target(start, std::bit_cast<std::int16_t>(word(start + 1)));
                } else if (op >= 0x44 && op <= 0x51) {
                    const auto second = word(start + 1);
                    const unsigned arrayReg = second & 0xff, indexReg = second >> 8;
                    auto& obj = object(reg(arrayReg));
                    const auto index = reg(indexReg).i32();
                    if (obj.type.empty() || obj.type[0] != '[')
                        invalid("array operation on non-array");
                    if (index < 0 || static_cast<std::size_t>(index) >= obj.elements.size())
                        raise("Ljava/lang/ArrayIndexOutOfBoundsException;", std::to_string(index));
                    auto& element = obj.elements[static_cast<std::size_t>(index)];
                    if (op < 0x4b) {
                        if (op == 0x45)
                            putWide(aa, element.bits);
                        else
                            reg(aa) = element;
                    } else {
                        auto value = op == 0x4c ? Value{wide(aa), false} : reg(aa);
                        if (op == 0x4d && !value.isNull() &&
                            !assignable(object(value).type, std::string_view(obj.type).substr(1)))
                            raise("Ljava/lang/ArrayStoreException;", object(value).type);
                        if (op == 0x4e)
                            value = Value::integer(value.i32() & 1);
                        if (op == 0x4f)
                            value = Value::integer(
                                std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value.bits)));
                        if (op == 0x50)
                            value = Value::integer(static_cast<std::uint16_t>(value.bits));
                        if (op == 0x51)
                            value = Value::integer(
                                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value.bits)));
                        element = value;
                    }
                } else if (op >= 0x52 && op <= 0x6d) {
                    const auto& field = dex.field(word(start + 1));
                    const bool static_ = op >= 0x60, put = static_ ? op >= 0x67 : op >= 0x59;
                    const unsigned variant = static_ ? (op - 0x60) % 7 : (op - 0x52) % 7;
                    const auto destination = static_ ? aa : a;
                    if (!put) {
                        const auto value = static_ ? getStatic(field) : getField(reg(b), field);
                        if (variant == 1)
                            putWide(destination, value.bits);
                        else
                            reg(destination) = value;
                    } else {
                        auto value = variant == 1 ? Value{wide(destination), false} : reg(destination);
                        if (variant == 3)
                            value = Value::integer(value.i32() & 1);
                        if (variant == 4)
                            value = Value::integer(
                                std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value.bits)));
                        if (variant == 5)
                            value = Value::integer(static_cast<std::uint16_t>(value.bits));
                        if (variant == 6)
                            value = Value::integer(
                                std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value.bits)));
                        if (static_)
                            putStatic(field, value);
                        else
                            putField(reg(b), field, value);
                    }
                } else if ((op >= 0x6e && op <= 0x72) || (op >= 0x74 && op <= 0x78)) {
                    const bool range = op >= 0x74;
                    const auto count = range ? aa : b;
                    if ((!range && count > 5) || count > code.outgoing)
                        invalid("invalid outgoing argument count");
                    std::vector<Value> args;
                    args.reserve(count);
                    for (unsigned n = 0; n < count; ++n) {
                        const auto index = range    ? word(start + 2) + n
                                           : n == 4 ? a
                                                    : (word(start + 2) >> (n * 4)) & 15;
                        args.push_back(reg(index));
                    }
                    const auto form = op - (range ? 0x74 : 0x6e);
                    const InvokeKind kind[] = {InvokeKind::virtual_, InvokeKind::super, InvokeKind::direct,
                                               InvokeKind::static_, InvokeKind::interface_};
                    result = invoke(dex.method(word(start + 1)), args, kind[form]);
                    resultReady = true;
                } else if (op >= 0x7b && op <= 0x8f) {
                    const auto x = reg(b);
                    switch (op) {
                    case 0x7b:
                        reg(a) = {std::uint32_t(0) - static_cast<std::uint32_t>(x.bits), false};
                        break;
                    case 0x7c:
                        reg(a) = {~static_cast<std::uint32_t>(x.bits), false};
                        break;
                    case 0x7d:
                        putWide(a, std::uint64_t(0) - wide(b));
                        break;
                    case 0x7e:
                        putWide(a, ~wide(b));
                        break;
                    case 0x7f:
                        reg(a) = Value::floating(-x.f32());
                        break;
                    case 0x80:
                        putWide(a, std::bit_cast<std::uint64_t>(-std::bit_cast<double>(wide(b))));
                        break;
                    case 0x81:
                        putWide(a, std::bit_cast<std::uint64_t>(std::int64_t(x.i32())));
                        break;
                    case 0x82:
                        reg(a) = Value::floating(static_cast<float>(x.i32()));
                        break;
                    case 0x83:
                        putWide(a, std::bit_cast<std::uint64_t>(static_cast<double>(x.i32())));
                        break;
                    case 0x84:
                        reg(a) = {wide(b) & 0xffffffff, false};
                        break;
                    case 0x85:
                        reg(a) = Value::floating(static_cast<float>(signed64(wide(b))));
                        break;
                    case 0x86:
                        putWide(a, std::bit_cast<std::uint64_t>(static_cast<double>(signed64(wide(b)))));
                        break;
                    case 0x87:
                        reg(a) = Value::integer(javaIntegral<std::int32_t>(x.f32()));
                        break;
                    case 0x88:
                        putWide(a, std::bit_cast<std::uint64_t>(javaIntegral<std::int64_t>(x.f32())));
                        break;
                    case 0x89:
                        putWide(a, std::bit_cast<std::uint64_t>(static_cast<double>(x.f32())));
                        break;
                    case 0x8a:
                        reg(a) = Value::integer(javaIntegral<std::int32_t>(std::bit_cast<double>(wide(b))));
                        break;
                    case 0x8b:
                        putWide(a, std::bit_cast<std::uint64_t>(
                                       javaIntegral<std::int64_t>(std::bit_cast<double>(wide(b)))));
                        break;
                    case 0x8c:
                        reg(a) = Value::floating(static_cast<float>(std::bit_cast<double>(wide(b))));
                        break;
                    case 0x8d:
                        reg(a) =
                            Value::integer(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(x.bits)));
                        break;
                    case 0x8e:
                        reg(a) = Value::integer(static_cast<std::uint16_t>(x.bits));
                        break;
                    case 0x8f:
                        reg(a) =
                            Value::integer(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(x.bits)));
                        break;
                    }
                } else if (op >= 0x90 && op <= 0xcf) {
                    const bool two = op >= 0xb0;
                    const unsigned operation = op - (two ? 0xb0 : 0x90);
                    const unsigned dst = two ? a : aa;
                    const unsigned left = two ? a : word(start + 1) & 0xff;
                    const unsigned right = two ? b : word(start + 1) >> 8;
                    if (operation < 11) {
                        const auto l = static_cast<std::uint32_t>(reg(left).bits),
                                   r = static_cast<std::uint32_t>(reg(right).bits);
                        std::uint32_t value = 0;
                        switch (operation) {
                        case 0:
                            value = l + r;
                            break;
                        case 1:
                            value = l - r;
                            break;
                        case 2:
                            value = l * r;
                            break;
                        case 3:
                        case 4:
                            if (!r)
                                raise("Ljava/lang/ArithmeticException;", "division by zero");
                            if (l == 0x80000000 && r == 0xffffffff)
                                value = operation == 3 ? l : 0;
                            else
                                value = std::bit_cast<std::uint32_t>(
                                    operation == 3 ? signed32(l) / signed32(r) : signed32(l) % signed32(r));
                            break;
                        case 5:
                            value = l & r;
                            break;
                        case 6:
                            value = l | r;
                            break;
                        case 7:
                            value = l ^ r;
                            break;
                        case 8:
                            value = l << (r & 31);
                            break;
                        case 9:
                            value = std::bit_cast<std::uint32_t>(signed32(l) >> (r & 31));
                            break;
                        case 10:
                            value = l >> (r & 31);
                            break;
                        }
                        reg(dst) = {value, false};
                    } else if (operation < 22) {
                        const auto operation64 = operation - 11;
                        const auto l = wide(left), r = operation64 >= 8 ? reg(right).bits : wide(right);
                        std::uint64_t value = 0;
                        switch (operation64) {
                        case 0:
                            value = l + r;
                            break;
                        case 1:
                            value = l - r;
                            break;
                        case 2:
                            value = l * r;
                            break;
                        case 3:
                        case 4:
                            if (!r)
                                raise("Ljava/lang/ArithmeticException;", "division by zero");
                            if (l == 0x8000000000000000ULL && r == 0xffffffffffffffffULL)
                                value = operation64 == 3 ? l : 0;
                            else
                                value = std::bit_cast<std::uint64_t>(
                                    operation64 == 3 ? signed64(l) / signed64(r) : signed64(l) % signed64(r));
                            break;
                        case 5:
                            value = l & r;
                            break;
                        case 6:
                            value = l | r;
                            break;
                        case 7:
                            value = l ^ r;
                            break;
                        case 8:
                            value = l << (r & 63);
                            break;
                        case 9:
                            value = std::bit_cast<std::uint64_t>(signed64(l) >> (r & 63));
                            break;
                        case 10:
                            value = l >> (r & 63);
                            break;
                        }
                        putWide(dst, value);
                    } else {
                        const bool double_ = operation >= 27;
                        const auto arithmetic = operation - (double_ ? 27 : 22);
                        const double l =
                            double_ ? std::bit_cast<double>(wide(left)) : double(reg(left).f32());
                        const double r =
                            double_ ? std::bit_cast<double>(wide(right)) : double(reg(right).f32());
                        const double value = arithmetic == 0   ? l + r
                                             : arithmetic == 1 ? l - r
                                             : arithmetic == 2 ? l * r
                                             : arithmetic == 3 ? l / r
                                                               : std::fmod(l, r);
                        if (double_)
                            putWide(dst, std::bit_cast<std::uint64_t>(value));
                        else
                            reg(dst) = Value::floating(static_cast<float>(value));
                    }
                } else if (op >= 0xd0 && op <= 0xe2) {
                    const bool small = op >= 0xd8;
                    const auto second = word(start + 1);
                    const unsigned dst = small ? aa : a, source = small ? second & 0xff : b;
                    const auto literal =
                        small
                            ? std::int32_t(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(second >> 8)))
                            : std::int32_t(std::bit_cast<std::int16_t>(second));
                    const auto l = static_cast<std::uint32_t>(reg(source).bits),
                               r = std::bit_cast<std::uint32_t>(literal);
                    const auto operation = op - (small ? 0xd8 : 0xd0);
                    std::uint32_t value = 0;
                    switch (operation) {
                    case 0:
                        value = l + r;
                        break;
                    case 1:
                        value = r - l;
                        break;
                    case 2:
                        value = l * r;
                        break;
                    case 3:
                    case 4:
                        if (!r)
                            raise("Ljava/lang/ArithmeticException;", "division by zero");
                        if (l == 0x80000000 && r == 0xffffffff)
                            value = operation == 3 ? l : 0;
                        else
                            value = std::bit_cast<std::uint32_t>(operation == 3 ? signed32(l) / literal
                                                                                : signed32(l) % literal);
                        break;
                    case 5:
                        value = l & r;
                        break;
                    case 6:
                        value = l | r;
                        break;
                    case 7:
                        value = l ^ r;
                        break;
                    case 8:
                        value = l << (r & 31);
                        break;
                    case 9:
                        value = std::bit_cast<std::uint32_t>(signed32(l) >> (r & 31));
                        break;
                    case 10:
                        value = l >> (r & 31);
                        break;
                    }
                    reg(dst) = {value, false};
                } else {
                    std::ostringstream message;
                    message << "unsupported dex opcode 0x" << std::hex << op << " at " << context << "+"
                            << std::dec << start;
                    throw Error(ErrorCode::unsupported, message.str());
                }
            }
            }
        } catch (GuestException& error) {
            bool handled = false;
            for (const auto& region : code.tries) {
                if (start < region.start || start - region.start >= region.count)
                    continue;
                for (const auto& handler : region.handlers) {
                    if (handler.type.empty() || assignable(object(error.value).type, handler.type)) {
                        if (handler.address >= starts.size() || !starts[handler.address])
                            invalid("invalid exception handler target");
                        pc = handler.address;
                        exception = error.value;
                        handled = true;
                        break;
                    }
                }
                if (handled)
                    break;
            }
            if (!handled)
                throw;
        }
    }
}

} // namespace gutenberg
