#pragma once
#include "gutenberg/bytes.hpp"
#include <initializer_list>

namespace fixture {
using gutenberg::Bytes;
inline void u16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
inline void u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
inline void patch32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
}
inline void append(Bytes& bytes, const Bytes& other) { bytes.insert(bytes.end(), other.begin(), other.end()); }
inline Bytes stringPool(std::initializer_list<std::string_view> strings) {
    Bytes pool;
    u16(pool, 1); u16(pool, 28); u32(pool, 0); u32(pool, static_cast<std::uint32_t>(strings.size()));
    u32(pool, 0); u32(pool, 0x100); u32(pool, 28 + static_cast<std::uint32_t>(strings.size()) * 4); u32(pool, 0);
    Bytes values;
    for (auto string : strings) {
        u32(pool, static_cast<std::uint32_t>(values.size()));
        values.push_back(static_cast<std::uint8_t>(string.size()));
        values.push_back(static_cast<std::uint8_t>(string.size()));
        values.insert(values.end(), string.begin(), string.end()); values.push_back(0);
    }
    append(pool, values);
    while (pool.size() % 4) pool.push_back(0);
    patch32(pool, 4, static_cast<std::uint32_t>(pool.size()));
    return pool;
}
inline Bytes manifest() {
    Bytes xml;
    u16(xml, 3); u16(xml, 8); u32(xml, 0);
    append(xml, stringPool({"manifest", "package", "org.example.app"}));
    u16(xml, 0x102); u16(xml, 16); u32(xml, 56); u32(xml, 1); u32(xml, 0xffffffff);
    u32(xml, 0xffffffff); u32(xml, 0); u16(xml, 20); u16(xml, 20); u16(xml, 1);
    u16(xml, 0); u16(xml, 0); u16(xml, 0);
    u32(xml, 0xffffffff); u32(xml, 1); u32(xml, 2); u16(xml, 8); xml.push_back(0); xml.push_back(3); u32(xml, 2);
    u16(xml, 0x103); u16(xml, 16); u32(xml, 24); u32(xml, 1); u32(xml, 0xffffffff); u32(xml, 0xffffffff); u32(xml, 0);
    patch32(xml, 4, static_cast<std::uint32_t>(xml.size()));
    return xml;
}
}
