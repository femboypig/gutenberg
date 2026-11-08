#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;

TEST(java_bootstrap_and_string_methods) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto text = vm.string("hello 😀");
    const Value args[]{text};
    CHECK(vm.invoke("Ljava/lang/String;", "length", "()I", args).i32() == 8);
    CHECK(javaString(vm, text) == "hello 😀");
    const Value substring[]{text, Value::integer(0), Value::integer(5)};
    CHECK(vm.text(vm.invoke("Ljava/lang/String;", "substring", "(II)Ljava/lang/String;", substring)) ==
          "hello");
}

TEST(java_arraycopy_preserves_overlap) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto array = vm.array("[I", 4);
    vm.object(array).elements = {Value::integer(1), Value::integer(2), Value::integer(3), Value::integer(4)};
    const Value args[]{array, Value::integer(0), array, Value::integer(1), Value::integer(3)};
    (void)vm.invoke("Ljava/lang/System;", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", args,
                    InvokeKind::static_);
    CHECK(vm.object(array).elements[3].i32() == 3);
    CHECK(vm.object(array).elements[2].i32() == 2);
}

TEST(java_unavailable_method_raises_linkage_error) {
    Runtime vm;
    installJavaRuntime(vm);
    try {
        (void)vm.invoke("Ljava/lang/System;", "notImplemented", "()V", {}, InvokeKind::static_);
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.assignable(vm.object(error.value).type, "Ljava/lang/LinkageError;"));
    }
}
