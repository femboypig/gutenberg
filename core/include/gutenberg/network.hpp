#pragma once

#include "bytes.hpp"
#include <functional>
#include <map>

namespace gutenberg {

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string, std::less<>> headers;
    Bytes body;
    std::int32_t connectTimeoutMs = 10000;
    std::int32_t readTimeoutMs = 30000;
    bool followRedirects = true;
    bool allowCleartext = true;
    std::size_t responseLimit = 16 * 1024 * 1024;
};

struct HttpResponse {
    std::int32_t status = 0;
    std::map<std::string, std::string, std::less<>> headers;
    Bytes body;
    std::string finalUrl;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest&, const std::function<bool()>& cancelled)>;

} // namespace gutenberg
