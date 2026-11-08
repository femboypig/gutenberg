#include "test.hpp"
#include "gutenberg/jit.hpp"
#include <random>

using namespace gutenberg;

TEST(jit_executes_native_integer_arithmetic) {
    Runtime vm;
    Code code; code.registers = 3; code.incoming = 2;
    code.words = {0x0090, 0x0201, 0x000f}; // add-int v0, v1, v2; return v0
    const Value args[]{Value::integer(19), Value::integer(23)};
    const auto result = vm.jit().execute(vm, code, args);
    CHECK(result.has_value()); CHECK(result->i32() == 42);
    CHECK(vm.jit().statistics().compiledMethods == 1);
    CHECK(vm.jit().statistics().executions == 1);
}

TEST(jit_integer_operations_match_java_bit_patterns) {
    std::mt19937 random(0x47555445);
    for (const unsigned operation : {0U, 1U, 2U, 5U, 6U, 7U, 8U, 9U, 10U}) {
        Runtime vm;
        Code code; code.registers = 3; code.incoming = 2;
        code.words = {static_cast<std::uint16_t>(0x90 + operation), 0x0201, 0x000f};
        for (unsigned n = 0; n < 200; ++n) {
            const auto left = static_cast<std::uint32_t>(random()), right = static_cast<std::uint32_t>(random());
            const Value args[]{{left, false}, {right, false}};
            const auto result = vm.jit().execute(vm, code, args);
            CHECK(result.has_value());
            std::uint32_t expected = 0;
            switch (operation) {
                case 0: expected = left + right; break; case 1: expected = left - right; break; case 2: expected = left * right; break;
                case 5: expected = left & right; break; case 6: expected = left | right; break; case 7: expected = left ^ right; break;
                case 8: expected = left << (right & 31); break;
                case 9: expected = std::bit_cast<std::uint32_t>(std::bit_cast<std::int32_t>(left) >> (right & 31)); break;
                case 10: expected = left >> (right & 31); break;
            }
            CHECK(static_cast<std::uint32_t>(result->bits) == expected);
        }
        CHECK(vm.jit().statistics().compiledMethods == 1);
    }
}

TEST(jit_branches_loops_and_immediates) {
    Runtime vm;
    Code code; code.registers = 3;
    code.words = {0x0012, 0x1112, 0x0213, 11, 0x0090, 0x0100, 0x01d8, 0x0101, 0x2134, 0xfffc, 0x000f};
    const auto result = vm.jit().execute(vm, code, {});
    CHECK(result.has_value()); CHECK(result->i32() == 55);
}

TEST(jit_respects_cancellation_and_instruction_budget) {
    Runtime vm(RuntimeLimits{20, 100, 100, 10});
    Code code; code.registers = 1; code.words = {0x0028};
    test::rejects([&] { (void)vm.jit().execute(vm, code, {}); });
    Runtime cancelled; cancelled.cancel();
    test::rejects([&] { (void)cancelled.jit().execute(cancelled, code, {}); });
}

TEST(jit_declines_object_and_exception_operations) {
    Runtime vm;
    Code code; code.registers = 1; code.incoming = 1; code.words = {0x0011};
    const Value args[]{Value::object(1)};
    CHECK(!vm.jit().execute(vm, code, args).has_value());
    code.incoming = 0; code.words = {0x001c, 0, 0x000f};
    CHECK(!vm.jit().execute(vm, code, {}).has_value());
}
