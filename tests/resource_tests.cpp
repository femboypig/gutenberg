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

namespace {
Bytes styledResources(std::size_t& firstEntry) {
    Bytes package;
    fixture::u16(package, 0x200);
    fixture::u16(package, 284);
    fixture::u32(package, 0);
    package.resize(284);
    fixture::patch32(package, 8, 0x7f);
    fixture::patch32(package, 268, static_cast<std::uint32_t>(package.size()));
    fixture::append(package, fixture::stringPool({"style"}));
    fixture::patch32(package, 276, static_cast<std::uint32_t>(package.size()));
    fixture::append(package, fixture::stringPool({"base", "child"}));
    const auto typeStart = package.size();
    fixture::u16(package, 0x201);
    fixture::u16(package, 24);
    fixture::u32(package, 88);
    package.push_back(1);
    package.push_back(0);
    fixture::u16(package, 0);
    fixture::u32(package, 2);
    fixture::u32(package, 32);
    fixture::u32(package, 4);
    fixture::u32(package, 0);
    fixture::u32(package, 28);
    for (std::uint32_t n = 0; n < 2; ++n) {
        fixture::u16(package, 16);
        fixture::u16(package, 1);
        fixture::u32(package, n);
        fixture::u32(package, n ? 0x7f010000 : 0);
        fixture::u32(package, 1);
        fixture::u32(package, 0x7f020000 + n);
        fixture::u16(package, 8);
        package.push_back(0);
        package.push_back(0x10);
        fixture::u32(package, 42 + n);
    }
    fixture::patch32(package, 4, static_cast<std::uint32_t>(package.size()));
    Bytes table;
    fixture::u16(table, 2);
    fixture::u16(table, 12);
    fixture::u32(table, 0);
    fixture::u32(table, 1);
    fixture::append(table, fixture::stringPool({}));
    firstEntry = table.size() + typeStart + 32;
    fixture::append(table, package);
    fixture::patch32(table, 4, static_cast<std::uint32_t>(table.size()));
    return table;
}
} // namespace

TEST(resource_bags_preserve_names_and_inherit_style_attributes) {
    std::size_t entry = 0;
    auto bytes = styledResources(entry);
    const Resources resources(bytes);
    CHECK(resources.name(0x7f010000) == "style/base");
    CHECK(resources.bag(0x7f010001)->parent == 0x7f010000);
    CHECK(resources.styled(0x7f010001, 0x7f020000)->data == 42);
    CHECK(resources.styled(0x7f010001, 0x7f020001)->data == 43);
    CHECK(!resources.styled(0x7f010001, 0x7f020002));
    fixture::patch32(bytes, entry + 8, 0x7f010001);
    const Resources cyclic(bytes);
    test::rejects([&] { (void)cyclic.styled(0x7f010001, 0x7f020002); });
    fixture::patch32(bytes, entry + 12, 0xffffffff);
    test::rejects([&] { (void)Resources(bytes); });
}
