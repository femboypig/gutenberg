#include "test.hpp"
#include "gutenberg/archive.hpp"
#include <limits>

using namespace gutenberg;

TEST(little_endian_and_bounds) {
    const Bytes bytes{0x78, 0x56, 0x34, 0x12, 0xff};
    const ByteView view(bytes);
    CHECK(view.u32(0) == 0x12345678);
    CHECK(view.u16(1) == 0x3456);
    test::rejects([&] { (void)view.u32(2); });
    test::rejects([&] { (void)view.slice(std::numeric_limits<std::size_t>::max(), 2); });
}

TEST(leb128_values_and_overflow) {
    Bytes bytes{0xe5, 0x8e, 0x26, 0x9b, 0xf1, 0x59};
    ByteView view(bytes);
    std::size_t cursor = 0;
    CHECK(view.uleb(cursor) == 624485);
    CHECK(view.sleb(cursor) == -624485);
    bytes = {0xff, 0xff, 0xff, 0xff, 0x1f};
    cursor = 0;
    test::rejects([&] { (void)ByteView(bytes).uleb(cursor); });
}

TEST(unicode_surrogate_pairs) {
    CHECK(utf8(u"hello мир 😀") == "hello мир 😀");
    const std::u16string invalid{0xd800};
    test::rejects([&] { (void)utf8(invalid); });
}

TEST(empty_archive_and_invalid_directory) {
    Bytes bytes(22);
    bytes[0] = 0x50; bytes[1] = 0x4b; bytes[2] = 5; bytes[3] = 6;
    CHECK(Archive(bytes).entries().empty());
    bytes[16] = 23;
    test::rejects([&] { (void)Archive(bytes); });
}

int main() {
    unsigned failures = 0;
    for (const auto& [name, body] : test::cases()) {
        try { body(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << test::cases().size() << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
