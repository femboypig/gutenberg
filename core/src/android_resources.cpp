#include "android_internal.hpp"
#include "gutenberg/android.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace gutenberg {
namespace {
constexpr auto typedArrayType = "Landroid/content/res/TypedArray;";
struct TypedArray final : NativeObject {
    std::vector<std::optional<ResourceValue>> values;
    bool recycled = false;
};
TypedArray& typedArray(Runtime& vm, Value value) {
    const auto data = std::dynamic_pointer_cast<TypedArray>(vm.object(value).native);
    if (!data || data->recycled)
        vm.raise("Ljava/lang/RuntimeException;", "typed array has been recycled");
    return *data;
}
std::optional<ResourceValue> at(Runtime& vm, Value value, Value index) {
    const auto& data = typedArray(vm, value);
    if (index.i32() < 0 || index.bits >= data.values.size())
        vm.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "invalid typed array index");
    return data.values[static_cast<std::size_t>(index.bits)];
}
float complex(std::uint32_t data) {
    constexpr float multipliers[]{1.0f / 256, 1.0f / 32768, 1.0f / 8388608, 1.0f / 2147483648.0f};
    return static_cast<float>(std::bit_cast<std::int32_t>(data & 0xffffff00)) * multipliers[(data >> 4) & 3];
}
const XmlAttribute* attribute(Runtime& vm, Value set, std::string_view ns, std::string_view name) {
    if (set.isNull())
        return nullptr;
    const auto data = std::dynamic_pointer_cast<ViewAttributes>(vm.object(set).native);
    if (!data)
        vm.raise("Ljava/lang/IllegalArgumentException;", "invalid XML attribute set");
    for (const auto& value : data->values)
        if (value.name == name && value.nameSpace == ns)
            return &value;
    return nullptr;
}
} // namespace

void AndroidSession::installResources() {
    vm_.defineClass(typedArrayType);
    vm_.defineClass("Landroid/content/res/Resources$NotFoundException;", "Ljava/lang/RuntimeException;");
    auto obtain = [this](Runtime& r, Arguments a) {
        const auto set = a[1];
        const auto ids = r.object(a[2]).elements;
        auto data = std::make_shared<TypedArray>();
        data->values.resize(ids.size());
        const auto fallbackStyle = a.size() == 5 ? static_cast<std::uint32_t>(a[4].bits) : 0;
        const auto style = attribute(r, set, "", "style");
        for (std::size_t n = 0; n < ids.size(); ++n) {
            const auto id = static_cast<std::uint32_t>(ids[n].bits);
            auto name = package_->resources().name(id);
            const auto slash = name.find('/');
            if (slash != name.npos)
                name.erase(0, slash + 1);
            if (const auto* value = attribute(r, set, "http://schemas.android.com/apk/res-auto", name))
                data->values[n] = value->value;
            else if (const auto* value =
                         attribute(r, set, "http://schemas.android.com/apk/res/" + package_->info().id, name))
                data->values[n] = value->value;
            else if (const auto* value =
                         attribute(r, set, "http://schemas.android.com/apk/res/android", name))
                data->values[n] = value->value;
            if (!data->values[n] && style)
                data->values[n] = package_->resources().styled(style->value.data, id);
            if (!data->values[n] && fallbackStyle)
                data->values[n] = package_->resources().styled(fallbackStyle, id);
        }
        const auto result = r.allocate(typedArrayType);
        r.object(result).native = std::move(data);
        return result;
    };
    for (const auto* owner : {"Landroid/content/Context;", "Landroid/content/res/Resources$Theme;"}) {
        vm_.bind(owner, "obtainStyledAttributes",
                 "(Landroid/util/AttributeSet;[I)Landroid/content/res/TypedArray;", obtain);
        vm_.bind(owner, "obtainStyledAttributes",
                 "(Landroid/util/AttributeSet;[III)Landroid/content/res/TypedArray;", obtain);
    }
    vm_.bind(typedArrayType, "recycle", "()V", [](Runtime& r, Arguments a) {
        auto& data = typedArray(r, a[0]);
        data.values.clear();
        data.recycled = true;
        return Value{};
    });
    vm_.bind(typedArrayType, "length", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<int>(typedArray(r, a[0]).values.size()));
    });
    vm_.bind(typedArrayType, "hasValue", "(I)Z", [](Runtime& r, Arguments a) {
        const auto value = at(r, a[0], a[1]);
        return Value::integer(value && value->type != 0);
    });
    vm_.bind(typedArrayType, "getIndexCount", "()I", [](Runtime& r, Arguments a) {
        const auto& values = typedArray(r, a[0]).values;
        return Value::integer(
            static_cast<int>(std::count_if(values.begin(), values.end(), [](const auto& value) {
                return value.has_value() && value->type != 0;
            })));
    });
    vm_.bind(typedArrayType, "getIndex", "(I)I", [](Runtime& r, Arguments a) {
        const auto& values = typedArray(r, a[0]).values;
        auto index = a[1].i32();
        for (std::size_t n = 0; n < values.size(); ++n)
            if (values[n] && values[n]->type != 0 && index-- == 0)
                return Value::integer(static_cast<int>(n));
        r.raise("Ljava/lang/ArrayIndexOutOfBoundsException;", "invalid typed array value index");
    });
    auto resolved = [this](Runtime& r, Value array, Value index) {
        auto value = at(r, array, index);
        if (value && value->isReference()) {
            const auto resolved = package_->resources().resolve(value->data);
            if (!resolved)
                r.raise("Landroid/content/res/Resources$NotFoundException;", "missing typed array resource");
            value = resolved;
        }
        return value;
    };
    vm_.bind(typedArrayType, "getResourceId", "(II)I", [](Runtime& r, Arguments a) {
        const auto value = at(r, a[0], a[1]);
        return value && value->isReference() ? Value{value->data, false} : a[2];
    });
    for (const std::string name : {"getInt", "getInteger", "getColor", "getBoolean"})
        vm_.bind(
            typedArrayType, name, name == "getBoolean" ? "(IZ)Z" : "(II)I",
            [resolved](Runtime& r, Arguments a) {
                const auto value = resolved(r, a[0], a[1]);
                if (!value || value->type == 0)
                    return a[2];
                if (value->type >= 0x10 && value->type <= 0x1f)
                    return Value{value->data, false};
                if (value->type == 3) {
                    std::int32_t number = 0;
                    const auto& text = value->text;
                    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
                    if (error == std::errc{} && end == text.data() + text.size())
                        return Value::integer(number);
                }
                r.raise("Ljava/lang/UnsupportedOperationException;", "typed array value is not an integer");
            });
    for (const std::string name : {"getString", "getText"})
        vm_.bind(typedArrayType, name,
                 name == "getString" ? "(I)Ljava/lang/String;" : "(I)Ljava/lang/CharSequence;",
                 [this, resolved](Runtime& r, Arguments a) {
                     const auto value = resolved(r, a[0], a[1]);
                     return value && value->type != 0 ? r.string(package_->resourceText(*value)) : Value{};
                 });
    for (const std::string name :
         {"getDimension", "getDimensionPixelSize", "getDimensionPixelOffset", "getFloat"})
        vm_.bind(typedArrayType, name, name == "getDimension" || name == "getFloat" ? "(IF)F" : "(II)I",
                 [this, name, resolved](Runtime& r, Arguments a) {
                     const auto value = resolved(r, a[0], a[1]);
                     if (!value || value->type == 0)
                         return a[2];
                     float number = 0;
                     if (value->type == 4)
                         number = std::bit_cast<float>(value->data);
                     else if (value->type >= 0x10 && value->type <= 0x1f)
                         number = static_cast<float>(std::bit_cast<std::int32_t>(value->data));
                     else if (value->type == 5) {
                         number = complex(value->data);
                         const auto unit = value->data & 15;
                         // The guest display uses one logical pixel per dp and sp.
                         if (unit == 3)
                             number *= 160.0f / 72;
                         else if (unit == 4)
                             number *= 160;
                         else if (unit == 5)
                             number *= 160.0f / 25.4f;
                         else if (unit > 5)
                             r.raise("Ljava/lang/UnsupportedOperationException;", "unknown dimension unit");
                     } else
                         r.raise("Ljava/lang/UnsupportedOperationException;",
                                 "typed array value is not a dimension");
                     if (!std::isfinite(number) || std::abs(number) > 1e8f)
                         throw Error(ErrorCode::limit_exceeded, "dimension exceeds display limits");
                     if (name == "getDimension" || name == "getFloat")
                         return Value::floating(number);
                     if (name == "getDimensionPixelOffset")
                         return Value::integer(static_cast<std::int32_t>(number));
                     const auto rounded = static_cast<std::int32_t>(number + (number >= 0 ? 0.5f : -0.5f));
                     return Value::integer(rounded != 0 || number == 0 ? rounded : number > 0 ? 1 : -1);
                 });
    const std::string attributes = "Landroid/util/AttributeSet;";
    vm_.bind(attributes, "getAttributeCount", "()I", [](Runtime& r, Arguments a) {
        const auto data = std::dynamic_pointer_cast<ViewAttributes>(r.object(a[0]).native);
        return Value::integer(data ? static_cast<int>(data->values.size()) : 0);
    });
    vm_.bind(attributes, "getAttributeValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
             [this](Runtime& r, Arguments a) {
                 const auto* value = attribute(r, a[0], a[1].isNull() ? "" : r.text(a[1]), r.text(a[2]));
                 return value ? r.string(package_->resourceText(value->value)) : Value{};
             });
}
} // namespace gutenberg
