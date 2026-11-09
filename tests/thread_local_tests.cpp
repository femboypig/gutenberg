#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value call(Runtime& vm, Value local, const std::string& method, const std::string& signature,
           std::initializer_list<Value> extra = {}) {
    std::vector<Value> args{local};
    args.insert(args.end(), extra.begin(), extra.end());
    return vm.invoke("Ljava/lang/ThreadLocal;", method, signature, args);
}
Value create(Runtime& vm, const std::string& type = "Ljava/lang/ThreadLocal;") {
    const auto value = vm.allocate(type);
    const Value args[]{value};
    (void)vm.invoke("Ljava/lang/ThreadLocal;", "<init>", "()V", args, InvokeKind::direct);
    return value;
}
} // namespace

TEST(thread_local_initial_values_are_lazy_and_null_is_a_stored_value) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LLocal;", "Ljava/lang/ThreadLocal;", {"Ljava/lang/Cloneable;"});
    unsigned calls = 0;
    vm.bind("LLocal;", "initialValue", "()Ljava/lang/Object;", [&](Runtime& r, Arguments) {
        ++calls;
        return r.string("initial");
    });
    const auto local = create(vm, "LLocal;");
    CHECK(calls == 0);
    CHECK(vm.text(call(vm, local, "get", "()Ljava/lang/Object;")) == "initial");
    (void)call(vm, local, "get", "()Ljava/lang/Object;");
    CHECK(calls == 1);
    (void)call(vm, local, "set", "(Ljava/lang/Object;)V", {Value{}});
    CHECK(call(vm, local, "get", "()Ljava/lang/Object;").isNull());
    CHECK(calls == 1);
    (void)call(vm, local, "remove", "()V");
    CHECK(vm.text(call(vm, local, "get", "()Ljava/lang/Object;")) == "initial");
    CHECK(calls == 2);
    const Value cloneArgs[]{local};
    const auto clone = vm.invoke("Ljava/lang/Object;", "clone", "()Ljava/lang/Object;", cloneArgs);
    (void)call(vm, clone, "get", "()Ljava/lang/Object;");
    CHECK(calls == 3);
}

TEST(thread_local_values_follow_guest_contexts_and_release_finished_threads) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto local = create(vm);
    const auto mainValue = vm.string("main");
    (void)call(vm, local, "set", "(Ljava/lang/Object;)V", {mainValue});
    auto worker = std::make_shared<GuestThreadIdentity>();
    Value workerValue;
    {
        auto context = vm.enterThread(worker);
        CHECK(call(vm, local, "get", "()Ljava/lang/Object;").isNull());
        workerValue = vm.string("worker");
        (void)call(vm, local, "set", "(Ljava/lang/Object;)V", {workerValue});
    }
    CHECK(call(vm, local, "get", "()Ljava/lang/Object;") == mainValue);
    const Value roots[]{local};
    (void)vm.collect(roots);
    CHECK(vm.isAlive(workerValue));
    worker.reset();
    (void)vm.collect(roots);
    CHECK(!vm.isAlive(workerValue));
    CHECK(call(vm, local, "get", "()Ljava/lang/Object;") == mainValue);
}

TEST(thread_local_suppliers_and_context_restrictions_match_guest_execution) {
    Runtime vm;
    installJavaRuntime(vm);
    vm.defineClass("LSupplier;", "Ljava/lang/Object;", {"Ljava/util/function/Supplier;"});
    vm.bind("LSupplier;", "get", "()Ljava/lang/Object;",
            [](Runtime& r, Arguments) { return r.string("supplied"); });
    const Value args[]{vm.allocate("LSupplier;")};
    const auto local =
        vm.invoke("Ljava/lang/ThreadLocal;", "withInitial",
                  "(Ljava/util/function/Supplier;)Ljava/lang/ThreadLocal;", args, InvokeKind::static_);
    CHECK(vm.text(call(vm, local, "get", "()Ljava/lang/Object;")) == "supplied");
    vm.defineClass("LContextProbe;");
    vm.bind("LContextProbe;", "change", "()V", [](Runtime& r, Arguments) {
        auto context = r.enterThread(std::make_shared<GuestThreadIdentity>());
        return Value{};
    });
    test::rejects([&] { (void)vm.invoke("LContextProbe;", "change", "()V", {}, InvokeKind::static_); });
}
