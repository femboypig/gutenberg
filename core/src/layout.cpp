#include "gutenberg/layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace gutenberg {
namespace {
constexpr float unbounded = std::numeric_limits<float>::infinity();
bool kind(const ViewNode& node, std::string_view name) {
    return node.kind == "Landroid/widget/" + std::string(name) + ";";
}
bool linear(const ViewNode& node) {
    return kind(node, "LinearLayout") || kind(node, "TableRow");
}
bool scroll(const ViewNode& node) {
    return kind(node, "ScrollView") || kind(node, "HorizontalScrollView");
}
float positive(float value) {
    return std::max(0.0f, value);
}
struct Constraint {
    Size maximum;
    bool widthKnown = false, heightKnown = false;
    std::optional<float> exactWidth, exactHeight;
};
struct Child {
    std::uint64_t id;
    Size size;
};

class Engine {
  public:
    Engine(const Scene& scene, IntrinsicMeasure measure) : scene_(scene), intrinsic_(std::move(measure)) {}
    std::set<std::uint64_t> verify() const {
        std::set<std::uint64_t> visited;
        if (scene_.root)
            validate(scene_.root, visited, 0);
        return visited;
    }
    Layout run(Size viewport) {
        if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) || viewport.width < 0 ||
            viewport.height < 0)
            throw Error(ErrorCode::verification, "invalid viewport");
        if (!scene_.root)
            return {};
        (void)verify();
        arrange(scene_.root, {0, 0, viewport.width, viewport.height}, true, true, 0);
        return std::move(result_);
    }

  private:
    const ViewNode& node(std::uint64_t id) const {
        const auto found = scene_.views.find(id);
        if (found == scene_.views.end())
            throw Error(ErrorCode::verification, "layout references a missing view");
        return found->second;
    }
    void validate(std::uint64_t id, std::set<std::uint64_t>& visited, unsigned depth) const {
        if (depth > 256 || !visited.insert(id).second)
            throw Error(ErrorCode::verification, "view hierarchy contains a cycle or repeated child");
        const auto& view = node(id);
        for (float value : {view.width, view.height, view.weight, view.textSize, view.padding.left,
                            view.padding.top, view.padding.right, view.padding.bottom})
            if (!std::isfinite(value))
                throw Error(ErrorCode::verification, "non-finite view geometry");
        for (auto child : view.children)
            validate(child, visited, depth + 1);
    }
    void tick(unsigned depth) {
        if (depth > 256 || ++operations_ > 1000000)
            throw Error(ErrorCode::limit_exceeded, "layout complexity limit exceeded");
    }
    std::vector<Child> children(const ViewNode& parent, Constraint constraint, unsigned depth) {
        std::vector<Child> output;
        for (auto id : parent.children)
            if (node(id).visible)
                output.push_back({id, measure(id, constraint, depth + 1)});
        return output;
    }
    std::vector<Child> weighted(const ViewNode& parent, Constraint constraint, float target, unsigned depth,
                                std::vector<Child> items) {
        const bool vertical = parent.orientation == 1;
        float total = 0, weights = 0;
        for (const auto& item : items) {
            total += vertical ? item.size.height : item.size.width;
            weights += positive(node(item.id).weight);
        }
        if (weights > 0 && std::isfinite(target)) {
            float remaining = target - total, remainingWeight = weights;
            for (auto& item : items) {
                const auto weight = positive(node(item.id).weight);
                if (!weight)
                    continue;
                const float original = vertical ? item.size.height : item.size.width;
                const float allocated = positive(original + remaining * weight / remainingWeight);
                remaining -= allocated - original;
                remainingWeight -= weight;
                auto exact = constraint;
                if (vertical)
                    exact.exactHeight = allocated;
                else
                    exact.exactWidth = allocated;
                item.size = measure(item.id, exact, depth + 1);
            }
        }
        return items;
    }
    std::vector<float> columns(const ViewNode& table, float width, unsigned depth) {
        std::vector<float> sizes, weights;
        float rowPadding = 0;
        for (auto id : table.children) {
            const auto& row = node(id);
            if (!row.visible || !kind(row, "TableRow"))
                continue;
            rowPadding = std::max(rowPadding, row.padding.left + row.padding.right);
            if (row.children.size() > sizes.size()) {
                sizes.resize(row.children.size());
                weights.resize(row.children.size());
            }
            for (std::size_t n = 0; n < row.children.size(); ++n) {
                const auto& cell = node(row.children[n]);
                if (!cell.visible)
                    continue;
                const auto measured =
                    measure(row.children[n],
                            {{positive(width - rowPadding), unbounded}, false, false, {}, {}}, depth + 1);
                sizes[n] = std::max(sizes[n], measured.width);
                weights[n] = std::max(weights[n], positive(cell.weight));
            }
        }
        float total = 0, totalWeight = 0;
        for (std::size_t n = 0; n < sizes.size(); ++n) {
            total += sizes[n];
            totalWeight += weights[n];
        }
        if (totalWeight > 0 && std::isfinite(width)) {
            float remaining = positive(width - rowPadding) - total;
            for (std::size_t n = 0; n < sizes.size(); ++n)
                if (weights[n] > 0) {
                    const auto old = sizes[n];
                    sizes[n] = positive(old + remaining * weights[n] / totalWeight);
                    remaining -= sizes[n] - old;
                    totalWeight -= weights[n];
                }
        }
        return sizes;
    }
    float rowHeight(const ViewNode& row, const std::vector<float>& widths, unsigned depth) {
        float height = 0;
        for (std::size_t n = 0; n < row.children.size() && n < widths.size(); ++n)
            if (node(row.children[n]).visible)
                height =
                    std::max(height, measure(row.children[n],
                                             {{widths[n], unbounded}, true, false, widths[n], {}}, depth + 1)
                                         .height);
        return height + row.padding.top + row.padding.bottom;
    }
    Size measure(std::uint64_t id, Constraint parent, unsigned depth) {
        tick(depth);
        const auto& view = node(id);
        if (!view.visible)
            return {};
        std::optional<float> width = parent.exactWidth, height = parent.exactHeight;
        if (!width && view.width >= 0)
            width = view.width;
        if (!height && view.height >= 0)
            height = view.height;
        if (!width && view.width == -1 && parent.widthKnown)
            width = parent.maximum.width;
        if (!height && view.height == -1 && parent.heightKnown)
            height = parent.maximum.height;
        const Size maximum{width.value_or(parent.maximum.width), height.value_or(parent.maximum.height)};
        const Size inner{positive(maximum.width - view.padding.left - view.padding.right),
                         positive(maximum.height - view.padding.top - view.padding.bottom)};
        Constraint child{inner, width.has_value(), height.has_value(), {}, {}};
        Size natural;
        if (kind(view, "TableLayout")) {
            const auto widths = columns(view, inner.width, depth);
            for (auto row : view.children)
                if (node(row).visible) {
                    const auto& item = node(row);
                    float rowWidth = item.padding.left + item.padding.right;
                    for (auto column : widths)
                        rowWidth += column;
                    const auto size = kind(item, "TableRow")
                                          ? Size{rowWidth, rowHeight(item, widths, depth + 1)}
                                          : measure(row, child, depth + 1);
                    natural.width = std::max(natural.width, size.width);
                    natural.height += size.height;
                }
        } else if (linear(view)) {
            auto items = children(view, child, depth);
            for (const auto& item : items) {
                if (view.orientation == 1) {
                    natural.width = std::max(natural.width, item.size.width);
                    natural.height += item.size.height;
                } else {
                    natural.width += item.size.width;
                    natural.height = std::max(natural.height, item.size.height);
                }
            }
            const auto target = view.orientation == 1
                                    ? height ? inner.height : std::min(natural.height, inner.height)
                                : width ? inner.width
                                        : std::min(natural.width, inner.width);
            items = weighted(view, child, target, depth, std::move(items));
            natural = {};
            for (const auto& item : items) {
                if (view.orientation == 1) {
                    natural.width = std::max(natural.width, item.size.width);
                    natural.height += item.size.height;
                } else {
                    natural.width += item.size.width;
                    natural.height = std::max(natural.height, item.size.height);
                }
            }
        } else if (!view.children.empty()) {
            if (scroll(view)) {
                if (kind(view, "HorizontalScrollView")) {
                    child.maximum.width = unbounded;
                    child.widthKnown = false;
                } else {
                    child.maximum.height = unbounded;
                    child.heightKnown = false;
                }
            }
            for (const auto& item : children(view, child, depth)) {
                natural.width = std::max(natural.width, item.size.width);
                natural.height = std::max(natural.height, item.size.height);
            }
        } else {
            natural = intrinsic_(view, inner);
            if (!std::isfinite(natural.width) || !std::isfinite(natural.height))
                throw Error(ErrorCode::verification, "invalid intrinsic view size");
        }
        natural.width += view.padding.left + view.padding.right;
        natural.height += view.padding.top + view.padding.bottom;
        return {width.value_or(std::min(positive(natural.width), parent.maximum.width)),
                height.value_or(std::min(positive(natural.height), parent.maximum.height))};
    }
    void tableRow(std::uint64_t id, Rect frame, const std::vector<float>& widths, unsigned depth) {
        const auto& row = node(id);
        result_[id].frame = frame;
        float x = row.padding.left;
        for (std::size_t n = 0; n < row.children.size() && n < widths.size(); ++n) {
            const auto cell = row.children[n];
            if (!node(cell).visible)
                continue;
            const auto size =
                measure(cell,
                        {{widths[n], positive(frame.height - row.padding.top - row.padding.bottom)},
                         true,
                         false,
                         widths[n],
                         {}},
                        depth + 1);
            arrange(cell, {x, row.padding.top, widths[n], size.height}, true, false, depth + 1);
            x += widths[n];
        }
    }
    void arrange(std::uint64_t id, Rect frame, bool forcedWidth, bool forcedHeight, unsigned depth) {
        tick(depth);
        const auto& view = node(id);
        result_[id].frame = frame;
        const Rect inner{view.padding.left, view.padding.top,
                         positive(frame.width - view.padding.left - view.padding.right),
                         positive(frame.height - view.padding.top - view.padding.bottom)};
        Constraint child{{inner.width, inner.height},
                         forcedWidth || view.width != -2,
                         forcedHeight || view.height != -2,
                         {},
                         {}};
        if (kind(view, "TableLayout")) {
            const auto widths = columns(view, inner.width, depth);
            float y = inner.y;
            for (auto row : view.children)
                if (node(row).visible) {
                    if (kind(node(row), "TableRow")) {
                        const auto height = rowHeight(node(row), widths, depth + 1);
                        tableRow(row, {inner.x, y, inner.width, height}, widths, depth + 1);
                        y += height;
                    } else {
                        const auto size = measure(row, child, depth + 1);
                        arrange(row, {inner.x, y, size.width, size.height}, false, false, depth + 1);
                        y += size.height;
                    }
                }
            return;
        }
        if (linear(view)) {
            const bool vertical = view.orientation == 1;
            auto items = weighted(view, child, vertical ? inner.height : inner.width, depth,
                                  children(view, child, depth));
            float cursor = vertical ? inner.y : inner.x;
            for (const auto& item : items) {
                const auto gravity =
                    node(item.id).layoutGravity < 0 ? view.gravity : node(item.id).layoutGravity;
                float x = vertical ? inner.x : cursor, y = vertical ? cursor : inner.y;
                if (vertical && (gravity & 7) == 1)
                    x += positive(inner.width - item.size.width) / 2;
                else if (vertical && (gravity & 7) == 5)
                    x += positive(inner.width - item.size.width);
                if (!vertical && (gravity & 0x70) == 0x10)
                    y += positive(inner.height - item.size.height) / 2;
                else if (!vertical && (gravity & 0x70) == 0x50)
                    y += positive(inner.height - item.size.height);
                arrange(item.id, {x, y, item.size.width, item.size.height},
                        !vertical && node(item.id).weight > 0, vertical && node(item.id).weight > 0,
                        depth + 1);
                cursor += vertical ? item.size.height : item.size.width;
            }
            return;
        }
        if (scroll(view)) {
            if (kind(view, "HorizontalScrollView")) {
                child.maximum.width = unbounded;
                child.widthKnown = false;
            } else {
                child.maximum.height = unbounded;
                child.heightKnown = false;
            }
        }
        Size content;
        for (const auto& item : children(view, child, depth)) {
            arrange(item.id, {inner.x, inner.y, item.size.width, item.size.height}, false, false, depth + 1);
            content.width = std::max(content.width, inner.x + item.size.width + view.padding.right);
            content.height = std::max(content.height, inner.y + item.size.height + view.padding.bottom);
        }
        result_[id].content = content;
    }
    const Scene& scene_;
    IntrinsicMeasure intrinsic_;
    Layout result_;
    std::size_t operations_ = 0;
};
} // namespace

Layout layoutScene(const Scene& scene, Size viewport, IntrinsicMeasure measure) {
    return Engine(scene, std::move(measure)).run(viewport);
}
std::set<std::uint64_t> validateScene(const Scene& scene) {
    return Engine(scene, {}).verify();
}

} // namespace gutenberg
