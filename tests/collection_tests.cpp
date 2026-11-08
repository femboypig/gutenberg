#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value call(Runtime& vm, std::string type, std::string name, std::string descriptor,
           std::initializer_list<Value> args, InvokeKind kind = InvokeKind::virtual_) {
    return vm.invoke(std::move(type), std::move(name), std::move(descriptor),
                     Arguments(args.begin(), args.size()), kind);
}
} // namespace

TEST(java_list_iteration_and_fail_fast) {
    Runtime vm;
    installJavaRuntime(vm);
    auto list = vm.allocate("Ljava/util/ArrayList;");
    auto one = vm.string("one"), two = vm.string("two");
    (void)call(vm, "Ljava/util/List;", "add", "(Ljava/lang/Object;)Z", {list, one});
    auto iterator = call(vm, "Ljava/util/List;", "iterator", "()Ljava/util/Iterator;", {list});
    CHECK(call(vm, "Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", {iterator}) == one);
    (void)call(vm, "Ljava/util/List;", "add", "(Ljava/lang/Object;)Z", {list, two});
    try {
        (void)call(vm, "Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", {iterator});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/util/ConcurrentModificationException;");
    }
}

TEST(java_array_list_view_is_backed_and_fixed_size) {
    Runtime vm;
    installJavaRuntime(vm);
    auto array = vm.array("[Ljava/lang/String;", 1);
    auto first = vm.string("first"), second = vm.string("second");
    vm.object(array).elements[0] = first;
    const auto list = call(vm, "Ljava/util/Arrays;", "asList", "([Ljava/lang/Object;)Ljava/util/List;",
                           {array}, InvokeKind::static_);
    (void)call(vm, "Ljava/util/List;", "set", "(ILjava/lang/Object;)Ljava/lang/Object;",
               {list, Value::integer(0), second});
    CHECK(vm.object(array).elements[0] == second);
    vm.object(array).elements[0] = first;
    CHECK(call(vm, "Ljava/util/List;", "get", "(I)Ljava/lang/Object;", {list, Value::integer(0)}) == first);
    try {
        (void)call(vm, "Ljava/util/List;", "add", "(Ljava/lang/Object;)Z", {list, first});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/UnsupportedOperationException;");
    }
}

TEST(java_empty_collection_is_immutable) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto list =
        call(vm, "Ljava/util/Collections;", "emptyList", "()Ljava/util/List;", {}, InvokeKind::static_);
    try {
        (void)call(vm, "Ljava/util/List;", "add", "(Ljava/lang/Object;)Z", {list, vm.string("value")});
        CHECK(false);
    } catch (const GuestException&) {
    }
}

TEST(java_map_keys_use_value_equality) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto map = vm.allocate("Ljava/util/HashMap;");
    const auto key = vm.string("key"), sameKey = vm.string("key"), value = vm.string("value");
    (void)call(vm, "Ljava/util/Map;", "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
               {map, key, value});
    CHECK(call(vm, "Ljava/util/Map;", "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {map, sameKey}) ==
          value);
    CHECK(call(vm, "Ljava/util/Map;", "containsKey", "(Ljava/lang/Object;)Z", {map, sameKey}).i32() == 1);
}

TEST(java_integer_parser_checks_sign_radix_and_overflow) {
    Runtime vm;
    installJavaRuntime(vm);
    CHECK(call(vm, "Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {vm.string("-2147483648")},
               InvokeKind::static_)
              .i32() == std::numeric_limits<std::int32_t>::min());
    CHECK(call(vm, "Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;I)I",
               {vm.string("ff"), Value::integer(16)}, InvokeKind::static_)
              .i32() == 255);
    for (auto text : {"2147483648", "", "+", "12x", " 12"}) {
        try {
            (void)call(vm, "Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {vm.string(text)},
                       InvokeKind::static_);
            CHECK(false);
        } catch (const GuestException& error) {
            CHECK(vm.object(error.value).type == "Ljava/lang/NumberFormatException;");
        }
    }
}

TEST(java_random_matches_seeded_jdk_sequence) {
    Runtime vm;
    installJavaRuntime(vm);
    auto random = vm.allocate("Ljava/util/Random;");
    (void)call(vm, "Ljava/util/Random;", "<init>", "(J)V", {random, Value::integer(0), Value::integer(0)},
               InvokeKind::direct);
    CHECK(call(vm, "Ljava/util/Random;", "nextInt", "()I", {random}).i32() == -1155484576);
    CHECK(call(vm, "Ljava/util/Random;", "nextInt", "()I", {random}).i32() == -723955400);
}
