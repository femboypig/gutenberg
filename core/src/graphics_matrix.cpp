#include "graphics_internal.hpp"
#include <algorithm>
#include <cmath>

namespace gutenberg::graphics {
namespace {
constexpr auto type = "Landroid/graphics/Matrix;";
constexpr auto fitType = "Landroid/graphics/Matrix$ScaleToFit;";
Matrix3 matrix(Runtime& vm, Value value) {
    return value.isNull() ? Matrix3{} : state<MatrixState>(vm, value).value;
}
void assign(Runtime& vm, Value value, Matrix3 data) {
    auto& object = vm.object(value);
    if (!object.native)
        object.native = std::make_shared<MatrixState>();
    state<MatrixState>(vm, value).value = data;
}
std::array<float, 4> rect(Runtime& vm, Value value) {
    std::array<float, 4> result;
    unsigned n = 0;
    for (const auto* name : {"left", "top", "right", "bottom"})
        result[n++] = vm.getField(value, {"Landroid/graphics/RectF;", name, "F"}).f32();
    return result;
}
void setRect(Runtime& vm, Value value, std::array<float, 4> bounds) {
    unsigned n = 0;
    for (const auto* name : {"left", "top", "right", "bottom"})
        vm.putField(value, {"Landroid/graphics/RectF;", name, "F"}, Value::floating(bounds[n++]));
}
std::span<Value> pointRange(Runtime& vm, Value array, int offset, int count) {
    auto& data = vm.object(array);
    if (data.type != "[F")
        vm.raise("Ljava/lang/IllegalArgumentException;", "matrix coordinates require a float array");
    if (offset < 0 || count < 0 || static_cast<std::size_t>(offset) > data.elements.size() ||
        static_cast<std::size_t>(count) > (data.elements.size() - static_cast<std::size_t>(offset)) / 2)
        vm.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "matrix coordinate range");
    return std::span(data.elements)
        .subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count) * 2);
}
std::vector<float> points(Runtime& vm, Value array, int offset, int count) {
    const auto data = pointRange(vm, array, offset, count);
    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(count) * 2);
    for (std::size_t n = 0; n < static_cast<std::size_t>(count) * 2; ++n)
        result.push_back(data[n].f32());
    return result;
}
void mapArray(Runtime& vm, Value value, Value destination, int destinationOffset, Value source,
              int sourceOffset, int count, bool vectors) {
    auto input = pointRange(vm, source, sourceOffset, count);
    const auto output = pointRange(vm, destination, destinationOffset, count);
    std::vector<Value> copy;
    if (source == destination && destinationOffset > sourceOffset &&
        static_cast<std::size_t>(destinationOffset - sourceOffset) < input.size()) {
        copy.assign(input.begin(), input.end());
        input = copy;
    }
    const auto transform = matrix(vm, value);
    for (std::size_t n = 0; n < input.size(); n += 2) {
        vm.tick();
        const auto point = vectors ? transform.mapVector(input[n].f32(), input[n + 1].f32())
                                   : transform.map(input[n].f32(), input[n + 1].f32());
        output[n] = Value::floating(point[0]);
        output[n + 1] = Value::floating(point[1]);
    }
}
} // namespace
void installMatrices(Runtime& vm) {
    vm.defineClass(type);
    vm.defineClass(fitType);
    const std::array<const char*, 4> modes{"FILL", "START", "CENTER", "END"};
    for (unsigned n = 0; n < modes.size(); ++n) {
        const auto mode = vm.allocate(fitType);
        vm.object(mode).fields["ordinal"] = Value{n, false};
        vm.object(mode).text = utf16(modes[n]);
        vm.putStatic({fitType, modes[n], fitType}, mode);
    }
    vm.bind(fitType, "ordinal", "()I",
            [](Runtime& r, Arguments a) { return r.object(a[0]).fields["ordinal"]; });
    vm.bind(fitType, "toString", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    const std::array<const char*, 9> constants{"MSCALE_X", "MSKEW_X",  "MTRANS_X", "MSKEW_Y", "MSCALE_Y",
                                               "MTRANS_Y", "MPERSP_0", "MPERSP_1", "MPERSP_2"};
    for (unsigned n = 0; n < constants.size(); ++n)
        vm.putStatic({type, constants[n], "I"}, Value{n, false});
    for (const auto* method : {"<init>", "reset"})
        vm.bind(type, method, "()V", [](Runtime& r, Arguments a) {
            assign(r, a[0], {});
            return Value{};
        });
    for (const auto* method : {"<init>", "set"})
        vm.bind(type, method, "(Landroid/graphics/Matrix;)V", [](Runtime& r, Arguments a) {
            assign(r, a[0], matrix(r, a[1]));
            return Value{};
        });
    for (const auto* method : {"isIdentity", "isAffine", "rectStaysRect"})
        vm.bind(type, method, "()Z", [name = std::string(method)](Runtime& r, Arguments a) {
            const auto value = matrix(r, a[0]);
            return Value::integer(name == "isIdentity" ? value.values == Matrix3{}.values
                                  : name == "isAffine" ? value.affine()
                                                       : value.rectStaysRect());
        });
    for (const bool get : {false, true})
        vm.bind(type, get ? "getValues" : "setValues", "([F)V", [get](Runtime& r, Arguments a) {
            auto& array = r.object(a[1]);
            if (array.type != "[F" || array.elements.size() < 9)
                r.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "matrix requires nine values");
            auto value = matrix(r, a[0]);
            for (unsigned n = 0; n < 9; ++n)
                if (get)
                    array.elements[n] = Value::floating(value.values[n]);
                else
                    value.values[n] = array.elements[n].f32();
            if (!get)
                assign(r, a[0], value);
            return Value{};
        });
    vm.bind(type, "setConcat", "(Landroid/graphics/Matrix;Landroid/graphics/Matrix;)Z",
            [](Runtime& r, Arguments a) {
                assign(r, a[0], matrix(r, a[1]) * matrix(r, a[2]));
                return Value::integer(1);
            });
    for (const bool post : {false, true})
        vm.bind(type, post ? "postConcat" : "preConcat", "(Landroid/graphics/Matrix;)Z",
                [post](Runtime& r, Arguments a) {
                    const auto own = matrix(r, a[0]), other = matrix(r, a[1]);
                    assign(r, a[0], post ? other * own : own * other);
                    return Value::integer(1);
                });
    for (const std::string operation : {"Translate", "Scale", "Rotate", "Skew", "SinCos"})
        for (const std::string prefix : {"set", "pre", "post"}) {
            if (operation == "SinCos" && prefix != "set")
                continue;
            for (const bool pivot : {false, true}) {
                if (pivot && operation == "Translate")
                    continue;
                const auto count = (operation == "Rotate" ? 1U : 2U) + (pivot ? 2U : 0U);
                vm.bind(type, prefix + operation,
                        "(" + std::string(count, 'F') + ")" + (prefix == "set" ? "V" : "Z"),
                        [operation, prefix, pivot](Runtime& r, Arguments a) {
                            const auto x = a[1].f32(), y = operation == "Rotate" ? 0.f : a[2].f32();
                            auto transform = operation == "Translate" ? Matrix3::translation(x, y)
                                             : operation == "Scale"   ? Matrix3::scaling(x, y)
                                             : operation == "Rotate"  ? Matrix3::rotation(x)
                                             : operation == "Skew"    ? Matrix3{{1, x, 0, y, 1, 0, 0, 0, 1}}
                                                                      : Matrix3{{y, -x, 0, x, y, 0, 0, 0, 1}};
                            if (pivot) {
                                const auto at = operation == "Rotate" ? 2U : 3U;
                                const auto px = a[at].f32(), py = a[at + 1].f32();
                                transform =
                                    Matrix3::translation(px, py) * transform * Matrix3::translation(-px, -py);
                            }
                            const auto own = matrix(r, a[0]);
                            assign(r, a[0],
                                   prefix == "set"   ? transform
                                   : prefix == "pre" ? own * transform
                                                     : transform * own);
                            return prefix == "set" ? Value{} : Value::integer(1);
                        });
            }
        }
    vm.bind(type, "invert", "(Landroid/graphics/Matrix;)Z", [](Runtime& r, Arguments a) {
        const auto result = matrix(r, a[0]).inverted();
        if (result && !a[1].isNull())
            assign(r, a[1], *result);
        return Value::integer(result.has_value());
    });
    for (const bool vectors : {false, true}) {
        const auto name = vectors ? "mapVectors" : "mapPoints";
        vm.bind(type, name, "([F)V", [vectors](Runtime& r, Arguments a) {
            mapArray(r, a[0], a[1], 0, a[1], 0, static_cast<int>(r.object(a[1]).elements.size() / 2),
                     vectors);
            return Value{};
        });
        vm.bind(type, name, "([F[F)V", [vectors](Runtime& r, Arguments a) {
            if (r.object(a[1]).elements.size() != r.object(a[2]).elements.size())
                r.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "matrix arrays differ in length");
            mapArray(r, a[0], a[1], 0, a[2], 0, static_cast<int>(r.object(a[2]).elements.size() / 2),
                     vectors);
            return Value{};
        });
        vm.bind(type, name, "([FI[FII)V", [vectors](Runtime& r, Arguments a) {
            mapArray(r, a[0], a[1], a[2].i32(), a[3], a[4].i32(), a[5].i32(), vectors);
            return Value{};
        });
    }
    vm.bind(type, "mapRadius", "(F)F", [](Runtime& r, Arguments a) {
        const auto value = matrix(r, a[0]);
        const auto x = value.mapVector(a[1].f32(), 0), y = value.mapVector(0, a[1].f32());
        return Value::floating(std::sqrt(std::hypot(x[0], x[1]) * std::hypot(y[0], y[1])));
    });
    for (const bool separate : {false, true})
        vm.bind(type, "mapRect",
                separate ? "(Landroid/graphics/RectF;Landroid/graphics/RectF;)Z"
                         : "(Landroid/graphics/RectF;)Z",
                [separate](Runtime& r, Arguments a) {
                    const auto value = matrix(r, a[0]);
                    const auto source = rect(r, a[separate ? 2 : 1]);
                    const auto p0 = value.map(source[0], source[1]), p1 = value.map(source[2], source[1]);
                    const auto p2 = value.map(source[2], source[3]), p3 = value.map(source[0], source[3]);
                    setRect(r, a[1],
                            {std::min({p0[0], p1[0], p2[0], p3[0]}), std::min({p0[1], p1[1], p2[1], p3[1]}),
                             std::max({p0[0], p1[0], p2[0], p3[0]}), std::max({p0[1], p1[1], p2[1], p3[1]})});
                    return Value::integer(value.rectStaysRect());
                });
    vm.bind(type, "setRectToRect",
            "(Landroid/graphics/RectF;Landroid/graphics/RectF;Landroid/graphics/Matrix$ScaleToFit;)Z",
            [](Runtime& r, Arguments a) {
                const auto mode = static_cast<unsigned>(r.object(a[3]).fields["ordinal"].bits);
                const auto result = Matrix3::fitRect(rect(r, a[1]), rect(r, a[2]), mode);
                assign(r, a[0], result.value_or(Matrix3{}));
                return Value::integer(result.has_value());
            });
    vm.bind(type, "setPolyToPoly", "([FI[FII)Z", [](Runtime& r, Arguments a) {
        const auto count = a[5].i32();
        if (count < 0 || count > 4)
            r.raise("Ljava/lang/IllegalArgumentException;", "matrix requires zero to four points");
        const auto result =
            Matrix3::fitPoints(points(r, a[1], a[2].i32(), count), points(r, a[3], a[4].i32(), count));
        if (result)
            assign(r, a[0], *result);
        return Value::integer(result.has_value());
    });
    vm.bind(type, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(a[0] == a[1] || (!a[1].isNull() && r.assignable(r.object(a[1]).type, type) &&
                                               matrix(r, a[0]).values == matrix(r, a[1]).values));
    });
    vm.bind(type, "hashCode", "()I", [](Runtime&, Arguments) { return Value::integer(44); });
    vm.bind(type, "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto value = matrix(r, a[0]);
        std::string text = "Matrix{";
        for (unsigned n = 0; n < 9; ++n)
            text += (n ? ", " : "") + std::to_string(value.values[n]);
        return r.string(text + "}");
    });
    for (const bool replace : {false, true})
        vm.bind(canvasType, replace ? "setMatrix" : "concat", "(Landroid/graphics/Matrix;)V",
                [replace](Runtime& r, Arguments a) {
                    const auto value = matrix(r, a[1]);
                    if (!value.affine())
                        throw Error(ErrorCode::unsupported,
                                    "perspective canvas transforms require a projective renderer");
                    DrawCommand command{replace ? DrawOperation::setMatrix : DrawOperation::concatMatrix};
                    std::copy_n(value.values.begin(), 6, command.coordinates.begin());
                    record(r, a[0], std::move(command));
                    return Value{};
                });
    vm.bind(canvasType, "getMatrix", "(Landroid/graphics/Matrix;)V", [](Runtime& r, Arguments a) {
        if (a[1].isNull())
            r.raise("Ljava/lang/NullPointerException;", "null destination matrix");
        assign(r, a[1], state<CanvasState>(r, a[0]).transform);
        return Value{};
    });
    vm.bind(canvasType, "getMatrix", "()Landroid/graphics/Matrix;", [](Runtime& r, Arguments a) {
        const auto result = r.allocate(type);
        assign(r, result, state<CanvasState>(r, a[0]).transform);
        return result;
    });
}
} // namespace gutenberg::graphics
