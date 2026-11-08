#include "gutenberg/resources.hpp"
#include <bit>
#include <limits>

namespace gutenberg {
namespace {
struct Chunk {
    std::uint16_t type, header;
    std::uint32_t size;
    std::size_t offset;
    [[nodiscard]] std::size_t end() const {
        return offset + size;
    }
};
Chunk chunkAt(const ByteView& data, std::size_t offset, std::size_t end) {
    data.require(offset, 8);
    Chunk chunk{data.u16(offset), data.u16(offset + 2), data.u32(offset + 4), offset};
    if (chunk.header < 8 || chunk.size < chunk.header || offset > end || chunk.size > end - offset)
        throw Error(ErrorCode::malformed_resources, "invalid resource chunk");
    return chunk;
}
ResourceValue readValue(const ByteView& data, std::size_t at, const StringPool& strings) {
    if (data.u16(at) < 8)
        throw Error(ErrorCode::malformed_resources, "invalid typed value");
    ResourceValue value{data.u8(at + 3), data.u32(at + 4), {}};
    if (value.type == 3)
        value.text = strings.at(value.data);
    return value;
}
std::uint32_t length8(const ByteView& view, std::size_t& at) {
    auto first = view.u8(at++);
    return first & 0x80 ? ((std::uint32_t(first & 0x7f) << 8) | view.u8(at++)) : first;
}
std::uint32_t length16(const ByteView& view, std::size_t& at) {
    const auto first = view.u16(at);
    at += 2;
    if (!(first & 0x8000))
        return first;
    const auto second = view.u16(at);
    at += 2;
    return (std::uint32_t(first & 0x7fff) << 16) | second;
}
} // namespace

StringPool::StringPool(const ByteView& data, std::size_t offset) {
    const auto chunk = chunkAt(data, offset, data.size());
    if (chunk.type != 1 || chunk.header < 28)
        throw Error(ErrorCode::malformed_resources, "invalid string pool");
    const ByteView local(data.slice(offset, chunk.size), ErrorCode::malformed_resources);
    const auto count = local.u32(8);
    const auto flags = local.u32(16);
    const auto start = local.u32(20);
    local.require(chunk.header, std::size_t(count) * 4);
    if (start < std::size_t(chunk.header) + std::size_t(count) * 4 || start > chunk.size)
        throw Error(ErrorCode::malformed_resources, "invalid string data offset");
    strings_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::size_t cursor = std::size_t(start) + local.u32(chunk.header + std::size_t(i) * 4);
        if (flags & 0x100) {
            (void)length8(local, cursor);
            const auto length = length8(local, cursor);
            strings_.push_back(local.text(cursor, length));
            if (local.u8(cursor + length))
                throw Error(ErrorCode::malformed_resources, "unterminated utf-8 string");
        } else {
            const auto length = length16(local, cursor);
            local.require(cursor, std::size_t(length) * 2 + 2);
            std::u16string text;
            text.reserve(length);
            for (std::uint32_t n = 0; n < length; ++n)
                text += static_cast<char16_t>(local.u16(cursor + std::size_t(n) * 2));
            if (local.u16(cursor + std::size_t(length) * 2))
                throw Error(ErrorCode::malformed_resources, "unterminated utf-16 string");
            strings_.push_back(utf8(text));
        }
    }
}

const std::string& StringPool::at(std::uint32_t index) const {
    if (index >= strings_.size())
        throw Error(ErrorCode::malformed_resources, "string pool index out of range");
    return strings_[index];
}

const ResourceValue* XmlNode::attribute(std::string_view key) const {
    for (const auto& item : attributes)
        if (item.name == key)
            return &item.value;
    return nullptr;
}
std::string XmlNode::stringAttribute(std::string_view key, std::string fallback) const {
    if (const auto* value = attribute(key))
        return value->text;
    return fallback;
}
std::int32_t XmlNode::intAttribute(std::string_view key, std::int32_t fallback) const {
    if (const auto* value = attribute(key))
        return std::bit_cast<std::int32_t>(value->data);
    return fallback;
}

XmlNode parseBinaryXml(std::span<const std::uint8_t> bytes) {
    const ByteView data(bytes, ErrorCode::malformed_xml);
    const auto rootChunk = chunkAt(data, 0, data.size());
    if (rootChunk.type != 3 || rootChunk.size != data.size())
        throw Error(ErrorCode::malformed_xml, "not binary android xml");
    StringPool strings;
    XmlNode root;
    std::vector<XmlNode*> stack;
    bool hasRoot = false;
    for (std::size_t cursor = rootChunk.header; cursor < rootChunk.end();) {
        const auto chunk = chunkAt(data, cursor, rootChunk.end());
        const auto ext = cursor + chunk.header;
        if (chunk.type == 1)
            strings = StringPool(data, cursor);
        else if (chunk.type == 0x0102) {
            if (chunk.header < 16 || chunk.size - chunk.header < 20)
                throw Error(ErrorCode::malformed_xml, "invalid start element");
            XmlNode node;
            node.name = strings.at(data.u32(ext + 4));
            const auto start = data.u16(ext + 8);
            const auto stride = data.u16(ext + 10);
            const auto count = data.u16(ext + 12);
            if (start < 20 || stride < 20 ||
                std::size_t(start) + std::size_t(stride) * count > chunk.size - chunk.header)
                throw Error(ErrorCode::malformed_xml, "invalid attribute table");
            for (unsigned i = 0; i < count; ++i) {
                const auto at = ext + start + std::size_t(i) * stride;
                const auto ns = data.u32(at);
                auto value = readValue(data, at + 12, strings);
                const auto raw = data.u32(at + 8);
                if (raw != 0xffffffff)
                    value.text = strings.at(raw);
                node.attributes.push_back(
                    {strings.at(data.u32(at + 4)), ns == 0xffffffff ? "" : strings.at(ns), std::move(value)});
            }
            if (stack.empty()) {
                if (hasRoot)
                    throw Error(ErrorCode::malformed_xml, "multiple xml roots");
                root = std::move(node);
                stack.push_back(&root);
                hasRoot = true;
            } else {
                stack.back()->children.push_back(std::move(node));
                stack.push_back(&stack.back()->children.back());
            }
            if (stack.size() > 256)
                throw Error(ErrorCode::limit_exceeded, "xml nesting limit exceeded");
        } else if (chunk.type == 0x0103) {
            if (chunk.header < 16 || chunk.size - chunk.header < 8 || stack.empty() ||
                stack.back()->name != strings.at(data.u32(ext + 4)))
                throw Error(ErrorCode::malformed_xml, "unbalanced xml element");
            stack.pop_back();
        } else if (chunk.type == 0x0104) {
            if (stack.empty() || chunk.size - chunk.header < 12)
                throw Error(ErrorCode::malformed_xml, "invalid xml text");
            stack.back()->text += strings.at(data.u32(ext));
        }
        cursor = chunk.end();
    }
    if (!hasRoot || !stack.empty())
        throw Error(ErrorCode::malformed_xml, "incomplete xml document");
    return root;
}

Resources::Resources(std::span<const std::uint8_t> bytes) {
    const ByteView data(bytes, ErrorCode::malformed_resources);
    const auto table = chunkAt(data, 0, data.size());
    if (table.type != 2 || table.header < 12 || table.size != bytes.size())
        throw Error(ErrorCode::malformed_resources, "not an android resource table");
    StringPool globals;
    for (std::size_t cursor = table.header; cursor < table.end();) {
        const auto chunk = chunkAt(data, cursor, table.end());
        if (chunk.type == 1)
            globals = StringPool(data, cursor);
        else if (chunk.type == 0x0200) {
            if (chunk.header < 284)
                throw Error(ErrorCode::malformed_resources, "invalid resource package");
            const auto packageId = data.u32(cursor + 8);
            if (packageId > 255)
                throw Error(ErrorCode::malformed_resources, "invalid package id");
            const auto typeOffset = chunk.header >= 288 ? data.u32(cursor + 284) : 0;
            const StringPool types(data, cursor + data.u32(cursor + 268));
            const StringPool keys(data, cursor + data.u32(cursor + 276));
            for (std::size_t at = cursor + chunk.header; at < chunk.end();) {
                const auto type = chunkAt(data, at, chunk.end());
                if (type.type == 0x0201) {
                    if (type.header < 24)
                        throw Error(ErrorCode::malformed_resources, "invalid resource type");
                    const auto typeId = data.u8(at + 8);
                    const auto flags = data.u8(at + 9);
                    const auto count = data.u32(at + 12);
                    const auto entryStart = data.u32(at + 16);
                    const auto configSize = data.u32(at + 20);
                    if (!typeId || typeId + typeOffset > 255 || configSize < 4 ||
                        configSize > unsigned(type.header) - 20 || entryStart > type.size)
                        throw Error(ErrorCode::malformed_resources, "invalid resource type layout");
                    // Prefer an unqualified configuration; retain others as fallbacks.
                    unsigned score = 0;
                    for (std::size_t n = 24; n < 20 + configSize; ++n)
                        if (data.u8(at + n))
                            ++score;
                    const bool sparse = flags & 1;
                    const bool shortOffsets = flags & 2;
                    const std::size_t stride = shortOffsets && !sparse ? 2 : 4;
                    if (std::size_t(type.header) + std::size_t(count) * stride > entryStart)
                        throw Error(ErrorCode::malformed_resources, "resource offset table overlaps entries");
                    for (std::uint32_t i = 0; i < count; ++i) {
                        const auto slot = at + type.header + std::size_t(i) * stride;
                        const auto index = sparse ? data.u16(slot) : i;
                        std::uint32_t offset;
                        if (sparse)
                            offset = std::uint32_t(data.u16(slot + 2)) * 4;
                        else if (shortOffsets) {
                            const auto value = data.u16(slot);
                            if (value == 0xffff)
                                continue;
                            offset = std::uint32_t(value) * 4;
                        } else {
                            offset = data.u32(slot);
                            if (offset == 0xffffffff)
                                continue;
                        }
                        if (index > 0xffff || offset > type.size - entryStart ||
                            type.size - entryStart - offset < 8)
                            throw Error(ErrorCode::malformed_resources, "invalid resource entry offset");
                        const auto entry = at + entryStart + offset;
                        const auto size = data.u16(entry);
                        const auto entryFlags = data.u16(entry + 2);
                        if (entryFlags & 1)
                            continue; // Bag resources are resolved by their individual consumers.
                        if (entryFlags & 8)
                            throw Error(ErrorCode::unsupported, "compact resource entries are not supported");
                        if (size < 8 || size > type.end() - entry || type.end() - entry - size < 8)
                            throw Error(ErrorCode::malformed_resources, "truncated resource value");
                        const auto id = (packageId << 24) | ((typeId + typeOffset) << 16) | index;
                        if (!scores_.contains(id) || score < scores_.at(id)) {
                            values_[id] = readValue(data, entry + size, globals);
                            names_[id] = types.at(typeId - 1) + "/" + keys.at(data.u32(entry + 4));
                            scores_[id] = score;
                        }
                    }
                }
                at = type.end();
            }
        }
        cursor = chunk.end();
    }
}

std::optional<ResourceValue> Resources::resolve(std::uint32_t id) const {
    for (unsigned depth = 0; depth < 32; ++depth) {
        const auto item = values_.find(id);
        if (item == values_.end())
            return std::nullopt;
        if (!item->second.isReference())
            return item->second;
        id = item->second.data;
    }
    throw Error(ErrorCode::malformed_resources, "cyclic resource reference");
}
std::string Resources::string(std::uint32_t id) const {
    if (auto value = resolve(id))
        return value->text;
    throw Error(ErrorCode::verification, "resource not found: " + std::to_string(id));
}
std::string Resources::name(std::uint32_t id) const {
    const auto item = names_.find(id);
    return item == names_.end() ? "" : item->second;
}

} // namespace gutenberg
