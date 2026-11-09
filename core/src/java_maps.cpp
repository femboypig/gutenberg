#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>

namespace gutenberg {
namespace {
constexpr auto iteratorType = "Lgutenberg/runtime/MapIterator;";
constexpr auto enumerationType = "Lgutenberg/runtime/MapEnumeration;";
constexpr auto entryType = "Lgutenberg/runtime/MapEntry;";
constexpr const char* viewTypes[]{"Lgutenberg/runtime/MapKeys;", "Lgutenberg/runtime/MapValues;",
                                  "Lgutenberg/runtime/MapEntries;"};
enum Projection { keys, values, entries };

bool equal(Runtime& vm, Value left, Value right) {
    if (left == right)
        return true;
    if (left.isNull() || right.isNull())
        return false;
    const Value args[]{left, right};
    return vm.invoke("Ljava/lang/Object;", "equals", "(Ljava/lang/Object;)Z", args).i32() != 0;
}
std::uint32_t hash(Runtime& vm, Value value) {
    if (value.isNull())
        return 0;
    const Value args[]{value};
    return static_cast<std::uint32_t>(vm.invoke("Ljava/lang/Object;", "hashCode", "()I", args).bits);
}
void modified(Runtime& vm) {
    vm.raise("Ljava/util/ConcurrentModificationException;", "map was modified during traversal");
}
struct MapEntry {
    Value key, value;
    std::uint32_t hash;
};
struct MapState final : NativeObject {
    using Order = std::list<std::shared_ptr<MapEntry>>;
    Order order;
    std::unordered_multimap<std::uint32_t, Order::iterator> buckets;
    std::uint64_t version = 0;
    bool rejectNull = false, accessOrder = false;

    void visitReferences(const std::function<void(Value)>& visit) const override {
        for (const auto& entry : order) {
            visit(entry->key);
            visit(entry->value);
        }
    }
    std::shared_ptr<NativeObject> cloneState() const override {
        auto result = std::make_shared<MapState>();
        result->rejectNull = rejectNull;
        result->accessOrder = accessOrder;
        for (const auto& entry : order)
            result->insert(std::make_shared<MapEntry>(*entry));
        return result;
    }
    void insert(std::shared_ptr<MapEntry> entry) {
        order.push_back(std::move(entry));
        try {
            buckets.emplace(order.back()->hash, std::prev(order.end()));
        } catch (...) {
            order.pop_back();
            throw;
        }
        ++version;
    }
    std::shared_ptr<MapEntry> find(Runtime& vm, Value key, std::uint32_t keyHash) {
        if (rejectNull && key.isNull())
            vm.raise("Ljava/lang/NullPointerException;", "null map key");
        std::vector<std::shared_ptr<MapEntry>> candidates;
        const auto [begin, end] = buckets.equal_range(keyHash);
        for (auto it = begin; it != end; ++it)
            candidates.push_back(*it->second);
        const auto expected = version;
        for (const auto& entry : candidates) {
            vm.tick();
            const bool matches = equal(vm, key, entry->key);
            if (version != expected)
                modified(vm);
            if (matches)
                return entry;
        }
        return {};
    }
    void erase(const std::shared_ptr<MapEntry>& entry) {
        const auto [begin, end] = buckets.equal_range(entry->hash);
        for (auto it = begin; it != end; ++it)
            if (*it->second == entry) {
                order.erase(it->second);
                buckets.erase(it);
                ++version;
                return;
            }
    }
    void access(const std::shared_ptr<MapEntry>& entry) {
        if (!accessOrder || order.back() == entry)
            return;
        const auto [begin, end] = buckets.equal_range(entry->hash);
        for (auto it = begin; it != end; ++it)
            if (*it->second == entry) {
                order.splice(order.end(), order, it->second);
                ++version;
                return;
            }
    }
};
std::shared_ptr<MapState> state(Runtime& vm, Value map) {
    auto& object = vm.object(map);
    if (!object.native) {
        auto data = std::make_shared<MapState>();
        data->rejectNull = vm.assignable(object.type, "Ljava/util/Hashtable;");
        object.native = data;
    }
    auto data = std::dynamic_pointer_cast<MapState>(object.native);
    if (!data)
        vm.raise("Ljava/lang/IllegalStateException;", "invalid map state");
    return data;
}
void writable(Runtime& vm, Value map) {
    if (vm.object(map).fields["immutable"].bits)
        vm.raise("Ljava/lang/UnsupportedOperationException;", "map is not mutable");
}
struct EntryState final : NativeObject {
    std::shared_ptr<MapEntry> entry;
    bool rejectNull = false;
    bool immutable = false;
    void visitReferences(const std::function<void(Value)>& visit) const override {
        visit(entry->key);
        visit(entry->value);
    }
};
Value entryObject(Runtime& vm, std::shared_ptr<MapEntry> entry, bool rejectNull = false) {
    const auto result = vm.allocate(entryType);
    auto data = std::make_shared<EntryState>();
    data->entry = std::move(entry);
    data->rejectNull = rejectNull;
    vm.object(result).native = std::move(data);
    return result;
}
EntryState& entryState(Runtime& vm, Value entry) {
    auto data = std::dynamic_pointer_cast<EntryState>(vm.object(entry).native);
    if (!data)
        vm.raise("Ljava/lang/IllegalStateException;", "invalid map entry");
    return *data;
}
Value entryKey(Runtime& vm, Value entry) {
    const Value args[]{entry};
    return vm.invoke("Ljava/util/Map$Entry;", "getKey", "()Ljava/lang/Object;", args);
}
Value entryValue(Runtime& vm, Value entry) {
    const Value args[]{entry};
    return vm.invoke("Ljava/util/Map$Entry;", "getValue", "()Ljava/lang/Object;", args);
}
struct IteratorState final : NativeObject {
    std::shared_ptr<MapState> map;
    MapState::Order::iterator next;
    std::shared_ptr<MapEntry> last;
    std::uint64_t expected;
    Projection projection;
};
IteratorState& iteratorState(Runtime& vm, Value iterator) {
    auto data = std::dynamic_pointer_cast<IteratorState>(vm.object(iterator).native);
    if (!data)
        vm.raise("Ljava/lang/IllegalStateException;", "invalid map iterator");
    if (data->expected != data->map->version)
        modified(vm);
    return *data;
}
Value iterator(Runtime& vm, Value map, Projection projection) {
    const auto result = vm.allocate(iteratorType);
    auto data = std::make_shared<IteratorState>();
    data->map = state(vm, map);
    data->next = data->map->order.begin();
    data->expected = data->map->version;
    data->projection = projection;
    vm.object(result).fields["map"] = map;
    vm.object(result).native = std::move(data);
    return result;
}
Value project(Runtime& vm, const std::shared_ptr<MapEntry>& entry, Projection projection, bool rejectNull) {
    return projection == keys     ? entry->key
           : projection == values ? entry->value
                                  : entryObject(vm, entry, rejectNull);
}
Value view(Runtime& vm, Value map, Projection projection) {
    const auto result = vm.allocate(viewTypes[projection]);
    vm.object(result).fields["map"] = map;
    return result;
}
Value findIterator(Runtime& vm, Value collection) {
    const Value args[]{collection};
    return vm.invoke("Ljava/lang/Iterable;", "iterator", "()Ljava/util/Iterator;", args);
}
bool hasNext(Runtime& vm, Value iterator) {
    const Value args[]{iterator};
    return vm.invoke("Ljava/util/Iterator;", "hasNext", "()Z", args).i32();
}
Value next(Runtime& vm, Value iterator) {
    const Value args[]{iterator};
    return vm.invoke("Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", args);
}
Value mapCall(Runtime& vm, Value map, std::string name, std::string descriptor,
              std::initializer_list<Value> extra = {}) {
    std::vector<Value> args{map};
    args.insert(args.end(), extra);
    return vm.invoke("Ljava/util/Map;", std::move(name), std::move(descriptor), args);
}
Value put(Runtime& vm, Value map, Value key, Value value) {
    writable(vm, map);
    auto data = state(vm, map);
    if (data->rejectNull && (key.isNull() || value.isNull()))
        vm.raise("Ljava/lang/NullPointerException;", "null hashtable key or value");
    const auto keyHash = hash(vm, key);
    if (const auto entry = data->find(vm, key, keyHash)) {
        const auto previous = entry->value;
        entry->value = value;
        data->access(entry);
        return previous;
    }
    if (data->order.size() >= vm.limits().arrayElements / 2)
        throw Error(ErrorCode::limit_exceeded, "map storage limit exceeded");
    data->insert(std::make_shared<MapEntry>(MapEntry{key, value, keyHash}));
    if (vm.assignable(vm.object(map).type, "Ljava/util/LinkedHashMap;")) {
        const auto eldest = data->order.front();
        const Value args[]{map, entryObject(vm, eldest)};
        if (vm.invoke("Ljava/util/LinkedHashMap;", "removeEldestEntry", "(Ljava/util/Map$Entry;)Z", args)
                .i32())
            data->erase(eldest);
    }
    return {};
}
void putAll(Runtime& vm, Value destination, Value source) {
    const auto set = mapCall(vm, source, "entrySet", "()Ljava/util/Set;");
    const auto it = findIterator(vm, set);
    while (hasNext(vm, it)) {
        const auto entry = next(vm, it);
        (void)mapCall(vm, destination, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                      {entryKey(vm, entry), entryValue(vm, entry)});
    }
}
} // namespace

void installJavaMaps(Runtime& vm) {
    vm.defineClass("Ljava/util/AbstractMap;", "Ljava/lang/Object;", {"Ljava/util/Map;"});
    vm.defineClass("Ljava/util/HashMap;", "Ljava/util/AbstractMap;",
                   {"Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/LinkedHashMap;", "Ljava/util/HashMap;");
    vm.defineClass("Ljava/util/Dictionary;");
    vm.defineClass("Ljava/util/Hashtable;", "Ljava/util/Dictionary;",
                   {"Ljava/util/Map;", "Ljava/lang/Cloneable;", "Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/Map$Entry;");
    vm.defineClass("Ljava/util/Enumeration;");
    vm.defineClass(entryType, "Ljava/lang/Object;", {"Ljava/util/Map$Entry;"});
    vm.defineClass("Ljava/util/AbstractMap$SimpleEntry;", entryType, {"Ljava/io/Serializable;"});
    vm.defineClass("Ljava/util/AbstractMap$SimpleImmutableEntry;", entryType, {"Ljava/io/Serializable;"});
    vm.defineClass(iteratorType, "Ljava/lang/Object;", {"Ljava/util/Iterator;"});
    vm.defineClass(enumerationType, "Ljava/lang/Object;", {"Ljava/util/Enumeration;"});
    for (const auto* function : {"Function", "BiFunction", "BiConsumer"})
        vm.defineClass("Ljava/util/function/" + std::string(function) + ";");
    for (const auto* owner :
         {"Ljava/util/AbstractMap$SimpleEntry;", "Ljava/util/AbstractMap$SimpleImmutableEntry;"}) {
        const bool immutable = std::string_view(owner).find("Immutable") != std::string_view::npos;
        vm.bind(owner, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V",
                [immutable](Runtime& r, Arguments a) {
                    auto data = std::make_shared<EntryState>();
                    data->entry = std::make_shared<MapEntry>(MapEntry{a[1], a[2], 0});
                    data->immutable = immutable;
                    r.object(a[0]).native = std::move(data);
                    return Value{};
                });
        vm.bind(owner, "<init>", "(Ljava/util/Map$Entry;)V", [immutable](Runtime& r, Arguments a) {
            auto data = std::make_shared<EntryState>();
            data->entry = std::make_shared<MapEntry>(MapEntry{entryKey(r, a[1]), entryValue(r, a[1]), 0});
            data->immutable = immutable;
            r.object(a[0]).native = std::move(data);
            return Value{};
        });
    }
    for (const auto* owner : {"Ljava/util/HashMap;", "Ljava/util/LinkedHashMap;", "Ljava/util/Hashtable;"}) {
        vm.bind(owner, "<init>", "()V", [](Runtime& r, Arguments a) {
            (void)state(r, a[0]);
            return Value{};
        });
        vm.bind(owner, "<init>", "(I)V", [](Runtime& r, Arguments a) {
            if (a[1].i32() < 0)
                r.raise("Ljava/lang/IllegalArgumentException;", "negative map capacity");
            (void)state(r, a[0]);
            return Value{};
        });
        vm.bind(owner, "<init>", "(IF)V", [](Runtime& r, Arguments a) {
            if (a[1].i32() < 0 || !(a[2].f32() > 0))
                r.raise("Ljava/lang/IllegalArgumentException;", "invalid map capacity or load factor");
            (void)state(r, a[0]);
            return Value{};
        });
        vm.bind(owner, "<init>", "(Ljava/util/Map;)V", [](Runtime& r, Arguments a) {
            putAll(r, a[0], a[1]);
            return Value{};
        });
    }
    vm.bind("Ljava/util/AbstractMap;", "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm.bind("Ljava/util/Dictionary;", "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm.bind("Ljava/util/LinkedHashMap;", "<init>", "(IFZ)V", [](Runtime& r, Arguments a) {
        if (a[1].i32() < 0 || !(a[2].f32() > 0))
            r.raise("Ljava/lang/IllegalArgumentException;", "invalid map capacity or load factor");
        state(r, a[0])->accessOrder = a[3].i32() != 0;
        return Value{};
    });
    vm.bind("Ljava/util/LinkedHashMap;", "removeEldestEntry", "(Ljava/util/Map$Entry;)Z",
            [](Runtime&, Arguments) { return Value::integer(0); });
    for (const auto* owner : {"Ljava/util/HashMap;", "Ljava/util/Hashtable;"}) {
        vm.bind(owner, "size", "()I", [](Runtime& r, Arguments a) {
            return Value::integer(static_cast<std::int32_t>(state(r, a[0])->order.size()));
        });
        vm.bind(owner, "isEmpty", "()Z",
                [](Runtime& r, Arguments a) { return Value::integer(state(r, a[0])->order.empty()); });
        vm.bind(owner, "containsKey", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
            return Value::integer(bool(state(r, a[0])->find(r, a[1], hash(r, a[1]))));
        });
        vm.bind(owner, "containsValue", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
            const auto data = state(r, a[0]);
            if (data->rejectNull && a[1].isNull())
                r.raise("Ljava/lang/NullPointerException;", "null hashtable value");
            const auto it = iterator(r, a[0], values);
            while (hasNext(r, it))
                if (equal(r, a[1], next(r, it)))
                    return Value::integer(1);
            return Value::integer(0);
        });
        vm.bind(owner, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
            const auto data = state(r, a[0]);
            const auto entry = data->find(r, a[1], hash(r, a[1]));
            if (!entry)
                return Value{};
            data->access(entry);
            return entry->value;
        });
        vm.bind(owner, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                [](Runtime& r, Arguments a) { return put(r, a[0], a[1], a[2]); });
        vm.bind(owner, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
            writable(r, a[0]);
            const auto data = state(r, a[0]);
            const auto entry = data->find(r, a[1], hash(r, a[1]));
            if (!entry)
                return Value{};
            const auto previous = entry->value;
            data->erase(entry);
            return previous;
        });
        vm.bind(owner, "clear", "()V", [](Runtime& r, Arguments a) {
            writable(r, a[0]);
            const auto data = state(r, a[0]);
            data->buckets.clear();
            data->order.clear();
            ++data->version;
            return Value{};
        });
        vm.bind(owner, "keySet", "()Ljava/util/Set;",
                [](Runtime& r, Arguments a) { return view(r, a[0], keys); });
        vm.bind(owner, "values", "()Ljava/util/Collection;",
                [](Runtime& r, Arguments a) { return view(r, a[0], values); });
        vm.bind(owner, "entrySet", "()Ljava/util/Set;",
                [](Runtime& r, Arguments a) { return view(r, a[0], entries); });
    }
    for (const auto* owner : {"Ljava/util/AbstractMap;", "Ljava/util/Hashtable;"}) {
        vm.bind(owner, "putAll", "(Ljava/util/Map;)V", [](Runtime& r, Arguments a) {
            writable(r, a[0]);
            putAll(r, a[0], a[1]);
            return Value{};
        });
        vm.bind(owner, "getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                [](Runtime& r, Arguments a) {
                    const auto value =
                        mapCall(r, a[0], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
                    return !value.isNull() ||
                                   mapCall(r, a[0], "containsKey", "(Ljava/lang/Object;)Z", {a[1]}).i32()
                               ? value
                               : a[2];
                });
        vm.bind(
            owner, "putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
            [](Runtime& r, Arguments a) {
                writable(r, a[0]);
                const auto value = mapCall(r, a[0], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
                if (value.isNull())
                    (void)mapCall(r, a[0], "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                  {a[1], a[2]});
                return value;
            });
        vm.bind(owner, "remove", "(Ljava/lang/Object;Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
            writable(r, a[0]);
            const auto value = mapCall(r, a[0], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
            if (!equal(r, value, a[2]) ||
                (value.isNull() && !mapCall(r, a[0], "containsKey", "(Ljava/lang/Object;)Z", {a[1]}).i32()))
                return Value::integer(0);
            (void)mapCall(r, a[0], "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
            return Value::integer(1);
        });
        vm.bind(owner, "replace", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                [](Runtime& r, Arguments a) {
                    writable(r, a[0]);
                    if (!mapCall(r, a[0], "containsKey", "(Ljava/lang/Object;)Z", {a[1]}).i32())
                        return Value{};
                    return mapCall(r, a[0], "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                   {a[1], a[2]});
                });
        vm.bind(owner, "replace", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z",
                [](Runtime& r, Arguments a) {
                    writable(r, a[0]);
                    const auto value =
                        mapCall(r, a[0], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
                    if (!equal(r, value, a[2]) ||
                        (value.isNull() &&
                         !mapCall(r, a[0], "containsKey", "(Ljava/lang/Object;)Z", {a[1]}).i32()))
                        return Value::integer(0);
                    (void)mapCall(r, a[0], "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                  {a[1], a[3]});
                    return Value::integer(1);
                });
        for (const auto* method : {"compute", "computeIfAbsent", "computeIfPresent", "merge"}) {
            const std::string name = method;
            const bool absent = name == "computeIfAbsent", merge = name == "merge";
            const auto signature =
                absent  ? "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;"
                : merge ? "(Ljava/lang/Object;Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/"
                          "Object;"
                        : "(Ljava/lang/Object;Ljava/util/function/BiFunction;)Ljava/lang/Object;";
            vm.bind(owner, name, signature, [name, absent, merge](Runtime& r, Arguments a) {
                writable(r, a[0]);
                const auto function = a[merge ? 3 : 2];
                if (function.isNull() || (merge && a[2].isNull()))
                    r.raise("Ljava/lang/NullPointerException;", "null map computation argument");
                const auto old = mapCall(r, a[0], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
                if ((absent && !old.isNull()) || (name == "computeIfPresent" && old.isNull()))
                    return old;
                const auto data = std::dynamic_pointer_cast<MapState>(r.object(a[0]).native);
                const auto expected = data ? data->version : 0;
                Value result;
                if (merge && old.isNull())
                    result = a[2];
                else if (absent) {
                    const Value args[]{function, a[1]};
                    result = r.invoke("Ljava/util/function/Function;", "apply",
                                      "(Ljava/lang/Object;)Ljava/lang/Object;", args);
                } else {
                    const Value args[]{function, merge ? old : a[1], merge ? a[2] : old};
                    result = r.invoke("Ljava/util/function/BiFunction;", "apply",
                                      "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", args);
                }
                if (data && data->version != expected)
                    modified(r);
                if (!result.isNull())
                    (void)mapCall(r, a[0], "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                                  {a[1], result});
                else if (!absent)
                    (void)mapCall(r, a[0], "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", {a[1]});
                return result;
            });
        }
        for (const bool replace : {false, true}) {
            vm.bind(owner, replace ? "replaceAll" : "forEach",
                    replace ? "(Ljava/util/function/BiFunction;)V" : "(Ljava/util/function/BiConsumer;)V",
                    [replace](Runtime& r, Arguments a) {
                        if (a[1].isNull())
                            r.raise("Ljava/lang/NullPointerException;", "null map callback");
                        if (replace)
                            writable(r, a[0]);
                        const auto it = findIterator(r, mapCall(r, a[0], "entrySet", "()Ljava/util/Set;"));
                        while (hasNext(r, it)) {
                            const auto entry = next(r, it);
                            const Value args[]{a[1], entryKey(r, entry), entryValue(r, entry)};
                            const auto value =
                                r.invoke(replace ? "Ljava/util/function/BiFunction;"
                                                 : "Ljava/util/function/BiConsumer;",
                                         replace ? "apply" : "accept",
                                         replace ? "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"
                                                 : "(Ljava/lang/Object;Ljava/lang/Object;)V",
                                         args);
                            (void)hasNext(r, it);
                            if (replace) {
                                const Value set[]{entry, value};
                                (void)r.invoke("Ljava/util/Map$Entry;", "setValue",
                                               "(Ljava/lang/Object;)Ljava/lang/Object;", set);
                            }
                        }
                        return Value{};
                    });
        }
        vm.bind(owner, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
            if (a[0] == a[1])
                return Value::integer(1);
            if (a[1].isNull() || !r.assignable(r.object(a[1]).type, "Ljava/util/Map;") ||
                mapCall(r, a[0], "size", "()I").i32() != mapCall(r, a[1], "size", "()I").i32())
                return Value::integer(0);
            const auto it = findIterator(r, mapCall(r, a[0], "entrySet", "()Ljava/util/Set;"));
            while (hasNext(r, it)) {
                const auto entry = next(r, it), key = entryKey(r, entry), value = entryValue(r, entry);
                const auto other = mapCall(r, a[1], "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {key});
                if (!equal(r, value, other) ||
                    (value.isNull() &&
                     !mapCall(r, a[1], "containsKey", "(Ljava/lang/Object;)Z", {key}).i32()))
                    return Value::integer(0);
            }
            return Value::integer(1);
        });
        vm.bind(owner, "hashCode", "()I", [](Runtime& r, Arguments a) {
            std::uint32_t result = 0;
            const auto it = findIterator(r, mapCall(r, a[0], "entrySet", "()Ljava/util/Set;"));
            while (hasNext(r, it)) {
                const auto entry = next(r, it);
                result += hash(r, entryKey(r, entry)) ^ hash(r, entryValue(r, entry));
            }
            return Value{result, false};
        });
        vm.bind(owner, "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
            const auto it = findIterator(r, mapCall(r, a[0], "entrySet", "()Ljava/util/Set;"));
            std::string result = "{";
            while (hasNext(r, it)) {
                const auto entry = next(r, it), key = entryKey(r, entry), value = entryValue(r, entry);
                if (result.size() > 1)
                    result += ", ";
                result += key == a[0] ? "(this Map)" : javaString(r, key);
                result += "=";
                result += value == a[0] ? "(this Map)" : javaString(r, value);
            }
            return r.string(result + "}");
        });
    }
    for (unsigned projection = keys; projection <= entries; ++projection) {
        const auto type = viewTypes[projection];
        vm.defineClass(type,
                       projection == values ? "Ljava/util/AbstractCollection;" : "Ljava/util/AbstractSet;");
        vm.bind(type, "size", "()I", [](Runtime& r, Arguments a) {
            return Value::integer(
                static_cast<std::int32_t>(state(r, r.object(a[0]).fields["map"])->order.size()));
        });
        vm.bind(type, "isEmpty", "()Z", [](Runtime& r, Arguments a) {
            return Value::integer(state(r, r.object(a[0]).fields["map"])->order.empty());
        });
        vm.bind(type, "iterator", "()Ljava/util/Iterator;", [projection](Runtime& r, Arguments a) {
            return iterator(r, r.object(a[0]).fields["map"], static_cast<Projection>(projection));
        });
        vm.bind(type, "clear", "()V", [](Runtime& r, Arguments a) {
            return mapCall(r, r.object(a[0]).fields["map"], "clear", "()V");
        });
        vm.bind(type, "add", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments) -> Value {
            r.raise("Ljava/lang/UnsupportedOperationException;", "cannot add to a map view");
        });
        vm.bind(type, "contains", "(Ljava/lang/Object;)Z", [projection](Runtime& r, Arguments a) {
            const auto map = r.object(a[0]).fields["map"];
            if (projection != entries)
                return mapCall(r, map, projection == keys ? "containsKey" : "containsValue",
                               "(Ljava/lang/Object;)Z", {a[1]});
            if (a[1].isNull() || !r.assignable(r.object(a[1]).type, "Ljava/util/Map$Entry;"))
                return Value::integer(0);
            const auto key = entryKey(r, a[1]);
            const auto entry = state(r, map)->find(r, key, hash(r, key));
            return Value::integer(entry && equal(r, entry->value, entryValue(r, a[1])));
        });
        vm.bind(type, "remove", "(Ljava/lang/Object;)Z", [projection](Runtime& r, Arguments a) {
            const auto map = r.object(a[0]).fields["map"];
            writable(r, map);
            const auto data = state(r, map);
            if (projection == values) {
                const auto it = iterator(r, map, values);
                while (hasNext(r, it))
                    if (equal(r, a[1], next(r, it))) {
                        const Value args[]{it};
                        (void)r.invoke(iteratorType, "remove", "()V", args);
                        return Value::integer(1);
                    }
                return Value::integer(0);
            }
            if (projection == entries &&
                (a[1].isNull() || !r.assignable(r.object(a[1]).type, "Ljava/util/Map$Entry;")))
                return Value::integer(0);
            const auto key = projection == keys ? a[1] : entryKey(r, a[1]);
            const auto entry = data->find(r, key, hash(r, key));
            if (!entry || (projection == entries && !equal(r, entry->value, entryValue(r, a[1]))))
                return Value::integer(0);
            data->erase(entry);
            return Value::integer(1);
        });
    }
    vm.bind(iteratorType, "hasNext", "()Z", [](Runtime& r, Arguments a) {
        const auto& it = iteratorState(r, a[0]);
        return Value::integer(it.next != it.map->order.end());
    });
    vm.bind(iteratorType, "next", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& it = iteratorState(r, a[0]);
        if (it.next == it.map->order.end())
            r.raise("Ljava/util/NoSuchElementException;", "map iterator exhausted");
        it.last = *it.next++;
        return project(r, it.last, it.projection, it.map->rejectNull);
    });
    vm.bind(iteratorType, "remove", "()V", [](Runtime& r, Arguments a) {
        auto& it = iteratorState(r, a[0]);
        if (!it.last)
            r.raise("Ljava/lang/IllegalStateException;", "next has not been called");
        writable(r, r.object(a[0]).fields["map"]);
        it.map->erase(it.last);
        it.last.reset();
        it.expected = it.map->version;
        return Value{};
    });
    vm.bind(entryType, "getKey", "()Ljava/lang/Object;",
            [](Runtime& r, Arguments a) { return entryState(r, a[0]).entry->key; });
    vm.bind(entryType, "getValue", "()Ljava/lang/Object;",
            [](Runtime& r, Arguments a) { return entryState(r, a[0]).entry->value; });
    vm.bind(entryType, "setValue", "(Ljava/lang/Object;)Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& data = entryState(r, a[0]);
        if (data.immutable)
            r.raise("Ljava/lang/UnsupportedOperationException;", "entry is not mutable");
        if (data.rejectNull && a[1].isNull())
            r.raise("Ljava/lang/NullPointerException;", "null hashtable value");
        const auto previous = data.entry->value;
        data.entry->value = a[1];
        return previous;
    });
    vm.bind(entryType, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        if (a[1].isNull() || !r.assignable(r.object(a[1]).type, "Ljava/util/Map$Entry;"))
            return Value::integer(0);
        const auto entry = entryState(r, a[0]).entry;
        return Value::integer(equal(r, entry->key, entryKey(r, a[1])) &&
                              equal(r, entry->value, entryValue(r, a[1])));
    });
    vm.bind(entryType, "hashCode", "()I", [](Runtime& r, Arguments a) {
        const auto entry = entryState(r, a[0]).entry;
        return Value{hash(r, entry->key) ^ hash(r, entry->value), false};
    });
    vm.bind(entryType, "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto entry = entryState(r, a[0]).entry;
        return r.string(javaString(r, entry->key) + "=" + javaString(r, entry->value));
    });
    vm.bind("Ljava/util/Hashtable;", "contains", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return mapCall(r, a[0], "containsValue", "(Ljava/lang/Object;)Z", {a[1]});
    });
    for (const auto projection : {keys, values}) {
        vm.bind("Ljava/util/Hashtable;", projection == keys ? "keys" : "elements",
                "()Ljava/util/Enumeration;", [projection](Runtime& r, Arguments a) {
                    const auto result = r.allocate(enumerationType);
                    const auto data = state(r, a[0]);
                    for (const auto& entry : data->order)
                        r.object(result).elements.push_back(projection == keys ? entry->key : entry->value);
                    return result;
                });
    }
    vm.bind(enumerationType, "hasMoreElements", "()Z", [](Runtime& r, Arguments a) {
        const auto& object = r.object(a[0]);
        return Value::integer(object.hostId < object.elements.size());
    });
    vm.bind(enumerationType, "nextElement", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& object = r.object(a[0]);
        if (object.hostId >= object.elements.size())
            r.raise("Ljava/util/NoSuchElementException;", "enumeration exhausted");
        return object.elements[object.hostId++];
    });
}
} // namespace gutenberg
