#pragma once
#include "filesystem.hpp"
#include "network.hpp"
#include "runtime.hpp"

namespace gutenberg {
struct JavaServices {
    std::shared_ptr<FileSandbox> files;
    std::function<Bytes(std::string_view)> asset;
    HttpTransport http;
    bool internetPermission = true;
    bool allowCleartext = true;
    std::size_t networkMemoryLimit = 128 * 1024 * 1024;
};
void installJavaRuntime(Runtime& runtime, JavaServices services = {});
[[nodiscard]] std::string javaString(Runtime& runtime, Value value);
[[nodiscard]] std::uint64_t argumentWide(Arguments arguments, std::size_t index);
} // namespace gutenberg
