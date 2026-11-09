#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value call(Runtime& vm, Value receiver, std::string name, std::string signature,
           std::initializer_list<Value> extra = {}) {
    std::vector<Value> args{receiver};
    args.insert(args.end(), extra);
    return vm.invoke(vm.object(receiver).type, std::move(name), std::move(signature), args);
}
Value put(Runtime& vm, Value map, Value key, Value value) {
    return call(vm, map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", {key, value});
}
Value get(Runtime& vm, Value map, Value key) {
    return call(vm, map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {key});
}
Value iterator(Runtime& vm, Value map, const char* view = "entrySet") {
    const auto collection = call(
        vm, map, view, std::string_view(view) == "values" ? "()Ljava/util/Collection;" : "()Ljava/util/Set;");
    return call(vm, collection, "iterator", "()Ljava/util/Iterator;");
}
Value next(Runtime& vm, Value it) {
    return call(vm, it, "next", "()Ljava/lang/Object;");
}
template <class F> void throwsType(Runtime& vm, const char* type, F action) {
    try {
        action();
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == type);
    }
}
} // namespace

TEST(maps_use_hash_buckets_and_resolve_equal_colliding_keys) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LKey;");
    unsigned comparisons = 0;
    vm.bind("LKey;", "hashCode", "()I",
            [](Runtime& r, Arguments a) { return r.object(a[0]).fields["hash"]; });
    vm.bind("LKey;", "equals", "(Ljava/lang/Object;)Z", [&](Runtime& r, Arguments a) {
        ++comparisons;
        return Value::integer(!a[1].isNull() && r.object(a[1]).type == "LKey;" &&
                              r.object(a[0]).fields["id"] == r.object(a[1]).fields["id"]);
    });
    auto key = [&](int id, int hash) {
        const auto value = vm.allocate("LKey;");
        vm.object(value).fields["id"] = Value::integer(id);
        vm.object(value).fields["hash"] = Value::integer(hash);
        return value;
    };
    const auto map = vm.allocate("Ljava/util/HashMap;");
    for (int n = 0; n < 2000; ++n)
        (void)put(vm, map, key(n, n), vm.string(std::to_string(n)));
    comparisons = 0;
    CHECK(vm.text(get(vm, map, key(1999, 1999))) == "1999");
    CHECK(comparisons == 1);
    const auto collision = key(2001, 1999);
    (void)put(vm, map, collision, vm.string("collision"));
    CHECK(vm.text(get(vm, map, key(1999, 1999))) == "1999");
    CHECK(vm.text(get(vm, map, key(2001, 1999))) == "collision");
    CHECK(vm.text(put(vm, map, key(2001, 1999), vm.string("updated"))) == "collision");
    CHECK(call(vm, map, "size", "()I").i32() == 2001);
}

TEST(map_views_are_live_and_iterator_entries_update_the_map) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto map = vm.allocate("Ljava/util/LinkedHashMap;");
    const auto key = vm.string("key"), value = vm.string("value"), replacement = vm.string("replacement");
    const auto keys = call(vm, map, "keySet", "()Ljava/util/Set;");
    (void)put(vm, map, key, value);
    (void)put(vm, map, Value{}, Value{});
    CHECK(call(vm, keys, "size", "()I").i32() == 2);
    const auto it = iterator(vm, map);
    const auto entry = next(vm, it);
    CHECK(call(vm, entry, "getKey", "()Ljava/lang/Object;") == key);
    CHECK(call(vm, entry, "setValue", "(Ljava/lang/Object;)Ljava/lang/Object;", {replacement}) == value);
    CHECK(get(vm, map, key) == replacement);
    CHECK(!next(vm, it).isNull());
    (void)call(vm, it, "remove", "()V");
    CHECK(call(vm, keys, "size", "()I").i32() == 1);
    throwsType(vm, "Ljava/lang/IllegalStateException;", [&] { (void)call(vm, it, "remove", "()V"); });
    const auto values = call(vm, map, "values", "()Ljava/util/Collection;");
    const auto array = call(vm, values, "toArray", "()[Ljava/lang/Object;");
    CHECK(vm.object(array).elements == std::vector<Value>{replacement});
    CHECK(call(vm, values, "remove", "(Ljava/lang/Object;)Z", {replacement}).i32());
    CHECK(call(vm, keys, "isEmpty", "()Z").i32());
    throwsType(vm, "Ljava/util/ConcurrentModificationException;", [&] { (void)next(vm, it); });
}

TEST(map_clones_have_independent_entries_and_collector_traces_native_storage) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto map = vm.allocate("Ljava/util/HashMap;");
    const auto key = vm.string("key"), value = vm.string("value");
    (void)put(vm, map, key, value);
    const auto clone = call(vm, map, "clone", "()Ljava/lang/Object;");
    const auto entry = next(vm, iterator(vm, map));
    (void)call(vm, map, "clear", "()V");
    const Value roots[]{clone, entry};
    (void)vm.collect(roots);
    CHECK(vm.isAlive(key) && vm.isAlive(value));
    CHECK(get(vm, clone, key) == value);
    (void)call(vm, entry, "setValue", "(Ljava/lang/Object;)Ljava/lang/Object;", {vm.string("detached")});
    CHECK(get(vm, clone, key) == value);
    (void)call(vm, clone, "clear", "()V");
    const Value remaining[]{entry};
    (void)vm.collect(remaining);
    CHECK(!vm.isAlive(value));
    CHECK(vm.isAlive(key));
}

TEST(hashtable_rejects_null_and_enumerations_retain_their_values) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto table = vm.allocate("Ljava/util/Hashtable;");
    const auto key = vm.string("key"), value = vm.string("value");
    throwsType(vm, "Ljava/lang/NullPointerException;", [&] { (void)put(vm, table, Value{}, value); });
    throwsType(vm, "Ljava/lang/NullPointerException;", [&] { (void)put(vm, table, key, Value{}); });
    throwsType(vm, "Ljava/lang/NullPointerException;", [&] { (void)get(vm, table, Value{}); });
    (void)put(vm, table, key, value);
    const auto enumeration = call(vm, table, "elements", "()Ljava/util/Enumeration;");
    CHECK(call(vm, enumeration, "hasMoreElements", "()Z").i32());
    const Value roots[]{enumeration};
    (void)vm.collect(roots);
    CHECK(!vm.isAlive(table));
    CHECK(call(vm, enumeration, "nextElement", "()Ljava/lang/Object;") == value);
    CHECK(!call(vm, enumeration, "hasMoreElements", "()Z").i32());
    throwsType(vm, "Ljava/util/NoSuchElementException;",
               [&] { (void)call(vm, enumeration, "nextElement", "()Ljava/lang/Object;"); });
}

TEST(linked_maps_support_access_order_and_eldest_eviction) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LCache;", "Ljava/util/LinkedHashMap;");
    vm.bind("LCache;", "removeEldestEntry", "(Ljava/util/Map$Entry;)Z",
            [](Runtime& r, Arguments a) { return Value::integer(call(r, a[0], "size", "()I").i32() > 2); });
    const auto map = vm.allocate("LCache;");
    const Value constructor[]{map, Value::integer(16), Value::floating(0.75f), Value::integer(1)};
    (void)vm.invoke("Ljava/util/LinkedHashMap;", "<init>", "(IFZ)V", constructor, InvokeKind::direct);
    const auto a = vm.string("a"), b = vm.string("b"), c = vm.string("c");
    (void)put(vm, map, a, a);
    (void)put(vm, map, b, b);
    const auto it = iterator(vm, map, "keySet");
    CHECK(get(vm, map, a) == a);
    throwsType(vm, "Ljava/util/ConcurrentModificationException;", [&] { (void)next(vm, it); });
    (void)put(vm, map, c, c);
    CHECK(get(vm, map, b).isNull());
    CHECK(next(vm, iterator(vm, map, "keySet")) == a);
}

TEST(map_computations_preserve_null_contracts_and_reject_structural_reentrancy) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto map = vm.allocate("Ljava/util/HashMap;"), key = vm.string("key"),
               fallback = vm.string("fallback");
    (void)put(vm, map, key, Value{});
    CHECK(call(vm, map, "getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
               {key, fallback})
              .isNull());
    CHECK(call(vm, map, "getOrDefault", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
               {vm.string("missing"), fallback}) == fallback);
    vm.defineClass("LFunction;", "Ljava/lang/Object;", {"Ljava/util/function/Function;"});
    vm.bind("LFunction;", "apply", "(Ljava/lang/Object;)Ljava/lang/Object;",
            [](Runtime&, Arguments) { return Value{}; });
    const auto function = vm.allocate("LFunction;");
    CHECK(call(vm, map, "computeIfAbsent",
               "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;", {key, function})
              .isNull());
    CHECK(call(vm, map, "containsKey", "(Ljava/lang/Object;)Z", {key}).i32());
    vm.defineClass("LMutatingFunction;", "Ljava/lang/Object;", {"Ljava/util/function/Function;"});
    vm.bind("LMutatingFunction;", "apply", "(Ljava/lang/Object;)Ljava/lang/Object;",
            [&](Runtime& r, Arguments) {
                (void)put(r, map, r.string("other"), fallback);
                return fallback;
            });
    throwsType(vm, "Ljava/util/ConcurrentModificationException;", [&] {
        (void)call(vm, map, "computeIfAbsent",
                   "(Ljava/lang/Object;Ljava/util/function/Function;)Ljava/lang/Object;",
                   {key, vm.allocate("LMutatingFunction;")});
    });
    CHECK(get(vm, map, key).isNull());
    vm.defineClass("LReplace;", "Ljava/lang/Object;", {"Ljava/util/function/BiFunction;"});
    vm.bind("LReplace;", "apply", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
            [](Runtime&, Arguments a) { return a[1]; });
    (void)call(vm, map, "replaceAll", "(Ljava/util/function/BiFunction;)V", {vm.allocate("LReplace;")});
    CHECK(get(vm, map, key) == key);
}

TEST(map_equality_entries_and_conditional_updates_follow_value_contracts) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto map = vm.allocate("Ljava/util/HashMap;"), key = vm.string("key"), value = vm.string("value");
    (void)put(vm, map, key, value);
    const auto copy = vm.allocate("Ljava/util/Hashtable;");
    (void)call(vm, copy, "putAll", "(Ljava/util/Map;)V", {map});
    CHECK(call(vm, map, "equals", "(Ljava/lang/Object;)Z", {copy}).i32());
    CHECK(call(vm, map, "hashCode", "()I") == call(vm, copy, "hashCode", "()I"));
    const auto entry = vm.allocate("Ljava/util/AbstractMap$SimpleImmutableEntry;");
    const Value constructor[]{entry, vm.string("key"), vm.string("value")};
    (void)vm.invoke(vm.object(entry).type, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V", constructor,
                    InvokeKind::direct);
    const auto entries = call(vm, map, "entrySet", "()Ljava/util/Set;");
    CHECK(call(vm, entries, "contains", "(Ljava/lang/Object;)Z", {entry}).i32());
    throwsType(vm, "Ljava/lang/UnsupportedOperationException;",
               [&] { (void)call(vm, entry, "setValue", "(Ljava/lang/Object;)Ljava/lang/Object;", {key}); });
    CHECK(!call(vm, map, "remove", "(Ljava/lang/Object;Ljava/lang/Object;)Z", {key, key}).i32());
    CHECK(call(vm, map, "replace", "(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Z",
               {key, value, key})
              .i32());
    CHECK(get(vm, map, key) == key);
}
