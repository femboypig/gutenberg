#include "test.hpp"
#include "binary_builder.hpp"
#include "gutenberg/runtime.hpp"
#include <cmath>
#include <zlib.h>

using namespace gutenberg;

namespace {
DexFile emptyDex() {
    Bytes bytes(112);
    const std::string magic("dex\n035\0", 8);
    std::copy(magic.begin(), magic.end(), bytes.begin());
    fixture::patch32(bytes, 32, 112); fixture::patch32(bytes, 36, 112); fixture::patch32(bytes, 40, 0x12345678);
    fixture::patch32(bytes, 8, static_cast<std::uint32_t>(adler32(1, bytes.data() + 12, 100)));
    return DexFile(std::move(bytes));
}
Value run(Runtime& vm, std::initializer_list<std::uint16_t> words, std::uint16_t registers = 8) {
    Code code; code.registers = registers; code.words = words;
    return vm.execute(emptyDex(), code, {});
}
}

TEST(interpreter_integer_arithmetic) {
    Runtime vm;
    // const/16 v0, 300; const/16 v1, -7; mul-int v2, v0, v1; return v2
    CHECK(run(vm, {0x0013, 300, 0x0113, 0xfff9, 0x0292, 0x0100, 0x020f}).i32() == -2100);
}

TEST(interpreter_overflow_matches_java) {
    Runtime vm;
    // MAX_VALUE + 1 wraps, rather than triggering signed C++ overflow.
    CHECK(run(vm, {0x0014, 0xffff, 0x7fff, 0x0112, 0x11d8, 0x0100, 0x110f}, 18).i32() == std::numeric_limits<std::int32_t>::min());
}

TEST(interpreter_loop_and_branch) {
    Runtime vm;
    // sum 1..10 using a backwards branch.
    CHECK(run(vm, {0x0012, 0x1112, 0x0213, 11, 0x0090, 0x0100, 0x01d8, 0x0101, 0x2134, 0xfffc, 0x000f}).i32() == 55);
}

TEST(interpreter_wide_register_pairs) {
    Runtime vm;
    CHECK(run(vm, {0x0018, 0xffff, 0xffff, 0xffff, 0x7fff, 0x0216, 1, 0x049b, 0x0200, 0x0410}).i64() == std::numeric_limits<std::int64_t>::min());
}

TEST(interpreter_exception_regions) {
    Runtime vm;
    vm.defineClass("Ljava/lang/Object;", "");
    vm.defineClass("Ljava/lang/Throwable;");
    vm.defineClass("Ljava/lang/ArithmeticException;", "Ljava/lang/Throwable;");
    Code code;
    code.registers = 3;
    // Divide 1 by zero, handler returns 42.
    code.words = {0x1012, 0x0112, 0x0293, 0x0100, 0x020f, 0x000d, 0x0113, 42, 0x010f};
    code.tries.push_back({2, 2, {{"Ljava/lang/ArithmeticException;", 5}}});
    CHECK(vm.execute(emptyDex(), code, {}).i32() == 42);
}

TEST(interpreter_rejects_mid_instruction_branch) {
    Runtime vm;
    test::rejects([&] { (void)run(vm, {0x0228, 0x0014, 42, 0, 0x000f}); });
}

TEST(interpreter_cancellation_and_budget) {
    Runtime vm(RuntimeLimits{20, 100, 100, 10});
    test::rejects([&] { (void)run(vm, {0x0028}); });
    Runtime cancelled;
    cancelled.cancel();
    test::rejects([&] { (void)run(cancelled, {0x000e}); });
}

TEST(runtime_object_identity_and_unicode) {
    Runtime vm;
    auto a = vm.string("текст 😀", true), b = vm.string("текст 😀", true);
    CHECK(a == b);
    CHECK(vm.text(a) == "текст 😀");
    CHECK(vm.object(a).text.size() == 8);
    CHECK(!(a == vm.string("текст 😀")));
}

TEST(runtime_class_assignability) {
    Runtime vm;
    vm.defineClass("Ljava/lang/Object;", "");
    vm.defineClass("LParent;"); vm.defineClass("LChild;", "LParent;", {"LInterface;"});
    CHECK(vm.assignable("LChild;", "LParent;"));
    CHECK(vm.assignable("LChild;", "LInterface;"));
    CHECK(vm.assignable("[LChild;", "[LParent;"));
    CHECK(!vm.assignable("[I", "[J"));
}

TEST(runtime_native_dispatch) {
    Runtime vm;
    vm.defineClass("Ljava/lang/Object;", ""); vm.defineClass("LMath;");
    vm.bind("LMath;", "twice", "(I)I", [](Runtime&, Arguments args) { return Value::integer(args[0].i32() * 2); });
    const Value args[] = {Value::integer(21)};
    CHECK(vm.invoke("LMath;", "twice", "(I)I", args, InvokeKind::static_).i32() == 42);
}

TEST(method_descriptors_are_validated) {
    CHECK(methodRef("LX;", "f", "([[IJD)Ljava/lang/String;").argumentWords(true) == 6);
    test::rejects([] { (void)methodRef("LX;", "f", "(V)V"); });
    test::rejects([] { (void)methodRef("LX;", "f", "(Lbroken)V"); });
}
