#pragma once
#include "gutenberg/error.hpp"
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {
using Case = std::pair<std::string, std::function<void()>>;
inline std::vector<Case>& cases() { static std::vector<Case> value; return value; }
struct Register {
    Register(std::string name, std::function<void()> body) { cases().emplace_back(std::move(name), std::move(body)); }
};
inline void check(bool condition, const char* expression, int line) {
    if (!condition) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
template<class F> void rejects(F body) {
    try { body(); } catch (const gutenberg::Error&) { return; }
    throw std::runtime_error("expected gutenberg::Error");
}
}
#define TEST(name) static void name(); static test::Register register_##name(#name, name); static void name()
#define CHECK(...) test::check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
