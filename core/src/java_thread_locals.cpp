#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <map>

namespace gutenberg {
namespace {
struct ThreadLocalState final : NativeObject {
    using Key = std::weak_ptr<GuestThreadIdentity>;
    std::map<Key, Value, std::owner_less<Key>> values;
    void removeExpired() {
        std::erase_if(values, [](const auto& entry) { return entry.first.expired(); });
    }
    void visitReferences(const std::function<void(Value)>& visit) const override {
        for (const auto& [thread, value] : values)
            if (!thread.expired())
                visit(value);
    }
    std::shared_ptr<NativeObject> cloneState() const override {
        return std::make_shared<ThreadLocalState>();
    }
};
ThreadLocalState& state(Runtime& vm, Value local) {
    const auto value = std::dynamic_pointer_cast<ThreadLocalState>(vm.object(local).native);
    if (!value)
        vm.raise("Ljava/lang/IllegalStateException;", "thread local is not initialized");
    value->removeExpired();
    return *value;
}
} // namespace

void installJavaThreadLocals(Runtime& vm) {
    constexpr auto type = "Ljava/lang/ThreadLocal;";
    constexpr auto supplied = "Ljava/lang/ThreadLocal$SuppliedThreadLocal;";
    vm.defineClass(type);
    vm.defineClass(supplied, type);
    vm.defineClass("Ljava/util/function/Supplier;");
    vm.bind(type, "<init>", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<ThreadLocalState>();
        return Value{};
    });
    vm.bind(type, "initialValue", "()Ljava/lang/Object;", [](Runtime&, Arguments) { return Value{}; });
    vm.bind(supplied, "initialValue", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        const Value args[]{r.object(a[0]).fields["supplier"]};
        return r.invoke("Ljava/util/function/Supplier;", "get", "()Ljava/lang/Object;", args,
                        InvokeKind::interface_);
    });
    vm.bind(type, "get", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) {
        auto& data = state(r, a[0]);
        const auto thread = r.threadIdentity();
        if (const auto value = data.values.find(thread); value != data.values.end())
            return value->second;
        const auto result = r.invoke("Ljava/lang/ThreadLocal;", "initialValue", "()Ljava/lang/Object;", a);
        data.values[thread] = result;
        return result;
    });
    vm.bind(type, "set", "(Ljava/lang/Object;)V", [](Runtime& r, Arguments a) {
        state(r, a[0]).values[r.threadIdentity()] = a[1];
        return Value{};
    });
    vm.bind(type, "remove", "()V", [](Runtime& r, Arguments a) {
        state(r, a[0]).values.erase(r.threadIdentity());
        return Value{};
    });
    vm.bind(type, "withInitial", "(Ljava/util/function/Supplier;)Ljava/lang/ThreadLocal;",
            [](Runtime& r, Arguments a) {
                if (a[0].isNull())
                    r.raise("Ljava/lang/NullPointerException;", "null thread local supplier");
                const auto value = r.allocate("Ljava/lang/ThreadLocal$SuppliedThreadLocal;");
                r.object(value).native = std::make_shared<ThreadLocalState>();
                r.object(value).fields["supplier"] = a[0];
                return value;
            });
}
} // namespace gutenberg
