#include "gutenberg/java.hpp"
#include "test.hpp"

using namespace gutenberg;
namespace {
Value call(Runtime& vm, std::string owner, std::string name, std::string descriptor,
           std::initializer_list<Value> args, InvokeKind kind = InvokeKind::virtual_) {
    return vm.invoke(std::move(owner), std::move(name), std::move(descriptor),
                     Arguments(args.begin(), args.size()), kind);
}
Value open(Runtime& vm, std::string text) {
    const auto url = vm.allocate("Ljava/net/URL;");
    (void)call(vm, "Ljava/net/URL;", "<init>", "(Ljava/lang/String;)V", {url, vm.string(text)},
               InvokeKind::direct);
    return call(vm, "Ljava/net/URL;", "openConnection", "()Ljava/net/URLConnection;", {url});
}
} // namespace

TEST(java_http_connection_fetches_once_and_returns_stream) {
    Runtime vm;
    JavaServices services;
    unsigned requests = 0;
    services.http = [&](const HttpRequest& request, const std::function<bool()>& cancelled) {
        CHECK(request.url == "https://example.test/data");
        CHECK(request.method == "GET");
        CHECK(!cancelled());
        CHECK(!request.headers.contains("missing"));
        ++requests;
        return HttpResponse{200,
                            {{"Content-Type", "text/plain"}, {"Content-Length", "5"}},
                            Bytes{'h', 'e', 'l', 'l', 'o'},
                            request.url};
    };
    installJavaRuntime(vm, services);
    const auto connection = open(vm, "https://example.test/data");
    CHECK(call(vm, "Ljava/net/URLConnection;", "getRequestProperty", "(Ljava/lang/String;)Ljava/lang/String;",
               {connection, vm.string("missing")})
              .isNull());
    CHECK(call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection}).i32() == 200);
    CHECK(call(vm, "Ljava/net/URLConnection;", "getContentLength", "()I", {connection}).i32() == 5);
    CHECK(vm.text(call(vm, "Ljava/net/URLConnection;", "getContentType", "()Ljava/lang/String;",
                       {connection})) == "text/plain");
    const auto stream =
        call(vm, "Ljava/net/URLConnection;", "getInputStream", "()Ljava/io/InputStream;", {connection});
    CHECK(call(vm, "Ljava/net/URLConnection;", "getInputStream", "()Ljava/io/InputStream;", {connection}) ==
          stream);
    CHECK(call(vm, "Ljava/io/InputStream;", "read", "()I", {stream}).i32() == 'h');
    CHECK(requests == 1);
}

TEST(java_http_post_preserves_body_and_headers) {
    Runtime vm;
    JavaServices services;
    services.http = [](const HttpRequest& request, const std::function<bool()>&) {
        CHECK(request.method == "POST");
        CHECK(request.body == Bytes({'A'}));
        CHECK(request.headers.at("x-test") == "value");
        CHECK(!request.followRedirects);
        return HttpResponse{204, {}, {}, request.url};
    };
    installJavaRuntime(vm, services);
    const auto connection = open(vm, "https://example.test/post");
    (void)call(vm, "Ljava/net/URLConnection;", "setDoOutput", "(Z)V", {connection, Value::integer(1)});
    (void)call(vm, "Ljava/net/URLConnection;", "setRequestProperty",
               "(Ljava/lang/String;Ljava/lang/String;)V",
               {connection, vm.string("X-Test"), vm.string("value")});
    (void)call(vm, "Ljava/net/HttpURLConnection;", "setInstanceFollowRedirects", "(Z)V",
               {connection, Value::integer(0)});
    const auto output =
        call(vm, "Ljava/net/URLConnection;", "getOutputStream", "()Ljava/io/OutputStream;", {connection});
    (void)call(vm, "Ljava/io/OutputStream;", "write", "(I)V", {output, Value::integer('A')});
    CHECK(call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection}).i32() == 204);
    CHECK(call(vm, "Ljava/net/URLConnection;", "getContentLength", "()I", {connection}).i32() == -1);
}

TEST(java_http_error_body_is_available_after_error_status) {
    Runtime vm;
    JavaServices services;
    services.http = [](const HttpRequest&, const std::function<bool()>&) {
        return HttpResponse{404, {}, Bytes{'x'}, {}};
    };
    installJavaRuntime(vm, services);
    const auto connection = open(vm, "https://example.test/missing");
    CHECK(call(vm, "Ljava/net/HttpURLConnection;", "getErrorStream", "()Ljava/io/InputStream;", {connection})
              .isNull());
    CHECK(call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection}).i32() == 404);
    try {
        (void)call(vm, "Ljava/net/URLConnection;", "getInputStream", "()Ljava/io/InputStream;", {connection});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/io/FileNotFoundException;");
    }
    const auto stream =
        call(vm, "Ljava/net/HttpURLConnection;", "getErrorStream", "()Ljava/io/InputStream;", {connection});
    CHECK(call(vm, "Ljava/io/InputStream;", "read", "()I", {stream}).i32() == 'x');
}

TEST(java_http_policy_blocks_requests_before_transport) {
    for (bool internet : {false, true}) {
        Runtime vm;
        JavaServices services;
        services.internetPermission = internet;
        services.allowCleartext = false;
        services.http = [](const HttpRequest&, const std::function<bool()>&) -> HttpResponse {
            throw std::runtime_error("policy did not block network");
        };
        installJavaRuntime(vm, services);
        const auto connection = open(vm, "http://example.test/data");
        try {
            (void)call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection});
            CHECK(false);
        } catch (const GuestException& error) {
            CHECK(vm.object(error.value).type == "Ljava/lang/SecurityException;");
        }
    }
}

TEST(java_http_rejects_header_injection_and_disabled_output) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto connection = open(vm, "https://example.test/");
    try {
        (void)call(vm, "Ljava/net/URLConnection;", "setRequestProperty",
                   "(Ljava/lang/String;Ljava/lang/String;)V",
                   {connection, vm.string("X-Test"), vm.string("a\r\nInjected: b")});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/lang/IllegalArgumentException;");
    }
    try {
        (void)call(vm, "Ljava/net/URLConnection;", "getOutputStream", "()Ljava/io/OutputStream;",
                   {connection});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/net/ProtocolException;");
    }
}

TEST(java_http_bounds_retained_response_memory) {
    Runtime vm;
    JavaServices services;
    services.networkMemoryLimit = 4;
    services.http = [](const HttpRequest&, const std::function<bool()>&) {
        return HttpResponse{200, {}, Bytes{'a', 'b', 'c'}, {}};
    };
    installJavaRuntime(vm, services);
    const auto connection = open(vm, "https://example.test/data");
    try {
        (void)call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/io/IOException;");
    }
}

TEST(java_http_maps_transport_timeouts_to_java_exceptions) {
    Runtime vm;
    JavaServices services;
    services.http = [](const HttpRequest&, const std::function<bool()>&) -> HttpResponse {
        throw Error(ErrorCode::network_timeout, "timed out");
    };
    installJavaRuntime(vm, services);
    const auto connection = open(vm, "https://example.test/data");
    try {
        (void)call(vm, "Ljava/net/HttpURLConnection;", "getResponseCode", "()I", {connection});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.object(error.value).type == "Ljava/net/SocketTimeoutException;");
    }
}
