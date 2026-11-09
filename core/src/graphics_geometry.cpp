#include "graphics_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace gutenberg::graphics {
namespace {
double number(Value value, bool floating) {
    return floating ? static_cast<double>(value.f32()) : static_cast<double>(value.i32());
}
Value coordinate(double value, bool floating) {
    return floating ? Value::floating(static_cast<float>(value))
                    : Value::integer(std::bit_cast<std::int32_t>(
                          static_cast<std::uint32_t>(static_cast<std::int64_t>(value))));
}
std::uint32_t floatBits(float value) {
    return std::isnan(value) ? 0x7fc00000U : std::bit_cast<std::uint32_t>(value);
}
std::int32_t truncate(double value) {
    if (std::isnan(value))
        return 0;
    if (value <= std::numeric_limits<std::int32_t>::min())
        return std::numeric_limits<std::int32_t>::min();
    if (value >= std::numeric_limits<std::int32_t>::max())
        return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(value);
}
std::array<double, 4> rectangle(Runtime& vm, Value value, const std::string& type, bool floating) {
    std::array<double, 4> result;
    unsigned n = 0;
    for (const auto* name : {"left", "top", "right", "bottom"})
        result[n++] = number(vm.getField(value, {type, name, floating ? "F" : "I"}), floating);
    return result;
}
void setRectangle(Runtime& vm, Value value, const std::string& type, bool floating,
                  std::array<double, 4> bounds) {
    unsigned n = 0;
    for (const auto* name : {"left", "top", "right", "bottom"})
        vm.putField(value, {type, name, floating ? "F" : "I"}, coordinate(bounds[n++], floating));
}
bool overlaps(std::array<double, 4> a, std::array<double, 4> b) {
    return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
}
void points(Runtime& vm) {
    for (const bool floating : {false, true}) {
        const std::string owner = floating ? "Landroid/graphics/PointF;" : "Landroid/graphics/Point;";
        const std::string scalar = floating ? "F" : "I";
        vm.defineClass(owner);
        vm.defineField(owner, "x", scalar);
        vm.defineField(owner, "y", scalar);
        vm.bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
        for (const auto* method : {"<init>", "set"})
            vm.bind(owner, method, "(" + scalar + scalar + ")V", [owner, scalar](Runtime& r, Arguments a) {
                r.putField(a[0], {owner, "x", scalar}, a[1]);
                r.putField(a[0], {owner, "y", scalar}, a[2]);
                return Value{};
            });
        const auto copy = [owner, scalar](Runtime& r, Arguments a) {
            r.putField(a[0], {owner, "x", scalar}, r.getField(a[1], {owner, "x", scalar}));
            r.putField(a[0], {owner, "y", scalar}, r.getField(a[1], {owner, "y", scalar}));
            return Value{};
        };
        vm.bind(owner, "<init>", "(" + owner + ")V", copy);
        if (floating)
            vm.bind(owner, "set", "(" + owner + ")V", copy);
        vm.bind(owner, "offset", "(" + scalar + scalar + ")V",
                [owner, scalar, floating](Runtime& r, Arguments a) {
                    r.putField(a[0], {owner, "x", scalar},
                               coordinate(number(r.getField(a[0], {owner, "x", scalar}), floating) +
                                              number(a[1], floating),
                                          floating));
                    r.putField(a[0], {owner, "y", scalar},
                               coordinate(number(r.getField(a[0], {owner, "y", scalar}), floating) +
                                              number(a[2], floating),
                                          floating));
                    return Value{};
                });
        vm.bind(owner, "negate", "()V", [owner, scalar, floating](Runtime& r, Arguments a) {
            for (const auto* name : {"x", "y"})
                r.putField(a[0], {owner, name, scalar},
                           coordinate(-number(r.getField(a[0], {owner, name, scalar}), floating), floating));
            return Value{};
        });
        vm.bind(owner, "equals", "(" + scalar + scalar + ")Z",
                [owner, scalar, floating](Runtime& r, Arguments a) {
                    return Value::integer(
                        number(r.getField(a[0], {owner, "x", scalar}), floating) == number(a[1], floating) &&
                        number(r.getField(a[0], {owner, "y", scalar}), floating) == number(a[2], floating));
                });
        vm.bind(owner, "equals", "(Ljava/lang/Object;)Z", [owner, scalar, floating](Runtime& r, Arguments a) {
            if (a[0] == a[1])
                return Value::integer(1);
            if (a[1].isNull() || r.object(a[0]).type != r.object(a[1]).type)
                return Value::integer(0);
            for (const auto* name : {"x", "y"}) {
                const auto left = r.getField(a[0], {owner, name, scalar}),
                           right = r.getField(a[1], {owner, name, scalar});
                if (floating ? floatBits(left.f32()) != floatBits(right.f32()) : left.i32() != right.i32())
                    return Value::integer(0);
            }
            return Value::integer(1);
        });
        vm.bind(owner, "hashCode", "()I", [owner, scalar, floating](Runtime& r, Arguments a) {
            const auto x = r.getField(a[0], {owner, "x", scalar}), y = r.getField(a[0], {owner, "y", scalar});
            return Value{31U * (floating ? floatBits(x.f32()) : static_cast<std::uint32_t>(x.bits)) +
                             (floating ? floatBits(y.f32()) : static_cast<std::uint32_t>(y.bits)),
                         false};
        });
        vm.bind(owner, "toString", "()Ljava/lang/String;",
                [owner, scalar, floating](Runtime& r, Arguments a) {
                    const auto x = r.getField(a[0], {owner, "x", scalar}),
                               y = r.getField(a[0], {owner, "y", scalar});
                    return r.string(std::string(floating ? "PointF(" : "Point(") +
                                    (floating ? std::to_string(x.f32()) : std::to_string(x.i32())) + ", " +
                                    (floating ? std::to_string(y.f32()) : std::to_string(y.i32())) + ")");
                });
    }
    vm.bind("Landroid/graphics/PointF;", "<init>", "(Landroid/graphics/Point;)V",
            [](Runtime& r, Arguments a) {
                for (const auto* name : {"x", "y"})
                    r.putField(a[0], {"Landroid/graphics/PointF;", name, "F"},
                               Value::floating(static_cast<float>(
                                   r.getField(a[1], {"Landroid/graphics/Point;", name, "I"}).i32())));
                return Value{};
            });
    vm.bind("Landroid/graphics/PointF;", "length", "()F", [](Runtime& r, Arguments a) {
        return Value::floating(std::hypot(r.getField(a[0], {"Landroid/graphics/PointF;", "x", "F"}).f32(),
                                          r.getField(a[0], {"Landroid/graphics/PointF;", "y", "F"}).f32()));
    });
    vm.bind("Landroid/graphics/PointF;", "length", "(FF)F",
            [](Runtime&, Arguments a) { return Value::floating(std::hypot(a[0].f32(), a[1].f32())); });
}
} // namespace
void installGeometry(Runtime& vm) {
    points(vm);
    for (const bool floating : {false, true}) {
        const std::string owner = floating ? "Landroid/graphics/RectF;" : "Landroid/graphics/Rect;";
        const std::string scalar = floating ? "F" : "I";
        vm.bind(owner, "set", "(" + owner + ")V", [owner, floating](Runtime& r, Arguments a) {
            setRectangle(r, a[0], owner, floating, rectangle(r, a[1], owner, floating));
            return Value{};
        });
        vm.bind(owner, "setEmpty", "()V", [owner, floating](Runtime& r, Arguments a) {
            setRectangle(r, a[0], owner, floating, {});
            return Value{};
        });
        vm.bind(owner, "sort", "()V", [owner, floating](Runtime& r, Arguments a) {
            auto bounds = rectangle(r, a[0], owner, floating);
            if (bounds[0] > bounds[2])
                std::swap(bounds[0], bounds[2]);
            if (bounds[1] > bounds[3])
                std::swap(bounds[1], bounds[3]);
            setRectangle(r, a[0], owner, floating, bounds);
            return Value{};
        });
        for (const std::string name : {"offset", "offsetTo", "inset"})
            vm.bind(owner, name, "(" + scalar + scalar + ")V",
                    [owner, floating, name](Runtime& r, Arguments a) {
                        auto bounds = rectangle(r, a[0], owner, floating);
                        auto x = number(a[1], floating), y = number(a[2], floating);
                        if (name == "offsetTo") {
                            x -= bounds[0];
                            y -= bounds[1];
                        }
                        bounds[0] += x;
                        bounds[1] += y;
                        bounds[2] += name == "inset" ? -x : x;
                        bounds[3] += name == "inset" ? -y : y;
                        setRectangle(r, a[0], owner, floating, bounds);
                        return Value{};
                    });
        vm.bind(owner, "contains", "(" + scalar + scalar + ")Z", [owner, floating](Runtime& r, Arguments a) {
            const auto b = rectangle(r, a[0], owner, floating);
            const auto x = number(a[1], floating), y = number(a[2], floating);
            return Value::integer(b[0] < b[2] && b[1] < b[3] && b[0] <= x && x < b[2] && b[1] <= y &&
                                  y < b[3]);
        });
        for (const bool object : {false, true})
            for (const std::string name : {"contains", "intersect", "union"})
                vm.bind(owner, name,
                        "(" + (object ? owner : scalar + scalar + scalar + scalar) + ")" +
                            (name == "union" ? "V" : "Z"),
                        [owner, floating, object, name](Runtime& r, Arguments a) {
                            auto b = rectangle(r, a[0], owner, floating);
                            std::array<double, 4> other;
                            if (object)
                                other = rectangle(r, a[1], owner, floating);
                            else
                                for (unsigned n = 0; n < 4; ++n)
                                    other[n] = number(a[n + 1], floating);
                            if (name == "contains")
                                return Value::integer(b[0] < b[2] && b[1] < b[3] && b[0] <= other[0] &&
                                                      b[1] <= other[1] && b[2] >= other[2] &&
                                                      b[3] >= other[3]);
                            if (name == "intersect") {
                                if (!overlaps(b, other))
                                    return Value::integer(0);
                                b = {std::max(b[0], other[0]), std::max(b[1], other[1]),
                                     std::min(b[2], other[2]), std::min(b[3], other[3])};
                            } else {
                                if (!(other[0] < other[2] && other[1] < other[3]))
                                    return Value{};
                                b = b[0] < b[2] && b[1] < b[3]
                                        ? std::array<double, 4>{std::min(b[0], other[0]),
                                                                std::min(b[1], other[1]),
                                                                std::max(b[2], other[2]),
                                                                std::max(b[3], other[3])}
                                        : other;
                            }
                            setRectangle(r, a[0], owner, floating, b);
                            return name == "union" ? Value{} : Value::integer(1);
                        });
        vm.bind(owner, "union", "(" + scalar + scalar + ")V", [owner, floating](Runtime& r, Arguments a) {
            auto b = rectangle(r, a[0], owner, floating);
            const auto x = number(a[1], floating), y = number(a[2], floating);
            setRectangle(r, a[0], owner, floating,
                         {std::min(b[0], x), std::min(b[1], y), std::max(b[2], x), std::max(b[3], y)});
            return Value{};
        });
        vm.bind(owner, "setIntersect", "(" + owner + owner + ")Z",
                [owner, floating](Runtime& r, Arguments a) {
                    const auto b = rectangle(r, a[1], owner, floating),
                               other = rectangle(r, a[2], owner, floating);
                    if (!overlaps(b, other))
                        return Value::integer(0);
                    setRectangle(r, a[0], owner, floating,
                                 {std::max(b[0], other[0]), std::max(b[1], other[1]),
                                  std::min(b[2], other[2]), std::min(b[3], other[3])});
                    return Value::integer(1);
                });
        vm.bind(owner, "intersects", "(" + owner + owner + ")Z", [owner, floating](Runtime& r, Arguments a) {
            return Value::integer(
                overlaps(rectangle(r, a[0], owner, floating), rectangle(r, a[1], owner, floating)));
        });
        vm.bind(owner, "intersects", "(" + scalar + scalar + scalar + scalar + ")Z",
                [owner, floating](Runtime& r, Arguments a) {
                    return Value::integer(overlaps(rectangle(r, a[0], owner, floating),
                                                   {number(a[1], floating), number(a[2], floating),
                                                    number(a[3], floating), number(a[4], floating)}));
                });
        vm.bind(owner, "equals", "(Ljava/lang/Object;)Z", [owner, floating](Runtime& r, Arguments a) {
            return Value::integer(
                a[0] == a[1] || (!a[1].isNull() && r.object(a[0]).type == r.object(a[1]).type &&
                                 rectangle(r, a[0], owner, floating) == rectangle(r, a[1], owner, floating)));
        });
        vm.bind(owner, "hashCode", "()I", [owner, floating](Runtime& r, Arguments a) {
            std::uint32_t hash = 0;
            for (const auto value : rectangle(r, a[0], owner, floating))
                hash = hash * 31 + (floating ? value == 0 ? 0U : floatBits(static_cast<float>(value))
                                             : static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
            return Value{hash, false};
        });
        vm.bind(owner, "toString", "()Ljava/lang/String;", [owner, floating](Runtime& r, Arguments a) {
            std::string text = floating ? "RectF(" : "Rect(";
            const auto bounds = rectangle(r, a[0], owner, floating);
            for (unsigned n = 0; n < 4; ++n)
                text += (n ? ", " : "") + (floating ? std::to_string(bounds[n])
                                                    : std::to_string(static_cast<std::int32_t>(bounds[n])));
            return r.string(text + ")");
        });
    }
    for (const auto* name : {"<init>", "set"})
        vm.bind("Landroid/graphics/RectF;", name, "(Landroid/graphics/Rect;)V", [](Runtime& r, Arguments a) {
            setRectangle(r, a[0], "Landroid/graphics/RectF;", true,
                         rectangle(r, a[1], "Landroid/graphics/Rect;", false));
            return Value{};
        });
    for (const bool outward : {false, true})
        vm.bind("Landroid/graphics/RectF;", outward ? "roundOut" : "round", "(Landroid/graphics/Rect;)V",
                [outward](Runtime& r, Arguments a) {
                    auto bounds = rectangle(r, a[0], "Landroid/graphics/RectF;", true);
                    for (unsigned n = 0; n < 4; ++n)
                        bounds[n] = truncate(outward ? n < 2 ? std::floor(bounds[n]) : std::ceil(bounds[n])
                                                     : std::floor(bounds[n] + 0.5));
                    setRectangle(r, a[1], "Landroid/graphics/Rect;", false, bounds);
                    return Value{};
                });
    for (const bool y : {false, true})
        vm.bind("Landroid/graphics/Rect;", y ? "exactCenterY" : "exactCenterX", "()F",
                [y](Runtime& r, Arguments a) {
                    const auto bounds = rectangle(r, a[0], "Landroid/graphics/Rect;", false);
                    const auto sum = coordinate(bounds[y ? 1 : 0] + bounds[y ? 3 : 2], false).i32();
                    return Value::floating(static_cast<float>(sum) * 0.5f);
                });
}
} // namespace gutenberg::graphics
