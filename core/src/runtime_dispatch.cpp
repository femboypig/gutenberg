#include "gutenberg/jit.hpp"
#include "gutenberg/runtime.hpp"
#include <algorithm>
#include <set>

namespace gutenberg {
namespace {
struct Signature {
    std::string_view name, descriptor;
    friend bool operator==(const Signature&, const Signature&) = default;
};
struct SignatureHash {
    std::size_t operator()(Signature value) const {
        const std::hash<std::string_view> hash;
        return hash(value.name) ^ (hash(value.descriptor) << 1);
    }
};
enum class Lookup { declared, hierarchy, virtual_ };
struct KeyView {
    std::string_view owner, name, descriptor;
    Lookup lookup;
};
struct Key {
    std::string owner, name, descriptor;
    Lookup lookup;
    operator KeyView() const {
        return {owner, name, descriptor, lookup};
    }
};
struct KeyHash {
    using is_transparent = void;
    std::size_t operator()(KeyView key) const {
        return std::hash<std::string_view>{}(key.owner) ^ (SignatureHash{}({key.name, key.descriptor}) << 1) ^
               static_cast<std::size_t>(key.lookup);
    }
};
struct KeyEqual {
    using is_transparent = void;
    bool operator()(KeyView a, KeyView b) const {
        return a.owner == b.owner && a.name == b.name && a.descriptor == b.descriptor && a.lookup == b.lookup;
    }
};
struct Resolved {
    NativeMethod* native = nullptr;
    std::shared_ptr<const DexFile> dex;
    const DexMethod* method = nullptr;
    std::string_view owner;
    bool conflict = false;
    explicit operator bool() const {
        return native || method;
    }
    bool concrete() const {
        return native || (method && !(method->access & 0x400));
    }
};
} // namespace

struct Runtime::Dispatch {
    using Index = std::unordered_map<Signature, const DexMethod*, SignatureHash>;
    std::unordered_map<const DexClass*, Index> indices;
    std::unordered_map<Key, Resolved, KeyHash, KeyEqual> cache;

    const DexMethod* declared(const ClassInfo& info, Signature signature) {
        if (!info.definition)
            return nullptr;
        auto entry = indices.find(info.definition);
        if (entry == indices.end()) {
            Index index;
            index.reserve(info.definition->directMethods.size() + info.definition->virtualMethods.size());
            for (const auto* methods : {&info.definition->directMethods, &info.definition->virtualMethods})
                for (const auto& method : *methods) {
                    const auto& ref = info.dex->method(method.index);
                    index.emplace(Signature{ref.name, ref.descriptor}, &method);
                }
            entry = indices.emplace(info.definition, std::move(index)).first;
        }
        const auto found = entry->second.find(signature);
        return found == entry->second.end() ? nullptr : found->second;
    }
    Resolved own(Runtime& vm, const std::string& type, const ClassInfo& info, Signature signature) {
        const auto native =
            vm.natives_.find(type + "->" + std::string(signature.name) + std::string(signature.descriptor));
        if (native != vm.natives_.end())
            return {&native->second, {}, nullptr, type};
        if (const auto* method = declared(info, signature))
            return {nullptr, info.dex, method, type};
        return {};
    }
    Resolved search(Runtime& vm, KeyView key) {
        const Signature signature{key.name, key.descriptor};
        std::string current(key.owner);
        std::unordered_set<std::string> visited;
        std::vector<std::string> pending;
        while (!current.empty()) {
            if (!visited.insert(current).second)
                throw Error(ErrorCode::verification, "cyclic class hierarchy: " + current);
            const auto entry = vm.classes_.find(current);
            if (entry == vm.classes_.end())
                break;
            const auto& info = entry->second;
            if (auto resolved = own(vm, entry->first, info, signature))
                return resolved;
            if (key.lookup == Lookup::declared)
                return {};
            pending.insert(pending.end(), info.interfaces.begin(), info.interfaces.end());
            current = info.super;
        }
        if (key.lookup != Lookup::virtual_)
            return {};
        visited.clear();
        std::vector<Resolved> candidates;
        while (!pending.empty()) {
            auto type = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(type).second)
                continue;
            const auto entry = vm.classes_.find(type);
            if (entry == vm.classes_.end())
                continue;
            const auto& info = entry->second;
            pending.insert(pending.end(), info.interfaces.begin(), info.interfaces.end());
            if (auto resolved = own(vm, entry->first, info, signature))
                if (resolved.native || !(resolved.method->access & (8U | 2U)))
                    candidates.push_back(std::move(resolved));
        }
        std::vector<Resolved> maximal;
        for (const auto& candidate : candidates) {
            const bool shadowed = std::any_of(candidates.begin(), candidates.end(), [&](const auto& other) {
                return other.owner != candidate.owner && vm.assignable(other.owner, candidate.owner);
            });
            if (!shadowed)
                maximal.push_back(candidate);
        }
        Resolved result;
        for (const auto& candidate : maximal)
            if (candidate.concrete()) {
                if (result.concrete()) {
                    result.conflict = true;
                    return result;
                }
                result = candidate;
            }
        return result ? result : maximal.empty() ? Resolved{} : maximal.front();
    }
    Resolved resolve(Runtime& vm, KeyView key) {
        if (const auto found = cache.find(key); found != cache.end())
            return found->second;
        auto result = search(vm, key);
        if (vm.limits_.methodCacheEntries) {
            if (cache.size() >= vm.limits_.methodCacheEntries)
                cache.clear();
            cache.emplace(
                Key{std::string(key.owner), std::string(key.name), std::string(key.descriptor), key.lookup},
                result);
        }
        return result;
    }
};

Runtime::Runtime(RuntimeLimits limits)
    : limits_(limits), jit_(std::make_unique<JitEngine>()), dispatch_(std::make_unique<Dispatch>()) {}
Runtime::~Runtime() = default;
void Runtime::invalidateDispatch() {
    dispatch_->cache.clear();
}

std::optional<MethodLocation> Runtime::findMethod(std::string_view type, std::string_view name,
                                                  std::string_view descriptor) const {
    std::set<std::string_view> visited;
    while (!type.empty() && visited.insert(type).second) {
        const auto found = classes_.find(std::string(type));
        if (found == classes_.end())
            break;
        if (const auto* method = dispatch_->declared(found->second, {name, descriptor}))
            return MethodLocation{found->second.dex, method};
        type = found->second.super;
    }
    return std::nullopt;
}
Value Runtime::dispatch(const MethodRef& ref, std::string_view receiver, Arguments arguments,
                        InvokeKind kind) {
    auto lookup = ref.name == "<init>" || kind == InvokeKind::direct               ? Lookup::declared
                  : kind == InvokeKind::virtual_ || kind == InvokeKind::interface_ ? Lookup::virtual_
                                                                                   : Lookup::hierarchy;
    if (kind == InvokeKind::super) {
        const auto target = classes_.find(std::string(receiver));
        if (target != classes_.end() && target->second.definition &&
            (target->second.definition->access & 0x200))
            lookup = Lookup::virtual_;
    }
    const auto resolved = dispatch_->resolve(*this, {receiver, ref.name, ref.descriptor, lookup});
    if (resolved.conflict)
        raise("Ljava/lang/IncompatibleClassChangeError;", "conflicting interface defaults: " + ref.key());
    if (!resolved)
        raise("Ljava/lang/NoSuchMethodError;", ref.key());
    if (resolved.native)
        return (*resolved.native)(*this, arguments);
    const auto& method = *resolved.method;
    const auto key = std::string(resolved.owner) + "->" + ref.name + ref.descriptor;
    if (bool(method.access & 8) != (kind == InvokeKind::static_))
        raise("Ljava/lang/IncompatibleClassChangeError;", key);
    const auto* code = resolved.dex->code(method);
    if (!code)
        raise(method.access & 0x100 ? "Ljava/lang/UnsatisfiedLinkError;" : "Ljava/lang/AbstractMethodError;",
              key);
    try {
        if (jitEnabled_)
            if (auto result = jit_->execute(*this, *code, arguments))
                return *result;
        return execute(*resolved.dex, *code, arguments, key);
    } catch (GuestException& error) {
        error.frames.push_back(key);
        throw;
    }
}
} // namespace gutenberg
