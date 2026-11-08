#pragma once

#include "bytes.hpp"
#include <variant>

namespace gutenberg {

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::u16string, JsonValue>>;
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::u16string, Array, Object> data = nullptr;
};

struct JsonLimits {
    std::size_t bytes = 16 * 1024 * 1024;
    std::size_t nodes = 1000000;
    unsigned depth = 128;
};

[[nodiscard]] JsonValue parseJson(std::string_view text, JsonLimits limits = {});
[[nodiscard]] std::string stringifyJson(const JsonValue& value, unsigned indent = 0, JsonLimits limits = {});

} // namespace gutenberg
