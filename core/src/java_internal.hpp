#pragma once
#include "gutenberg/runtime.hpp"

namespace gutenberg {
void installJavaCollections(Runtime& runtime);
void installJavaNumbers(Runtime& runtime);
struct JavaServices;
void installJavaIo(Runtime& runtime, JavaServices services);
} // namespace gutenberg
