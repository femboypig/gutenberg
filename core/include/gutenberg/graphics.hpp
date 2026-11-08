#pragma once

#include "runtime.hpp"
#include <array>

namespace gutenberg {

enum class PaintStyle { fill, stroke, fillAndStroke };
struct Paint {
    std::uint32_t color = 0xff000000;
    float strokeWidth = 0, textSize = 16, miter = 4;
    PaintStyle style = PaintStyle::fill;
    unsigned cap = 0, join = 0, textAlign = 0;
    bool antiAlias = false;
};
enum class PathVerb { move, line, quad, cubic, close };
struct PathElement {
    PathVerb verb;
    std::array<float, 6> coordinates{};
};
enum class DrawOperation {
    color,
    rect,
    oval,
    roundRect,
    line,
    point,
    path,
    text,
    save,
    restore,
    translate,
    scale,
    rotate,
    skew,
    clipRect,
    clipPath,
    bitmap
};
struct BitmapImage;
struct DrawCommand {
    DrawCommand(DrawOperation operation) : operation(operation) {}
    DrawOperation operation;
    std::array<float, 8> coordinates{};
    Paint paint;
    std::vector<PathElement> path;
    std::u16string text;
    std::shared_ptr<const BitmapImage> image;
    bool evenOdd = false;
};
struct DisplayList {
    std::vector<DrawCommand> commands;
    std::size_t bytes = 0;
    std::size_t replayCost = 0;
    unsigned depth = 0;
};
struct BitmapImage {
    std::int32_t width = 0, height = 0;
    std::shared_ptr<const Bytes> encoded;
    std::shared_ptr<const DisplayList> drawing;
};

void installGraphics(Runtime& runtime);
[[nodiscard]] Value createRecordingCanvas(Runtime& runtime);
void beginRecording(Runtime& runtime, Value canvas, float width, float height);
[[nodiscard]] std::shared_ptr<const DisplayList> finishRecording(Runtime& runtime, Value canvas);
[[nodiscard]] Value decodeBitmap(Runtime& runtime, Bytes bytes);
[[nodiscard]] std::shared_ptr<const BitmapImage> bitmapSnapshot(Runtime& runtime, Value bitmap);

} // namespace gutenberg
