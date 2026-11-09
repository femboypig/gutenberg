#include "binary_builder.hpp"
#include "gutenberg/elf.hpp"
#include "test.hpp"
#include <bit>

using namespace gutenberg;
namespace {
void put64(Bytes& bytes, std::size_t at, std::uint64_t value) {
    fixture::patch32(bytes, at, static_cast<std::uint32_t>(value));
    fixture::patch32(bytes, at + 4, static_cast<std::uint32_t>(value >> 32));
}
void put16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
Bytes library(bool gnuHash = false) {
    Bytes bytes(1024);
    bytes[0] = 127;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    put16(bytes, 16, 3);
    put16(bytes, 18, 183);
    fixture::patch32(bytes, 20, 1);
    put64(bytes, 32, 64);
    put16(bytes, 52, 64);
    put16(bytes, 54, 56);
    put16(bytes, 56, 2);
    fixture::patch32(bytes, 64, 1);
    fixture::patch32(bytes, 68, 6);
    put64(bytes, 96, bytes.size());
    put64(bytes, 104, 0x2000);
    put64(bytes, 112, 0x1000);
    fixture::patch32(bytes, 120, 2);
    fixture::patch32(bytes, 124, 6);
    put64(bytes, 128, 0x100);
    put64(bytes, 136, 0x100);
    put64(bytes, 152, 0xb0);
    put64(bytes, 160, 0xb0);
    put64(bytes, 168, 8);
    std::size_t at = 0x100;
    for (const auto [tag, value] : {std::pair<std::uint64_t, std::uint64_t>{6, 0x200},
                                    {11, 24},
                                    {5, 0x260},
                                    {10, 49},
                                    {gnuHash ? 0x6ffffef5U : 4U, 0x2a0},
                                    {14, 19},
                                    {1, 32},
                                    {7, 0x300},
                                    {8, 72},
                                    {9, 24},
                                    {0, 0}}) {
        put64(bytes, at, tag);
        put64(bytes, at + 8, value);
        at += 16;
    }
    fixture::patch32(bytes, 0x218, 1);
    bytes[0x21c] = 0x11;
    put16(bytes, 0x21e, 1);
    put64(bytes, 0x220, 0x1000);
    put64(bytes, 0x228, 8);
    fixture::patch32(bytes, 0x230, 10);
    bytes[0x234] = 0x12;
    const std::string names("\0provided\0imported\0libsample.so\0libdependency.so\0", 49);
    std::copy(names.begin(), names.end(), bytes.begin() + 0x260);
    fixture::patch32(bytes, 0x2a0, 1);
    if (gnuHash) {
        fixture::patch32(bytes, 0x2a4, 1);
        fixture::patch32(bytes, 0x2a8, 1);
        fixture::patch32(bytes, 0x2ac, 5);
        put64(bytes, 0x2b0, UINT64_MAX);
        fixture::patch32(bytes, 0x2b8, 1);
        fixture::patch32(bytes, 0x2bc, 0x20);
        fixture::patch32(bytes, 0x2c0, 0x21);
    } else {
        fixture::patch32(bytes, 0x2a4, 3);
        fixture::patch32(bytes, 0x2a8, 1);
    }
    put64(bytes, 0x300, 0x380);
    put64(bytes, 0x308, 1027);
    put64(bytes, 0x310, 0x123);
    put64(bytes, 0x318, 0x388);
    put64(bytes, 0x320, (std::uint64_t{1} << 32) | 257);
    put64(bytes, 0x328, UINT64_MAX - 3);
    put64(bytes, 0x330, 0x390);
    put64(bytes, 0x338, (std::uint64_t{2} << 32) | 1026);
    put64(bytes, 0x340, 8);
    return bytes;
}
void sleb(Bytes& bytes, std::int64_t value) {
    for (;;) {
        auto byte = static_cast<std::uint8_t>(value & 127);
        value >>= 7;
        if ((value == 0 && !(byte & 64)) || (value == -1 && (byte & 64))) {
            bytes.push_back(byte);
            return;
        }
        bytes.push_back(byte | 128);
    }
}
} // namespace

TEST(elf_parses_stripped_symbols_dependencies_and_zero_fills_bss) {
    for (const bool gnuHash : {false, true}) {
        const ElfFile file(library(gnuHash));
        CHECK(file.machine() == ElfMachine::aarch64 && file.imageSize() == 0x2000);
        CHECK(file.soname() == "libsample.so");
        CHECK(file.dependencies() == std::vector<std::string>{"libdependency.so"});
        CHECK(file.symbols().size() == 3);
        CHECK(file.exported("provided") && file.exported("provided")->value == 0x1000);
        CHECK(!file.exported("imported"));
        Bytes image(static_cast<std::size_t>(file.imageSize()), 0xff);
        file.map(image);
        CHECK(ByteView(image).u64(0x1000) == 0);
        CHECK(ByteView(image).u32(0) == 0x464c457f);
    }
}

TEST(elf_applies_relative_absolute_and_import_relocations) {
    const ElfFile file(library());
    Bytes image(static_cast<std::size_t>(file.imageSize()));
    file.map(image);
    file.relocate(image, 0x70000000, [](std::string_view name) -> std::optional<std::uint64_t> {
        return name == "imported" ? std::optional<std::uint64_t>(0x60000000) : std::nullopt;
    });
    CHECK(ByteView(image).u64(0x380) == 0x70000123);
    CHECK(ByteView(image).u64(0x388) == 0x70000ffc);
    CHECK(ByteView(image).u64(0x390) == 0x60000008);
    test::rejects([&] { file.relocate(image, 0x70000000, {}); });
    auto weak = library();
    weak[0x234] = 0x22;
    const ElfFile optional(std::move(weak));
    optional.map(image);
    optional.relocate(image, 0x70000000, {});
    CHECK(ByteView(image).u64(0x390) == 8);
}

TEST(elf_android_packed_addends_are_shared_per_group_and_accumulate_between_groups) {
    auto bytes = library();
    Bytes packed{'A', 'P', 'S', '2'};
    for (const auto value : {3, 0x378, 2, 15, 8, 1027, 0x100, 1, 9, 1027, 8, -8})
        sleb(packed, value);
    put64(bytes, 0x170, 0x60000011);
    put64(bytes, 0x180, 0x60000012);
    put64(bytes, 0x188, packed.size());
    std::copy(packed.begin(), packed.end(), bytes.begin() + 0x300);
    const ElfFile file(bytes);
    CHECK(file.relocations().size() == 3);
    Bytes image(static_cast<std::size_t>(file.imageSize()));
    file.map(image);
    file.relocate(image, 0x10000000, {});
    CHECK(ByteView(image).u64(0x380) == 0x10000100);
    CHECK(ByteView(image).u64(0x388) == 0x10000100);
    CHECK(ByteView(image).u64(0x390) == 0x100000f8);
    bytes[0x300] = 'X';
    test::rejects([&] { (void)ElfFile(bytes); });
}

TEST(elf_relr_bitmaps_use_existing_pointer_addends) {
    auto bytes = library();
    put64(bytes, 0x170, 36);
    put64(bytes, 0x180, 35);
    put64(bytes, 0x188, 16);
    put64(bytes, 0x300, 0x380);
    put64(bytes, 0x308, 11);
    put64(bytes, 0x380, 0x10);
    put64(bytes, 0x388, 0x20);
    put64(bytes, 0x398, 0x30);
    const ElfFile file(bytes);
    CHECK(file.relocations().size() == 3);
    Bytes image(static_cast<std::size_t>(file.imageSize()));
    file.map(image);
    file.relocate(image, 0x20000000, {});
    CHECK(ByteView(image).u64(0x380) == 0x20000010);
    CHECK(ByteView(image).u64(0x388) == 0x20000020);
    CHECK(ByteView(image).u64(0x398) == 0x20000030);
    put64(bytes, 0x300, 3);
    test::rejects([&] { (void)ElfFile(bytes); });
}

TEST(elf_rejects_truncation_overlaps_and_out_of_image_relocations) {
    auto bytes = library();
    for (std::size_t size = 0; size < bytes.size(); ++size)
        test::rejects(
            [&] { (void)ElfFile(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size))); });
    put64(bytes, 0x300, UINT64_MAX);
    test::rejects([&] { (void)ElfFile(bytes); });
    bytes = library();
    put64(bytes, 96, 0x3000);
    test::rejects([&] { (void)ElfFile(bytes); });
    bytes = library();
    put64(bytes, 112, 3);
    test::rejects([&] { (void)ElfFile(bytes); });
    bytes = library();
    put64(bytes, 0x338, (std::uint64_t{999} << 32) | 1026);
    test::rejects([&] { (void)ElfFile(bytes); });
}
