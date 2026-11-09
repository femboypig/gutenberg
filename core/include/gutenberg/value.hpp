#pragma once

#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gutenberg {

struct Value {
    std::uint64_t bits = 0;
    bool reference = false;
    static Value integer(std::int32_t n) {
        return {std::bit_cast<std::uint32_t>(n), false};
    }
    static Value wide(std::int64_t n) {
        return {std::bit_cast<std::uint64_t>(n), false};
    }
    static Value floating(float n) {
        return {std::bit_cast<std::uint32_t>(n), false};
    }
    static Value floating(double n) {
        return {std::bit_cast<std::uint64_t>(n), false};
    }
    static Value object(std::uint64_t id) {
        return {id, true};
    }
    [[nodiscard]] std::int32_t i32() const {
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
    }
    [[nodiscard]] std::int64_t i64() const {
        return std::bit_cast<std::int64_t>(bits);
    }
    [[nodiscard]] float f32() const {
        return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    }
    [[nodiscard]] double f64() const {
        return std::bit_cast<double>(bits);
    }
    [[nodiscard]] bool isNull() const {
        return bits == 0;
    }
    friend bool operator==(const Value& a, const Value& b) {
        return a.bits == b.bits && (a.isNull() || a.reference == b.reference);
    }
};

struct NativeObject {
    virtual ~NativeObject() = default;
    virtual void visitReferences(const std::function<void(Value)>&) const {}
    [[nodiscard]] virtual std::shared_ptr<NativeObject> cloneState() const {
        return {};
    }
};

struct Object {
    std::string type;
    std::unordered_map<std::string, Value> fields;
    std::vector<Value> elements;
    std::u16string text;
    std::uint64_t hostId = 0;
    std::shared_ptr<NativeObject> native;
};

} // namespace gutenberg
