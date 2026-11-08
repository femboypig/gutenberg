#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>

namespace gutenberg {
namespace {
constexpr auto iteratorType = "Lgutenberg/runtime/Iterator;";

std::vector<Value>& storage(Runtime& vm, Value value) {
    auto& object = vm.object(value);
    const auto backing = object.fields.find("backingArray");
    return backing == object.fields.end() ? object.elements : vm.object(backing->second).elements;
}
void mutableCollection(Runtime& vm, Value value, bool structural = true) {
    auto& object = vm.object(value);
    if (object.fields["immutable"].bits || (structural && object.fields["fixedSize"].bits))
        vm.raise("Ljava/lang/UnsupportedOperationException;", "collection is not mutable");
}
bool equal(Runtime& vm, Value a, Value b) {
    if (a == b)
        return true;
    if (a.isNull() || b.isNull())
        return false;
    const Value args[]{a, b};
    return vm.invoke("Ljava/lang/Object;", "equals", "(Ljava/lang/Object;)Z", args).i32() != 0;
}
std::size_t index(Runtime& vm, Value collection, Value needle, std::size_t stride = 1) {
    const auto values = storage(vm, collection);
    for (std::size_t n = 0; n < values.size(); n += stride)
        if (equal(vm, values[n], needle))
            return n;
    return values.size();
}
std::size_t position(Runtime& vm, std::int32_t value, std::size_t size, bool atEnd = false) {
    if (value < 0 || static_cast<std::size_t>(value) > size ||
        (!atEnd && static_cast<std::size_t>(value) == size))
        vm.raise("Ljava/lang/IndexOutOfBoundsException;", std::to_string(value));
    return static_cast<std::size_t>(value);
}
void changed(Object& object) {
    const auto current = static_cast<std::uint32_t>(object.fields["modCount"].bits);
    object.fields["modCount"] = {current + 1U, false};
}
std::vector<Value> collect(Runtime& vm, Value collection) {
    const Value args[]{collection};
    const auto iterator =
        vm.invoke("Ljava/lang/Iterable;", "iterator", "()Ljava/util/Iterator;", args, InvokeKind::interface_);
    const Value iteratorArgs[]{iterator};
    std::vector<Value> result;
    while (vm.invoke("Ljava/util/Iterator;", "hasNext", "()Z", iteratorArgs, InvokeKind::interface_).i32()) {
        if (result.size() >= vm.limits().arrayElements)
            throw Error(ErrorCode::limit_exceeded, "collection limit exceeded");
        result.push_back(vm.invoke("Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", iteratorArgs,
                                   InvokeKind::interface_));
    }
    return result;
}
} // namespace

void installJavaCollections(Runtime& vm) {
    vm.defineClass("Ljava/util/ConcurrentModificationException;", "Ljava/lang/RuntimeException;");
    vm.defineClass("Ljava/util/NoSuchElementException;", "Ljava/lang/RuntimeException;");
    vm.defineClass("Ljava/util/AbstractCollection;", "Ljava/lang/Object;", {"Ljava/util/Collection;"});
    vm.defineClass("Ljava/util/AbstractList;", "Ljava/util/AbstractCollection;", {"Ljava/util/List;"});
    vm.defineClass("Ljava/util/ArrayList;", "Ljava/util/AbstractList;",
                   {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/LinkedList;", "Ljava/util/AbstractList;",
                   {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/AbstractSet;", "Ljava/util/AbstractCollection;", {"Ljava/util/Set;"});
    vm.defineClass("Ljava/util/HashSet;", "Ljava/util/AbstractSet;",
                   {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/LinkedHashSet;", "Ljava/util/HashSet;");
    vm.defineClass("Ljava/util/AbstractMap;", "Ljava/lang/Object;", {"Ljava/util/Map;"});
    vm.defineClass("Ljava/util/HashMap;", "Ljava/util/AbstractMap;",
                   {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/LinkedHashMap;", "Ljava/util/HashMap;");
    vm.defineClass("Ljava/util/Map$Entry;");
    vm.defineClass(iteratorType, "Ljava/lang/Object;", {"Ljava/util/Iterator;"});
    vm.defineClass("Ljava/util/Arrays;");
    vm.defineClass("Ljava/util/Collections;");
    for (const auto* owner :
         {"Ljava/util/ArrayList;", "Ljava/util/LinkedList;", "Ljava/util/HashSet;",
          "Ljava/util/LinkedHashSet;", "Ljava/util/HashMap;", "Ljava/util/LinkedHashMap;"}) {
        vm.bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
        vm.bind(owner, "<init>", "(I)V", [](Runtime& r, Arguments a) {
            if (a[1].i32() < 0)
                r.raise("Ljava/lang/IllegalArgumentException;", "negative capacity");
            return Value{};
        });
    }
    for (const auto* owner : {"Ljava/util/ArrayList;", "Ljava/util/LinkedList;", "Ljava/util/HashSet;",
                              "Ljava/util/LinkedHashSet;"})
        vm.bind(owner, "<init>", "(Ljava/util/Collection;)V", [](Runtime& r, Arguments a) {
            auto values = collect(r, a[1]);
            for (auto value : values) {
                const Value args[]{a[0], value};
                (void)r.invoke("Ljava/util/Collection;", "add", "(Ljava/lang/Object;)Z", args,
                               InvokeKind::interface_);
            }
            return Value{};
        });
    const std::string collection = "Ljava/util/AbstractCollection;", list = "Ljava/util/AbstractList;",
                      set = "Ljava/util/AbstractSet;", map = "Ljava/util/AbstractMap;";
    vm.bind(collection, "size", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(storage(r, a[0]).size()));
    });
    vm.bind(collection, "isEmpty", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(storage(r, a[0]).empty()); });
    vm.bind(collection, "clear", "()V", [](Runtime& r, Arguments a) {
        mutableCollection(r, a[0]);
        auto& obj = r.object(a[0]);
        storage(r, a[0]).clear();
        changed(obj);
        return Value{};
    });
    vm.bind(collection, "contains", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(index(r, a[0], a[1]) < storage(r, a[0]).size());
    });
    vm.bind(collection, "remove", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        const auto at = index(r, a[0], a[1]);
        if (at >= storage(r, a[0]).size())
            return Value::integer(0);
        mutableCollection(r, a[0]);
        storage(r, a[0]).erase(storage(r, a[0]).begin() + static_cast<std::ptrdiff_t>(at));
        changed(obj);
        return Value::integer(1);
    });
    vm.bind(collection, "iterator", "()Ljava/util/Iterator;", [](Runtime& r, Arguments a) {
        const auto result = r.allocate(iteratorType);
        auto& it = r.object(result);
        it.fields["source"] = a[0];
        it.fields["expectedMod"] = r.object(a[0]).fields["modCount"];
        it.fields["position"] = Value::integer(0);
        it.fields["last"] = Value::integer(-1);
        return result;
    });
    vm.bind(collection, "toArray", "()[Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto copy = storage(r, a[0]);
        const auto result = r.array("[Ljava/lang/Object;", static_cast<std::int32_t>(copy.size()));
        r.object(result).elements = copy;
        return result;
    });
    vm.bind(collection, "toArray", "([Ljava/lang/Object;)[Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto values = storage(r, a[0]);
        auto result = a[1];
        const auto type = r.object(result).type;
        if (r.object(result).elements.size() < values.size())
            result = r.array(type, static_cast<std::int32_t>(values.size()));
        auto& out = r.object(result).elements;
        for (std::size_t n = 0; n < values.size(); ++n) {
            if (!values[n].isNull() &&
                !r.assignable(r.object(values[n]).type, std::string_view(type).substr(1)))
                r.raise("Ljava/lang/ArrayStoreException;", type);
            out[n] = values[n];
        }
        if (out.size() > values.size())
            out[values.size()] = {};
        return result;
    });
    vm.bind(collection, "addAll", "(Ljava/util/Collection;)Z", [](Runtime& r, Arguments a) {
        const auto values = collect(r, a[1]);
        bool modified = false;
        for (auto value : values) {
            const Value args[]{a[0], value};
            modified = r.invoke("Ljava/util/Collection;", "add", "(Ljava/lang/Object;)Z", args,
                                InvokeKind::interface_)
                           .i32() ||
                       modified;
        }
        return Value::integer(modified);
    });
    vm.bind(collection, "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto values = storage(r, a[0]);
        std::string result = "[";
        for (std::size_t n = 0; n < values.size(); ++n) {
            if (n)
                result += ", ";
            result += values[n] == a[0] ? "(this Collection)" : javaString(r, values[n]);
        }
        return r.string(result + "]");
    });
    vm.bind(list, "add", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        if (storage(r, a[0]).size() >= r.limits().arrayElements)
            throw Error(ErrorCode::limit_exceeded, "list limit exceeded");
        mutableCollection(r, a[0]);
        storage(r, a[0]).push_back(a[1]);
        changed(obj);
        return Value::integer(1);
    });
    vm.bind(list, "add", "(ILjava/lang/Object;)V", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        const auto at = position(r, a[1].i32(), storage(r, a[0]).size(), true);
        if (storage(r, a[0]).size() >= r.limits().arrayElements)
            throw Error(ErrorCode::limit_exceeded, "list limit exceeded");
        mutableCollection(r, a[0]);
        storage(r, a[0]).insert(storage(r, a[0]).begin() + static_cast<std::ptrdiff_t>(at), a[2]);
        changed(obj);
        return Value{};
    });
    vm.bind(list, "get", "(I)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& values = storage(r, a[0]);
        return values[position(r, a[1].i32(), values.size())];
    });
    vm.bind(list, "set", "(ILjava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        mutableCollection(r, a[0], false);
        auto& values = storage(r, a[0]);
        const auto at = position(r, a[1].i32(), values.size());
        const auto previous = values[at];
        values[at] = a[2];
        return previous;
    });
    vm.bind(list, "remove", "(I)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        const auto at = position(r, a[1].i32(), storage(r, a[0]).size());
        const auto previous = storage(r, a[0])[at];
        mutableCollection(r, a[0]);
        storage(r, a[0]).erase(storage(r, a[0]).begin() + static_cast<std::ptrdiff_t>(at));
        changed(obj);
        return previous;
    });
    vm.bind(list, "indexOf", "(Ljava/lang/Object;)I", [](Runtime& r, Arguments a) {
        const auto at = index(r, a[0], a[1]);
        return Value::integer(at >= storage(r, a[0]).size() ? -1 : static_cast<std::int32_t>(at));
    });
    vm.bind(list, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        if (a[0] == a[1])
            return Value::integer(1);
        if (a[1].isNull() || !r.assignable(r.object(a[1]).type, "Ljava/util/List;"))
            return Value::integer(0);
        const auto left = storage(r, a[0]), right = collect(r, a[1]);
        if (left.size() != right.size())
            return Value::integer(0);
        for (std::size_t n = 0; n < left.size(); ++n)
            if (!equal(r, left[n], right[n]))
                return Value::integer(0);
        return Value::integer(1);
    });
    vm.bind(list, "hashCode", "()I", [](Runtime& r, Arguments a) {
        const auto values = storage(r, a[0]);
        std::uint32_t hash = 1;
        for (auto value : values) {
            const Value args[]{value};
            hash = hash * 31 + (value.isNull()
                                    ? 0U
                                    : static_cast<std::uint32_t>(
                                          r.invoke("Ljava/lang/Object;", "hashCode", "()I", args).bits));
        }
        return Value{hash, false};
    });
    vm.bind(set, "add", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        if (index(r, a[0], a[1]) < storage(r, a[0]).size())
            return Value::integer(0);
        if (storage(r, a[0]).size() >= r.limits().arrayElements)
            throw Error(ErrorCode::limit_exceeded, "set limit exceeded");
        mutableCollection(r, a[0]);
        storage(r, a[0]).push_back(a[1]);
        changed(obj);
        return Value::integer(1);
    });
    vm.bind(iteratorType, "hasNext", "()Z", [](Runtime& r, Arguments a) {
        auto& it = r.object(a[0]);
        return Value::integer(it.fields["position"].bits < storage(r, it.fields["source"]).size());
    });
    vm.bind(iteratorType, "next", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& it = r.object(a[0]);
        auto& source = r.object(it.fields["source"]);
        if (!(it.fields["expectedMod"] == source.fields["modCount"]))
            r.raise("Ljava/util/ConcurrentModificationException;", "collection was modified");
        const auto at = it.fields["position"].bits;
        if (at >= storage(r, it.fields["source"]).size())
            r.raise("Ljava/util/NoSuchElementException;", "iterator exhausted");
        it.fields["last"] = it.fields["position"];
        it.fields["position"] = Value::integer(static_cast<std::int32_t>(at + 1));
        return storage(r, it.fields["source"])[static_cast<std::size_t>(at)];
    });
    vm.bind(iteratorType, "remove", "()V", [](Runtime& r, Arguments a) {
        auto& it = r.object(a[0]);
        auto& source = r.object(it.fields["source"]);
        if (!(it.fields["expectedMod"] == source.fields["modCount"]))
            r.raise("Ljava/util/ConcurrentModificationException;", "collection was modified");
        const auto at = it.fields["last"].i32();
        if (at < 0)
            r.raise("Ljava/lang/IllegalStateException;", "next has not been called");
        mutableCollection(r, it.fields["source"]);
        storage(r, it.fields["source"]).erase(storage(r, it.fields["source"]).begin() + at);
        changed(source);
        it.fields["expectedMod"] = source.fields["modCount"];
        it.fields["position"] = Value::integer(at);
        it.fields["last"] = Value::integer(-1);
        return Value{};
    });
    vm.bind(map, "size", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(storage(r, a[0]).size() / 2));
    });
    vm.bind(map, "isEmpty", "()Z",
            [](Runtime& r, Arguments a) { return Value::integer(storage(r, a[0]).empty()); });
    vm.bind(map, "clear", "()V", [](Runtime& r, Arguments a) {
        mutableCollection(r, a[0]);
        auto& obj = r.object(a[0]);
        storage(r, a[0]).clear();
        changed(obj);
        return Value{};
    });
    vm.bind(map, "containsKey", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(index(r, a[0], a[1], 2) < storage(r, a[0]).size());
    });
    vm.bind(map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const auto at = index(r, a[0], a[1], 2);
        return at >= storage(r, a[0]).size() ? Value{} : storage(r, a[0])[at + 1];
    });
    vm.bind(map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
            [](Runtime& r, Arguments a) {
                auto& obj = r.object(a[0]);
                const auto at = index(r, a[0], a[1], 2);
                if (at < storage(r, a[0]).size()) {
                    mutableCollection(r, a[0]);
                    const auto previous = storage(r, a[0])[at + 1];
                    storage(r, a[0])[at + 1] = a[2];
                    return previous;
                }
                if (storage(r, a[0]).size() + 2 > r.limits().arrayElements)
                    throw Error(ErrorCode::limit_exceeded, "map limit exceeded");
                mutableCollection(r, a[0]);
                storage(r, a[0]).push_back(a[1]);
                storage(r, a[0]).push_back(a[2]);
                changed(obj);
                return Value{};
            });
    vm.bind(map, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& obj = r.object(a[0]);
        const auto at = index(r, a[0], a[1], 2);
        if (at >= storage(r, a[0]).size())
            return Value{};
        const auto previous = storage(r, a[0])[at + 1];
        mutableCollection(r, a[0]);
        storage(r, a[0]).erase(storage(r, a[0]).begin() + static_cast<std::ptrdiff_t>(at),
                               storage(r, a[0]).begin() + static_cast<std::ptrdiff_t>(at + 2));
        changed(obj);
        return previous;
    });
    vm.bind("Ljava/util/Arrays;", "asList", "([Ljava/lang/Object;)Ljava/util/List;",
            [](Runtime& r, Arguments a) {
                const auto list = r.allocate("Ljava/util/ArrayList;");
                r.object(list).fields["backingArray"] = a[0];
                r.object(list).fields["fixedSize"] = Value::integer(1);
                return list;
            });
    vm.bind("Ljava/util/Arrays;", "fill", "([II)V", [](Runtime& r, Arguments a) {
        for (auto& value : storage(r, a[0]))
            value = a[1];
        return Value{};
    });
    vm.bind("Ljava/util/Collections;", "emptyList", "()Ljava/util/List;", [](Runtime& r, Arguments) {
        const auto value = r.allocate("Ljava/util/ArrayList;");
        r.object(value).fields["immutable"] = Value::integer(1);
        return value;
    });
    vm.bind("Ljava/util/Collections;", "emptyMap", "()Ljava/util/Map;", [](Runtime& r, Arguments) {
        const auto value = r.allocate("Ljava/util/HashMap;");
        r.object(value).fields["immutable"] = Value::integer(1);
        return value;
    });
    vm.bind("Ljava/util/Collections;", "emptySet", "()Ljava/util/Set;", [](Runtime& r, Arguments) {
        const auto value = r.allocate("Ljava/util/HashSet;");
        r.object(value).fields["immutable"] = Value::integer(1);
        return value;
    });
    vm.bind("Ljava/util/Collections;", "singletonList", "(Ljava/lang/Object;)Ljava/util/List;",
            [](Runtime& r, Arguments a) {
                const auto list = r.allocate("Ljava/util/ArrayList;");
                r.object(list).elements.push_back(a[0]);
                r.object(list).fields["immutable"] = Value::integer(1);
                return list;
            });
}

} // namespace gutenberg
