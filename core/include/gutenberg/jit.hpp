#pragma once

#include "runtime.hpp"
#include <memory>

namespace gutenberg {

struct JitStatistics {
    std::size_t compiledMethods = 0;
    std::size_t interpretedMethods = 0;
    std::uint64_t executions = 0;
};

class JitEngine {
  public:
    JitEngine();
    ~JitEngine();
    JitEngine(const JitEngine&) = delete;
    JitEngine& operator=(const JitEngine&) = delete;
    [[nodiscard]] std::optional<Value> execute(Runtime& runtime, const Code& code, Arguments arguments);
    [[nodiscard]] const JitStatistics& statistics() const noexcept;
    [[nodiscard]] bool available() const noexcept;

  private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace gutenberg
