#include "gutenberg/graphics.hpp"
#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;

TEST(collector_reclaims_cycles_and_rejects_reused_stale_handles) {
    Runtime vm;
    const auto root = vm.allocate("LNode;"), child = vm.allocate("LNode;"), dead = vm.allocate("LNode;");
    vm.object(root).fields["child"] = child;
    vm.object(child).fields["parent"] = root;
    vm.object(dead).fields["self"] = dead;
    const Value roots[]{root};
    CHECK(vm.collect(roots) == 1);
    CHECK(vm.heapSize() == 2);
    CHECK(vm.isAlive(root) && vm.isAlive(child) && !vm.isAlive(dead));
    const auto replacement = vm.allocate("LNode;");
    CHECK(static_cast<std::uint32_t>(replacement.bits) == static_cast<std::uint32_t>(dead.bits));
    CHECK(replacement != dead);
    test::rejects([&] { (void)vm.object(dead); });
    vm.object(root).fields["corrupt"] = dead;
    test::rejects([&] { (void)vm.collect(roots); });
    CHECK(vm.isAlive(replacement));
    vm.object(root).fields.erase("corrupt");
    CHECK(vm.collect() == 3);
}

TEST(collector_releases_array_budgets_and_runs_only_between_invocations) {
    Runtime vm(RuntimeLimits{1000, 32, 8, 100});
    const auto array = vm.array("[I", 8);
    test::rejects([&] { (void)vm.array("[I", 1); });
    CHECK(vm.collect() == 1);
    CHECK(!vm.isAlive(array));
    CHECK(vm.object(vm.array("[I", 8)).elements.size() == 8);
    vm.defineClass("LTest;");
    vm.bind("LTest;", "collect", "()V", [](Runtime& r, Arguments) {
        (void)r.collect();
        return Value{};
    });
    test::rejects([&] { (void)vm.invoke("LTest;", "collect", "()V", {}, InvokeKind::static_); });
}

TEST(collector_traces_json_members_and_preserves_static_mirrors) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto root = vm.allocate("Lorg/json/JSONObject;");
    const Value args[]{root, vm.string("{\"child\":{\"name\":\"retained\"}}")};
    (void)vm.invoke("Lorg/json/JSONObject;", "<init>", "(Ljava/lang/String;)V", args, InvokeKind::direct);
    const auto native = std::weak_ptr<NativeObject>(vm.object(root).native);
    const Value roots[]{root};
    (void)vm.collect(roots);
    const Value getArgs[]{root, vm.string("child")};
    const auto child = vm.invoke("Lorg/json/JSONObject;", "getJSONObject",
                                 "(Ljava/lang/String;)Lorg/json/JSONObject;", getArgs);
    const Value childArgs[]{child, vm.string("name")};
    CHECK(vm.text(vm.invoke("Lorg/json/JSONObject;", "getString", "(Ljava/lang/String;)Ljava/lang/String;",
                            childArgs)) == "retained");
    const Value cycleArgs[]{root, vm.string("cycle"), root};
    (void)vm.invoke("Lorg/json/JSONObject;", "put",
                    "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;", cycleArgs);
    (void)vm.collect();
    CHECK(native.expired());
    CHECK(vm.isAlive(vm.getStatic({"Lorg/json/JSONObject;", "NULL", "Ljava/lang/Object;"})));
    CHECK(vm.isAlive(vm.getStatic({"Ljava/lang/Integer;", "TYPE", "Ljava/lang/Class;"})));
}

TEST(collector_keeps_native_canvas_targets_alive_and_releases_bitmap_resources) {
    Runtime vm;
    installJavaRuntime(vm);
    installGraphics(vm);
    const auto config =
        vm.getStatic({"Landroid/graphics/Bitmap$Config;", "ARGB_8888", "Landroid/graphics/Bitmap$Config;"});
    const Value bitmapArgs[]{Value::integer(8), Value::integer(8), config};
    const auto bitmap = vm.invoke("Landroid/graphics/Bitmap;", "createBitmap",
                                  "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", bitmapArgs,
                                  InvokeKind::static_);
    const auto canvas = vm.allocate("Landroid/graphics/Canvas;");
    const Value initArgs[]{canvas, bitmap};
    (void)vm.invoke("Landroid/graphics/Canvas;", "<init>", "(Landroid/graphics/Bitmap;)V", initArgs,
                    InvokeKind::direct);
    const auto resource = std::weak_ptr<NativeObject>(vm.object(bitmap).native);
    const Value roots[]{canvas};
    (void)vm.collect(roots);
    CHECK(vm.isAlive(bitmap));
    const Value drawArgs[]{canvas, Value{0xff123456, false}};
    (void)vm.invoke("Landroid/graphics/Canvas;", "drawColor", "(I)V", drawArgs);
    CHECK(bitmapSnapshot(vm, bitmap)->drawing->commands.size() == 1);
    (void)vm.collect();
    CHECK(resource.expired());
}

TEST(collector_reuses_a_bounded_heap_over_many_allocation_cycles) {
    Runtime vm(RuntimeLimits{1000, 32, 32, 100});
    const auto root = vm.allocate("LNode;");
    const Value roots[]{root};
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        const auto value = vm.allocate("LNode;");
        vm.object(value).fields["parent"] = root;
        if (iteration % 16 == 15)
            (void)vm.collect(roots);
    }
    CHECK(vm.heapSize() == 1);
    CHECK(vm.isAlive(root));
}
