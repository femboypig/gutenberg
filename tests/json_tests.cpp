#include "gutenberg/json.hpp"
#include "test.hpp"
#include <limits>

using namespace gutenberg;

TEST(json_round_trips_unicode_numbers_and_duplicate_keys) {
    const auto value =
        parseJson(R"({"text":"Привет 🌙\n\u0000\ud800","n":-9223372036854775808,"a":[null,true,2.5],"n":7})");
    const auto& object = std::get<JsonValue::Object>(value.data);
    CHECK(object.size() == 3);
    CHECK(object[0].first == u"text");
    const auto& text = std::get<std::u16string>(object[0].second.data);
    CHECK(text[text.size() - 1] == 0xd800);
    CHECK(text[text.size() - 2] == 0);
    CHECK(std::get<std::int64_t>(object[1].second.data) == 7);
    const auto encoded = stringifyJson(value);
    CHECK(encoded.find("\\ud800") != std::string::npos);
    CHECK(stringifyJson(parseJson(encoded)) == encoded);
    CHECK(std::get<std::int64_t>(parseJson("-9223372036854775808").data) == INT64_MIN);
    CHECK(std::get<double>(parseJson("6.02e23").data) == 6.02e23);
    CHECK(stringifyJson(parseJson("\xef\xbb\xbf[1]"), 2) == "[\n  1\n]");
}

TEST(json_rejects_invalid_syntax_and_encoding) {
    for (const auto* text :
         {"", "01", "-.2", "1.", "1e", "NaN", "1e999", "true false", "[1,]", "{\"a\":}", "{\"a\":1,}",
          "\"\\x\"", "\"\\u00zz\"", "\"\n\"", "\"\xc0\xaf\"", "\"\xed\xa0\x80\"", "\"unterminated"})
        test::rejects([&] { (void)parseJson(text); });
    test::rejects([] { (void)stringifyJson(JsonValue{std::numeric_limits<double>::infinity()}); });
}

TEST(json_enforces_input_output_and_recursion_budgets) {
    JsonLimits limits;
    limits.bytes = 5;
    test::rejects([&] { (void)parseJson("[1, 2]", limits); });
    test::rejects([&] { (void)stringifyJson(JsonValue{u"long string"}, 0, limits); });
    limits.bytes = 1024;
    limits.depth = 2;
    test::rejects([&] { (void)parseJson("[[[0]]]", limits); });
    const auto deep = parseJson("[[[0]]]");
    test::rejects([&] { (void)stringifyJson(deep, 0, limits); });
    limits.depth = 128;
    limits.nodes = 2;
    test::rejects([&] { (void)parseJson("[1,2]", limits); });
}
