#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>

namespace gutenberg {
namespace {
bool primitive(std::string_view type) {
    return type.size() == 1 && std::string_view("VZBCSIJFD").find(type[0]) != std::string_view::npos;
}
std::string component(Runtime& vm, Value mirror) {
    const auto type = vm.text(mirror);
    if (type.empty() || type == "V" || vm.object(mirror).type != "Ljava/lang/Class;")
        vm.raise("Ljava/lang/IllegalArgumentException;", "invalid array component class");
    return type;
}
Value create(Runtime& vm, const std::string& componentType, const std::vector<std::int32_t>& dimensions,
             std::size_t depth = 0) {
    const auto type = std::string(dimensions.size() - depth, '[') + componentType;
    const auto result = vm.array(type, dimensions[depth]);
    if (depth + 1 < dimensions.size())
        for (auto& value : vm.object(result).elements)
            value = create(vm, componentType, dimensions, depth + 1);
    return result;
}
void validateDimensions(Runtime& vm, std::string_view componentType,
                        const std::vector<std::int32_t>& dimensions) {
    const auto existing = componentType.find_first_not_of('[');
    if (dimensions.empty() || dimensions.size() + (existing == componentType.npos ? 0 : existing) > 255)
        vm.raise("Ljava/lang/IllegalArgumentException;", "invalid array dimension count");
    std::size_t arrays = 1, elements = 0;
    for (const auto dimension : dimensions) {
        if (dimension < 0)
            vm.raise("Ljava/lang/NegativeArraySizeException;", "negative array dimension");
        if (dimension && arrays > vm.limits().arrayElements / static_cast<std::size_t>(dimension))
            throw Error(ErrorCode::limit_exceeded, "multidimensional array storage limit exceeded");
        arrays *= static_cast<std::size_t>(dimension);
        if (arrays > vm.limits().arrayElements - elements)
            throw Error(ErrorCode::limit_exceeded, "multidimensional array storage limit exceeded");
        elements += arrays;
    }
}
} // namespace

void installJavaReflection(Runtime& vm) {
    vm.bind("Ljava/lang/Class;", "isInstance", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(!a[1].isNull() && r.assignable(r.object(a[1]).type, r.text(a[0])));
    });
    vm.bind("Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;",
            [](Runtime& r, Arguments a) {
                auto name = r.text(a[0]);
                std::replace(name.begin(), name.end(), '.', '/');
                const bool array = name.starts_with('[');
                if (!array)
                    name = "L" + name + ";";
                const auto dimensions = name.find_first_not_of('[');
                const auto base = name.substr(dimensions == name.npos ? 0 : dimensions);
                const bool validArray = array && dimensions <= 255 && dimensions > 0 &&
                                        ((primitive(base) && base != "V") || r.hasClass(base));
                if (array ? !validArray : !r.hasClass(name))
                    r.raise("Ljava/lang/ClassNotFoundException;", name);
                if (!array)
                    r.initialize(name);
                return r.classObject(name);
            });
    vm.defineClass("Ljava/lang/Void;");
    const std::pair<const char*, const char*> types[] = {
        {"Void", "V"},    {"Boolean", "Z"}, {"Byte", "B"},  {"Character", "C"}, {"Short", "S"},
        {"Integer", "I"}, {"Long", "J"},    {"Float", "F"}, {"Double", "D"}};
    for (const auto& [name, type] : types)
        vm.putStatic({"Ljava/lang/" + std::string(name) + ";", "TYPE", "Ljava/lang/Class;"},
                     vm.classObject(type));
    vm.bind("Ljava/lang/Class;", "getName", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        auto name = r.text(a[0]);
        const std::pair<const char*, const char*> names[] = {
            {"V", "void"}, {"Z", "boolean"}, {"B", "byte"},  {"C", "char"},  {"S", "short"},
            {"I", "int"},  {"J", "long"},    {"F", "float"}, {"D", "double"}};
        for (const auto& [type, text] : names)
            if (name == type)
                return r.string(text);
        if (name.starts_with('L'))
            name = name.substr(1, name.size() - 2);
        std::replace(name.begin(), name.end(), '/', '.');
        return r.string(name);
    });
    vm.bind("Ljava/lang/Class;", "isPrimitive", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(primitive(r.text(a[0]))); });
    vm.bind("Ljava/lang/Class;", "isArray", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(r.text(a[0]).starts_with('[')); });
    vm.bind("Ljava/lang/Class;", "getComponentType", "()Ljava/lang/Class;", [](Runtime& r, Arguments a) {
        const auto type = r.text(a[0]);
        return type.starts_with('[') ? r.classObject(type.substr(1)) : Value{};
    });
    vm.bind("Ljava/lang/Class;", "isAssignableFrom", "(Ljava/lang/Class;)Z", [](Runtime& r, Arguments a) {
        const auto expected = r.text(a[0]), actual = r.text(a[1]);
        return Value::integer(primitive(actual) || primitive(expected) ? actual == expected
                                                                       : r.assignable(actual, expected));
    });
    constexpr auto array = "Ljava/lang/reflect/Array;";
    vm.defineClass(array);
    vm.bind(array, "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto type = component(r, a[0]);
        const std::vector<std::int32_t> dimensions{a[1].i32()};
        validateDimensions(r, type, dimensions);
        return create(r, type, dimensions);
    });
    vm.bind(array, "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto type = component(r, a[0]);
        std::vector<std::int32_t> dimensions;
        for (const auto value : r.object(a[1]).elements)
            dimensions.push_back(value.i32());
        validateDimensions(r, type, dimensions);
        return create(r, type, dimensions);
    });
    vm.bind(array, "getLength", "(Ljava/lang/Object;)I", [](Runtime& r, Arguments a) {
        const auto& object = r.object(a[0]);
        if (!object.type.starts_with('['))
            r.raise("Ljava/lang/IllegalArgumentException;", "object is not an array");
        return Value::integer(static_cast<std::int32_t>(object.elements.size()));
    });
}
} // namespace gutenberg
