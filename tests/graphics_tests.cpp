#include "gutenberg/graphics.hpp"
#include "gutenberg/java.hpp"
#include "test.hpp"
#include <limits>

using namespace gutenberg;
namespace {
Value call(Runtime& vm, std::string owner, std::string name, std::string descriptor,
           std::initializer_list<Value> args, InvokeKind kind = InvokeKind::virtual_) {
    return vm.invoke(std::move(owner), std::move(name), std::move(descriptor),
                     Arguments(args.begin(), args.size()), kind);
}
Value create(Runtime& vm, const std::string& type) {
    const auto value = vm.allocate(type);
    (void)call(vm, type, "<init>", "()V", {value}, InvokeKind::direct);
    return value;
}
constexpr auto paintType = "Landroid/graphics/Paint;";
constexpr auto pathType = "Landroid/graphics/Path;";
constexpr auto canvasType = "Landroid/graphics/Canvas;";
} // namespace

TEST(canvas_snapshots_mutable_paint_and_path_state) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto paint = create(vm, paintType), path = create(vm, pathType);
    const auto canvas = createRecordingCanvas(vm);
    beginRecording(vm, canvas, 320, 480);
    (void)call(vm, pathType, "moveTo", "(FF)V", {path, Value::floating(10.f), Value::floating(20.f)});
    (void)call(vm, pathType, "rLineTo", "(FF)V", {path, Value::floating(5.f), Value::floating(7.f)});
    (void)call(vm, paintType, "setColor", "(I)V", {paint, Value{0xff12abcd, false}});
    const auto style =
        vm.getStatic({"Landroid/graphics/Paint$Style;", "STROKE", "Landroid/graphics/Paint$Style;"});
    (void)call(vm, paintType, "setStyle", "(Landroid/graphics/Paint$Style;)V", {paint, style});
    (void)call(vm, canvasType, "drawPath", "(Landroid/graphics/Path;Landroid/graphics/Paint;)V",
               {canvas, path, paint});
    (void)call(vm, pathType, "reset", "()V", {path});
    (void)call(vm, paintType, "setColor", "(I)V", {paint, Value::integer(0)});
    const auto frame = finishRecording(vm, canvas);
    CHECK(frame->commands.size() == 1);
    CHECK(frame->commands[0].paint.color == 0xff12abcd);
    CHECK(frame->commands[0].paint.style == PaintStyle::stroke);
    CHECK(frame->commands[0].path.size() == 2);
    CHECK(frame->commands[0].path[1].coordinates[0] == 15);
    CHECK(frame->commands[0].path[1].coordinates[1] == 27);
    beginRecording(vm, canvas, 640, 480);
    CHECK(finishRecording(vm, canvas)->commands.empty());
    CHECK(frame->commands.size() == 1);
}

TEST(canvas_rejects_invalid_geometry_and_unbalanced_restore) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto canvas = createRecordingCanvas(vm);
    beginRecording(vm, canvas, 100, 100);
    CHECK(call(vm, canvasType, "save", "()I", {canvas}).i32() == 1);
    CHECK(call(vm, canvasType, "save", "()I", {canvas}).i32() == 2);
    (void)call(vm, canvasType, "restoreToCount", "(I)V", {canvas, Value::integer(1)});
    CHECK(call(vm, canvasType, "getSaveCount", "()I", {canvas}).i32() == 1);
    try {
        (void)call(vm, canvasType, "restore", "()V", {canvas});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/IllegalStateException;");
    }
    try {
        (void)call(vm, canvasType, "translate", "(FF)V",
                   {canvas, Value::floating(std::numeric_limits<float>::infinity()), Value::floating(0.f)});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/IllegalArgumentException;");
    }
    CHECK(finishRecording(vm, canvas)->commands.size() == 4);
}

TEST(integer_rect_geometry_uses_java_overflow_rules) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto rect = create(vm, "Landroid/graphics/Rect;");
    (void)call(
        vm, "Landroid/graphics/Rect;", "set", "(IIII)V",
        {rect, Value::integer(INT32_MIN), Value::integer(-5), Value::integer(INT32_MAX), Value::integer(0)});
    CHECK(call(vm, "Landroid/graphics/Rect;", "width", "()I", {rect}).i32() == -1);
    CHECK(call(vm, "Landroid/graphics/Rect;", "centerY", "()I", {rect}).i32() == -3);
}
