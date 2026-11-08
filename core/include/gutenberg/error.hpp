#pragma once

#include <stdexcept>
#include <string>

namespace gutenberg {

enum class ErrorCode {
    io,
    malformed_archive,
    malformed_xml,
    malformed_resources,
    malformed_dex,
    limit_exceeded,
    unsupported,
    verification,
    missing_class,
    missing_method,
    guest_exception,
    cancelled,
    network_timeout,
    network_dns,
    network_connect,
    network_tls
};

class Error final : public std::runtime_error {
  public:
    Error(ErrorCode code, std::string message) : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] ErrorCode code() const noexcept {
        return code_;
    }

  private:
    ErrorCode code_;
};

} // namespace gutenberg
