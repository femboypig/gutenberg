#include "binary_builder.hpp"
#include "gutenberg/resources.hpp"
#include "test.hpp"

using namespace gutenberg;

TEST(binary_manifest_attributes) {
    const auto bytes = fixture::manifest();
    const auto xml = parseBinaryXml(bytes);
    CHECK(xml.name == "manifest");
    CHECK(xml.stringAttribute("package") == "org.example.app");
    CHECK(xml.attribute("missing") == nullptr);
}

TEST(binary_xml_rejects_every_truncation) {
    auto bytes = fixture::manifest();
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        test::rejects([&] { (void)parseBinaryXml(std::span(bytes).first(size)); });
    }
}

TEST(string_pool_indices_are_checked) {
    const auto bytes = fixture::stringPool({"one", "two"});
    const StringPool pool(ByteView(bytes), 0);
    CHECK(pool.at(1) == "two");
    test::rejects([&] { (void)pool.at(2); });
}

TEST(resource_table_rejects_truncated_package) {
    Bytes bytes;
    fixture::u16(bytes, 2);
    fixture::u16(bytes, 12);
    fixture::u32(bytes, 20);
    fixture::u32(bytes, 1);
    fixture::u16(bytes, 0x200);
    fixture::u16(bytes, 8);
    fixture::u32(bytes, 8);
    test::rejects([&] { (void)Resources(bytes); });
}
