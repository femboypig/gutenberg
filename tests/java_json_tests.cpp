#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value call(Runtime& vm, std::string owner, std::string name, std::string descriptor,
           std::initializer_list<Value> args, InvokeKind kind = InvokeKind::virtual_) {
    return vm.invoke(std::move(owner), std::move(name), std::move(descriptor),
                     Arguments(args.begin(), args.size()), kind);
}
Value parse(Runtime& vm, std::string type, std::string text) {
    const auto value = vm.allocate(type);
    (void)call(vm, type, "<init>", "(Ljava/lang/String;)V", {value, vm.string(text)}, InvokeKind::direct);
    return value;
}
constexpr auto objectType = "Lorg/json/JSONObject;";
constexpr auto arrayType = "Lorg/json/JSONArray;";
} // namespace

TEST(java_json_preserves_nested_identity_and_null_contracts) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto object = parse(vm, objectType, R"({"array":[1,null,{"name":"мир"}],"null":null})");
    const auto name = vm.string("array");
    const auto array =
        call(vm, objectType, "getJSONArray", "(Ljava/lang/String;)Lorg/json/JSONArray;", {object, name});
    CHECK(call(vm, objectType, "get", "(Ljava/lang/String;)Ljava/lang/Object;", {object, name}) == array);
    CHECK(call(vm, arrayType, "getInt", "(I)I", {array, Value::integer(0)}).i32() == 1);
    CHECK(call(vm, arrayType, "isNull", "(I)Z", {array, Value::integer(1)}).i32());
    const auto null = vm.getStatic({objectType, "NULL", "Ljava/lang/Object;"});
    CHECK(call(vm, objectType, "get", "(Ljava/lang/String;)Ljava/lang/Object;",
               {object, vm.string("null")}) == null);
    CHECK(call(vm, "Ljava/lang/Object;", "equals", "(Ljava/lang/Object;)Z", {null, Value{}}).i32());
    (void)call(vm, objectType, "put", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;",
               {object, vm.string("null"), Value{}});
    CHECK(!call(vm, objectType, "has", "(Ljava/lang/String;)Z", {object, vm.string("null")}).i32());
    (void)call(vm, arrayType, "put", "(II)Lorg/json/JSONArray;",
               {array, Value::integer(0), Value::integer(42)});
    CHECK(vm.text(call(vm, objectType, "toString", "()Ljava/lang/String;", {object})) ==
          R"({"array":[42,null,{"name":"мир"}]})");
}

TEST(java_json_coerces_optional_values_and_reports_errors) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto object = parse(vm, objectType, R"({"count":"2.8","flag":"TrUe","big":9223372036854775807})");
    CHECK(call(vm, objectType, "getInt", "(Ljava/lang/String;)I", {object, vm.string("count")}).i32() == 2);
    CHECK(call(vm, objectType, "getBoolean", "(Ljava/lang/String;)Z", {object, vm.string("flag")}).i32());
    CHECK(call(vm, objectType, "getLong", "(Ljava/lang/String;)J", {object, vm.string("big")}).i64() ==
          INT64_MAX);
    CHECK(call(vm, objectType, "optInt", "(Ljava/lang/String;I)I",
               {object, vm.string("absent"), Value::integer(7)})
              .i32() == 7);
    try {
        (void)call(vm, objectType, "getJSONArray", "(Ljava/lang/String;)Lorg/json/JSONArray;",
                   {object, vm.string("count")});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Lorg/json/JSONException;");
    }
    const auto array = parse(vm, arrayType, "[]");
    (void)call(vm, arrayType, "put", "(ILjava/lang/Object;)Lorg/json/JSONArray;",
               {array, Value::integer(3), object});
    CHECK(call(vm, arrayType, "length", "()I", {array}).i32() == 4);
    CHECK(call(vm, arrayType, "isNull", "(I)Z", {array, Value::integer(0)}).i32());
    try {
        (void)call(vm, arrayType, "get", "(I)Ljava/lang/Object;", {array, Value::integer(0)});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Lorg/json/JSONException;");
    }
}

TEST(java_json_detects_cycles_and_keeps_keys_iterator_backed) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto object = parse(vm, objectType, "{\"first\":1,\"second\":2}");
    const auto keys = call(vm, objectType, "keys", "()Ljava/util/Iterator;", {object});
    CHECK(vm.text(call(vm, "Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", {keys})) == "first");
    (void)call(vm, "Ljava/util/Iterator;", "remove", "()V", {keys});
    CHECK(!call(vm, objectType, "has", "(Ljava/lang/String;)Z", {object, vm.string("first")}).i32());
    CHECK(vm.text(call(vm, "Ljava/util/Iterator;", "next", "()Ljava/lang/Object;", {keys})) == "second");
    (void)call(vm, objectType, "put", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;",
               {object, vm.string("cycle"), object});
    CHECK(call(vm, objectType, "toString", "()Ljava/lang/String;", {object}).isNull());
    try {
        (void)call(vm, objectType, "toString", "(I)Ljava/lang/String;", {object, Value::integer(2)});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Lorg/json/JSONException;");
    }
}
