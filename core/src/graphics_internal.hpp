#pragma once
#include "gutenberg/graphics.hpp"
#include "gutenberg/matrix.hpp"

namespace gutenberg::graphics {
inline constexpr auto paintType = "Landroid/graphics/Paint;";
inline constexpr auto pathType = "Landroid/graphics/Path;";
inline constexpr auto canvasType = "Landroid/graphics/Canvas;";
inline constexpr auto bitmapType = "Landroid/graphics/Bitmap;";
inline constexpr std::size_t byteLimit = 16 * 1024 * 1024, elementLimit = 100000;
struct PaintState final : NativeObject {
    Paint value;
};
struct PathState final : NativeObject {
    std::vector<PathElement> elements;
    float x = 0, y = 0, startX = 0, startY = 0;
    bool evenOdd = false;
};
struct MatrixState final : NativeObject {
    Matrix3 value;
};
struct CanvasState final : NativeObject {
    std::shared_ptr<DisplayList> display;
    Value target;
    Matrix3 transform;
    std::vector<Matrix3> savedTransforms;
    float width = 0, height = 0;
    void visitReferences(const std::function<void(Value)>& visit) const override {
        visit(target);
    }
};
struct BitmapBudget final : NativeObject {
    std::size_t pixels = 0;
    std::size_t bytes = 0;
};
struct BitmapState final : NativeObject {
    std::shared_ptr<BitmapBudget> budget;
    std::int32_t width = 0, height = 0;
    bool recycled = false;
    std::shared_ptr<const Bytes> encoded;
    std::shared_ptr<DisplayList> drawing;
    std::shared_ptr<const BitmapImage> snapshot;
    ~BitmapState() override {
        if (budget && !recycled) {
            budget->pixels -= static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            budget->bytes -= encoded ? encoded->size() : 0;
        }
    }
};
template <class T> T& state(Runtime& vm, Value value) {
    const auto result = std::dynamic_pointer_cast<T>(vm.object(value).native);
    if (!result)
        vm.raise("Ljava/lang/IllegalStateException;", "graphics object is not initialized");
    return *result;
}
[[nodiscard]] float finite(Runtime& vm, float value);
void record(Runtime& vm, Value canvas, DrawCommand command);
void installBitmaps(Runtime& vm);
void installGeometry(Runtime& vm);
void installMatrices(Runtime& vm);
} // namespace gutenberg::graphics
