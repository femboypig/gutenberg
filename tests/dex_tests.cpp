#include "binary_builder.hpp"
#include "gutenberg/dex.hpp"
#include "test.hpp"
#include <atomic>
#include <thread>
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
Bytes methodDex(bool invalidHandlers = false) {
    auto bytes = emptyDex();
    bytes.resize(192);
    fixture::patch32(bytes, 56, 4);
    fixture::patch32(bytes, 60, 112);
    fixture::patch32(bytes, 64, 3);
    fixture::patch32(bytes, 68, 128);
    fixture::patch32(bytes, 72, 1);
    fixture::patch32(bytes, 76, 140);
    fixture::patch32(bytes, 88, 1);
    fixture::patch32(bytes, 92, 152);
    fixture::patch32(bytes, 96, 1);
    fixture::patch32(bytes, 100, 160);
    fixture::patch32(bytes, 132, 1);
    fixture::patch32(bytes, 136, 2);
    fixture::patch32(bytes, 140, 2);
    fixture::patch32(bytes, 144, 2);
    fixture::patch32(bytes, 156, 3);
    fixture::patch32(bytes, 164, 1);
    fixture::patch32(bytes, 168, 1);
    fixture::patch32(bytes, 176, 0xffffffff);
    unsigned index = 0;
    for (const auto text : {std::string("LExample;"), std::string("Ljava/lang/Object;"), std::string("I"),
                            std::string("answer")}) {
        fixture::patch32(bytes, 112 + index++ * 4, static_cast<std::uint32_t>(bytes.size()));
        bytes.push_back(static_cast<std::uint8_t>(text.size()));
        bytes.insert(bytes.end(), text.begin(), text.end());
        bytes.push_back(0);
    }
    fixture::patch32(bytes, 184, static_cast<std::uint32_t>(bytes.size()));
    const auto offset = (bytes.size() + 8 + 3) & ~std::size_t(3);
    bytes.insert(bytes.end(), {0, 0, 1, 0, 0, 9, static_cast<std::uint8_t>((offset & 127) | 128),
                               static_cast<std::uint8_t>(offset >> 7)});
    bytes.resize(offset);
    fixture::u16(bytes, 1);
    fixture::u16(bytes, 0);
    fixture::u16(bytes, 0);
    fixture::u16(bytes, invalidHandlers ? 1 : 0);
    fixture::u32(bytes, 0);
    fixture::u32(bytes, 3);
    fixture::u16(bytes, 0x0013);
    fixture::u16(bytes, 42);
    fixture::u16(bytes, 0x000f);
    fixture::patch32(bytes, 32, static_cast<std::uint32_t>(bytes.size()));
    fixture::patch32(bytes, 104, static_cast<std::uint32_t>(bytes.size() - 192));
    fixture::patch32(bytes, 108, 192);
    fixture::patch32(
        bytes, 8,
        static_cast<std::uint32_t>(adler32(1, bytes.data() + 12, static_cast<uInt>(bytes.size() - 12))));
    return bytes;
}
} // namespace

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
        test::rejects(
            [&] { (void)DexFile(Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size))); });
    }
}

TEST(method_register_word_count) {
    MethodRef ref{
        "LExample;", "run", "(IJD[Ljava/lang/String;)V", {"I", "J", "D", "[Ljava/lang/String;"}, "V"};
    CHECK(ref.argumentWords(false) == 6);
    CHECK(ref.argumentWords(true) == 7);
    CHECK(ref.key() == "LExample;->run(IJD[Ljava/lang/String;)V");
}

TEST(dex_metadata_validates_tables_without_parsing_method_bodies) {
    auto bytes = methodDex();
    const auto info = inspectDex(bytes);
    CHECK(info.classes == 1 && info.methods == 1 && info.version == "035");
    fixture::patch32(bytes, 100, static_cast<std::uint32_t>(bytes.size()));
    fixture::patch32(
        bytes, 8,
        static_cast<std::uint32_t>(adler32(1, bytes.data() + 12, static_cast<uInt>(bytes.size() - 12))));
    test::rejects([&] { (void)inspectDex(bytes); });
    bytes = methodDex(true);
    const DexFile dex(bytes);
    test::rejects([&] { (void)dex.code(dex.classes()[0].directMethods[0]); });
}

TEST(dex_method_code_is_immutable_and_shared_across_readers) {
    const DexFile dex(methodDex());
    const auto& method = dex.classes()[0].directMethods[0];
    const auto* code = dex.code(method);
    CHECK(code && code->registers == 1 && code->words == std::vector<std::uint16_t>({0x13, 42, 0xf}));
    std::atomic_bool consistent{true};
    std::vector<std::thread> readers;
    for (unsigned n = 0; n < 4; ++n)
        readers.emplace_back([&] {
            for (unsigned call = 0; call < 100; ++call)
                if (dex.code(method) != code)
                    consistent.store(false);
        });
    for (auto& reader : readers)
        reader.join();
    CHECK(consistent.load());
    CHECK(dex.code(DexMethod{0, 0x100, 0}) == nullptr);
}
