#pragma once

#include "dex.hpp"
#include "value.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace gutenberg {

class Runtime;
class JitEngine;
using Arguments = std::span<const Value>;
using NativeMethod = std::function<Value(Runtime&, Arguments)>;
enum class InvokeKind { direct, virtual_, static_, super, interface_ };

struct RuntimeLimits {
    std::uint64_t instructions = 10000000;
    std::size_t objects = 250000;
    std::size_t arrayElements = 4000000;
    std::size_t callDepth = 256;
};

class GuestException final : public std::exception {
  public:
    GuestException(Value value, std::string message) : value(value), message_(std::move(message)) {}
    [[nodiscard]] const char* what() const noexcept override {
        return message_.c_str();
    }
    Value value;
    std::vector<std::string> frames;

  private:
    std::string message_;
};

struct MethodLocation {
    std::shared_ptr<const DexFile> dex;
    const DexMethod* method;
};

class Runtime {
  public:
    explicit Runtime(RuntimeLimits limits = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void load(std::shared_ptr<const DexFile> dex);
    void defineClass(std::string type, std::string super = "Ljava/lang/Object;",
                     std::vector<std::string> interfaces = {});
    void bind(std::string owner, std::string name, std::string descriptor, NativeMethod implementation);
    [[nodiscard]] Value invoke(const MethodRef& method, Arguments arguments,
                               InvokeKind kind = InvokeKind::virtual_);
    [[nodiscard]] Value invoke(std::string owner, std::string name, std::string descriptor,
                               Arguments arguments, InvokeKind kind = InvokeKind::virtual_);
    [[nodiscard]] Value execute(const DexFile& dex, const Code& code, Arguments arguments,
                                std::string_view context = {});

    [[nodiscard]] Value allocate(std::string type);
    [[nodiscard]] Value array(std::string type, std::int32_t size);
    [[nodiscard]] Value string(std::string_view utf8Text, bool intern = false);
    [[nodiscard]] Value string(std::u16string text, bool intern = false);
    [[nodiscard]] std::string text(Value value) const;
    [[nodiscard]] Value classObject(std::string type);
    [[nodiscard]] Object& object(Value value);
    [[nodiscard]] const Object& object(Value value) const;
    [[nodiscard]] bool assignable(std::string_view actual, std::string_view expected) const;
    [[nodiscard]] bool hasClass(std::string_view type) const;
    [[nodiscard]] std::optional<MethodLocation> findMethod(std::string_view type, std::string_view name,
                                                           std::string_view descriptor) const;
    [[nodiscard]] Value getStatic(const FieldRef& field);
    void putStatic(const FieldRef& field, Value value);
    [[nodiscard]] Value getField(Value receiver, const FieldRef& field);
    void putField(Value receiver, const FieldRef& field, Value value);
    void defineField(std::string owner, std::string name, std::string type, Value initial = {});
    void initialize(std::string_view type);
    [[noreturn]] void raise(std::string type, std::string message);
    void cancel() noexcept {
        cancelled_.store(true, std::memory_order_relaxed);
    }
    [[nodiscard]] bool isCancelled() const noexcept {
        return cancelled_.load(std::memory_order_relaxed);
    }
    void resetCancellation() noexcept {
        cancelled_.store(false, std::memory_order_relaxed);
    }
    void tick();
    [[nodiscard]] std::size_t heapSize() const noexcept {
        return heap_.size();
    }
    [[nodiscard]] std::uint64_t instructionCount() const noexcept {
        return instructions_;
    }
    [[nodiscard]] const RuntimeLimits& limits() const noexcept {
        return limits_;
    }
    [[nodiscard]] JitEngine& jit() noexcept;
    void enableJit(bool enabled) noexcept {
        jitEnabled_ = enabled;
    }
    std::function<void(std::string_view)> logger;

  private:
    struct ClassInfo {
        std::string super;
        std::vector<std::string> interfaces;
        std::shared_ptr<const DexFile> dex;
        const DexClass* definition = nullptr;
        std::unordered_map<std::string, Value> instanceDefaults;
        enum class State { unloaded, initializing, ready, failed } state = State::unloaded;
    };
    [[nodiscard]] Value initialValue(const DexFile& dex, const EncodedValue& value);
    [[nodiscard]] std::string fieldOwner(const FieldRef& ref) const;
    RuntimeLimits limits_;
    std::unordered_map<std::string, ClassInfo> classes_;
    std::unordered_map<std::string, NativeMethod> natives_;
    std::unordered_map<std::string, Value> statics_;
    std::unordered_map<std::u16string, Value> interns_;
    std::unordered_map<std::string, Value> mirrors_;
    std::vector<std::unique_ptr<Object>> heap_;
    std::atomic<bool> cancelled_ = false;
    std::uint64_t instructions_ = 0;
    std::size_t depth_ = 0;
    std::size_t arrayElements_ = 0;
    std::unique_ptr<JitEngine> jit_;
    bool jitEnabled_ = true;
};

[[nodiscard]] MethodRef methodRef(std::string owner, std::string name, std::string descriptor);

} // namespace gutenberg
