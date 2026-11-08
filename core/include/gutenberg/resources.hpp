#pragma once

#include "bytes.hpp"
#include <map>
#include <optional>
#include <string_view>

namespace gutenberg {

struct ResourceValue {
    std::uint8_t type = 0;
    std::uint32_t data = 0;
    std::string text;
    [[nodiscard]] bool isReference() const noexcept {
        return type == 1;
    }
};

struct ResourceBag {
    std::uint32_t parent = 0;
    std::map<std::uint32_t, ResourceValue> values;
};

class StringPool {
  public:
    StringPool() = default;
    StringPool(const ByteView& data, std::size_t offset);
    [[nodiscard]] const std::string& at(std::uint32_t index) const;
    [[nodiscard]] std::size_t size() const noexcept {
        return strings_.size();
    }

  private:
    std::vector<std::string> strings_;
};

struct XmlAttribute {
    std::string name;
    std::string nameSpace;
    ResourceValue value;
};

struct XmlNode {
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlNode> children;
    std::string text;
    [[nodiscard]] const ResourceValue* attribute(std::string_view key) const;
    [[nodiscard]] std::string stringAttribute(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] std::int32_t intAttribute(std::string_view key, std::int32_t fallback = 0) const;
};

[[nodiscard]] XmlNode parseBinaryXml(std::span<const std::uint8_t> bytes);

class Resources {
  public:
    Resources() = default;
    explicit Resources(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::optional<ResourceValue> resolve(std::uint32_t id) const;
    [[nodiscard]] std::string string(std::uint32_t id) const;
    [[nodiscard]] std::string name(std::uint32_t id) const;
    [[nodiscard]] const ResourceBag* bag(std::uint32_t id) const;
    [[nodiscard]] std::optional<ResourceValue> styled(std::uint32_t style, std::uint32_t attribute) const;
    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

  private:
    std::map<std::uint32_t, ResourceValue> values_;
    std::map<std::uint32_t, ResourceBag> bags_;
    std::map<std::uint32_t, std::string> names_;
    std::map<std::uint32_t, unsigned> scores_;
};

} // namespace gutenberg
