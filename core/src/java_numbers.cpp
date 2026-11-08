#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace gutenberg {
namespace {
std::int64_t parseInteger(Runtime& vm, Value string, std::int32_t radix, bool wide) {
    if (radix < 2 || radix > 36 || string.isNull())
        vm.raise("Ljava/lang/NumberFormatException;", "invalid radix or null string");
    const auto text = vm.text(string);
    std::size_t at = 0;
    bool negative = false;
    if (!text.empty() && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        ++at;
    }
    if (at == text.size())
        vm.raise("Ljava/lang/NumberFormatException;", text);
    const auto maximum = wide ? std::uint64_t(std::numeric_limits<std::int64_t>::max())
                              : std::uint64_t(std::numeric_limits<std::int32_t>::max());
    const auto limit = maximum + (negative ? 1U : 0U);
    std::uint64_t value = 0;
    for (; at < text.size(); ++at) {
        const auto c = text[at];
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'z' ? c - 'a' + 10
                          : c >= 'A' && c <= 'Z' ? c - 'A' + 10
                                                 : -1;
        if (digit < 0 || digit >= radix ||
            value > (limit - static_cast<unsigned>(digit)) / static_cast<unsigned>(radix))
            vm.raise("Ljava/lang/NumberFormatException;", text);
        value = value * static_cast<unsigned>(radix) + static_cast<unsigned>(digit);
    }
    return std::bit_cast<std::int64_t>(negative ? std::uint64_t(0) - value : value);
}
constexpr auto randomType = "Ljava/util/Random;";
std::uint32_t next(Runtime& vm, Value random, unsigned bits) {
    auto& seed = vm.object(random).fields["seed"];
    seed.bits = (seed.bits * 0x5deece66dULL + 0xb) & ((1ULL << 48) - 1);
    return static_cast<std::uint32_t>(seed.bits >> (48 - bits));
}
void seed(Runtime& vm, Value random, std::uint64_t value) {
    vm.object(random).fields["seed"] = {(value ^ 0x5deece66dULL) & ((1ULL << 48) - 1), false};
}
std::uint64_t randomSeed() {
    static std::atomic<std::uint64_t> sequence{0x1234abcd330eULL};
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
           sequence.fetch_add(0x9e3779b97f4a7c15ULL);
}
} // namespace

void installJavaNumbers(Runtime& vm) {
    for (const auto* name : {"Integer", "Long"}) {
        const std::string owner = "Ljava/lang/" + std::string(name) + ";",
                          type = std::string(name) == "Long" ? "J" : "I";
        const bool wide = type == "J";
        vm.bind(owner, wide ? "parseLong" : "parseInt", "(Ljava/lang/String;)" + type,
                [wide](Runtime& r, Arguments a) {
                    const auto value = parseInteger(r, a[0], 10, wide);
                    return wide ? Value::wide(value) : Value::integer(static_cast<std::int32_t>(value));
                });
        vm.bind(owner, wide ? "parseLong" : "parseInt", "(Ljava/lang/String;I)" + type,
                [wide](Runtime& r, Arguments a) {
                    const auto value = parseInteger(r, a[0], a[1].i32(), wide);
                    return wide ? Value::wide(value) : Value::integer(static_cast<std::int32_t>(value));
                });
        vm.bind(owner, "toString", "(" + type + ")Ljava/lang/String;", [wide](Runtime& r, Arguments a) {
            return r.string(std::to_string(wide ? std::bit_cast<std::int64_t>(argumentWide(a, 0))
                                                : std::int64_t(a[0].i32())));
        });
        vm.bind(owner, "valueOf", "(Ljava/lang/String;)" + owner, [owner, wide](Runtime& r, Arguments a) {
            const auto value = parseInteger(r, a[0], 10, wide);
            const auto result = r.allocate(owner);
            r.object(result).fields["value"] = Value::wide(value);
            return result;
        });
    }
    for (const auto* name : {"Float", "Double"}) {
        const bool double_ = std::string_view(name) == "Double";
        const std::string owner = "Ljava/lang/" + std::string(name) + ";", type = double_ ? "D" : "F";
        vm.bind(owner, double_ ? "parseDouble" : "parseFloat", "(Ljava/lang/String;)" + type,
                [double_](Runtime& r, Arguments a) {
                    if (a[0].isNull())
                        r.raise("Ljava/lang/NullPointerException;", "null number string");
                    auto text = r.text(a[0]);
                    const auto first = text.find_first_not_of(" \t\n\r\f");
                    const auto last = text.find_last_not_of(" \t\n\r\f");
                    if (first == text.npos)
                        r.raise("Ljava/lang/NumberFormatException;", text);
                    text = text.substr(first, last - first + 1);
                    if (text.back() == 'f' || text.back() == 'F' || text.back() == 'd' || text.back() == 'D')
                        text.pop_back();
                    char* end = nullptr;
                    const auto value = std::strtod(text.c_str(), &end);
                    if (text.empty() || end != text.data() + text.size())
                        r.raise("Ljava/lang/NumberFormatException;", text);
                    return double_ ? Value::floating(value) : Value::floating(static_cast<float>(value));
                });
        vm.bind(owner, "isNaN", "(" + type + ")Z", [double_](Runtime&, Arguments a) {
            return Value::integer(
                std::isnan(double_ ? std::bit_cast<double>(argumentWide(a, 0)) : double(a[0].f32())));
        });
    }
    for (const auto* name : {"Integer", "Long", "Float", "Double", "Short", "Byte", "Boolean", "Character"}) {
        const std::string owner = "Ljava/lang/" + std::string(name) + ";";
        const std::string_view kind(name);
        vm.bind(owner, "hashCode", "()I", [kind](Runtime& r, Arguments a) {
            const auto value = r.object(a[0]).fields["value"];
            if (kind == "Boolean")
                return Value::integer(value.bits ? 1231 : 1237);
            std::uint64_t bits = value.bits;
            if (kind == "Float" && std::isnan(value.f32()))
                bits = 0x7fc00000;
            if (kind == "Double" && std::isnan(value.f64()))
                bits = 0x7ff8000000000000ULL;
            return Value{
                static_cast<std::uint32_t>((kind == "Long" || kind == "Double") ? bits ^ (bits >> 32) : bits),
                false};
        });
    }
    vm.defineClass(randomType);
    vm.bind(randomType, "<init>", "()V", [](Runtime& r, Arguments a) {
        seed(r, a[0], randomSeed());
        return Value{};
    });
    vm.bind(randomType, "<init>", "(J)V", [](Runtime& r, Arguments a) {
        seed(r, a[0], argumentWide(a, 1));
        return Value{};
    });
    vm.bind(randomType, "setSeed", "(J)V", [](Runtime& r, Arguments a) {
        seed(r, a[0], argumentWide(a, 1));
        return Value{};
    });
    vm.bind(randomType, "nextInt", "()I",
            [](Runtime& r, Arguments a) { return Value{next(r, a[0], 32), false}; });
    vm.bind(randomType, "nextInt", "(I)I", [](Runtime& r, Arguments a) {
        const auto bound = a[1].i32();
        if (bound <= 0)
            r.raise("Ljava/lang/IllegalArgumentException;", "bound must be positive");
        const auto n = static_cast<std::uint32_t>(bound);
        if (!(n & (n - 1)))
            return Value::integer(static_cast<std::int32_t>((std::uint64_t(n) * next(r, a[0], 31)) >> 31));
        std::uint32_t bits, value;
        do {
            r.tick();
            bits = next(r, a[0], 31);
            value = bits % n;
        } while (bits - value + (n - 1) >= 0x80000000U);
        return Value::integer(static_cast<std::int32_t>(value));
    });
    vm.bind(randomType, "nextBoolean", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(next(r, a[0], 1) != 0); });
    vm.bind(randomType, "nextFloat", "()F", [](Runtime& r, Arguments a) {
        return Value::floating(static_cast<float>(next(r, a[0], 24)) / 16777216.0f);
    });
    vm.bind(randomType, "nextDouble", "()D", [](Runtime& r, Arguments a) {
        const auto high = next(r, a[0], 26), low = next(r, a[0], 27);
        return Value::floating(static_cast<double>((std::uint64_t(high) << 27) + low) / 9007199254740992.0);
    });
    const auto mathRandom = vm.allocate(randomType);
    seed(vm, mathRandom, randomSeed());
    for (const auto* math : {"Ljava/lang/Math;", "Ljava/lang/StrictMath;"}) {
        vm.bind(math, "random", "()D", [mathRandom](Runtime& r, Arguments) {
            const auto high = next(r, mathRandom, 26), low = next(r, mathRandom, 27);
            return Value::floating(static_cast<double>((std::uint64_t(high) << 27) + low) /
                                   9007199254740992.0);
        });
        const std::pair<const char*, double (*)(double)> functions[] = {
            {"sin", std::sin},     {"cos", std::cos},     {"tan", std::tan},  {"asin", std::asin},
            {"acos", std::acos},   {"atan", std::atan},   {"exp", std::exp},  {"log", std::log},
            {"log10", std::log10}, {"floor", std::floor}, {"ceil", std::ceil}};
        for (const auto& [name, function] : functions)
            vm.bind(math, name, "(D)D", [function](Runtime&, Arguments a) {
                return Value::floating(function(std::bit_cast<double>(argumentWide(a, 0))));
            });
    }
}

} // namespace gutenberg
