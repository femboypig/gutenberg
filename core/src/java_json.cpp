#include "gutenberg/java.hpp"
#include "gutenberg/json.hpp"
#include "java_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace gutenberg {
namespace {
constexpr auto objectType = "Lorg/json/JSONObject;";
constexpr auto arrayType = "Lorg/json/JSONArray;";
constexpr auto nullType = "Lorg/json/JSONObject$Null;";
constexpr auto iteratorType = "Lgutenberg/runtime/JsonKeys;";

struct Members final : NativeObject {
    std::vector<std::pair<std::u16string, Value>> values;
    std::unordered_map<std::u16string, std::size_t> indices;
    std::uint64_t changes = 0;
    void visitReferences(const std::function<void(Value)>& visit) const override {
        for (const auto& [name, value] : values)
            visit(value);
    }
};
[[noreturn]] void fail(Runtime& vm, std::string message) {
    vm.raise("Lorg/json/JSONException;", std::move(message));
}
Members& members(Runtime& vm, Value value) {
    auto& object = vm.object(value);
    if (!object.native)
        object.native = std::make_shared<Members>();
    const auto data = std::dynamic_pointer_cast<Members>(object.native);
    if (!data)
        fail(vm, "invalid JSON object storage");
    return *data;
}
Value lookup(Runtime& vm, Value object, Value key) {
    if (vm.assignable(vm.object(object).type, arrayType)) {
        const auto index = key.i32();
        const auto& values = vm.object(object).elements;
        return index < 0 || static_cast<std::size_t>(index) >= values.size()
                   ? Value{}
                   : values[static_cast<std::size_t>(index)];
    }
    if (key.isNull())
        return {};
    auto& data = members(vm, object);
    const auto found = data.indices.find(vm.object(key).text);
    return found == data.indices.end() ? Value{} : data.values[found->second].second;
}
Value erase(Runtime& vm, Value object, Value key) {
    if (vm.assignable(vm.object(object).type, arrayType)) {
        auto& values = vm.object(object).elements;
        const auto index = key.i32();
        if (index < 0 || static_cast<std::size_t>(index) >= values.size())
            return {};
        const auto result = values[static_cast<std::size_t>(index)];
        values.erase(values.begin() + index);
        return result;
    }
    if (key.isNull())
        return {};
    auto& data = members(vm, object);
    const auto found = data.indices.find(vm.object(key).text);
    if (found == data.indices.end())
        return {};
    const auto at = found->second;
    const auto result = data.values[at].second;
    data.values.erase(data.values.begin() + static_cast<std::ptrdiff_t>(at));
    data.indices.erase(found);
    for (std::size_t n = at; n < data.values.size(); ++n)
        data.indices[data.values[n].first] = n;
    ++data.changes;
    return result;
}
Value boxed(Runtime& vm, std::string type, Value value) {
    const auto result = vm.allocate(std::move(type));
    vm.object(result).fields["value"] = value;
    return result;
}
std::optional<double> numeric(Runtime& vm, Value value, bool strings = true) {
    if (value.isNull())
        return {};
    const auto type = vm.object(value).type;
    if (type == "Ljava/lang/String;" && strings) {
        try {
            const Value args[]{value};
            return vm
                .invoke("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", args,
                        InvokeKind::static_)
                .f64();
        } catch (const GuestException&) {
            return {};
        }
    }
    if (!vm.assignable(type, "Ljava/lang/Number;"))
        return {};
    const auto data = vm.object(value).fields["value"];
    if (type == "Ljava/lang/Double;")
        return data.f64();
    if (type == "Ljava/lang/Float;")
        return data.f32();
    if (type == "Ljava/lang/Long;")
        return static_cast<double>(data.i64());
    if (type == "Ljava/lang/Integer;" || type == "Ljava/lang/Short;" || type == "Ljava/lang/Byte;")
        return data.i32();
    const Value args[]{value};
    return vm.invoke("Ljava/lang/Number;", "doubleValue", "()D", args).f64();
}
void validNumber(Runtime& vm, Value value) {
    if (const auto number = numeric(vm, value, false); number && !std::isfinite(*number))
        fail(vm, "non-finite JSON number");
}
void put(Runtime& vm, Value object, Value key, Value value) {
    auto& data = members(vm, object);
    if (value.isNull()) {
        (void)erase(vm, object, key);
        return;
    }
    validNumber(vm, value);
    if (key.isNull())
        fail(vm, "null JSON member name");
    const auto name = vm.object(key).text;
    const auto found = data.indices.find(name);
    if (found != data.indices.end())
        data.values[found->second].second = value;
    else {
        if (data.values.size() >= vm.limits().arrayElements)
            throw Error(ErrorCode::limit_exceeded, "JSON member limit exceeded");
        data.indices.emplace(name, data.values.size());
        data.values.emplace_back(name, value);
        ++data.changes;
    }
}
void append(Runtime& vm, Value array, Value value) {
    auto& values = vm.object(array).elements;
    if (values.size() >= vm.limits().arrayElements)
        throw Error(ErrorCode::limit_exceeded, "JSON array limit exceeded");
    values.push_back(value);
}
Value import(Runtime& vm, const JsonValue& json, Value null) {
    if (std::holds_alternative<std::nullptr_t>(json.data))
        return null;
    if (const auto* value = std::get_if<bool>(&json.data))
        return boxed(vm, "Ljava/lang/Boolean;", Value::integer(*value));
    if (const auto* value = std::get_if<std::int64_t>(&json.data))
        return *value >= INT32_MIN && *value <= INT32_MAX
                   ? boxed(vm, "Ljava/lang/Integer;", Value::integer(static_cast<std::int32_t>(*value)))
                   : boxed(vm, "Ljava/lang/Long;", Value::wide(*value));
    if (const auto* value = std::get_if<double>(&json.data))
        return boxed(vm, "Ljava/lang/Double;", Value::floating(*value));
    if (const auto* value = std::get_if<std::u16string>(&json.data))
        return vm.string(*value);
    if (const auto* values = std::get_if<JsonValue::Array>(&json.data)) {
        const auto result = vm.allocate(arrayType);
        for (const auto& value : *values)
            append(vm, result, import(vm, value, null));
        return result;
    }
    const auto result = vm.allocate(objectType);
    for (const auto& [name, value] : std::get<JsonValue::Object>(json.data))
        put(vm, result, vm.string(name), import(vm, value, null));
    return result;
}
class Export {
  public:
    explicit Export(Runtime& vm) : vm_(vm) {}
    JsonValue value(Value ref, unsigned depth = 0) {
        if (depth > 128 || ++nodes_ > 1000000)
            fail(vm_, "JSON complexity limit exceeded");
        if (ref.isNull())
            return {};
        const auto type = vm_.object(ref).type;
        if (type == nullType)
            return {};
        if (type == "Ljava/lang/String;")
            return JsonValue{vm_.object(ref).text};
        if (type == "Ljava/lang/Boolean;")
            return JsonValue{vm_.object(ref).fields["value"].bits != 0};
        if (type == "Ljava/lang/Long;")
            return JsonValue{vm_.object(ref).fields["value"].i64()};
        if (type == "Ljava/lang/Integer;" || type == "Ljava/lang/Short;" || type == "Ljava/lang/Byte;")
            return JsonValue{static_cast<std::int64_t>(vm_.object(ref).fields["value"].i32())};
        if (const auto number = numeric(vm_, ref, false))
            return JsonValue{*number};
        if (vm_.assignable(type, objectType) || vm_.assignable(type, arrayType)) {
            if (!active_.insert(ref.bits).second)
                fail(vm_, "cyclic JSON graph");
            JsonValue result;
            if (vm_.assignable(type, arrayType)) {
                JsonValue::Array values;
                const auto source = vm_.object(ref).elements;
                for (const auto element : source)
                    values.push_back(value(element, depth + 1));
                result.data = std::move(values);
            } else {
                JsonValue::Object values;
                const auto source = members(vm_, ref).values;
                for (const auto& [name, element] : source)
                    values.emplace_back(name, value(element, depth + 1));
                result.data = std::move(values);
            }
            active_.erase(ref.bits);
            return result;
        }
        return JsonValue{utf16(javaString(vm_, ref))};
    }

  private:
    Runtime& vm_;
    std::size_t nodes_ = 0;
    std::unordered_set<std::uint64_t> active_;
};
Value encoded(Runtime& vm, Value value, unsigned indent) {
    try {
        return vm.string(stringifyJson(Export(vm).value(value), indent));
    } catch (const Error& error) {
        if (error.code() != ErrorCode::malformed_json)
            throw;
        fail(vm, error.what());
    }
}
template <class T> T narrow(double value) {
    if (std::isnan(value))
        return 0;
    if (value >= static_cast<double>(std::numeric_limits<T>::max()))
        return std::numeric_limits<T>::max();
    if (value <= static_cast<double>(std::numeric_limits<T>::min()))
        return std::numeric_limits<T>::min();
    return static_cast<T>(value);
}
std::optional<Value> convert(Runtime& vm, Value value, std::string_view kind) {
    if (value.isNull())
        return {};
    const auto type = vm.object(value).type;
    if (kind == "String") {
        if (type == "Ljava/lang/String;")
            return value;
        return vm.string(javaString(vm, value));
    }
    if (kind == "JSONObject" || kind == "JSONArray")
        return vm.assignable(type, kind == "JSONObject" ? objectType : arrayType)
                   ? std::optional<Value>(value)
                   : std::nullopt;
    if (kind == "Boolean") {
        if (type == "Ljava/lang/Boolean;")
            return vm.object(value).fields["value"];
        if (type == "Ljava/lang/String;") {
            auto text = vm.text(value);
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
            });
            if (text == "true" || text == "false")
                return Value::integer(text == "true");
        }
        return {};
    }
    if (const auto number = numeric(vm, value)) {
        if (kind == "Double")
            return Value::floating(*number);
        // Number.longValue/intValue preserve integer bits; string coercion uses double.
        const auto stored = vm.object(value).fields["value"];
        if (type == "Ljava/lang/Long;" || type == "Ljava/lang/Integer;" || type == "Ljava/lang/Short;" ||
            type == "Ljava/lang/Byte;") {
            const auto integer = type == "Ljava/lang/Long;" ? stored.i64() : std::int64_t(stored.i32());
            return kind == "Long" ? Value::wide(integer) : Value{static_cast<std::uint32_t>(integer), false};
        }
        return kind == "Long" ? Value::wide(narrow<std::int64_t>(*number))
                              : Value::integer(narrow<std::int32_t>(*number));
    }
    return {};
}
Value primitive(Runtime& vm, Arguments args, std::size_t at, char type) {
    if (type == 'I')
        return boxed(vm, "Ljava/lang/Integer;", args[at]);
    if (type == 'J')
        return boxed(vm, "Ljava/lang/Long;", Value{argumentWide(args, at), false});
    if (type == 'D')
        return boxed(vm, "Ljava/lang/Double;", Value{argumentWide(args, at), false});
    if (type == 'Z')
        return boxed(vm, "Ljava/lang/Boolean;", Value::integer(args[at].bits != 0));
    return args[at];
}
} // namespace

void installJavaJson(Runtime& vm) {
    vm.defineClass("Lorg/json/JSONException;", "Ljava/lang/Exception;");
    vm.defineClass(objectType);
    vm.defineClass(arrayType);
    vm.defineClass(nullType);
    vm.defineClass(iteratorType, "Ljava/lang/Object;", {"Ljava/util/Iterator;"});
    const auto null = vm.allocate(nullType);
    vm.putStatic({objectType, "NULL", "Ljava/lang/Object;"}, null);
    vm.bind(nullType, "equals", "(Ljava/lang/Object;)Z",
            [](Runtime&, Arguments a) { return Value::integer(a[1].isNull() || a[0] == a[1]); });
    vm.bind(nullType, "hashCode", "()I", [](Runtime&, Arguments) { return Value::integer(0); });
    vm.bind(nullType, "toString", "()Ljava/lang/String;",
            [](Runtime& r, Arguments) { return r.string("null"); });
    for (const std::string owner : {objectType, arrayType}) {
        const bool array = owner == arrayType;
        const std::string key = array ? "I" : "Ljava/lang/String;";
        vm.bind(owner, "<init>", "()V", [array](Runtime& r, Arguments a) {
            if (array)
                r.object(a[0]).elements.clear();
            else
                r.object(a[0]).native = std::make_shared<Members>();
            return Value{};
        });
        vm.bind(owner, "<init>", "(Ljava/lang/String;)V", [null, array](Runtime& r, Arguments a) {
            if (a[1].isNull())
                r.raise("Ljava/lang/NullPointerException;", "null JSON string");
            try {
                const auto tree = parseJson(utf8Replacing(r.object(a[1]).text));
                if (array != std::holds_alternative<JsonValue::Array>(tree.data) ||
                    (!array && !std::holds_alternative<JsonValue::Object>(tree.data)))
                    fail(r, "JSON root has wrong type");
                const auto value = import(r, tree, null);
                if (array)
                    r.object(a[0]).elements = std::move(r.object(value).elements);
                else
                    r.object(a[0]).native = r.object(value).native;
                return Value{};
            } catch (const Error& error) {
                if (error.code() != ErrorCode::malformed_json)
                    throw;
                fail(r, error.what());
            }
        });
        vm.bind(owner, "length", "()I", [array](Runtime& r, Arguments a) {
            return Value::integer(static_cast<std::int32_t>(array ? r.object(a[0]).elements.size()
                                                                  : members(r, a[0]).values.size()));
        });
        vm.bind(owner, "get", "(" + key + ")Ljava/lang/Object;", [](Runtime& r, Arguments a) {
            const auto value = lookup(r, a[0], a[1]);
            if (value.isNull())
                fail(r, "missing JSON value");
            return value;
        });
        vm.bind(owner, "opt", "(" + key + ")Ljava/lang/Object;",
                [](Runtime& r, Arguments a) { return lookup(r, a[0], a[1]); });
        vm.bind(owner, "isNull", "(" + key + ")Z", [null](Runtime& r, Arguments a) {
            const auto value = lookup(r, a[0], a[1]);
            return Value::integer(value.isNull() || value == null);
        });
        vm.bind(owner, "remove", "(" + key + ")Ljava/lang/Object;",
                [](Runtime& r, Arguments a) { return erase(r, a[0], a[1]); });
        vm.bind(owner, "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
            try {
                return encoded(r, a[0], 0);
            } catch (const GuestException& error) {
                if (!r.assignable(r.object(error.value).type, "Lorg/json/JSONException;"))
                    throw;
                return Value{};
            }
        });
        vm.bind(owner, "toString", "(I)Ljava/lang/String;", [](Runtime& r, Arguments a) {
            return encoded(r, a[0], static_cast<unsigned>(std::max(0, a[1].i32())));
        });
        for (const std::string kind :
             {"String", "Int", "Long", "Double", "Boolean", "JSONObject", "JSONArray"}) {
            const std::string type = kind == "String"    ? "Ljava/lang/String;"
                                     : kind == "Int"     ? "I"
                                     : kind == "Long"    ? "J"
                                     : kind == "Double"  ? "D"
                                     : kind == "Boolean" ? "Z"
                                                         : "Lorg/json/" + kind + ";";
            vm.bind(owner, "get" + kind, "(" + key + ")" + type, [kind](Runtime& r, Arguments a) {
                const auto result = convert(r, lookup(r, a[0], a[1]), kind);
                if (!result)
                    fail(r, "JSON value cannot be converted to " + kind);
                return *result;
            });
            vm.bind(owner, "opt" + kind, "(" + key + ")" + type, [kind](Runtime& r, Arguments a) {
                const auto result = convert(r, lookup(r, a[0], a[1]), kind);
                if (result)
                    return *result;
                if (kind == "String")
                    return r.string("");
                if (kind == "Double")
                    return Value::floating(std::numeric_limits<double>::quiet_NaN());
                return Value{};
            });
            if (kind != "JSONObject" && kind != "JSONArray")
                vm.bind(owner, "opt" + kind, "(" + key + type + ")" + type, [kind](Runtime& r, Arguments a) {
                    const auto result = convert(r, lookup(r, a[0], a[1]), kind);
                    return result.value_or(
                        kind == "Long" || kind == "Double" ? Value{argumentWide(a, 2), false} : a[2]);
                });
        }
        for (const std::string type : {"I", "J", "D", "Z", "Ljava/lang/Object;"}) {
            vm.bind(owner, "put", "(" + (array ? std::string() : key) + type + ")" + owner,
                    [array, type](Runtime& r, Arguments a) {
                        const auto value = primitive(r, a, array ? 1 : 2, type[0]);
                        if (array) {
                            // Android permits non-finite boxed objects on the append(Object) path.
                            if (type[0] == 'D')
                                validNumber(r, value);
                            append(r, a[0], value);
                        } else
                            put(r, a[0], a[1], value);
                        return a[0];
                    });
            if (array)
                vm.bind(owner, "put", "(I" + type + ")" + owner, [type](Runtime& r, Arguments a) {
                    const auto index = a[1].i32();
                    if (index < 0)
                        fail(r, "negative JSON array index");
                    if (static_cast<std::size_t>(index) >= r.limits().arrayElements)
                        throw Error(ErrorCode::limit_exceeded, "JSON array limit exceeded");
                    const auto value = primitive(r, a, 2, type[0]);
                    validNumber(r, value);
                    auto& values = r.object(a[0]).elements;
                    if (values.size() <= static_cast<std::size_t>(index))
                        values.resize(static_cast<std::size_t>(index) + 1);
                    values[static_cast<std::size_t>(index)] = value;
                    return a[0];
                });
        }
    }
    vm.bind(objectType, "has", "(Ljava/lang/String;)Z",
            [](Runtime& r, Arguments a) { return Value::integer(!lookup(r, a[0], a[1]).isNull()); });
    vm.bind(objectType, "putOpt", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;",
            [](Runtime& r, Arguments a) {
                if (!a[1].isNull() && !a[2].isNull())
                    put(r, a[0], a[1], a[2]);
                return a[0];
            });
    vm.bind(objectType, "names", "()Lorg/json/JSONArray;", [](Runtime& r, Arguments a) {
        const auto values = members(r, a[0]).values;
        if (values.empty())
            return Value{};
        const auto result = r.allocate(arrayType);
        for (const auto& [name, value] : values)
            append(r, result, r.string(name));
        return result;
    });
    vm.bind(objectType, "keys", "()Ljava/util/Iterator;", [](Runtime& r, Arguments a) {
        const auto result = r.allocate(iteratorType);
        auto& fields = r.object(result).fields;
        fields["source"] = a[0];
        fields["changes"] = Value{members(r, a[0]).changes, false};
        fields["position"] = Value::integer(0);
        fields["last"] = Value::integer(-1);
        return result;
    });
    for (const std::string method : {"hasNext", "next", "remove"})
        vm.bind(iteratorType, method,
                method == "hasNext" ? "()Z"
                : method == "next"  ? "()Ljava/lang/Object;"
                                    : "()V",
                [method](Runtime& r, Arguments a) {
                    auto& fields = r.object(a[0]).fields;
                    const auto source = fields["source"];
                    auto& data = members(r, source);
                    if (fields["changes"].bits != data.changes && method != "hasNext")
                        r.raise("Ljava/util/ConcurrentModificationException;", "JSON members changed");
                    auto& position = fields["position"];
                    if (method == "hasNext")
                        return Value::integer(position.bits < data.values.size());
                    if (method == "next") {
                        if (position.bits >= data.values.size())
                            r.raise("Ljava/util/NoSuchElementException;", "no more JSON member names");
                        fields["last"] = position;
                        return r.string(data.values[position.bits++].first);
                    }
                    const auto last = fields["last"].i32();
                    if (last < 0)
                        r.raise("Ljava/lang/IllegalStateException;", "next was not called");
                    (void)erase(r, source, r.string(data.values[static_cast<std::size_t>(last)].first));
                    fields["last"] = Value::integer(-1);
                    --position.bits;
                    fields["changes"] = Value{data.changes, false};
                    return Value{};
                });
    vm.bind(objectType, "quote", "(Ljava/lang/String;)Ljava/lang/String;", [](Runtime& r, Arguments a) {
        return r.string(stringifyJson(JsonValue{a[0].isNull() ? std::u16string() : r.object(a[0]).text}));
    });
    vm.bind(objectType, "numberToString", "(Ljava/lang/Number;)Ljava/lang/String;",
            [](Runtime& r, Arguments a) {
                if (a[0].isNull())
                    fail(r, "null JSON number");
                validNumber(r, a[0]);
                return encoded(r, a[0], 0);
            });
}
} // namespace gutenberg
