#pragma once

#include "android.hpp"
#include <set>

namespace gutenberg {

struct Size {
    float width = 0, height = 0;
};
struct Rect {
    float x = 0, y = 0, width = 0, height = 0;
};
struct LayoutBox {
    Rect frame;
    Size content;
};
using IntrinsicMeasure = std::function<Size(const ViewNode&, Size)>;
using Layout = std::map<std::uint64_t, LayoutBox>;

[[nodiscard]] Layout layoutScene(const Scene& scene, Size viewport, IntrinsicMeasure measure);
[[nodiscard]] std::set<std::uint64_t> validateScene(const Scene& scene);

} // namespace gutenberg
