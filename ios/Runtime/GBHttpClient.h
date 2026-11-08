#pragma once
#include "gutenberg/network.hpp"

namespace gutenberg {
[[nodiscard]] HttpResponse performHttpRequest(const HttpRequest& request,
                                              const std::function<bool()>& cancelled);
}
