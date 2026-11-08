#include "test.hpp"
#include "binary_builder.hpp"
#include "gutenberg/dex.hpp"
#include <zlib.h>

using namespace gutenberg;

namespace {
Bytes emptyDex() {
    Bytes bytes(112);
    const std::string magic("dex\n035\0", 8);
    std::copy(magic.begin(), magic.end(), bytes.begin());
    fixture::patch32(bytes, 32, 112);
    fixture::patch32(bytes, 36, 112);
    fixture::patch32(bytes, 40, 0x12345678);
    fixture::patch32(bytes, 8, static_cast<std::uint32_t>(adler32(1, bytes.data() + 12, 100)));
    return bytes;
}
}

TEST(dex_header_and_version) {
    DexFile dex(emptyDex());
    CHECK(dex.version() == "035");
    CHECK(dex.classes().empty());
    test::rejects([&] { (void)dex.string(0); });
    test::rejects([&] { (void)dex.method(0); });
}

TEST(dex_checksum_and_truncation) {
    auto bytes = emptyDex();
    bytes[100] ^= 1;
    test::rejects([&] { (void)DexFile(bytes); });
    bytes = emptyDex();
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        test::rejects([&] { (void)DexFile(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size))); });
    }
}

TEST(method_register_word_count) {
    MethodRef ref{"LExample;", "run", "(IJD[Ljava/lang/String;)V", {"I", "J", "D", "[Ljava/lang/String;"}, "V"};
    CHECK(ref.argumentWords(false) == 6);
    CHECK(ref.argumentWords(true) == 7);
    CHECK(ref.key() == "LExample;->run(IJD[Ljava/lang/String;)V");
}
