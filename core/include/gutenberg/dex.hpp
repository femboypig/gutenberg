#pragma once

#include "bytes.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace gutenberg {

struct DexInfo {
    std::string version;
    std::uint32_t classes = 0, methods = 0;
};
[[nodiscard]] DexInfo inspectDex(std::span<const std::uint8_t> bytes);

struct MethodRef {
    std::string owner;
    std::string name;
    std::string descriptor;
    std::vector<std::string> parameters;
    std::string returnType;
    [[nodiscard]] std::string key() const {
        return owner + "->" + name + descriptor;
    }
    [[nodiscard]] std::size_t argumentWords(bool instance) const;
};

struct FieldRef {
    std::string owner;
    std::string name;
    std::string type;
    [[nodiscard]] std::string key() const {
        return owner + "->" + name + ":" + type;
    }
};

struct EncodedValue {
    std::uint8_t type = 0x1e;
    std::uint64_t bits = 0;
    std::vector<EncodedValue> elements;
};

struct CatchHandler {
    std::string type; // Empty denotes catch-all.
    std::uint32_t address;
};
struct TryRegion {
    std::uint32_t start;
    std::uint32_t count;
    std::vector<CatchHandler> handlers;
};
struct Code {
    std::uint16_t registers = 0;
    std::uint16_t incoming = 0;
    std::uint16_t outgoing = 0;
    std::vector<std::uint16_t> words;
    std::vector<TryRegion> tries;
};
struct DexMethod {
    std::uint32_t index;
    std::uint32_t access;
    std::uint32_t codeOffset = 0;
};
struct DexField {
    std::uint32_t index;
    std::uint32_t access;
    EncodedValue initial;
};
struct DexClass {
    std::string type;
    std::string super;
    std::uint32_t access;
    std::vector<std::string> interfaces;
    std::vector<DexField> staticFields;
    std::vector<DexField> instanceFields;
    std::vector<DexMethod> directMethods;
    std::vector<DexMethod> virtualMethods;
};

class DexFile {
  public:
    explicit DexFile(Bytes bytes);
    [[nodiscard]] const std::string& string(std::uint32_t index) const;
    [[nodiscard]] const std::u16string& string16(std::uint32_t index) const;
    [[nodiscard]] const std::string& type(std::uint32_t index) const;
    [[nodiscard]] const MethodRef& method(std::uint32_t index) const;
    [[nodiscard]] const FieldRef& field(std::uint32_t index) const;
    [[nodiscard]] const Code* code(const DexMethod& method) const;
    [[nodiscard]] const std::vector<DexClass>& classes() const noexcept {
        return classes_;
    }
    [[nodiscard]] const std::vector<MethodRef>& methods() const noexcept {
        return methods_;
    }
    [[nodiscard]] const std::vector<FieldRef>& fields() const noexcept {
        return fields_;
    }
    [[nodiscard]] const std::string& version() const noexcept {
        return version_;
    }

  private:
    [[nodiscard]] Code readCode(std::uint32_t offset) const;
    [[nodiscard]] EncodedValue readEncoded(std::size_t& cursor, unsigned depth = 0) const;
    Bytes bytes_;
    std::string version_;
    std::vector<std::string> strings_;
    std::vector<std::u16string> strings16_;
    std::vector<std::string> types_;
    std::vector<MethodRef> methods_;
    std::vector<FieldRef> fields_;
    std::vector<DexClass> classes_;
    mutable std::mutex codeMutex_;
    mutable std::unordered_map<std::uint32_t, std::unique_ptr<const Code>> code_;
};

} // namespace gutenberg
