#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace gutenberg {
namespace {
struct Url {
    std::string scheme, authority, host, path, query, fragment;
    std::int32_t port = -1;
    bool hasQuery = false, hasFragment = false;
};
Url parse(Runtime& vm, std::string_view value) {
    if (value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos)
        vm.raise("Ljava/net/MalformedURLException;", "invalid URL characters");
    const auto separator = value.find("://");
    if (separator == value.npos)
        vm.raise("Ljava/net/MalformedURLException;", "URL has no protocol");
    Url result;
    result.scheme = value.substr(0, separator);
    std::transform(result.scheme.begin(), result.scheme.end(), result.scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (result.scheme != "http" && result.scheme != "https")
        vm.raise("Ljava/net/MalformedURLException;", "unsupported URL protocol");
    const auto start = separator + 3;
    const auto end = value.find_first_of("/?#", start);
    result.authority = value.substr(start, end == value.npos ? value.size() - start : end - start);
    std::string_view address(result.authority);
    if (const auto at = address.rfind('@'); at != address.npos)
        address.remove_prefix(at + 1);
    auto port = address.npos;
    if (address.starts_with('[')) {
        const auto bracket = address.find(']');
        if (bracket == address.npos)
            vm.raise("Ljava/net/MalformedURLException;", "invalid IPv6 address");
        result.host = address.substr(0, bracket + 1);
        if (bracket + 1 < address.size()) {
            if (address[bracket + 1] != ':')
                vm.raise("Ljava/net/MalformedURLException;", "invalid authority");
            port = bracket + 1;
        }
    } else {
        port = address.rfind(':');
        result.host = address.substr(0, port);
    }
    if (result.host.empty())
        vm.raise("Ljava/net/MalformedURLException;", "URL has no host");
    if (port != address.npos) {
        const auto text = address.substr(port + 1);
        const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), result.port);
        if (error != std::errc{} || last != text.data() + text.size() || result.port < 0 ||
            result.port > 65535)
            vm.raise("Ljava/net/MalformedURLException;", "invalid port");
    }
    if (end != value.npos) {
        auto remainder = value.substr(end);
        const auto hash = remainder.find('#');
        if (hash != remainder.npos) {
            result.hasFragment = true;
            result.fragment = remainder.substr(hash + 1);
            remainder = remainder.substr(0, hash);
        }
        const auto question = remainder.find('?');
        if (question != remainder.npos) {
            result.hasQuery = true;
            result.query = remainder.substr(question + 1);
            remainder = remainder.substr(0, question);
        }
        result.path = remainder;
    }
    return result;
}
struct NetworkContext {
    JavaServices services;
    std::size_t reserved = 0;
};
struct Connection : NativeObject {
    HttpResponse response;
    std::shared_ptr<NetworkContext> context;
    std::size_t reserved = 0;
    ~Connection() override {
        if (context)
            context->reserved -= reserved;
    }
};
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::shared_ptr<Connection> connect(Runtime& vm, Value value,
                                    const std::shared_ptr<NetworkContext>& context) {
    const auto& services = context->services;
    auto& object = vm.object(value);
    if (object.fields["disconnected"].bits)
        vm.raise("Ljava/io/IOException;", "connection is closed");
    if (auto connected = std::dynamic_pointer_cast<Connection>(object.native))
        return connected;
    if (!services.internetPermission)
        vm.raise("Ljava/lang/SecurityException;", "package does not declare android.permission.INTERNET");
    if (vm.hasClass("Landroid/os/StrictMode;")) {
        const auto policy =
            vm.getStatic({"Landroid/os/StrictMode;", "threadPolicy", "Landroid/os/StrictMode$ThreadPolicy;"});
        if (!vm.object(policy).fields["allowNetwork"].bits)
            vm.raise("Landroid/os/NetworkOnMainThreadException;", "network access on the activity thread");
    }
    const auto address = parse(vm, vm.text(value));
    if (address.scheme == "http" && !services.allowCleartext)
        vm.raise("Ljava/lang/SecurityException;", "cleartext HTTP is disabled by the package policy");
    if (!services.http)
        vm.raise("Ljava/io/IOException;", "network transport is unavailable");
    HttpRequest request;
    request.url = vm.text(value);
    request.allowCleartext = services.allowCleartext;
    if (object.fields.contains("method"))
        request.method = vm.text(object.fields["method"]);
    for (const auto& [name, header] : object.fields)
        if (name.starts_with("header:"))
            request.headers[name.substr(7)] = vm.text(header);
    if (const auto body = object.fields.find("output"); body != object.fields.end())
        request.body = outputStreamBytes(vm, body->second);
    if (const auto timeout = object.fields.find("connectTimeout"); timeout != object.fields.end())
        request.connectTimeoutMs = timeout->second.i32();
    if (const auto timeout = object.fields.find("readTimeout"); timeout != object.fields.end())
        request.readTimeoutMs = timeout->second.i32();
    if (const auto redirects = object.fields.find("followRedirects"); redirects != object.fields.end())
        request.followRedirects = redirects->second.bits != 0;
    auto result = std::make_shared<Connection>();
    try {
        result->response = services.http(request, [&vm] { return vm.isCancelled(); });
    } catch (const Error& error) {
        if (error.code() == ErrorCode::cancelled)
            throw;
        const auto type = error.code() == ErrorCode::network_timeout   ? "Ljava/net/SocketTimeoutException;"
                          : error.code() == ErrorCode::network_dns     ? "Ljava/net/UnknownHostException;"
                          : error.code() == ErrorCode::network_connect ? "Ljava/net/ConnectException;"
                          : error.code() == ErrorCode::network_tls ? "Ljavax/net/ssl/SSLHandshakeException;"
                                                                   : "Ljava/io/IOException;";
        vm.raise(type, error.what());
    }
    if (result->response.body.size() > request.responseLimit)
        vm.raise("Ljava/io/IOException;", "response size limit exceeded");
    if (result->response.status < 100 || result->response.status > 599)
        vm.raise("Ljava/io/IOException;", "invalid HTTP status");
    if (result->response.body.size() > (services.networkMemoryLimit - context->reserved) / 2)
        vm.raise("Ljava/io/IOException;", "session network memory limit exceeded");
    result->context = context;
    result->reserved = result->response.body.size() * 2;
    context->reserved += result->reserved;
    object.native = result;
    return result;
}
void mutableConnection(Runtime& vm, Value value) {
    if (vm.object(value).native)
        vm.raise("Ljava/lang/IllegalStateException;", "connection is already open");
}
Value input(Runtime& vm, Value value, const std::shared_ptr<NetworkContext>& context, bool errorStream) {
    if (!errorStream) {
        const auto option = vm.object(value).fields.find("DoInput");
        if (option != vm.object(value).fields.end() && !option->second.bits)
            vm.raise("Ljava/net/ProtocolException;", "connection input is disabled");
    }
    auto state = errorStream ? std::dynamic_pointer_cast<Connection>(vm.object(value).native)
                             : connect(vm, value, context);
    if (errorStream && (!state || state->response.status < 400))
        return {};
    if (!errorStream && state->response.status >= 400)
        vm.raise(state->response.status == 404 ? "Ljava/io/FileNotFoundException;" : "Ljava/io/IOException;",
                 "HTTP status " + std::to_string(state->response.status));
    auto& stream = vm.object(value).fields["input"];
    if (stream.isNull())
        stream = memoryInputStream(vm, state->response.body, state);
    return stream;
}
Value connection(Runtime& vm, Value url) {
    const auto scheme = parse(vm, vm.text(url)).scheme;
    const auto result = vm.allocate(scheme == "https" ? "Ljavax/net/ssl/HttpsURLConnection;"
                                                      : "Ljava/net/HttpURLConnection;");
    vm.object(result).text = vm.object(url).text;
    vm.object(result).fields["url"] = url;
    return result;
}
std::int64_t contentLength(const HttpResponse& response) {
    for (const auto& [name, value] : response.headers)
        if (lower(name) == "content-length") {
            std::int64_t length = -1;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), length);
            return error == std::errc{} && end == value.data() + value.size() && length >= 0 ? length : -1;
        }
    return -1;
}
} // namespace

void installJavaNetwork(Runtime& vm, JavaServices services) {
    const auto context = std::make_shared<NetworkContext>(NetworkContext{std::move(services), 0});
    vm.defineClass("Ljava/net/URL;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"});
    vm.defineClass("Ljava/net/URLConnection;");
    vm.defineClass("Ljava/net/HttpURLConnection;", "Ljava/net/URLConnection;");
    vm.defineClass("Ljavax/net/ssl/HttpsURLConnection;", "Ljava/net/HttpURLConnection;");
    vm.defineClass("Ljavax/net/ssl/SSLException;", "Ljava/io/IOException;");
    vm.defineClass("Ljavax/net/ssl/SSLHandshakeException;", "Ljavax/net/ssl/SSLException;");
    for (const auto* type : {"MalformedURLException", "ProtocolException", "SocketTimeoutException",
                             "ConnectException", "UnknownHostException"}) {
        const std::string owner = "Ljava/net/" + std::string(type) + ";";
        vm.defineClass(owner, "Ljava/io/IOException;");
        vm.bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
        vm.bind(owner, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
            r.object(a[0]).text = a[1].isNull() ? u"" : r.object(a[1]).text;
            return Value{};
        });
    }
    const std::string url = "Ljava/net/URL;", base = "Ljava/net/URLConnection;",
                      http = "Ljava/net/HttpURLConnection;";
    vm.bind(url, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        (void)parse(r, r.text(a[1]));
        r.object(a[0]).text = r.object(a[1]).text;
        return Value{};
    });
    vm.bind(url, "toString", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    vm.bind(url, "toExternalForm", "()Ljava/lang/String;",
            [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    for (const auto* property : {"Host", "Protocol", "Path", "Query", "Ref", "Authority", "File"}) {
        const std::string name(property);
        vm.bind(url, "get" + name, "()Ljava/lang/String;", [name](Runtime& r, Arguments a) {
            const auto value = parse(r, r.text(a[0]));
            const auto text = name == "Host"        ? value.host
                              : name == "Protocol"  ? value.scheme
                              : name == "Path"      ? value.path
                              : name == "Query"     ? value.query
                              : name == "Ref"       ? value.fragment
                              : name == "Authority" ? value.authority
                                                    : value.path + (value.hasQuery ? "?" + value.query : "");
            if ((name == "Query" && !value.hasQuery) || (name == "Ref" && !value.hasFragment))
                return Value{};
            return r.string(text);
        });
    }
    vm.bind(url, "getPort", "()I",
            [](Runtime& r, Arguments a) { return Value::integer(parse(r, r.text(a[0])).port); });
    vm.bind(url, "getDefaultPort", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(parse(r, r.text(a[0])).scheme == "https" ? 443 : 80);
    });
    vm.bind(url, "openConnection", "()Ljava/net/URLConnection;",
            [](Runtime& r, Arguments a) { return connection(r, a[0]); });
    vm.bind(url, "openStream", "()Ljava/io/InputStream;",
            [context](Runtime& r, Arguments a) { return input(r, connection(r, a[0]), context, false); });
    vm.bind(base, "getURL", "()Ljava/net/URL;",
            [](Runtime& r, Arguments a) { return r.object(a[0]).fields["url"]; });
    vm.bind(base, "connect", "()V", [context](Runtime& r, Arguments a) {
        (void)connect(r, a[0], context);
        return Value{};
    });
    vm.bind(base, "getInputStream", "()Ljava/io/InputStream;",
            [context](Runtime& r, Arguments a) { return input(r, a[0], context, false); });
    vm.bind(base, "getOutputStream", "()Ljava/io/OutputStream;", [](Runtime& r, Arguments a) {
        mutableConnection(r, a[0]);
        auto& fields = r.object(a[0]).fields;
        if (!fields["DoOutput"].bits)
            r.raise("Ljava/net/ProtocolException;", "connection output is disabled");
        if (fields.contains("method") &&
            (r.text(fields["method"]) == "HEAD" || r.text(fields["method"]) == "TRACE"))
            r.raise("Ljava/net/ProtocolException;", "request method cannot have a body");
        if (!fields.contains("method") || r.text(fields["method"]) == "GET")
            fields["method"] = r.string("POST");
        if (!fields.contains("output"))
            fields["output"] = memoryOutputStream(r);
        return fields["output"];
    });
    for (const auto* property : {"ConnectTimeout", "ReadTimeout"}) {
        const std::string name(property), key = name == "ConnectTimeout" ? "connectTimeout" : "readTimeout";
        vm.bind(base, "set" + name, "(I)V", [key](Runtime& r, Arguments a) {
            if (a[1].i32() < 0)
                r.raise("Ljava/lang/IllegalArgumentException;", "negative timeout");
            r.object(a[0]).fields[key] = a[1];
            return Value{};
        });
        vm.bind(base, "get" + name, "()I", [key](Runtime& r, Arguments a) {
            const auto item = r.object(a[0]).fields.find(key);
            return item == r.object(a[0]).fields.end() ? Value::integer(0) : item->second;
        });
    }
    for (const auto* property : {"DoInput", "DoOutput", "UseCaches", "AllowUserInteraction"}) {
        const std::string key(property);
        vm.bind(base, "set" + key, "(Z)V", [key](Runtime& r, Arguments a) {
            mutableConnection(r, a[0]);
            r.object(a[0]).fields[key] = a[1];
            return Value{};
        });
        vm.bind(base, "get" + key, "()Z", [key](Runtime& r, Arguments a) {
            const auto item = r.object(a[0]).fields.find(key);
            return item == r.object(a[0]).fields.end()
                       ? Value::integer(key == "DoInput" || key == "UseCaches")
                       : item->second;
        });
    }
    vm.bind(base, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V",
            [](Runtime& r, Arguments a) {
                mutableConnection(r, a[0]);
                if (a[1].isNull())
                    r.raise("Ljava/lang/NullPointerException;", "null header name");
                const auto name = r.text(a[1]), value = r.text(a[2]);
                if (name.empty())
                    r.raise("Ljava/lang/IllegalArgumentException;", "empty header name");
                for (char c : name)
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) &&
                        std::string_view("!#$%&'*+-.^_`|~").find(c) == std::string_view::npos)
                        r.raise("Ljava/lang/IllegalArgumentException;", "invalid header name");
                if (value.find_first_of("\r\n") != value.npos || value.find('\0') != value.npos)
                    r.raise("Ljava/lang/IllegalArgumentException;", "invalid header value");
                r.object(a[0]).fields["header:" + lower(name)] = a[2];
                return Value{};
            });
    vm.bind(base, "getRequestProperty", "(Ljava/lang/String;)Ljava/lang/String;",
            [](Runtime& r, Arguments a) {
                mutableConnection(r, a[0]);
                const auto item = r.object(a[0]).fields.find("header:" + lower(r.text(a[1])));
                return item == r.object(a[0]).fields.end() ? Value{} : item->second;
            });
    vm.bind(base, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;",
            [context](Runtime& r, Arguments a) {
                const auto response = connect(r, a[0], context);
                if (a[1].isNull())
                    return r.string("HTTP/1.1 " + std::to_string(response->response.status));
                const auto key = lower(r.text(a[1]));
                for (const auto& [name, value] : response->response.headers)
                    if (lower(name) == key)
                        return r.string(value);
                return Value{};
            });
    vm.bind(base, "getContentLength", "()I", [context](Runtime& r, Arguments a) {
        const auto length = contentLength(connect(r, a[0], context)->response);
        return Value::integer(
            length > std::numeric_limits<std::int32_t>::max() ? -1 : static_cast<std::int32_t>(length));
    });
    vm.bind(base, "getContentLengthLong", "()J", [context](Runtime& r, Arguments a) {
        return Value::wide(contentLength(connect(r, a[0], context)->response));
    });
    for (const auto* property : {"ContentType", "ContentEncoding"}) {
        const std::string name(property), key = name == "ContentType" ? "content-type" : "content-encoding";
        vm.bind(base, "get" + name, "()Ljava/lang/String;", [context, key](Runtime& r, Arguments a) {
            const auto response = connect(r, a[0], context);
            for (const auto& [header, value] : response->response.headers)
                if (lower(header) == key)
                    return r.string(value);
            return Value{};
        });
    }
    vm.bind(http, "setRequestMethod", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        mutableConnection(r, a[0]);
        const auto method = r.text(a[1]);
        if (method.empty() ||
            !std::all_of(method.begin(), method.end(), [](char c) { return c >= 'A' && c <= 'Z'; }))
            r.raise("Ljava/net/ProtocolException;", "invalid request method");
        r.object(a[0]).fields["method"] = a[1];
        return Value{};
    });
    vm.bind(http, "getRequestMethod", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto it = r.object(a[0]).fields.find("method");
        return it == r.object(a[0]).fields.end() ? r.string("GET") : it->second;
    });
    vm.bind(http, "getResponseCode", "()I", [context](Runtime& r, Arguments a) {
        return Value::integer(connect(r, a[0], context)->response.status);
    });
    vm.bind(http, "getErrorStream", "()Ljava/io/InputStream;",
            [context](Runtime& r, Arguments a) { return input(r, a[0], context, true); });
    vm.bind(http, "setInstanceFollowRedirects", "(Z)V", [](Runtime& r, Arguments a) {
        mutableConnection(r, a[0]);
        r.object(a[0]).fields["followRedirects"] = a[1];
        return Value{};
    });
    vm.bind(http, "getInstanceFollowRedirects", "()Z", [](Runtime& r, Arguments a) {
        const auto it = r.object(a[0]).fields.find("followRedirects");
        return it == r.object(a[0]).fields.end() ? Value::integer(1) : it->second;
    });
    vm.bind(http, "usingProxy", "()Z", [](Runtime&, Arguments) { return Value::integer(0); });
    vm.bind(http, "disconnect", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).fields["disconnected"] = Value::integer(1);
        return Value{};
    });
}

} // namespace gutenberg
