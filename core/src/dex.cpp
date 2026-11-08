#include "gutenberg/dex.hpp"
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <zlib.h>

namespace gutenberg {
namespace {
template <class T> const T& checked(const std::vector<T>& items, std::uint32_t index, const char* kind) {
    if (index >= items.size())
        throw Error(ErrorCode::malformed_dex, std::string(kind) + " index out of range");
    return items[index];
}
std::u16string readString(const ByteView& data, std::size_t cursor) {
    const auto length = data.uleb(cursor);
    if (length > data.size() - cursor)
        throw Error(ErrorCode::malformed_dex, "invalid dex string length");
    std::u16string chars;
    chars.reserve(length);
    for (std::uint32_t n = 0; n < length; ++n) {
        const auto byte = data.u8(cursor++);
        std::uint16_t value;
        if (!byte)
            throw Error(ErrorCode::malformed_dex, "early dex string terminator");
        if (byte < 0x80)
            value = byte;
        else if ((byte & 0xe0) == 0xc0) {
            const auto next = data.u8(cursor++);
            if ((next & 0xc0) != 0x80)
                throw Error(ErrorCode::malformed_dex, "invalid mutf-8 continuation");
            value = static_cast<std::uint16_t>(((byte & 0x1f) << 6) | (next & 0x3f));
            if (value && value < 0x80)
                throw Error(ErrorCode::malformed_dex, "overlong mutf-8 sequence");
        } else if ((byte & 0xf0) == 0xe0) {
            const auto second = data.u8(cursor++), third = data.u8(cursor++);
            if ((second & 0xc0) != 0x80 || (third & 0xc0) != 0x80)
                throw Error(ErrorCode::malformed_dex, "invalid mutf-8 continuation");
            value =
                static_cast<std::uint16_t>(((byte & 0xf) << 12) | ((second & 0x3f) << 6) | (third & 0x3f));
            if (value < 0x800)
                throw Error(ErrorCode::malformed_dex, "overlong mutf-8 sequence");
        } else
            throw Error(ErrorCode::malformed_dex, "invalid mutf-8 leading byte");
        chars.push_back(static_cast<char16_t>(value));
    }
    if (data.u8(cursor))
        throw Error(ErrorCode::malformed_dex, "missing dex string terminator");
    return chars;
}
} // namespace

std::size_t MethodRef::argumentWords(bool instance) const {
    std::size_t result = instance ? 1 : 0;
    for (const auto& type : parameters)
        result += type == "J" || type == "D" ? 2U : 1U;
    return result;
}

DexFile::DexFile(Bytes bytes) : bytes_(std::move(bytes)) {
    const ByteView data(bytes_, ErrorCode::malformed_dex);
    data.require(0, 112);
    if (data.text(0, 4) != std::string("dex\n", 4) || data.u8(7))
        throw Error(ErrorCode::malformed_dex, "invalid dex magic");
    version_ = data.text(4, 3);
    if (version_ != "035" && version_ != "037" && version_ != "038" && version_ != "039" && version_ != "040")
        throw Error(ErrorCode::unsupported, "unsupported dex version: " + version_);
    if (data.u32(32) != data.size() || data.u32(36) != 112 || data.u32(40) != 0x12345678)
        throw Error(ErrorCode::malformed_dex, "invalid dex header");
    if (data.size() > std::numeric_limits<uInt>::max())
        throw Error(ErrorCode::limit_exceeded, "dex too large");
    const auto checksum =
        adler32(adler32(0, Z_NULL, 0), bytes_.data() + 12, static_cast<uInt>(bytes_.size() - 12));
    if (checksum != data.u32(8))
        throw Error(ErrorCode::malformed_dex, "dex checksum mismatch");
    auto table = [&](std::size_t at, std::size_t stride) {
        const auto count = data.u32(at), offset = data.u32(at + 4);
        if (count > 1000000)
            throw Error(ErrorCode::limit_exceeded, "dex table limit exceeded");
        if (count && (offset < 112 || offset % 4))
            throw Error(ErrorCode::malformed_dex, "misaligned dex table");
        data.require(offset, std::size_t(count) * stride);
        return std::pair{count, offset};
    };
    const auto [stringCount, stringOffset] = table(56, 4);
    strings_.reserve(stringCount);
    for (std::uint32_t i = 0; i < stringCount; ++i) {
        strings16_.push_back(readString(data, data.u32(stringOffset + std::size_t(i) * 4)));
        strings_.push_back(utf8Replacing(strings16_.back()));
    }
    const auto [typeCount, typeOffset] = table(64, 4);
    for (std::uint32_t i = 0; i < typeCount; ++i)
        types_.push_back(string(data.u32(typeOffset + std::size_t(i) * 4)));
    const auto [protoCount, protoOffset] = table(72, 12);
    std::vector<MethodRef> prototypes;
    for (std::uint32_t i = 0; i < protoCount; ++i) {
        const auto at = protoOffset + std::size_t(i) * 12;
        MethodRef proto;
        (void)string(data.u32(at));
        proto.returnType = type(data.u32(at + 4));
        proto.descriptor = "(";
        const auto parameters = data.u32(at + 8);
        if (parameters) {
            const auto count = data.u32(parameters);
            data.require(std::size_t(parameters) + 4, std::size_t(count) * 2);
            for (std::uint32_t n = 0; n < count; ++n) {
                proto.parameters.push_back(type(data.u16(parameters + 4 + std::size_t(n) * 2)));
                proto.descriptor += proto.parameters.back();
            }
        }
        proto.descriptor += ")" + proto.returnType;
        prototypes.push_back(std::move(proto));
    }
    const auto [fieldCount, fieldOffset] = table(80, 8);
    for (std::uint32_t i = 0; i < fieldCount; ++i) {
        const auto at = fieldOffset + std::size_t(i) * 8;
        fields_.push_back({type(data.u16(at)), string(data.u32(at + 4)), type(data.u16(at + 2))});
    }
    const auto [methodCount, methodOffset] = table(88, 8);
    for (std::uint32_t i = 0; i < methodCount; ++i) {
        const auto at = methodOffset + std::size_t(i) * 8;
        auto proto = checked(prototypes, data.u16(at + 2), "prototype");
        proto.owner = type(data.u16(at));
        proto.name = string(data.u32(at + 4));
        methods_.push_back(std::move(proto));
    }
    const auto [classCount, classOffset] = table(96, 32);
    std::set<std::string> defined;
    for (std::uint32_t i = 0; i < classCount; ++i) {
        const auto at = classOffset + std::size_t(i) * 32;
        DexClass klass;
        klass.type = type(data.u32(at));
        klass.access = data.u32(at + 4);
        if (!defined.insert(klass.type).second)
            throw Error(ErrorCode::malformed_dex, "duplicate class definition");
        const auto super = data.u32(at + 8);
        if (super != 0xffffffff)
            klass.super = type(super);
        const auto interfaces = data.u32(at + 12);
        if (interfaces) {
            const auto count = data.u32(interfaces);
            data.require(std::size_t(interfaces) + 4, std::size_t(count) * 2);
            for (std::uint32_t n = 0; n < count; ++n)
                klass.interfaces.push_back(type(data.u16(interfaces + 4 + std::size_t(n) * 2)));
        }
        std::size_t cursor = data.u32(at + 24);
        if (cursor) {
            const auto staticCount = data.uleb(cursor), instanceCount = data.uleb(cursor);
            const auto directCount = data.uleb(cursor), virtualCount = data.uleb(cursor);
            auto fields = [&](std::vector<DexField>& output, std::uint32_t count) {
                if (count > fields_.size())
                    throw Error(ErrorCode::malformed_dex, "invalid class field count");
                std::uint32_t index = 0;
                for (std::uint32_t n = 0; n < count; ++n) {
                    const auto delta = data.uleb(cursor);
                    if ((n && !delta) || delta > std::numeric_limits<std::uint32_t>::max() - index)
                        throw Error(ErrorCode::malformed_dex, "invalid field index delta");
                    index += delta;
                    if (field(index).owner != klass.type)
                        throw Error(ErrorCode::malformed_dex, "field owner mismatch");
                    output.push_back({index, data.uleb(cursor), {}});
                }
            };
            fields(klass.staticFields, staticCount);
            fields(klass.instanceFields, instanceCount);
            auto methods = [&](std::vector<DexMethod>& output, std::uint32_t count) {
                if (count > methods_.size())
                    throw Error(ErrorCode::malformed_dex, "invalid class method count");
                std::uint32_t index = 0;
                for (std::uint32_t n = 0; n < count; ++n) {
                    const auto delta = data.uleb(cursor);
                    if ((n && !delta) || delta > std::numeric_limits<std::uint32_t>::max() - index)
                        throw Error(ErrorCode::malformed_dex, "invalid method index delta");
                    index += delta;
                    const auto& ref = method(index);
                    if (ref.owner != klass.type)
                        throw Error(ErrorCode::malformed_dex, "method owner mismatch");
                    const auto access = data.uleb(cursor), offset = data.uleb(cursor);
                    DexMethod item{index, access, std::nullopt};
                    if (offset) {
                        item.code = readCode(offset);
                        if (item.code->incoming != ref.argumentWords(!(access & 8)))
                            throw Error(ErrorCode::malformed_dex, "method argument count mismatch");
                    }
                    output.push_back(std::move(item));
                }
            };
            methods(klass.directMethods, directCount);
            methods(klass.virtualMethods, virtualCount);
        }
        cursor = data.u32(at + 28);
        if (cursor) {
            const auto count = data.uleb(cursor);
            if (count > klass.staticFields.size())
                throw Error(ErrorCode::malformed_dex, "too many static initializers");
            for (std::uint32_t n = 0; n < count; ++n)
                klass.staticFields[n].initial = readEncoded(cursor);
        }
        classes_.push_back(std::move(klass));
    }
}

const std::string& DexFile::string(std::uint32_t index) const {
    return checked(strings_, index, "string");
}
const std::u16string& DexFile::string16(std::uint32_t index) const {
    return checked(strings16_, index, "string");
}
const std::string& DexFile::type(std::uint32_t index) const {
    return checked(types_, index, "type");
}
const MethodRef& DexFile::method(std::uint32_t index) const {
    return checked(methods_, index, "method");
}
const FieldRef& DexFile::field(std::uint32_t index) const {
    return checked(fields_, index, "field");
}

Code DexFile::readCode(std::uint32_t offset) const {
    const ByteView data(bytes_, ErrorCode::malformed_dex);
    data.require(offset, 16);
    if (offset % 4)
        throw Error(ErrorCode::malformed_dex, "misaligned method code");
    Code code;
    code.registers = data.u16(offset);
    code.incoming = data.u16(offset + 2);
    code.outgoing = data.u16(offset + 4);
    const auto tries = data.u16(offset + 6);
    const auto count = data.u32(offset + 12);
    if (code.incoming > code.registers)
        throw Error(ErrorCode::malformed_dex, "method registers smaller than arguments");
    data.require(std::size_t(offset) + 16, std::size_t(count) * 2);
    code.words.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        code.words.push_back(data.u16(offset + 16 + std::size_t(i) * 2));
    if (tries) {
        const auto tryOffset = std::size_t(offset) + 16 + std::size_t(count) * 2 + (count % 2 ? 2 : 0);
        data.require(tryOffset, std::size_t(tries) * 8);
        const auto handlerOffset = tryOffset + std::size_t(tries) * 8;
        auto cursor = handlerOffset;
        const auto handlerCount = data.uleb(cursor);
        if (handlerCount > data.size())
            throw Error(ErrorCode::malformed_dex, "invalid catch handler count");
        std::map<std::size_t, std::vector<CatchHandler>> handlers;
        for (std::uint32_t n = 0; n < handlerCount; ++n) {
            const auto relative = cursor - handlerOffset;
            const auto size = data.sleb(cursor);
            const auto typed = size < 0 ? -std::int64_t(size) : std::int64_t(size);
            if (static_cast<std::uint64_t>(typed) > data.size())
                throw Error(ErrorCode::malformed_dex, "invalid catch handler size");
            auto& list = handlers[relative];
            for (std::int64_t j = 0; j < typed; ++j) {
                const auto name = type(data.uleb(cursor));
                const auto target = data.uleb(cursor);
                if (target >= count)
                    throw Error(ErrorCode::malformed_dex, "catch target outside method");
                list.push_back({name, target});
            }
            if (size <= 0) {
                const auto target = data.uleb(cursor);
                if (target >= count)
                    throw Error(ErrorCode::malformed_dex, "catch target outside method");
                list.push_back({"", target});
            }
        }
        for (unsigned n = 0; n < tries; ++n) {
            const auto at = tryOffset + std::size_t(n) * 8;
            const auto start = data.u32(at);
            const auto length = data.u16(at + 4), relative = data.u16(at + 6);
            if (start > count || length > count - start || !handlers.contains(relative))
                throw Error(ErrorCode::malformed_dex, "invalid try region");
            code.tries.push_back({start, length, handlers.at(relative)});
        }
    }
    return code;
}

EncodedValue DexFile::readEncoded(std::size_t& cursor, unsigned depth) const {
    if (depth > 64)
        throw Error(ErrorCode::limit_exceeded, "encoded value nesting limit");
    const ByteView data(bytes_, ErrorCode::malformed_dex);
    const auto header = data.u8(cursor++);
    const auto tag = static_cast<std::uint8_t>(header & 0x1f);
    const unsigned argument = header >> 5;
    EncodedValue result{tag, 0, {}};
    if (tag == 0x1e || tag == 0x1f) {
        if (argument > (tag == 0x1f ? 1U : 0U))
            throw Error(ErrorCode::malformed_dex, "invalid encoded null or boolean");
        result.bits = argument;
        return result;
    }
    if (tag == 0x1c) {
        if (argument)
            throw Error(ErrorCode::malformed_dex, "invalid encoded array");
        const auto count = data.uleb(cursor);
        if (count > data.size() - cursor)
            throw Error(ErrorCode::malformed_dex, "invalid encoded array length");
        for (std::uint32_t n = 0; n < count; ++n)
            result.elements.push_back(readEncoded(cursor, depth + 1));
        return result;
    }
    unsigned maxBytes;
    switch (tag) {
    case 0x00:
        maxBytes = 1;
        break;
    case 0x02:
    case 0x03:
        maxBytes = 2;
        break;
    case 0x04:
    case 0x10:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
        maxBytes = 4;
        break;
    case 0x06:
    case 0x11:
        maxBytes = 8;
        break;
    default:
        throw Error(ErrorCode::unsupported, "unsupported encoded value tag " + std::to_string(tag));
    }
    const auto count = argument + 1;
    if (count > maxBytes)
        throw Error(ErrorCode::malformed_dex, "invalid encoded value width");
    for (unsigned n = 0; n < count; ++n)
        result.bits |= std::uint64_t(data.u8(cursor++)) << (8 * n);
    if (tag == 0x10 || tag == 0x11)
        result.bits <<= (maxBytes - count) * 8;
    else if ((tag == 0 || tag == 2 || tag == 4 || tag == 6) && count < 8 &&
             (result.bits & (std::uint64_t(0x80) << (8 * (count - 1)))))
        result.bits |= ~std::uint64_t(0) << (count * 8);
    if (tag == 0x17)
        (void)string(static_cast<std::uint32_t>(result.bits));
    if (tag == 0x18)
        (void)type(static_cast<std::uint32_t>(result.bits));
    return result;
}

} // namespace gutenberg
