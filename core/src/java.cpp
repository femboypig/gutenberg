#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace gutenberg {

std::uint64_t argumentWide(Arguments args, std::size_t i) {
    if (i >= args.size() || args.size() - i < 2)
        throw Error(ErrorCode::verification, "missing wide argument");
    return (args[i].bits & 0xffffffff) | ((args[i + 1].bits & 0xffffffff) << 32);
}
std::string javaString(Runtime& vm, Value value) {
    if (value.isNull())
        return "null";
    const Value args[]{value};
    const auto result = vm.invoke(vm.object(value).type, "toString", "()Ljava/lang/String;", args);
    return vm.text(result);
}

namespace {
Value nothing(Runtime&, Arguments) {
    return {};
}
std::pair<std::size_t, std::size_t> range(Runtime& vm, std::int32_t start, std::int32_t end,
                                          std::size_t size) {
    if (start < 0 || end < start || static_cast<std::size_t>(end) > size)
        vm.raise("Ljava/lang/IndexOutOfBoundsException;", "invalid range");
    return {static_cast<std::size_t>(start), static_cast<std::size_t>(end)};
}
std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
}
} // namespace

void installJavaRuntime(Runtime& vm, JavaServices services) {
    vm.defineClass("Ljava/lang/Object;", "");
    for (const auto* name :
         {"java/lang/Cloneable", "java/io/Serializable", "java/lang/CharSequence", "java/lang/Comparable",
          "java/lang/Runnable", "java/lang/Iterable", "java/util/Map", "java/util/Iterator"})
        vm.defineClass("L" + std::string(name) + ";");
    vm.defineClass("Ljava/util/Collection;", "Ljava/lang/Object;", {"Ljava/lang/Iterable;"});
    vm.defineClass("Ljava/util/List;", "Ljava/lang/Object;", {"Ljava/util/Collection;"});
    vm.defineClass("Ljava/util/Set;", "Ljava/lang/Object;", {"Ljava/util/Collection;"});
    vm.defineClass("Ljava/lang/Class;");
    vm.defineClass("Ljava/lang/String;", "Ljava/lang/Object;",
                   {"Ljava/lang/CharSequence;", "Ljava/lang/Comparable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/lang/StringBuilder;", "Ljava/lang/Object;", {"Ljava/lang/CharSequence;"});
    vm.defineClass("Ljava/lang/Number;");
    for (const auto* type : {"Integer", "Long", "Float", "Double", "Short", "Byte"})
        vm.defineClass("Ljava/lang/" + std::string(type) + ";", "Ljava/lang/Number;");
    vm.defineClass("Ljava/lang/Boolean;");
    vm.defineClass("Ljava/lang/Character;");
    for (const auto* type : {"System", "Math", "StrictMath"})
        vm.defineClass("Ljava/lang/" + std::string(type) + ";");
    vm.defineClass("Ljava/io/PrintStream;");
    const std::pair<const char*, const char*> exceptions[] = {
        {"Throwable", "Object"},
        {"Exception", "Throwable"},
        {"RuntimeException", "Exception"},
        {"Error", "Throwable"},
        {"LinkageError", "Error"},
        {"VirtualMachineError", "Error"},
        {"StackOverflowError", "VirtualMachineError"},
        {"OutOfMemoryError", "VirtualMachineError"},
        {"IncompatibleClassChangeError", "LinkageError"},
        {"NoSuchMethodError", "IncompatibleClassChangeError"},
        {"NoSuchFieldError", "IncompatibleClassChangeError"},
        {"AbstractMethodError", "IncompatibleClassChangeError"},
        {"UnsatisfiedLinkError", "LinkageError"},
        {"NoClassDefFoundError", "LinkageError"},
        {"ExceptionInInitializerError", "LinkageError"},
        {"AssertionError", "Error"},
        {"NullPointerException", "RuntimeException"},
        {"ArithmeticException", "RuntimeException"},
        {"IllegalArgumentException", "RuntimeException"},
        {"IllegalStateException", "RuntimeException"},
        {"IndexOutOfBoundsException", "RuntimeException"},
        {"ArrayIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"StringIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"ClassCastException", "RuntimeException"},
        {"ArrayStoreException", "RuntimeException"},
        {"NegativeArraySizeException", "RuntimeException"},
        {"UnsupportedOperationException", "RuntimeException"},
        {"SecurityException", "RuntimeException"},
        {"NumberFormatException", "IllegalArgumentException"},
        {"ClassNotFoundException", "Exception"},
        {"InterruptedException", "Exception"},
        {"CloneNotSupportedException", "Exception"}};
    for (const auto& [name, parent] : exceptions) {
        const auto owner = "Ljava/lang/" + std::string(name) + ";";
        vm.defineClass(owner, "Ljava/lang/" + std::string(parent) + ";");
        vm.bind(owner, "<init>", "()V", nothing);
        vm.bind(owner, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
            r.object(a[0]).text = a[1].isNull() ? u"" : r.object(a[1]).text;
            return Value{};
        });
        vm.bind(owner, "<init>", "(Ljava/lang/Throwable;)V", [](Runtime& r, Arguments a) {
            r.object(a[0]).fields["cause"] = a[1];
            return Value{};
        });
    }
    vm.bind("Ljava/lang/Throwable;", "getMessage", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    vm.bind("Ljava/lang/Throwable;", "toString", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).type + ": " + r.text(a[0])); });

    vm.bind("Ljava/lang/Object;", "<init>", "()V", nothing);
    vm.bind("Ljava/lang/Object;", "equals", "(Ljava/lang/Object;)Z",
            [](Runtime&, Arguments a) { return Value::integer(a[0] == a[1]); });
    vm.bind("Ljava/lang/Object;", "hashCode", "()I", [](Runtime&, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(a[0].bits & 0x7fffffff));
    });
    vm.bind("Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;",
            [](Runtime& r, Arguments a) { return r.classObject(r.object(a[0]).type); });
    vm.bind("Ljava/lang/Object;", "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        return r.string(r.object(a[0]).type + "@" + std::to_string(a[0].bits));
    });
    vm.bind("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto copy = r.object(a[0]);
        if (!r.assignable(copy.type, "Ljava/lang/Cloneable;"))
            r.raise("Ljava/lang/CloneNotSupportedException;", copy.type);
        const auto result = copy.type.starts_with('[')
                                ? r.array(copy.type, static_cast<std::int32_t>(copy.elements.size()))
                                : r.allocate(copy.type);
        r.object(result) = copy;
        return result;
    });
    vm.bind("Ljava/lang/Class;", "getName", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        auto name = r.text(a[0]);
        if (name.size() > 2 && name[0] == 'L')
            name = name.substr(1, name.size() - 2);
        std::replace(name.begin(), name.end(), '/', '.');
        return r.string(name);
    });
    vm.bind("Ljava/lang/Class;", "isInstance", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(!a[1].isNull() && r.assignable(r.object(a[1]).type, r.text(a[0])));
    });
    vm.bind("Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;",
            [](Runtime& r, Arguments a) {
                auto name = r.text(a[0]);
                std::replace(name.begin(), name.end(), '.', '/');
                name = "L" + name + ";";
                if (!r.hasClass(name))
                    r.raise("Ljava/lang/ClassNotFoundException;", name);
                r.initialize(name);
                return r.classObject(name);
            });

    const std::string str = "Ljava/lang/String;";
    vm.bind(str, "<init>", "()V", nothing);
    vm.bind(str, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).text = r.object(a[1]).text;
        return Value{};
    });
    vm.bind(str, "<init>", "([C)V", [](Runtime& r, Arguments a) {
        std::u16string text;
        for (auto value : r.object(a[1]).elements)
            text += static_cast<char16_t>(value.bits);
        r.object(a[0]).text = std::move(text);
        return Value{};
    });
    vm.bind(str, "<init>", "([B)V", [](Runtime& r, Arguments a) {
        std::string text;
        for (auto value : r.object(a[1]).elements)
            text += static_cast<char>(value.bits & 0xff);
        r.object(a[0]).text = utf16Replacing(text);
        return Value{};
    });
    vm.bind(str, "toString", "()Ljava/lang/String;", [](Runtime&, Arguments a) { return a[0]; });
    vm.bind(str, "length", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(r.object(a[0]).text.size()));
    });
    vm.bind(str, "isEmpty", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(r.object(a[0]).text.empty()); });
    vm.bind(str, "charAt", "(I)C", [](Runtime& r, Arguments a) {
        const auto& text = r.object(a[0]).text;
        const auto [start, end] = range(r, a[1].i32(), a[1].i32(), text.size());
        (void)end;
        if (start == text.size())
            r.raise("Ljava/lang/StringIndexOutOfBoundsException;", "charAt");
        return Value::integer(text[start]);
    });
    vm.bind(str, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(!a[1].isNull() && r.object(a[1]).type == "Ljava/lang/String;" &&
                              r.object(a[0]).text == r.object(a[1]).text);
    });
    vm.bind(str, "hashCode", "()I", [](Runtime& r, Arguments a) {
        std::uint32_t hash = 0;
        for (auto c : r.object(a[0]).text)
            hash = hash * 31 + c;
        return Value{hash, false};
    });
    vm.bind(str, "substring", "(I)Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto& text = r.object(a[0]).text;
        const auto [start, end] = range(r, a[1].i32(), static_cast<std::int32_t>(text.size()), text.size());
        return r.string(text.substr(start, end - start));
    });
    vm.bind(str, "substring", "(II)Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto& text = r.object(a[0]).text;
        const auto [start, end] = range(r, a[1].i32(), a[2].i32(), text.size());
        return r.string(text.substr(start, end - start));
    });
    vm.bind(str, "concat", "(Ljava/lang/String;)Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text + r.object(a[1]).text); });
    vm.bind(str, "startsWith", "(Ljava/lang/String;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(r.object(a[0]).text.starts_with(r.object(a[1]).text));
    });
    vm.bind(str, "endsWith", "(Ljava/lang/String;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(r.object(a[0]).text.ends_with(r.object(a[1]).text));
    });
    vm.bind(str, "contains", "(Ljava/lang/CharSequence;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(r.object(a[0]).text.find(r.object(a[1]).text) != std::u16string::npos);
    });
    vm.bind(str, "indexOf", "(Ljava/lang/String;)I", [](Runtime& r, Arguments a) {
        const auto at = r.object(a[0]).text.find(r.object(a[1]).text);
        return Value::integer(at == std::u16string::npos ? -1 : static_cast<std::int32_t>(at));
    });
    vm.bind(str, "indexOf", "(I)I", [](Runtime& r, Arguments a) {
        const auto at = r.object(a[0]).text.find(static_cast<char16_t>(a[1].bits));
        return Value::integer(at == std::u16string::npos ? -1 : static_cast<std::int32_t>(at));
    });
    vm.bind(str, "toCharArray", "()[C", [](Runtime& r, Arguments a) {
        const auto text = r.object(a[0]).text;
        auto result = r.array("[C", static_cast<std::int32_t>(text.size()));
        for (std::size_t n = 0; n < text.size(); ++n)
            r.object(result).elements[n] = Value::integer(text[n]);
        return result;
    });
    vm.bind(str, "getBytes", "()[B", [](Runtime& r, Arguments a) {
        const auto text = utf8Replacing(r.object(a[0]).text, u'?');
        auto result = r.array("[B", static_cast<std::int32_t>(text.size()));
        for (std::size_t n = 0; n < text.size(); ++n)
            r.object(result).elements[n] =
                Value::integer(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(text[n])));
        return result;
    });
    vm.bind(str, "intern", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text, true); });
    vm.bind(str, "valueOf", "(Ljava/lang/Object;)Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(javaString(r, a[0])); });
    vm.bind(str, "valueOf", "(I)Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(std::to_string(a[0].i32())); });
    vm.bind(str, "valueOf", "(J)Ljava/lang/String;", [](Runtime& r, Arguments a) {
        return r.string(std::to_string(std::bit_cast<std::int64_t>(argumentWide(a, 0))));
    });
    vm.bind(str, "valueOf", "(Z)Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(a[0].bits ? "true" : "false"); });
    vm.bind(str, "valueOf", "(C)Ljava/lang/String;", [](Runtime& r, Arguments a) {
        return r.string(std::u16string(1, static_cast<char16_t>(a[0].bits)));
    });

    const std::string builder = "Ljava/lang/StringBuilder;";
    vm.bind(builder, "<init>", "()V", nothing);
    vm.bind(builder, "<init>", "(I)V", nothing);
    vm.bind(builder, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).text = r.object(a[1]).text;
        return Value{};
    });
    vm.bind(builder, "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        const auto text = a[1].isNull() ? u"null" : r.object(a[1]).text;
        r.object(a[0]).text += text;
        return a[0];
    });
    vm.bind(builder, "append", "(Ljava/lang/Object;)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        const auto text = utf16(javaString(r, a[1]));
        r.object(a[0]).text += text;
        return a[0];
    });
    vm.bind(builder, "append", "(I)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += utf16(std::to_string(a[1].i32()));
        return a[0];
    });
    vm.bind(builder, "append", "(J)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += utf16(std::to_string(std::bit_cast<std::int64_t>(argumentWide(a, 1))));
        return a[0];
    });
    vm.bind(builder, "append", "(Z)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += a[1].bits ? u"true" : u"false";
        return a[0];
    });
    vm.bind(builder, "append", "(C)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += static_cast<char16_t>(a[1].bits);
        return a[0];
    });
    vm.bind(builder, "append", "(F)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += utf16(number(a[1].f32()));
        return a[0];
    });
    vm.bind(builder, "append", "(D)Ljava/lang/StringBuilder;", [](Runtime& r, Arguments a) {
        r.object(a[0]).text += utf16(number(std::bit_cast<double>(argumentWide(a, 1))));
        return a[0];
    });
    vm.bind(builder, "length", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(r.object(a[0]).text.size()));
    });
    vm.bind(builder, "toString", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });

    const auto output = vm.allocate("Ljava/io/PrintStream;");
    vm.putStatic({"Ljava/lang/System;", "out", "Ljava/io/PrintStream;"}, output);
    vm.putStatic({"Ljava/lang/System;", "err", "Ljava/io/PrintStream;"}, output);
    for (const auto* name : {"print", "println"}) {
        vm.bind("Ljava/io/PrintStream;", name, "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
            const auto text = r.text(a[1]);
            if (r.logger)
                r.logger(text);
            return Value{};
        });
        vm.bind("Ljava/io/PrintStream;", name, "(I)V", [](Runtime& r, Arguments a) {
            const auto text = std::to_string(a[1].i32());
            if (r.logger)
                r.logger(text);
            return Value{};
        });
        vm.bind("Ljava/io/PrintStream;", name, "(Ljava/lang/Object;)V", [](Runtime& r, Arguments a) {
            const auto text = javaString(r, a[1]);
            if (r.logger)
                r.logger(text);
            return Value{};
        });
    }
    vm.bind("Ljava/io/PrintStream;", "println", "()V", [](Runtime& r, Arguments) {
        if (r.logger)
            r.logger("");
        return Value{};
    });
    vm.bind("Ljava/lang/System;", "currentTimeMillis", "()J", [](Runtime&, Arguments) {
        return Value::wide(std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count());
    });
    vm.bind("Ljava/lang/System;", "nanoTime", "()J", [](Runtime&, Arguments) {
        return Value::wide(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count());
    });
    vm.bind("Ljava/lang/System;", "identityHashCode", "(Ljava/lang/Object;)I", [](Runtime&, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(a[0].bits & 0x7fffffff));
    });
    vm.bind("Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V",
            [](Runtime& r, Arguments a) {
                const auto& source = r.object(a[0]);
                auto& destination = r.object(a[2]);
                if (source.type.empty() || destination.type.empty() || source.type[0] != '[' ||
                    destination.type[0] != '[')
                    r.raise("Ljava/lang/ArrayStoreException;", "arraycopy requires arrays");
                const auto count = a[4].i32(), src = a[1].i32(), dst = a[3].i32();
                if (count < 0 || src < 0 || dst < 0 ||
                    std::uint64_t(src) + std::uint32_t(count) > source.elements.size() ||
                    std::uint64_t(dst) + std::uint32_t(count) > destination.elements.size())
                    r.raise("Ljava/lang/IndexOutOfBoundsException;", "arraycopy range");
                const bool sourceRef = source.type[1] == 'L' || source.type[1] == '[';
                const bool destinationRef = destination.type[1] == 'L' || destination.type[1] == '[';
                if (sourceRef != destinationRef || (!sourceRef && source.type != destination.type))
                    r.raise("Ljava/lang/ArrayStoreException;", "incompatible arrays");
                const std::vector<Value> copy(source.elements.begin() + src,
                                              source.elements.begin() + src + count);
                for (std::int32_t n = 0; n < count; ++n) {
                    const auto value = copy[static_cast<std::size_t>(n)];
                    if (destinationRef && !value.isNull() &&
                        !r.assignable(r.object(value).type, std::string_view(destination.type).substr(1)))
                        r.raise("Ljava/lang/ArrayStoreException;", "incompatible array element");
                    destination.elements[static_cast<std::size_t>(dst + n)] = value;
                }
                return Value{};
            });
    for (const auto* math : {"Ljava/lang/Math;", "Ljava/lang/StrictMath;"}) {
        vm.bind(math, "abs", "(I)I", [](Runtime&, Arguments a) {
            const auto n = static_cast<std::uint32_t>(a[0].bits);
            return Value{a[0].i32() < 0 ? std::uint32_t(0) - n : n, false};
        });
        vm.bind(math, "min", "(II)I",
                [](Runtime&, Arguments a) { return Value::integer(std::min(a[0].i32(), a[1].i32())); });
        vm.bind(math, "max", "(II)I",
                [](Runtime&, Arguments a) { return Value::integer(std::max(a[0].i32(), a[1].i32())); });
        vm.bind(math, "sqrt", "(D)D", [](Runtime&, Arguments a) {
            return Value::floating(std::sqrt(std::bit_cast<double>(argumentWide(a, 0))));
        });
        vm.bind(math, "pow", "(DD)D", [](Runtime&, Arguments a) {
            return Value::floating(std::pow(std::bit_cast<double>(argumentWide(a, 0)),
                                            std::bit_cast<double>(argumentWide(a, 2))));
        });
    }
    for (const auto* wrapper :
         {"Integer", "Long", "Float", "Double", "Short", "Byte", "Boolean", "Character"}) {
        const std::string name(wrapper), owner = "Ljava/lang/" + name + ";";
        const std::string type = name == "Integer"   ? "I"
                                 : name == "Long"    ? "J"
                                 : name == "Float"   ? "F"
                                 : name == "Double"  ? "D"
                                 : name == "Short"   ? "S"
                                 : name == "Byte"    ? "B"
                                 : name == "Boolean" ? "Z"
                                                     : "C";
        const bool wide = type == "J" || type == "D";
        vm.bind(owner, "valueOf", "(" + type + ")" + owner, [owner, wide](Runtime& r, Arguments a) {
            const auto value = r.allocate(owner);
            r.object(value).fields["value"] = wide ? Value{argumentWide(a, 0), false} : a[0];
            return value;
        });
        vm.bind(owner, "<init>", "(" + type + ")V", [wide](Runtime& r, Arguments a) {
            r.object(a[0]).fields["value"] = wide ? Value{argumentWide(a, 1), false} : a[1];
            return Value{};
        });
        const std::string method =
            name == "Integer"     ? "intValue"
            : name == "Character" ? "charValue"
            : name == "Boolean"
                ? "booleanValue"
                : std::string(1, static_cast<char>(std::tolower(wrapper[0]))) + name.substr(1) + "Value";
        vm.bind(owner, method, "()" + type,
                [](Runtime& r, Arguments a) { return r.object(a[0]).fields["value"]; });
        vm.bind(owner, "equals", "(Ljava/lang/Object;)Z", [owner](Runtime& r, Arguments a) {
            return Value::integer(!a[1].isNull() && r.object(a[1]).type == owner &&
                                  r.object(a[0]).fields["value"] == r.object(a[1]).fields["value"]);
        });
        vm.bind(owner, "toString", "()Ljava/lang/String;", [type](Runtime& r, Arguments a) {
            const auto value = r.object(a[0]).fields["value"];
            if (type == "Z")
                return r.string(value.bits ? "true" : "false");
            if (type == "C")
                return r.string(std::u16string(1, static_cast<char16_t>(value.bits)));
            if (type == "F" || type == "D")
                return r.string(number(type == "F" ? double(value.f32()) : value.f64()));
            return r.string(std::to_string(type == "J" ? value.i64() : std::int64_t(value.i32())));
        });
    }
    installJavaCollections(vm);
    installJavaNumbers(vm);
    installJavaJson(vm);
    installJavaIo(vm, services);
    installJavaNetwork(vm, std::move(services));
}

} // namespace gutenberg
