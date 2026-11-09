#include "gutenberg/elf.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <unordered_map>

namespace gutenberg {
namespace {
constexpr std::size_t tableLimit = 1000000;
[[noreturn]] void malformed(const char* message) {
    throw Error(ErrorCode::malformed_elf, message);
}
bool range(std::uint64_t start, std::uint64_t size, std::uint64_t address, std::uint64_t count) {
    return address >= start && address - start <= size && count <= size - (address - start);
}
std::int64_t sleb64(const ByteView& data, std::size_t& cursor) {
    std::uint64_t result = 0;
    for (unsigned n = 0; n < 10; ++n) {
        const auto byte = data.u8(cursor++);
        const auto value = static_cast<std::uint64_t>(byte & 127);
        if (n == 9 && value != 0 && value != 127)
            malformed("overflow in packed ELF relocation");
        result |= value << (n * 7);
        if (!(byte & 128)) {
            if (n < 9 && (byte & 64))
                result |= (~std::uint64_t{0}) << ((n + 1) * 7);
            return std::bit_cast<std::int64_t>(result);
        }
    }
    malformed("unterminated packed ELF relocation");
}
std::uint64_t addChecked(std::uint64_t value, std::int64_t delta) {
    if ((delta >= 0 && static_cast<std::uint64_t>(delta) > UINT64_MAX - value) ||
        (delta < 0 && std::uint64_t{0} - static_cast<std::uint64_t>(delta) > value))
        malformed("ELF relocation address overflow");
    return value + static_cast<std::uint64_t>(delta);
}
std::size_t relocationWidth(ElfMachine machine, std::uint32_t type) {
    return machine == ElfMachine::aarch64 ? (type == 258 || type == 261 ? 4 : 8)
                                          : (type == 2 || type == 10 || type == 11 ? 4 : 8);
}
} // namespace

bool ElfFile::contains(std::uint64_t address, std::size_t count, std::uint32_t flags) const {
    return std::any_of(segments_.begin(), segments_.end(), [&](const auto& segment) {
        return (segment.flags & flags) == flags && range(segment.address, segment.memorySize, address, count);
    });
}
std::size_t ElfFile::fileOffset(std::uint64_t address, std::size_t count) const {
    for (const auto& segment : segments_)
        if (range(segment.address, segment.fileSize, address, count))
            return static_cast<std::size_t>(segment.offset + address - segment.address);
    malformed("ELF address is outside file-backed load segments");
}
ElfFile::ElfFile(Bytes bytes) : bytes_(std::move(bytes)) {
    const ByteView data(bytes_, ErrorCode::malformed_elf);
    data.require(0, 64);
    if (data.text(0, 4) != std::string("\x7f"
                                       "ELF",
                                       4) ||
        data.u8(4) != 2 || data.u8(5) != 1 || data.u8(6) != 1)
        malformed("expected a little-endian ELF64 shared library");
    if (data.u16(16) != 3 || data.u32(20) != 1 || data.u16(52) != 64 || data.u16(54) != 56)
        malformed("invalid ELF shared library header");
    if (data.u16(18) != 62 && data.u16(18) != 183)
        throw Error(ErrorCode::unsupported, "ELF machine requires an instruction translator");
    machine_ = static_cast<ElfMachine>(data.u16(18));
    const auto headers = data.u64(32);
    const auto headerCount = data.u16(56);
    if (!headerCount || headerCount > 4096 || headers > data.size() || headers % 8)
        malformed("invalid ELF program header table");
    data.require(static_cast<std::size_t>(headers), std::size_t(headerCount) * 56);
    std::optional<std::pair<std::uint64_t, std::uint64_t>> dynamic;
    for (unsigned n = 0; n < headerCount; ++n) {
        const auto at = static_cast<std::size_t>(headers) + n * 56;
        const auto kind = data.u32(at);
        const auto offset = data.u64(at + 8), address = data.u64(at + 16);
        const auto fileSize = data.u64(at + 32), memorySize = data.u64(at + 40),
                   alignment = data.u64(at + 48);
        if (offset > data.size() || fileSize > data.size() - offset || memorySize > UINT64_MAX - address)
            malformed("ELF segment range overflow");
        if (kind == 1) {
            if (fileSize > memorySize || (alignment > 1 && ((alignment & (alignment - 1)) ||
                                                            address % alignment != offset % alignment)))
                malformed("invalid ELF load segment alignment or size");
            if (memorySize)
                segments_.push_back({address, offset, fileSize, memorySize, alignment, data.u32(at + 4)});
        } else if (kind == 2) {
            if (dynamic || fileSize % 16)
                malformed("invalid ELF dynamic segment");
            dynamic = {offset, fileSize};
        } else if (kind == 7)
            hasTls_ = memorySize != 0;
    }
    if (segments_.empty())
        malformed("ELF contains no load segments");
    std::sort(segments_.begin(), segments_.end(),
              [](const auto& a, const auto& b) { return a.address < b.address; });
    imageStart_ = segments_.front().address;
    for (const auto& segment : segments_) {
        if (segment.address < imageEnd_)
            malformed("overlapping ELF load segments");
        imageEnd_ = segment.address + segment.memorySize;
    }
    if (imageSize() > 512 * 1024 * 1024)
        throw Error(ErrorCode::limit_exceeded, "ELF image exceeds its address space budget");
    if (!dynamic)
        return;
    std::unordered_map<std::uint64_t, std::uint64_t> tags;
    std::vector<std::uint64_t> needed;
    bool terminated = false;
    for (std::uint64_t at = dynamic->first; at < dynamic->first + dynamic->second; at += 16) {
        const auto tag = data.u64(static_cast<std::size_t>(at)),
                   value = data.u64(static_cast<std::size_t>(at + 8));
        if (!tag) {
            terminated = true;
            break;
        }
        if (tag == 1)
            needed.push_back(value);
        else if (!tags.emplace(tag, value).second)
            malformed("duplicate ELF dynamic tag");
    }
    if (!terminated)
        malformed("unterminated ELF dynamic table");
    const auto tag = [&](std::uint64_t key, std::uint64_t fallback = 0) {
        const auto it = tags.find(key);
        return it == tags.end() ? fallback : it->second;
    };
    const auto stringSize = tag(10);
    if (stringSize > bytes_.size())
        malformed("ELF dynamic string table size");
    const auto stringOffset = stringSize ? fileOffset(tag(5), static_cast<std::size_t>(stringSize)) : 0;
    const auto string = [&](std::uint64_t index) {
        if (index >= stringSize)
            malformed("ELF symbol string index");
        auto end = index;
        while (end < stringSize && data.u8(stringOffset + static_cast<std::size_t>(end))) {
            if (end - index >= 65536)
                throw Error(ErrorCode::limit_exceeded, "ELF symbol name budget exceeded");
            ++end;
        }
        if (end == stringSize)
            malformed("unterminated ELF symbol string");
        return data.text(stringOffset + static_cast<std::size_t>(index),
                         static_cast<std::size_t>(end - index));
    };
    for (const auto index : needed)
        dependencies_.push_back(string(index));
    if (tags.contains(14))
        soname_ = string(tag(14));
    symbolic_ = tags.contains(16) || (tag(30) & 2);
    initializer_ = tag(12);
    finalizer_ = tag(13);
    if (initializer_ && !contains(initializer_, 1, 1))
        malformed("ELF initializer is outside executable segments");
    if (finalizer_ && !contains(finalizer_, 1, 1))
        malformed("ELF finalizer is outside executable segments");
    initializerArray_ = {tag(25), tag(27)};
    finalizerArray_ = {tag(26), tag(28)};
    for (const auto [address, size] : {initializerArray_, finalizerArray_})
        if (size &&
            (size % 8 || size > tableLimit * 8 || !contains(address, static_cast<std::size_t>(size), 4)))
            malformed("invalid ELF constructor array");
    const auto append = [&](std::uint64_t address, std::uint64_t info, std::int64_t addend,
                            bool implicit = false) {
        if (relocations_.size() >= tableLimit)
            throw Error(ErrorCode::limit_exceeded, "ELF relocation budget exceeded");
        const auto type = static_cast<std::uint32_t>(info);
        if (type && !contains(address, relocationWidth(machine_, type)))
            malformed("ELF relocation target is outside load segments");
        relocations_.push_back({address, type, static_cast<std::uint32_t>(info >> 32), addend, implicit});
    };
    const auto rela = [&](std::uint64_t address, std::uint64_t size) {
        if (!size)
            return;
        if (size % 24 || size / 24 > tableLimit)
            malformed("invalid ELF RELA table size");
        const auto offset = fileOffset(address, static_cast<std::size_t>(size));
        for (std::size_t at = offset; at < offset + size; at += 24)
            append(data.u64(at), data.u64(at + 8), std::bit_cast<std::int64_t>(data.u64(at + 16)));
    };
    if (tag(8) && tag(9, 24) != 24)
        malformed("invalid ELF RELA entry size");
    rela(tag(7), tag(8));
    if (tag(2)) {
        if (tag(20) != 7)
            throw Error(ErrorCode::unsupported, "ELF PLT requires RELA relocations");
        rela(tag(23), tag(2));
    }
    if (tag(18) || tag(0x60000010))
        throw Error(ErrorCode::unsupported, "ELF64 REL relocations are not supported");
    if (tag(0x60000012)) {
        const auto size = tag(0x60000012);
        if (size > bytes_.size())
            malformed("packed ELF relocation size");
        const auto offset = fileOffset(tag(0x60000011), static_cast<std::size_t>(size));
        const ByteView packed(data.slice(offset, static_cast<std::size_t>(size)), ErrorCode::malformed_elf);
        if (packed.text(0, 4) != "APS2")
            malformed("invalid Android packed relocation header");
        std::size_t cursor = 4;
        const auto count = sleb64(packed, cursor), initialOffset = sleb64(packed, cursor);
        if (count < 0 || count > static_cast<std::int64_t>(tableLimit) || initialOffset < 0)
            malformed("invalid Android packed relocation count");
        auto address = static_cast<std::uint64_t>(initialOffset);
        std::uint64_t addend = 0;
        for (std::int64_t read = 0; read < count;) {
            const auto group = sleb64(packed, cursor), flags = sleb64(packed, cursor);
            if (group <= 0 || group > count - read || flags < 0 || flags > 15 ||
                ((flags & 4) && !(flags & 8)))
                malformed("invalid Android packed relocation group");
            const auto delta = flags & 2 ? sleb64(packed, cursor) : 0;
            const auto info = flags & 1 ? static_cast<std::uint64_t>(sleb64(packed, cursor)) : 0;
            const auto addendDelta = (flags & 12) == 12 ? sleb64(packed, cursor) : 0;
            if ((flags & 12) == 12)
                addend += static_cast<std::uint64_t>(addendDelta);
            else if (!(flags & 8))
                addend = 0;
            for (std::int64_t n = 0; n < group; ++n) {
                address = addChecked(address, flags & 2 ? delta : sleb64(packed, cursor));
                const auto itemInfo = flags & 1 ? info : static_cast<std::uint64_t>(sleb64(packed, cursor));
                if ((flags & 12) == 8)
                    addend += static_cast<std::uint64_t>(sleb64(packed, cursor));
                append(address, itemInfo, std::bit_cast<std::int64_t>(addend));
            }
            read += group;
        }
        if (cursor != packed.size())
            malformed("trailing Android packed relocation data");
    }
    const auto relrSize = tag(35, tag(0x6fffe001));
    if (relrSize) {
        if (relrSize % 8 || relrSize / 8 > tableLimit || tag(37, tag(0x6fffe003, 8)) != 8)
            malformed("invalid ELF RELR table size");
        const auto at = fileOffset(tag(36, tag(0x6fffe000)), static_cast<std::size_t>(relrSize));
        std::uint64_t address = 0;
        const auto relative = machine_ == ElfMachine::aarch64 ? 1027U : 8U;
        for (std::size_t n = 0; n < relrSize; n += 8) {
            const auto entry = data.u64(at + n);
            if (!(entry & 1)) {
                if (entry < address || entry > UINT64_MAX - 8)
                    malformed("invalid ELF RELR address");
                append(entry, relative, 0, true);
                address = entry + 8;
            } else {
                if (!address || address > UINT64_MAX - 63 * 8)
                    malformed("invalid ELF RELR bitmap base");
                for (unsigned bit = 1; bit < 64; ++bit)
                    if (entry & (std::uint64_t{1} << bit))
                        append(address + (bit - 1) * 8, relative, 0, true);
                address += 63 * 8;
            }
        }
    }
    std::size_t symbolCount = 0;
    if (tag(4)) {
        const auto at = fileOffset(tag(4), 8);
        symbolCount = data.u32(at + 4);
        const auto buckets = data.u32(at);
        if (symbolCount > tableLimit || buckets > tableLimit)
            throw Error(ErrorCode::limit_exceeded, "ELF hash table budget exceeded");
        (void)fileOffset(tag(4), 8 + (symbolCount + buckets) * 4);
    } else if (tag(0x6ffffef5)) {
        const auto address = tag(0x6ffffef5);
        const auto at = fileOffset(address, 16);
        const auto buckets = data.u32(at), first = data.u32(at + 4), bloom = data.u32(at + 8);
        if (!bloom || bloom > tableLimit || buckets > tableLimit || first > tableLimit)
            malformed("invalid GNU ELF hash table");
        const auto bucketAddress = address + 16 + std::uint64_t(bloom) * 8;
        const auto bucketOffset = fileOffset(bucketAddress, std::size_t(buckets) * 4);
        std::uint32_t maximum = 0;
        for (std::uint32_t n = 0; n < buckets; ++n)
            maximum = std::max(maximum, data.u32(bucketOffset + std::size_t(n) * 4));
        symbolCount = first;
        if (maximum) {
            if (maximum < first || maximum >= tableLimit)
                malformed("invalid GNU ELF hash bucket");
            auto chainAddress =
                bucketAddress + std::uint64_t(buckets) * 4 + std::uint64_t(maximum - first) * 4;
            for (symbolCount = maximum;; ++symbolCount, chainAddress += 4) {
                if (symbolCount >= tableLimit)
                    throw Error(ErrorCode::limit_exceeded, "ELF symbol budget exceeded");
                if (data.u32(fileOffset(chainAddress, 4)) & 1) {
                    ++symbolCount;
                    break;
                }
            }
        }
    } else if (tag(6))
        throw Error(ErrorCode::unsupported, "ELF symbol table requires a dynamic hash table");
    if (symbolCount) {
        if (tag(11, 24) != 24 || !tag(6))
            malformed("invalid ELF symbol table");
        const auto offset = fileOffset(tag(6), symbolCount * 24);
        symbols_.reserve(symbolCount);
        std::size_t nameBytes = 0;
        for (std::size_t n = 0; n < symbolCount; ++n) {
            const auto at = offset + n * 24;
            const auto info = data.u8(at + 4);
            ElfSymbol symbol{string(data.u32(at)),
                             data.u64(at + 8),
                             data.u64(at + 16),
                             data.u16(at + 6),
                             static_cast<std::uint8_t>(info >> 4),
                             static_cast<std::uint8_t>(info & 15),
                             static_cast<std::uint8_t>(data.u8(at + 5) & 3)};
            if (symbol.section && symbol.section < 0xff00 && symbol.type != 6 &&
                !contains(symbol.value, static_cast<std::size_t>(symbol.size)))
                malformed("ELF symbol is outside load segments");
            nameBytes += symbol.name.size();
            if (nameBytes > 64 * 1024 * 1024)
                throw Error(ErrorCode::limit_exceeded, "ELF symbol table text budget exceeded");
            if (symbol.section && symbol.binding && symbol.visibility != 1 && symbol.visibility != 2) {
                const auto [entry, inserted] = exports_.emplace(symbol.name, n);
                if (!inserted && symbols_[entry->second].binding == 2 && symbol.binding == 1)
                    entry->second = n;
            }
            symbols_.push_back(std::move(symbol));
        }
    }
    for (const auto& relocation : relocations_)
        if (relocation.symbol && relocation.symbol >= symbols_.size())
            malformed("ELF relocation symbol index");
}
const ElfSymbol* ElfFile::exported(std::string_view name) const {
    const auto found = exports_.find(std::string(name));
    return found == exports_.end() ? nullptr : &symbols_[found->second];
}
void ElfFile::map(std::span<std::uint8_t> image) const {
    if (image.size() < imageSize())
        throw Error(ErrorCode::native_link, "ELF mapping is smaller than its load segments");
    std::fill(image.begin(), image.end(), 0);
    for (const auto& segment : segments_)
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(segment.offset),
                    static_cast<std::size_t>(segment.fileSize),
                    image.begin() + static_cast<std::ptrdiff_t>(segment.address - imageStart_));
}
void ElfFile::relocate(std::span<std::uint8_t> image, std::uint64_t loadBias,
                       const Resolver& resolver) const {
    if (image.size() < imageSize())
        throw Error(ErrorCode::native_link, "ELF mapping is smaller than its load segments");
    for (const auto& relocation : relocations_) {
        if (!relocation.type)
            continue;
        const auto at = static_cast<std::size_t>(relocation.address - imageStart_);
        auto addend = static_cast<std::uint64_t>(relocation.addend);
        if (relocation.implicitAddend)
            addend = ByteView(image, ErrorCode::malformed_elf).u64(at);
        const auto relative = machine_ == ElfMachine::aarch64 ? 1027U : 8U;
        std::uint64_t value;
        if (relocation.type == relative) {
            if (relocation.symbol)
                malformed("ELF relative relocation has a symbol");
            value = loadBias + addend;
        } else {
            if (relocation.symbol && relocation.symbol >= symbols_.size())
                malformed("ELF relocation requires a symbol");
            const ElfSymbol nullSymbol{};
            const auto& symbol = relocation.symbol ? symbols_[relocation.symbol] : nullSymbol;
            if (symbol.type == 6 || symbol.type == 10 || symbol.section == 0xfff2)
                throw Error(ErrorCode::unsupported,
                            "ELF TLS, indirect functions and common symbols require runtime resolution");
            std::optional<std::uint64_t> resolved =
                relocation.symbol ? std::nullopt : std::optional<std::uint64_t>{0};
            if (!resolved && (!symbol.section || (!symbolic_ && symbol.binding && !symbol.visibility)) &&
                resolver)
                resolved = resolver(symbol.name);
            if (!resolved && symbol.section)
                resolved = symbol.section == 0xfff1 ? symbol.value : loadBias + symbol.value;
            if (!resolved && symbol.binding == 2)
                resolved = 0;
            if (!resolved)
                throw Error(ErrorCode::native_link, "unresolved ELF symbol: " + symbol.name);
            value = *resolved + addend;
            const bool valid =
                machine_ == ElfMachine::aarch64
                    ? relocation.type == 257 || relocation.type == 258 || relocation.type == 261 ||
                          relocation.type == 1025 || relocation.type == 1026
                    : relocation.type == 1 || relocation.type == 2 || relocation.type == 6 ||
                          relocation.type == 7 || relocation.type == 10 || relocation.type == 11;
            if (!valid)
                throw Error(ErrorCode::unsupported,
                            "unsupported ELF relocation: " + std::to_string(relocation.type));
            if (machine_ == ElfMachine::x86_64 && (relocation.type == 6 || relocation.type == 7))
                value = *resolved;
            if ((machine_ == ElfMachine::aarch64 && relocation.type == 261) ||
                (machine_ == ElfMachine::x86_64 && relocation.type == 2))
                value -= loadBias + relocation.address;
        }
        const auto width = relocationWidth(machine_, relocation.type);
        if (width == 4) {
            const bool signedValue = machine_ == ElfMachine::aarch64
                                         ? relocation.type == 261
                                         : relocation.type == 2 || relocation.type == 11;
            const auto signedResult = std::bit_cast<std::int64_t>(value);
            if (signedValue ? signedResult < INT32_MIN || signedResult > INT32_MAX : value > UINT32_MAX)
                throw Error(ErrorCode::native_link, "ELF relocation does not fit its target");
        }
        for (std::size_t n = 0; n < width; ++n)
            image[at + n] = static_cast<std::uint8_t>(value >> (n * 8));
    }
}
} // namespace gutenberg
