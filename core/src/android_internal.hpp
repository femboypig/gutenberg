#pragma once
#include "gutenberg/resources.hpp"
#include "gutenberg/value.hpp"

namespace gutenberg {
struct ViewAttributes final : NativeObject {
    std::vector<XmlAttribute> values;
};
} // namespace gutenberg
