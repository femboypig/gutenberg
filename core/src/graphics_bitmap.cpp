#include "graphics_internal.hpp"
#include <algorithm>
#include <zlib.h>

namespace gutenberg {
using namespace graphics;
namespace {
constexpr auto budgetType = "Lgutenberg/runtime/BitmapBudget;";
Value createBitmap(Runtime& vm, std::int32_t width, std::int32_t height,
                   std::shared_ptr<const Bytes> encoded = {}) {
    if (width <= 0 || height <= 0)
        vm.raise("Ljava/lang/IllegalArgumentException;", "bitmap dimensions must be positive");
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    auto budget = std::dynamic_pointer_cast<BitmapBudget>(
        vm.object(vm.getStatic({bitmapType, "budget", budgetType})).native);
    if (width > 8192 || height > 8192 || pixels > 16 * 1024 * 1024 - budget->pixels)
        throw Error(ErrorCode::limit_exceeded, "bitmap pixel budget exceeded");
    if (encoded && encoded->size() > 64 * 1024 * 1024 - budget->bytes)
        throw Error(ErrorCode::limit_exceeded, "bitmap encoded data budget exceeded");
    auto data = std::make_shared<BitmapState>();
    data->width = width;
    data->height = height;
    data->encoded = std::move(encoded);
    data->drawing = std::make_shared<DisplayList>();
    data->budget = budget;
    budget->pixels += static_cast<std::size_t>(pixels);
    budget->bytes += data->encoded ? data->encoded->size() : 0;
    const auto result = vm.allocate(bitmapType);
    vm.object(result).native = std::move(data);
    return result;
}
std::pair<std::int32_t, std::int32_t> imageSize(const Bytes& bytes) {
    const ByteView view(bytes);
    auto be16 = [&](std::size_t at) { return (std::uint32_t(view.u8(at)) << 8) | view.u8(at + 1); };
    auto be32 = [&](std::size_t at) { return (be16(at) << 16) | be16(at + 2); };
    constexpr std::array<std::uint8_t, 8> png{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() >= 33 && std::equal(png.begin(), png.end(), bytes.begin()) && be32(8) == 13 &&
        std::equal(bytes.begin() + 12, bytes.begin() + 16, "IHDR")) {
        bool hasData = false;
        for (std::size_t at = 8; at < bytes.size();) {
            if (bytes.size() - at < 12)
                return {};
            const auto size = be32(at);
            if (size > bytes.size() - at - 12)
                return {};
            const auto checksum = crc32(0, bytes.data() + at + 4, static_cast<uInt>(size + 4));
            if (checksum != be32(at + 8 + size))
                return {};
            const auto* kind = bytes.data() + at + 4;
            if (std::equal(kind, kind + 4, "IDAT"))
                hasData = true;
            if (std::equal(kind, kind + 4, "IEND"))
                return size == 0 && hasData && at + 12 == bytes.size()
                           ? std::pair{std::bit_cast<std::int32_t>(be32(16)),
                                       std::bit_cast<std::int32_t>(be32(20))}
                           : std::pair<std::int32_t, std::int32_t>{};
            at += 12 + size;
        }
        return {};
    }
    if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8) {
        std::size_t at = 2;
        while (at < bytes.size()) {
            if (view.u8(at++) != 0xff)
                return {};
            while (view.u8(at) == 0xff)
                ++at;
            const auto marker = view.u8(at++);
            if (marker == 0xd9 || marker == 0xda)
                return {};
            if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            const auto length = be16(at);
            if (length < 2 || length > bytes.size() - at)
                return {};
            if ((marker >= 0xc0 && marker <= 0xc3) || (marker >= 0xc5 && marker <= 0xc7) ||
                (marker >= 0xc9 && marker <= 0xcb) || (marker >= 0xcd && marker <= 0xcf)) {
                if (length < 8)
                    return {};
                return {static_cast<std::int32_t>(be16(at + 5)), static_cast<std::int32_t>(be16(at + 3))};
            }
            at += length;
        }
    }
    return {};
}
Paint bitmapPaint(Runtime& vm, Value ref) {
    return ref.isNull() ? Paint{.color = 0xffffffff} : state<PaintState>(vm, ref).value;
}
} // namespace

Value decodeBitmap(Runtime& vm, Bytes bytes) {
    if (bytes.size() > byteLimit)
        throw Error(ErrorCode::limit_exceeded, "encoded bitmap size limit exceeded");
    std::pair<std::int32_t, std::int32_t> size;
    try {
        size = imageSize(bytes);
    } catch (const Error&) {
        return {};
    }
    if (size.first <= 0 || size.second <= 0)
        return {};
    return createBitmap(vm, size.first, size.second, std::make_shared<const Bytes>(std::move(bytes)));
}
std::shared_ptr<const BitmapImage> bitmapSnapshot(Runtime& vm, Value bitmap) {
    auto& data = state<BitmapState>(vm, bitmap);
    if (data.recycled)
        vm.raise("Ljava/lang/IllegalStateException;", "bitmap has been recycled");
    if (data.snapshot)
        return data.snapshot;
    auto result = std::make_shared<BitmapImage>();
    result->width = data.width;
    result->height = data.height;
    result->encoded = data.encoded;
    result->drawing = data.drawing;
    data.snapshot = result;
    return result;
}
namespace graphics {
void installBitmaps(Runtime& vm) {
    vm.defineClass(bitmapType);
    vm.defineClass("Landroid/graphics/BitmapFactory;");
    vm.defineClass(budgetType);
    const auto budget = vm.allocate(budgetType);
    vm.object(budget).native = std::make_shared<BitmapBudget>();
    vm.putStatic({bitmapType, "budget", budgetType}, budget);
    const std::string config = "Landroid/graphics/Bitmap$Config;";
    vm.defineClass(config);
    for (const auto* name : {"ARGB_8888", "RGB_565", "ALPHA_8", "ARGB_4444"}) {
        const auto value = vm.allocate(config);
        vm.object(value).text = utf16(name);
        vm.putStatic({config, name, config}, value);
    }
    vm.bind(bitmapType, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;",
            [](Runtime& r, Arguments a) {
                if (r.object(a[2]).text != u"ARGB_8888")
                    r.raise("Ljava/lang/UnsupportedOperationException;",
                            "only ARGB_8888 bitmap allocation is supported");
                return createBitmap(r, a[0].i32(), a[1].i32());
            });
    for (const std::string name :
         {"getWidth", "getHeight", "getByteCount", "getAllocationByteCount", "getRowBytes"})
        vm.bind(bitmapType, name, "()I", [name](Runtime& r, Arguments a) {
            const auto data = bitmapSnapshot(r, a[0]);
            return Value::integer(name == "getWidth"      ? data->width
                                  : name == "getHeight"   ? data->height
                                  : name == "getRowBytes" ? data->width * 4
                                                          : data->width * data->height * 4);
        });
    vm.bind(bitmapType, "isRecycled", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(state<BitmapState>(r, a[0]).recycled); });
    vm.bind(bitmapType, "recycle", "()V", [](Runtime& r, Arguments a) {
        auto& data = state<BitmapState>(r, a[0]);
        if (!data.recycled) {
            data.budget->pixels -=
                static_cast<std::size_t>(data.width) * static_cast<std::size_t>(data.height);
            data.budget->bytes -= data.encoded ? data.encoded->size() : 0;
            data.recycled = true;
            data.encoded.reset();
            data.drawing.reset();
            data.snapshot.reset();
        }
        return Value{};
    });
    vm.bind(canvasType, "<init>", "(Landroid/graphics/Bitmap;)V", [](Runtime& r, Arguments a) {
        const auto image = bitmapSnapshot(r, a[1]);
        auto& bitmap = state<BitmapState>(r, a[1]);
        auto data = std::make_shared<CanvasState>();
        data->target = a[1];
        data->width = static_cast<float>(image->width);
        data->height = static_cast<float>(image->height);
        data->display = bitmap.drawing;
        r.object(a[0]).native = std::move(data);
        return Value{};
    });
    vm.bind(canvasType, "drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V",
            [](Runtime& r, Arguments a) {
                DrawCommand command{DrawOperation::bitmap};
                command.image = bitmapSnapshot(r, a[1]);
                command.paint = bitmapPaint(r, a[4]);
                const auto width = static_cast<float>(command.image->width),
                           height = static_cast<float>(command.image->height);
                command.coordinates = {a[2].f32(), a[3].f32(), a[2].f32() + width, a[3].f32() + height, 0, 0,
                                       width,      height};
                record(r, a[0], std::move(command));
                return Value{};
            });
    for (const auto* destination : {"Landroid/graphics/Rect;", "Landroid/graphics/RectF;"})
        vm.bind(canvasType, "drawBitmap",
                "(Landroid/graphics/Bitmap;Landroid/graphics/Rect;" + std::string(destination) +
                    "Landroid/graphics/Paint;)V",
                [destination](Runtime& r, Arguments a) {
                    DrawCommand command{DrawOperation::bitmap};
                    command.image = bitmapSnapshot(r, a[1]);
                    command.paint = bitmapPaint(r, a[4]);
                    const bool floating = std::string_view(destination) == "Landroid/graphics/RectF;";
                    unsigned index = 0;
                    for (const auto* field : {"left", "top", "right", "bottom"}) {
                        const auto value = r.getField(a[3], {destination, field, floating ? "F" : "I"});
                        command.coordinates[index] = floating ? value.f32() : static_cast<float>(value.i32());
                        if (!a[2].isNull())
                            command.coordinates[index + 4] = static_cast<float>(
                                r.getField(a[2], {"Landroid/graphics/Rect;", field, "I"}).i32());
                        ++index;
                    }
                    if (a[2].isNull()) {
                        command.coordinates[6] = static_cast<float>(command.image->width);
                        command.coordinates[7] = static_cast<float>(command.image->height);
                    }
                    record(r, a[0], std::move(command));
                    return Value{};
                });
    vm.bind("Landroid/graphics/BitmapFactory;", "decodeByteArray", "([BII)Landroid/graphics/Bitmap;",
            [](Runtime& r, Arguments a) {
                const auto& source = r.object(a[0]).elements;
                const auto offset = a[1].i32(), length = a[2].i32();
                if (offset < 0 || length < 0 || static_cast<std::size_t>(offset) > source.size() ||
                    static_cast<std::size_t>(length) > source.size() - static_cast<std::size_t>(offset))
                    r.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "invalid bitmap byte range");
                if (static_cast<std::size_t>(length) > byteLimit)
                    throw Error(ErrorCode::limit_exceeded, "encoded bitmap size limit exceeded");
                Bytes bytes;
                bytes.reserve(static_cast<std::size_t>(length));
                for (std::size_t n = static_cast<std::size_t>(offset);
                     n < static_cast<std::size_t>(offset) + static_cast<std::size_t>(length); ++n)
                    bytes.push_back(static_cast<std::uint8_t>(source[n].bits));
                return decodeBitmap(r, std::move(bytes));
            });
}
} // namespace graphics
} // namespace gutenberg
