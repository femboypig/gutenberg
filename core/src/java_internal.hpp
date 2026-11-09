#pragma once
#include "gutenberg/runtime.hpp"

namespace gutenberg {
void installJavaCollections(Runtime& runtime);
void installJavaNumbers(Runtime& runtime);
void installJavaJson(Runtime& runtime);
void installJavaReflection(Runtime& runtime);
void installJavaThreadLocals(Runtime& runtime);
struct JavaServices;
void installJavaIo(Runtime& runtime, JavaServices services);
void installJavaNetwork(Runtime& runtime, JavaServices services);
[[nodiscard]] Value memoryInputStream(Runtime& runtime, Bytes bytes,
                                      std::shared_ptr<NativeObject> owner = {});
[[nodiscard]] Value memoryOutputStream(Runtime& runtime);
[[nodiscard]] Bytes outputStreamBytes(Runtime& runtime, Value stream);
} // namespace gutenberg
