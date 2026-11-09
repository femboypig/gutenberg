#include "gutenberg/graphics.hpp"
#include "gutenberg/java.hpp"
#include "test.hpp"
#include <cmath>
#include <limits>

using namespace gutenberg;
namespace {
constexpr auto type = "Landroid/graphics/Matrix;";
Value call(Runtime& vm, Value receiver, std::string name, std::string signature,
           std::initializer_list<Value> extra = {}) {
    std::vector<Value> args{receiver};
    args.insert(args.end(), extra);
    return vm.invoke(vm.object(receiver).type, std::move(name), std::move(signature), args);
}
Value create(Runtime& vm, const char* owner) {
    const auto value = vm.allocate(owner);
    const Value args[]{value};
    (void)vm.invoke(owner, "<init>", "()V", args, InvokeKind::direct);
    return value;
}
Value floats(Runtime& vm, std::initializer_list<float> values) {
    const auto array = vm.array("[F", static_cast<std::int32_t>(values.size()));
    unsigned n = 0;
    for (const auto value : values)
        vm.object(array).elements[n++] = Value::floating(value);
    return array;
}
} // namespace

TEST(android_matrices_preserve_pre_and_post_order_and_overlapping_arrays) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto matrix = create(vm, type);
    (void)call(vm, matrix, "setScale", "(FF)V", {Value::floating(2.f), Value::floating(3.f)});
    (void)call(vm, matrix, "preTranslate", "(FF)Z", {Value::floating(4.f), Value::floating(5.f)});
    auto array = floats(vm, {1, 1, 2, 2, 9, 9});
    (void)call(vm, matrix, "mapPoints", "([FI[FII)V",
               {array, Value::integer(2), array, Value::integer(0), Value::integer(2)});
    CHECK(vm.object(array).elements[2].f32() == 10 && vm.object(array).elements[3].f32() == 18);
    CHECK(vm.object(array).elements[4].f32() == 12 && vm.object(array).elements[5].f32() == 21);
    (void)call(vm, matrix, "setScale", "(FF)V", {Value::floating(2.f), Value::floating(3.f)});
    (void)call(vm, matrix, "postTranslate", "(FF)Z", {Value::floating(4.f), Value::floating(5.f)});
    array = floats(vm, {1, 1});
    (void)call(vm, matrix, "mapPoints", "([F)V", {array});
    CHECK(vm.object(array).elements[0].f32() == 6 && vm.object(array).elements[1].f32() == 8);
    try {
        (void)call(vm, matrix, "mapPoints", "([FI[FII)V",
                   {array, Value::integer(0), array, Value::integer(0),
                    Value::integer(std::numeric_limits<int>::max())});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/ArrayIndexOutOfBoundsException;");
    }
    CHECK(vm.object(array).elements[0].f32() == 6);
}

TEST(android_matrix_copy_and_failed_inversion_leave_destination_intact) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto source = create(vm, type), destination = create(vm, type);
    (void)call(vm, destination, "setTranslate", "(FF)V", {Value::floating(7.f), Value::floating(9.f)});
    (void)call(vm, source, "setScale", "(FF)V", {Value::floating(0.f), Value::floating(1.f)});
    CHECK(!call(vm, source, "invert", "(Landroid/graphics/Matrix;)Z", {destination}).i32());
    const auto array = floats(vm, {0, 0});
    (void)call(vm, destination, "mapPoints", "([F)V", {array});
    CHECK(vm.object(array).elements[0].f32() == 7 && vm.object(array).elements[1].f32() == 9);
    (void)call(vm, source, "set", "(Landroid/graphics/Matrix;)V", {destination});
    (void)call(vm, source, "reset", "()V");
    CHECK(!call(vm, destination, "isIdentity", "()Z").i32());
    CHECK(call(vm, source, "isIdentity", "()Z").i32());
}

TEST(canvas_matrix_state_survives_saves_and_recovers_from_singular_transforms) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto canvas = createRecordingCanvas(vm), matrix = create(vm, type);
    beginRecording(vm, canvas, 100, 100);
    (void)call(vm, canvas, "translate", "(FF)V", {Value::floating(10.f), Value::floating(20.f)});
    const auto save = call(vm, canvas, "save", "()I");
    CHECK(save.i32() == 1);
    (void)call(vm, canvas, "scale", "(FF)V", {Value::floating(0.f), Value::floating(0.f)});
    (void)call(vm, canvas, "setMatrix", "(Landroid/graphics/Matrix;)V", {matrix});
    auto current = call(vm, canvas, "getMatrix", "()Landroid/graphics/Matrix;");
    CHECK(call(vm, current, "isIdentity", "()Z").i32());
    (void)call(vm, canvas, "restoreToCount", "(I)V", {save});
    current = call(vm, canvas, "getMatrix", "()Landroid/graphics/Matrix;");
    const auto array = floats(vm, {0, 0});
    (void)call(vm, current, "mapPoints", "([F)V", {array});
    CHECK(vm.object(array).elements[0].f32() == 10 && vm.object(array).elements[1].f32() == 20);
    CHECK(call(vm, canvas, "getSaveCount", "()I").i32() == 1);
    const auto display = finishRecording(vm, canvas);
    CHECK(display->commands[3].operation == DrawOperation::setMatrix);
}

TEST(android_points_preserve_integer_overflow_and_float_identity) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto point = create(vm, "Landroid/graphics/Point;");
    (void)call(
        vm, point, "set", "(II)V",
        {Value::integer(std::numeric_limits<int>::min()), Value::integer(std::numeric_limits<int>::max())});
    (void)call(vm, point, "negate", "()V");
    CHECK(vm.getField(point, {"Landroid/graphics/Point;", "x", "I"}).i32() ==
          std::numeric_limits<int>::min());
    (void)call(vm, point, "offset", "(II)V", {Value::integer(-1), Value::integer(-2)});
    CHECK(vm.getField(point, {"Landroid/graphics/Point;", "x", "I"}).i32() ==
          std::numeric_limits<int>::max());
    const auto a = create(vm, "Landroid/graphics/PointF;"), b = create(vm, "Landroid/graphics/PointF;");
    (void)call(vm, a, "set", "(FF)V",
               {Value::floating(-0.f), Value::floating(std::numeric_limits<float>::quiet_NaN())});
    (void)call(vm, b, "set", "(Landroid/graphics/PointF;)V", {a});
    CHECK(call(vm, a, "equals", "(Ljava/lang/Object;)Z", {b}).i32());
    CHECK(call(vm, a, "hashCode", "()I") == call(vm, b, "hashCode", "()I"));
    (void)call(vm, b, "set", "(FF)V",
               {Value::floating(0.f), Value::floating(std::numeric_limits<float>::quiet_NaN())});
    CHECK(!call(vm, a, "equals", "(Ljava/lang/Object;)Z", {b}).i32());
}

TEST(android_rectangles_preserve_exclusive_edges_and_failed_intersections) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto rect = create(vm, "Landroid/graphics/Rect;");
    (void)call(vm, rect, "set", "(IIII)V",
               {Value::integer(1), Value::integer(2), Value::integer(11), Value::integer(22)});
    CHECK(call(vm, rect, "contains", "(II)Z", {Value::integer(1), Value::integer(2)}).i32());
    CHECK(!call(vm, rect, "contains", "(II)Z", {Value::integer(11), Value::integer(22)}).i32());
    CHECK(!call(vm, rect, "intersect", "(IIII)Z",
                {Value::integer(11), Value::integer(2), Value::integer(30), Value::integer(22)})
               .i32());
    CHECK(call(vm, rect, "width", "()I").i32() == 10);
    (void)call(vm, rect, "offsetTo", "(II)V", {Value::integer(100), Value::integer(-100)});
    CHECK(call(vm, rect, "width", "()I").i32() == 10);
    CHECK(call(vm, rect, "height", "()I").i32() == 20);
    const auto fractional = create(vm, "Landroid/graphics/RectF;");
    (void)call(
        vm, fractional, "set", "(FFFF)V",
        {Value::floating(-1.5f), Value::floating(-0.5f), Value::floating(1.5f), Value::floating(2.5f)});
    (void)call(vm, fractional, "roundOut", "(Landroid/graphics/Rect;)V", {rect});
    CHECK(vm.getField(rect, {"Landroid/graphics/Rect;", "left", "I"}).i32() == -2);
    CHECK(vm.getField(rect, {"Landroid/graphics/Rect;", "bottom", "I"}).i32() == 3);
    (void)call(vm, fractional, "round", "(Landroid/graphics/Rect;)V", {rect});
    CHECK(vm.getField(rect, {"Landroid/graphics/Rect;", "left", "I"}).i32() == -1);
    CHECK(vm.getField(rect, {"Landroid/graphics/Rect;", "top", "I"}).i32() == 0);
}
