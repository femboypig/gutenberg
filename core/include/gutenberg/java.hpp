#pragma once
#include "runtime.hpp"

namespace gutenberg {
void installJavaRuntime(Runtime& runtime);
[[nodiscard]] std::string javaString(Runtime& runtime, Value value);
[[nodiscard]] std::uint64_t argumentWide(Arguments arguments, std::size_t index);
} // namespace gutenberg
