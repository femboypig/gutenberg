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

TEST(java_clone_requires_cloneable_and_accounts_array_storage) {
    Runtime vm(RuntimeLimits{10000, 100, 5, 100});
    installJavaRuntime(vm);
    const auto plain = vm.allocate("Ljava/lang/Object;");
    const Value plainArgs[]{plain};
    try {
        (void)vm.invoke("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", plainArgs);
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/CloneNotSupportedException;");
    }
    const auto array = vm.array("[I", 3);
    const Value arrayArgs[]{array};
    test::rejects([&] { (void)vm.invoke("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", arrayArgs); });
}

TEST(reflection_creates_primitive_matrices_and_preserves_class_identity) {
    Runtime vm(RuntimeLimits{10000, 100, 32, 100});
    installJavaRuntime(vm);
    const auto integer = vm.getStatic({"Ljava/lang/Integer;", "TYPE", "Ljava/lang/Class;"});
    const auto dimensions = vm.array("[I", 2);
    vm.object(dimensions).elements = {Value::integer(3), Value::integer(4)};
    const Value args[]{integer, dimensions};
    const auto matrix = vm.invoke("Ljava/lang/reflect/Array;", "newInstance",
                                  "(Ljava/lang/Class;[I)Ljava/lang/Object;", args, InvokeKind::static_);
    CHECK(vm.object(matrix).type == "[[I");
    CHECK(vm.object(matrix).elements.size() == 3);
    CHECK(vm.object(vm.object(matrix).elements[0]).elements.size() == 4);
    const Value classArgs[]{vm.classObject("[[I")};
    CHECK(vm.invoke("Ljava/lang/Class;", "getComponentType", "()Ljava/lang/Class;", classArgs) ==
          vm.classObject("[I"));
    const Value nameArgs[]{integer};
    CHECK(vm.text(vm.invoke("Ljava/lang/Class;", "getName", "()Ljava/lang/String;", nameArgs)) == "int");
    const Value arrayName[]{vm.string("[[I")};
    CHECK(vm.invoke("Ljava/lang/Class;", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", arrayName,
                    InvokeKind::static_) == vm.classObject("[[I"));
    vm.object(dimensions).elements = {Value::integer(100), Value::integer(100)};
    test::rejects([&] {
        (void)vm.invoke("Ljava/lang/reflect/Array;", "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
                        args, InvokeKind::static_);
    });
    vm.object(dimensions).elements = {Value::integer(0), Value::integer(-1)};
    try {
        (void)vm.invoke("Ljava/lang/reflect/Array;", "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
                        args, InvokeKind::static_);
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/NegativeArraySizeException;");
    }
}
