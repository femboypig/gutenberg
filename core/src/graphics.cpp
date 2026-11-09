#include "gutenberg/graphics.hpp"
#include "graphics_internal.hpp"
#include "gutenberg/java.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace gutenberg::graphics {
float finite(Runtime& vm, float value) {
    if (!std::isfinite(value) || std::abs(value) > 1e9f)
        vm.raise("Ljava/lang/IllegalArgumentException;", "invalid graphics coordinate");
    return value;
}
void record(Runtime& vm, Value canvas, DrawCommand command) {
    auto& data = state<CanvasState>(vm, canvas);
    if (!data.display)
        vm.raise("Ljava/lang/IllegalStateException;", "canvas is outside its drawing callback");
    if (!data.target.isNull()) {
        auto& target = state<BitmapState>(vm, data.target);
        if (target.recycled)
            vm.raise("Ljava/lang/IllegalStateException;", "bitmap has been recycled");
        target.snapshot.reset();
        if (data.display.use_count() > 2)
            data.display = std::make_shared<DisplayList>(*data.display);
        target.drawing = data.display;
    }
    for (const auto coordinate : command.coordinates)
        (void)finite(vm, coordinate);
    const auto bytes = sizeof(DrawCommand) + command.text.size() * sizeof(char16_t) +
                       command.path.size() * sizeof(PathElement);
    if (bytes > byteLimit - data.display->bytes || data.display->commands.size() >= elementLimit)
        throw Error(ErrorCode::limit_exceeded, "canvas display list limit exceeded");
    const auto cost = 1 + (command.image && command.image->drawing ? command.image->drawing->replayCost : 0);
    const auto depth = command.image && command.image->drawing ? command.image->drawing->depth + 1 : 0;
    if (cost > 2000000 - data.display->replayCost || depth > 64)
        throw Error(ErrorCode::limit_exceeded, "nested bitmap drawing limit exceeded");
    if (command.operation == DrawOperation::save)
        data.savedTransforms.reserve(data.savedTransforms.size() + 1);
    data.display->commands.push_back(std::move(command));
    data.display->bytes += bytes;
    data.display->replayCost += cost;
    data.display->depth = std::max(data.display->depth, depth);
    const auto& recorded = data.display->commands.back();
    if (recorded.operation == DrawOperation::save)
        data.savedTransforms.push_back(data.transform);
    else if (recorded.operation == DrawOperation::restore) {
        data.transform = data.savedTransforms.back();
        data.savedTransforms.pop_back();
    } else
        (void)applyTransform(data.transform, recorded);
}
} // namespace gutenberg::graphics

namespace gutenberg {
using namespace graphics;
namespace {
void pathElement(Runtime& vm, PathState& path, PathVerb verb, std::array<float, 6> coordinates = {}) {
    for (const auto coordinate : coordinates)
        (void)finite(vm, coordinate);
    if (path.elements.size() >= elementLimit)
        throw Error(ErrorCode::limit_exceeded, "path element limit exceeded");
    path.elements.push_back({verb, coordinates});
    if (verb == PathVerb::close) {
        path.x = path.startX;
        path.y = path.startY;
    } else {
        const auto at = verb == PathVerb::cubic ? 4U : verb == PathVerb::quad ? 2U : 0U;
        path.x = coordinates[at];
        path.y = coordinates[at + 1];
        if (verb == PathVerb::move) {
            path.startX = path.x;
            path.startY = path.y;
        }
    }
}
Value enumeration(Runtime& vm, const std::string& type, const std::string& name, unsigned ordinal) {
    const auto value = vm.allocate(type);
    vm.object(value).fields["ordinal"] = Value{ordinal, false};
    vm.object(value).text = utf16(name);
    vm.putStatic({type, name, type}, value);
    return value;
}
unsigned ordinal(Runtime& vm, Value value) {
    return static_cast<unsigned>(vm.object(value).fields["ordinal"].bits);
}
Paint paint(Runtime& vm, Value value) {
    return value.isNull() ? Paint{} : state<PaintState>(vm, value).value;
}
std::array<float, 4> rectangle(Runtime& vm, Value ref) {
    const auto type = vm.object(ref).type;
    const bool floating = type == "Landroid/graphics/RectF;";
    std::array<float, 4> result{};
    unsigned n = 0;
    for (const auto* name : {"left", "top", "right", "bottom"}) {
        const auto value = vm.getField(ref, {type, name, floating ? "F" : "I"});
        result[n++] = floating ? value.f32() : static_cast<float>(value.i32());
    }
    return result;
}
void installRectangles(Runtime& vm) {
    for (const bool floating : {false, true}) {
        const std::string owner = floating ? "Landroid/graphics/RectF;" : "Landroid/graphics/Rect;";
        const std::string type = floating ? "F" : "I";
        for (const auto* field : {"left", "top", "right", "bottom"})
            vm.defineField(owner, field, type);
        vm.bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
        const auto set = [owner, type](Runtime& r, Arguments a) {
            unsigned n = 1;
            for (const auto* name : {"left", "top", "right", "bottom"})
                r.putField(a[0], {owner, name, type}, a[n++]);
            return Value{};
        };
        vm.bind(owner, "<init>", "(" + type + type + type + type + ")V", set);
        vm.bind(owner, "set", "(" + type + type + type + type + ")V", set);
        vm.bind(owner, "<init>", "(" + owner + ")V", [owner, type](Runtime& r, Arguments a) {
            for (const auto* name : {"left", "top", "right", "bottom"})
                r.putField(a[0], {owner, name, type}, r.getField(a[1], {owner, name, type}));
            return Value{};
        });
        for (const std::string method : {"width", "height", "centerX", "centerY", "isEmpty"})
            vm.bind(owner, method, "()" + (method == "isEmpty" ? "Z" : type),
                    [method, floating, owner](Runtime& r, Arguments a) {
                        if (!floating) {
                            const auto left = r.getField(a[0], {owner, "left", "I"}).i32();
                            const auto top = r.getField(a[0], {owner, "top", "I"}).i32();
                            const auto right = r.getField(a[0], {owner, "right", "I"}).i32();
                            const auto bottom = r.getField(a[0], {owner, "bottom", "I"}).i32();
                            if (method == "isEmpty")
                                return Value::integer(left >= right || top >= bottom);
                            const auto x = static_cast<std::uint32_t>(
                                method == "height" || method == "centerY" ? top : left);
                            const auto y = static_cast<std::uint32_t>(
                                method == "height" || method == "centerY" ? bottom : right);
                            const bool center = method == "centerX" || method == "centerY";
                            const auto result = std::bit_cast<std::int32_t>(center ? x + y : y - x);
                            return Value::integer(center ? result >> 1 : result);
                        }
                        const auto rect = rectangle(r, a[0]);
                        if (method == "isEmpty")
                            return Value::integer(rect[0] >= rect[2] || rect[1] >= rect[3]);
                        const float value = method == "width"     ? rect[2] - rect[0]
                                            : method == "height"  ? rect[3] - rect[1]
                                            : method == "centerX" ? (rect[2] + rect[0]) / 2
                                                                  : (rect[3] + rect[1]) / 2;
                        return Value::floating(value);
                    });
    }
}
void installPaint(Runtime& vm) {
    vm.bind(paintType, "<init>", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<PaintState>();
        return Value{};
    });
    vm.bind(paintType, "<init>", "(I)V", [](Runtime& r, Arguments a) {
        auto data = std::make_shared<PaintState>();
        data->value.antiAlias = (a[1].bits & 1) != 0;
        r.object(a[0]).native = std::move(data);
        return Value{};
    });
    vm.bind(paintType, "<init>", "(Landroid/graphics/Paint;)V", [](Runtime& r, Arguments a) {
        auto data = std::make_shared<PaintState>();
        data->value = paint(r, a[1]);
        r.object(a[0]).native = std::move(data);
        return Value{};
    });
    vm.bind(paintType, "reset", "()V", [](Runtime& r, Arguments a) {
        state<PaintState>(r, a[0]).value = {};
        return Value{};
    });
    vm.bind(paintType, "set", "(Landroid/graphics/Paint;)V", [](Runtime& r, Arguments a) {
        state<PaintState>(r, a[0]).value = paint(r, a[1]);
        return Value{};
    });
    vm.bind(paintType, "setColor", "(I)V", [](Runtime& r, Arguments a) {
        state<PaintState>(r, a[0]).value.color = static_cast<std::uint32_t>(a[1].bits);
        return Value{};
    });
    vm.bind(paintType, "getColor", "()I",
            [](Runtime& r, Arguments a) { return Value{paint(r, a[0]).color, false}; });
    vm.bind(paintType, "setAlpha", "(I)V", [](Runtime& r, Arguments a) {
        auto& value = state<PaintState>(r, a[0]).value;
        value.color = (value.color & 0xffffff) | ((static_cast<std::uint32_t>(a[1].bits) & 255) << 24);
        return Value{};
    });
    vm.bind(paintType, "getAlpha", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(paint(r, a[0]).color >> 24));
    });
    vm.bind(paintType, "setARGB", "(IIII)V", [](Runtime& r, Arguments a) {
        std::uint32_t color = 0;
        for (std::size_t n = 1; n < 5; ++n)
            color = (color << 8) | (static_cast<std::uint32_t>(a[n].bits) & 255);
        state<PaintState>(r, a[0]).value.color = color;
        return Value{};
    });
    for (const std::string name : {"StrokeWidth", "TextSize", "StrokeMiter"}) {
        vm.bind(paintType, "set" + name, "(F)V", [name](Runtime& r, Arguments a) {
            const auto value = finite(r, a[1].f32());
            if (value < 0)
                r.raise("Ljava/lang/IllegalArgumentException;", "negative paint dimension");
            auto& p = state<PaintState>(r, a[0]).value;
            (name == "StrokeWidth" ? p.strokeWidth : name == "TextSize" ? p.textSize : p.miter) = value;
            return Value{};
        });
        vm.bind(paintType, "get" + name, "()F", [name](Runtime& r, Arguments a) {
            const auto p = paint(r, a[0]);
            return Value::floating(name == "StrokeWidth" ? p.strokeWidth
                                   : name == "TextSize"  ? p.textSize
                                                         : p.miter);
        });
    }
    vm.bind(paintType, "setAntiAlias", "(Z)V", [](Runtime& r, Arguments a) {
        state<PaintState>(r, a[0]).value.antiAlias = a[1].bits != 0;
        return Value{};
    });
    vm.bind(paintType, "isAntiAlias", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(paint(r, a[0]).antiAlias); });
    for (const std::string kind : {"Style", "Cap", "Join", "Align"}) {
        const auto type = "Landroid/graphics/Paint$" + kind + ";";
        vm.defineClass(type);
        const std::vector<std::string> names =
            kind == "Style"  ? std::vector<std::string>{"FILL", "STROKE", "FILL_AND_STROKE"}
            : kind == "Cap"  ? std::vector<std::string>{"BUTT", "ROUND", "SQUARE"}
            : kind == "Join" ? std::vector<std::string>{"MITER", "ROUND", "BEVEL"}
                             : std::vector<std::string>{"LEFT", "CENTER", "RIGHT"};
        std::vector<Value> values;
        for (unsigned n = 0; n < names.size(); ++n)
            values.push_back(enumeration(vm, type, names[n], n));
        vm.bind(type, "ordinal", "()I", [](Runtime& r, Arguments a) {
            return Value::integer(static_cast<std::int32_t>(ordinal(r, a[0])));
        });
        vm.bind(type, "toString", "()Ljava/lang/String;",
                [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
        const auto method = kind == "Style" ? "Style" : kind == "Align" ? "TextAlign" : "Stroke" + kind;
        vm.bind(paintType, "set" + method, "(" + type + ")V", [kind](Runtime& r, Arguments a) {
            auto& p = state<PaintState>(r, a[0]).value;
            const auto n = ordinal(r, a[1]);
            if (kind == "Style")
                p.style = static_cast<PaintStyle>(n);
            else
                (kind == "Cap" ? p.cap : kind == "Join" ? p.join : p.textAlign) = n;
            return Value{};
        });
        vm.bind(paintType, "get" + method, "()" + type, [kind, values](Runtime& r, Arguments a) {
            const auto p = paint(r, a[0]);
            const auto n = kind == "Style"  ? static_cast<unsigned>(p.style)
                           : kind == "Cap"  ? p.cap
                           : kind == "Join" ? p.join
                                            : p.textAlign;
            return values.at(n);
        });
    }
    vm.bind(paintType, "measureText", "(Ljava/lang/String;)F", [](Runtime& r, Arguments a) {
        return Value::floating(static_cast<float>(r.object(a[1]).text.size()) * paint(r, a[0]).textSize *
                               0.5f);
    });
    vm.bind(paintType, "ascent", "()F",
            [](Runtime& r, Arguments a) { return Value::floating(-paint(r, a[0]).textSize * 0.8f); });
    vm.bind(paintType, "descent", "()F",
            [](Runtime& r, Arguments a) { return Value::floating(paint(r, a[0]).textSize * 0.2f); });
}
void installPath(Runtime& vm) {
    vm.bind(pathType, "<init>", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<PathState>();
        return Value{};
    });
    vm.bind(pathType, "<init>", "(Landroid/graphics/Path;)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<PathState>(state<PathState>(r, a[1]));
        return Value{};
    });
    vm.bind(pathType, "set", "(Landroid/graphics/Path;)V", [](Runtime& r, Arguments a) {
        state<PathState>(r, a[0]) = state<PathState>(r, a[1]);
        return Value{};
    });
    for (const auto* name : {"reset", "rewind"})
        vm.bind(pathType, name, "()V", [](Runtime& r, Arguments a) {
            auto& path = state<PathState>(r, a[0]);
            const auto evenOdd = path.evenOdd;
            path = {};
            path.evenOdd = evenOdd;
            return Value{};
        });
    vm.bind(pathType, "isEmpty", "()Z", [](Runtime& r, Arguments a) {
        return Value::integer(state<PathState>(r, a[0]).elements.empty());
    });
    vm.bind(pathType, "close", "()V", [](Runtime& r, Arguments a) {
        pathElement(r, state<PathState>(r, a[0]), PathVerb::close);
        return Value{};
    });
    for (const bool relative : {false, true})
        for (const auto verb : {PathVerb::move, PathVerb::line, PathVerb::quad, PathVerb::cubic}) {
            const std::string name = verb == PathVerb::move   ? "MoveTo"
                                     : verb == PathVerb::line ? "LineTo"
                                     : verb == PathVerb::quad ? "QuadTo"
                                                              : "CubicTo";
            const unsigned count = verb == PathVerb::cubic ? 6 : verb == PathVerb::quad ? 4 : 2;
            vm.bind(pathType,
                    (relative ? "r" : "") +
                        std::string(1, relative ? name[0] : static_cast<char>(name[0] + 'a' - 'A')) +
                        name.substr(1),
                    "(" + std::string(count, 'F') + ")V", [verb, relative, count](Runtime& r, Arguments a) {
                        auto& p = state<PathState>(r, a[0]);
                        if (p.elements.empty() && verb != PathVerb::move)
                            pathElement(r, p, PathVerb::move);
                        std::array<float, 6> values{};
                        for (unsigned n = 0; n < count; ++n)
                            values[n] = a[n + 1].f32() + (relative ? (n % 2 ? p.y : p.x) : 0);
                        pathElement(r, p, verb, values);
                        return Value{};
                    });
        }
    vm.bind(pathType, "offset", "(FF)V", [](Runtime& r, Arguments a) {
        auto& p = state<PathState>(r, a[0]);
        const auto dx = finite(r, a[1].f32()), dy = finite(r, a[2].f32());
        auto copy = p;
        for (auto& e : copy.elements) {
            const unsigned count = e.verb == PathVerb::cubic   ? 6
                                   : e.verb == PathVerb::quad  ? 4
                                   : e.verb == PathVerb::close ? 0
                                                               : 2;
            for (unsigned n = 0; n < count; ++n)
                e.coordinates[n] = finite(r, e.coordinates[n] + (n % 2 ? dy : dx));
        }
        copy.x += dx;
        copy.y += dy;
        copy.startX += dx;
        copy.startY += dy;
        p = std::move(copy);
        return Value{};
    });
}
void installCanvas(Runtime& vm) {
    vm.bind(canvasType, "<init>", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<CanvasState>();
        return Value{};
    });
    for (const std::string name : {"getWidth", "getHeight", "getSaveCount"})
        vm.bind(canvasType, name, "()I", [name](Runtime& r, Arguments a) {
            const auto& c = state<CanvasState>(r, a[0]);
            return Value::integer(static_cast<std::int32_t>(
                name == "getWidth"    ? c.width
                : name == "getHeight" ? c.height
                                      : static_cast<float>(c.savedTransforms.size() + 1)));
        });
    vm.bind(canvasType, "save", "()I", [](Runtime& r, Arguments a) {
        auto& c = state<CanvasState>(r, a[0]);
        if (c.savedTransforms.size() >= 1023)
            throw Error(ErrorCode::limit_exceeded, "canvas save stack limit exceeded");
        const auto count = c.savedTransforms.size() + 1;
        record(r, a[0], {DrawOperation::save});
        return Value::integer(static_cast<std::int32_t>(count));
    });
    vm.bind(canvasType, "restore", "()V", [](Runtime& r, Arguments a) {
        auto& c = state<CanvasState>(r, a[0]);
        if (c.savedTransforms.empty())
            r.raise("Ljava/lang/IllegalStateException;", "canvas save stack underflow");
        record(r, a[0], {DrawOperation::restore});
        return Value{};
    });
    vm.bind(canvasType, "restoreToCount", "(I)V", [](Runtime& r, Arguments a) {
        auto& c = state<CanvasState>(r, a[0]);
        const auto count = a[1].i32();
        if (count < 1 || static_cast<std::size_t>(count) > c.savedTransforms.size() + 1)
            r.raise("Ljava/lang/IllegalArgumentException;", "invalid canvas save count");
        while (c.savedTransforms.size() + 1 > static_cast<std::size_t>(count)) {
            record(r, a[0], {DrawOperation::restore});
        }
        return Value{};
    });
    for (const auto [name, op] : {std::pair{"translate", DrawOperation::translate},
                                  {"scale", DrawOperation::scale},
                                  {"skew", DrawOperation::skew}})
        vm.bind(canvasType, name, "(FF)V", [op](Runtime& r, Arguments a) {
            DrawCommand c{op};
            c.coordinates[0] = a[1].f32();
            c.coordinates[1] = a[2].f32();
            record(r, a[0], std::move(c));
            return Value{};
        });
    vm.bind(canvasType, "rotate", "(F)V", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::rotate};
        c.coordinates[0] = a[1].f32();
        record(r, a[0], std::move(c));
        return Value{};
    });
    for (const auto [name, op] :
         {std::pair{"rotate", DrawOperation::rotate}, {"scale", DrawOperation::scale}}) {
        const bool rotation = op == DrawOperation::rotate;
        vm.bind(canvasType, name, rotation ? "(FFF)V" : "(FFFF)V", [op, rotation](Runtime& r, Arguments a) {
            DrawCommand c{DrawOperation::translate};
            c.coordinates[0] = a[rotation ? 2 : 3].f32();
            c.coordinates[1] = a[rotation ? 3 : 4].f32();
            record(r, a[0], c);
            DrawCommand transform{op};
            transform.coordinates[0] = a[1].f32();
            transform.coordinates[1] = rotation ? 0 : a[2].f32();
            record(r, a[0], transform);
            c.coordinates[0] *= -1;
            c.coordinates[1] *= -1;
            record(r, a[0], c);
            return Value{};
        });
    }
    vm.bind(canvasType, "drawColor", "(I)V", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::color};
        c.paint.color = static_cast<std::uint32_t>(a[1].bits);
        record(r, a[0], std::move(c));
        return Value{};
    });
    vm.bind(canvasType, "drawRGB", "(III)V", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::color};
        c.paint.color = 0xff000000;
        for (unsigned n = 0; n < 3; ++n)
            c.paint.color |= (static_cast<std::uint32_t>(a[n + 1].bits) & 255) << (16 - n * 8);
        record(r, a[0], std::move(c));
        return Value{};
    });
    for (const auto [name, op] : {std::pair{"drawRect", DrawOperation::rect},
                                  {"drawOval", DrawOperation::oval},
                                  {"drawLine", DrawOperation::line}})
        vm.bind(canvasType, name, "(FFFFLandroid/graphics/Paint;)V", [op](Runtime& r, Arguments a) {
            DrawCommand c{op};
            for (unsigned n = 0; n < 4; ++n)
                c.coordinates[n] = a[n + 1].f32();
            c.paint = paint(r, a[5]);
            record(r, a[0], std::move(c));
            return Value{};
        });
    for (const auto [name, op] :
         {std::pair{"drawRect", DrawOperation::rect}, {"drawOval", DrawOperation::oval}})
        for (const auto* type : {"Landroid/graphics/Rect;", "Landroid/graphics/RectF;"}) {
            if (op == DrawOperation::oval && std::string_view(type) == "Landroid/graphics/Rect;")
                continue;
            vm.bind(canvasType, name, "(" + std::string(type) + "Landroid/graphics/Paint;)V",
                    [op](Runtime& r, Arguments a) {
                        DrawCommand c{op};
                        const auto rect = rectangle(r, a[1]);
                        std::copy(rect.begin(), rect.end(), c.coordinates.begin());
                        c.paint = paint(r, a[2]);
                        record(r, a[0], std::move(c));
                        return Value{};
                    });
        }
    vm.bind(canvasType, "drawCircle", "(FFFLandroid/graphics/Paint;)V", [](Runtime& r, Arguments a) {
        const float x = a[1].f32(), y = a[2].f32(), radius = a[3].f32();
        if (radius < 0)
            return Value{};
        DrawCommand c{DrawOperation::oval};
        c.coordinates = {x - radius, y - radius, x + radius, y + radius};
        c.paint = paint(r, a[4]);
        record(r, a[0], std::move(c));
        return Value{};
    });
    vm.bind(canvasType, "drawPoint", "(FFLandroid/graphics/Paint;)V", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::point};
        c.coordinates[0] = a[1].f32();
        c.coordinates[1] = a[2].f32();
        c.paint = paint(r, a[3]);
        record(r, a[0], std::move(c));
        return Value{};
    });
    vm.bind(canvasType, "drawRoundRect", "(FFFFFFLandroid/graphics/Paint;)V", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::roundRect};
        for (unsigned n = 0; n < 6; ++n)
            c.coordinates[n] = a[n + 1].f32();
        c.paint = paint(r, a[7]);
        record(r, a[0], std::move(c));
        return Value{};
    });
    vm.bind(canvasType, "drawRoundRect", "(Landroid/graphics/RectF;FFLandroid/graphics/Paint;)V",
            [](Runtime& r, Arguments a) {
                DrawCommand c{DrawOperation::roundRect};
                const auto rect = rectangle(r, a[1]);
                std::copy(rect.begin(), rect.end(), c.coordinates.begin());
                c.coordinates[4] = a[2].f32();
                c.coordinates[5] = a[3].f32();
                c.paint = paint(r, a[4]);
                record(r, a[0], std::move(c));
                return Value{};
            });
    vm.bind(canvasType, "drawPath", "(Landroid/graphics/Path;Landroid/graphics/Paint;)V",
            [](Runtime& r, Arguments a) {
                DrawCommand c{DrawOperation::path};
                const auto& p = state<PathState>(r, a[1]);
                c.path = p.elements;
                c.evenOdd = p.evenOdd;
                c.paint = paint(r, a[2]);
                record(r, a[0], std::move(c));
                return Value{};
            });
    vm.bind(canvasType, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V",
            [](Runtime& r, Arguments a) {
                DrawCommand c{DrawOperation::text};
                c.text = r.object(a[1]).text;
                c.coordinates[0] = a[2].f32();
                c.coordinates[1] = a[3].f32();
                c.paint = paint(r, a[4]);
                record(r, a[0], std::move(c));
                return Value{};
            });
    vm.bind(canvasType, "clipRect", "(FFFF)Z", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::clipRect};
        for (unsigned n = 0; n < 4; ++n)
            c.coordinates[n] = a[n + 1].f32();
        const bool nonempty = c.coordinates[0] < c.coordinates[2] && c.coordinates[1] < c.coordinates[3];
        record(r, a[0], std::move(c));
        return Value::integer(nonempty);
    });
    vm.bind(canvasType, "clipPath", "(Landroid/graphics/Path;)Z", [](Runtime& r, Arguments a) {
        DrawCommand c{DrawOperation::clipPath};
        const auto& p = state<PathState>(r, a[1]);
        c.path = p.elements;
        c.evenOdd = p.evenOdd;
        record(r, a[0], std::move(c));
        return Value::integer(!p.elements.empty());
    });
}
} // namespace

void installGraphics(Runtime& vm) {
    for (const auto* type :
         {paintType, pathType, canvasType, "Landroid/graphics/Rect;", "Landroid/graphics/RectF;"})
        vm.defineClass(type);
    installRectangles(vm);
    installGeometry(vm);
    installMatrices(vm);
    installPaint(vm);
    installPath(vm);
    installCanvas(vm);
    installBitmaps(vm);
}
Value createRecordingCanvas(Runtime& vm) {
    const auto canvas = vm.allocate(canvasType);
    vm.object(canvas).native = std::make_shared<CanvasState>();
    return canvas;
}
void beginRecording(Runtime& vm, Value canvas, float width, float height) {
    auto& data = state<CanvasState>(vm, canvas);
    data.display = std::make_shared<DisplayList>();
    data.transform = {};
    data.savedTransforms.clear();
    data.width = finite(vm, width);
    data.height = finite(vm, height);
}
std::shared_ptr<const DisplayList> finishRecording(Runtime& vm, Value canvas) {
    auto& data = state<CanvasState>(vm, canvas);
    return std::exchange(data.display, {});
}
} // namespace gutenberg
