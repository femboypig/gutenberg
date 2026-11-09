#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value invoke(Runtime& vm, Value receiver, std::string name = "value") {
    const Value args[]{receiver};
    return vm.invoke("LRoot;", std::move(name), "()I", args, InvokeKind::interface_);
}
void constant(Runtime& vm, std::string owner, int value) {
    vm.bind(std::move(owner), "value", "()I", [value](Runtime&, Arguments) { return Value::integer(value); });
}
} // namespace

TEST(dispatch_selects_most_specific_interface_defaults_and_class_overrides) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LRoot;", "");
    vm.defineClass("LLeft;", "", {"LRoot;"});
    vm.defineClass("LRight;", "", {"LRoot;"});
    vm.defineClass("LReceiver;", "Ljava/lang/Object;", {"LLeft;", "LRight;"});
    constant(vm, "LRoot;", 7);
    const auto object = vm.allocate("LReceiver;");
    CHECK(invoke(vm, object).i32() == 7);
    constant(vm, "LLeft;", 11);
    CHECK(invoke(vm, object).i32() == 11);
    constant(vm, "LReceiver;", 19);
    CHECK(invoke(vm, object).i32() == 19);
}

TEST(dispatch_rejects_conflicting_defaults_and_invalidates_negative_results) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LRoot;", "");
    vm.defineClass("LOther;", "");
    vm.defineClass("LReceiver;", "Ljava/lang/Object;", {"LRoot;", "LOther;"});
    const auto object = vm.allocate("LReceiver;");
    try {
        (void)invoke(vm, object);
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/NoSuchMethodError;");
    }
    constant(vm, "LRoot;", 3);
    CHECK(invoke(vm, object).i32() == 3);
    constant(vm, "LOther;", 5);
    try {
        (void)invoke(vm, object);
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/IncompatibleClassChangeError;");
    }
    constant(vm, "LReceiver;", 9);
    CHECK(invoke(vm, object).i32() == 9);
}

TEST(dispatch_cache_survives_reentrant_class_registration_and_respects_its_budget) {
    RuntimeLimits limits;
    limits.methodCacheEntries = 2;
    Runtime vm(limits);
    installJavaRuntime(vm);
    vm.defineClass("LRoot;", "");
    vm.defineClass("LReceiver;", "Ljava/lang/Object;", {"LRoot;"});
    vm.bind("LRoot;", "value", "()I", [](Runtime& r, Arguments) {
        if (!r.hasClass("LDynamic;"))
            r.defineClass("LDynamic;");
        return Value::integer(17);
    });
    const auto object = vm.allocate("LReceiver;");
    CHECK(invoke(vm, object).i32() == 17);
    for (unsigned n = 0; n < 10; ++n) {
        const Value args[]{object};
        (void)vm.invoke("Ljava/lang/Object;", "hashCode", "()I", args);
        (void)vm.invoke("Ljava/lang/Object;", "getClass", "()Ljava/lang/Class;", args);
        CHECK(invoke(vm, object).i32() == 17);
    }
    RuntimeLimits uncachedLimits;
    uncachedLimits.methodCacheEntries = 0;
    Runtime uncached(uncachedLimits);
    installJavaRuntime(uncached);
    const Value value[]{uncached.string("value")};
    CHECK(uncached.invoke("Ljava/lang/String;", "length", "()I", value).i32() == 5);
}
