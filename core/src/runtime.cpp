#include "gutenberg/runtime.hpp"
#include "gutenberg/jit.hpp"
#include <algorithm>
#include <set>
#include <utility>

namespace gutenberg {

MethodRef methodRef(std::string owner, std::string name, std::string descriptor) {
    MethodRef result{std::move(owner), std::move(name), std::move(descriptor), {}, {}};
    const auto& text = result.descriptor;
    if (text.empty() || text.front() != '(')
        throw Error(ErrorCode::verification, "invalid method descriptor");
    auto parseType = [&](std::size_t& cursor, bool allowVoid) {
        const auto start = cursor;
        while (cursor < text.size() && text[cursor] == '[')
            ++cursor;
        if (cursor == text.size())
            throw Error(ErrorCode::verification, "incomplete type descriptor");
        const auto kind = text[cursor++];
        if (kind == 'L') {
            const auto end = text.find(';', cursor);
            if (end == text.npos || end == cursor)
                throw Error(ErrorCode::verification, "invalid object descriptor");
            cursor = end + 1;
        } else if (std::string_view("ZBCSIJFD").find(kind) == std::string_view::npos &&
                   !(allowVoid && kind == 'V' && cursor == start + 1))
            throw Error(ErrorCode::verification, "invalid primitive descriptor");
        return text.substr(start, cursor - start);
    };
    std::size_t cursor = 1;
    while (cursor < text.size() && text[cursor] != ')')
        result.parameters.push_back(parseType(cursor, false));
    if (cursor == text.size())
        throw Error(ErrorCode::verification, "unterminated method descriptor");
    ++cursor;
    result.returnType = parseType(cursor, true);
    if (cursor != text.size())
        throw Error(ErrorCode::verification, "trailing method descriptor data");
    return result;
}

Runtime::Runtime(RuntimeLimits limits) : limits_(limits), jit_(std::make_unique<JitEngine>()) {}
Runtime::~Runtime() = default;
Runtime::ThreadScope::ThreadScope(Runtime& runtime, std::shared_ptr<GuestThreadIdentity> previous)
    : runtime_(&runtime), previous_(std::move(previous)) {}
Runtime::ThreadScope::ThreadScope(ThreadScope&& other) noexcept
    : runtime_(std::exchange(other.runtime_, nullptr)), previous_(std::move(other.previous_)) {}
Runtime::ThreadScope::~ThreadScope() {
    if (runtime_)
        runtime_->threadIdentity_ = std::move(previous_);
}
Runtime::ThreadScope Runtime::enterThread(std::shared_ptr<GuestThreadIdentity> thread) {
    if (!thread || depth_)
        throw Error(ErrorCode::verification, "thread context changes require a quiescent runtime");
    auto previous = std::exchange(threadIdentity_, std::move(thread));
    return ThreadScope(*this, std::move(previous));
}
JitEngine& Runtime::jit() noexcept {
    return *jit_;
}

void Runtime::defineClass(std::string type, std::string super, std::vector<std::string> interfaces) {
    if (classes_.contains(type))
        throw Error(ErrorCode::verification, "duplicate class: " + type);
    ClassInfo info;
    info.super = std::move(super);
    info.interfaces = std::move(interfaces);
    info.state = ClassInfo::State::ready;
    classes_.emplace(std::move(type), std::move(info));
}

void Runtime::bind(std::string owner, std::string name, std::string descriptor, NativeMethod implementation) {
    const auto ref = methodRef(std::move(owner), std::move(name), std::move(descriptor));
    if (!natives_.emplace(ref.key(), std::move(implementation)).second)
        throw Error(ErrorCode::verification, "duplicate native method: " + ref.key());
}

void Runtime::load(std::shared_ptr<const DexFile> dex) {
    for (const auto& cls : dex->classes()) {
        if (const auto found = classes_.find(cls.type); found != classes_.end() && found->second.definition)
            throw Error(ErrorCode::verification, "duplicate class: " + cls.type);
    }
    for (const auto& cls : dex->classes()) {
        if (classes_.contains(cls.type))
            continue;
        ClassInfo info;
        info.super = cls.super;
        info.interfaces = cls.interfaces;
        info.dex = dex;
        info.definition = &cls;
        classes_.emplace(cls.type, std::move(info));
    }
}

bool Runtime::hasClass(std::string_view type) const {
    return classes_.contains(std::string(type));
}

bool Runtime::assignable(std::string_view actual, std::string_view expected) const {
    if (actual == expected)
        return true;
    if (actual.empty() || expected.empty())
        return false;
    if (actual.front() == '[') {
        if (expected == "Ljava/lang/Object;" || expected == "Ljava/lang/Cloneable;" ||
            expected == "Ljava/io/Serializable;")
            return true;
        if (expected.front() != '[')
            return false;
        const auto a = actual.substr(1), b = expected.substr(1);
        if (a.front() != 'L' && a.front() != '[')
            return a == b;
        return assignable(a, b);
    }
    std::vector<std::string> pending{std::string(actual)};
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        auto name = std::move(pending.back());
        pending.pop_back();
        if (name == expected)
            return true;
        if (!visited.insert(name).second)
            continue;
        const auto entry = classes_.find(name);
        if (entry == classes_.end())
            continue;
        if (!entry->second.super.empty())
            pending.push_back(entry->second.super);
        for (const auto& parent : entry->second.interfaces)
            pending.push_back(parent);
    }
    return false;
}

std::optional<MethodLocation> Runtime::findMethod(std::string_view type, std::string_view name,
                                                  std::string_view descriptor) const {
    std::unordered_set<std::string> visited;
    std::string current(type);
    while (!current.empty() && visited.insert(current).second) {
        const auto it = classes_.find(current);
        if (it == classes_.end())
            break;
        const auto& info = it->second;
        if (info.definition) {
            for (const auto* methods : {&info.definition->directMethods, &info.definition->virtualMethods}) {
                for (const auto& method : *methods) {
                    const auto& ref = info.dex->method(method.index);
                    if (ref.name == name && ref.descriptor == descriptor)
                        return MethodLocation{info.dex, &method};
                }
            }
        }
        current = info.super;
    }
    return std::nullopt;
}

Value Runtime::invoke(std::string owner, std::string name, std::string descriptor, Arguments arguments,
                      InvokeKind kind) {
    return invoke(methodRef(std::move(owner), std::move(name), std::move(descriptor)), arguments, kind);
}

Value Runtime::invoke(const MethodRef& ref, Arguments arguments, InvokeKind kind) {
    if (depth_ >= limits_.callDepth)
        raise("Ljava/lang/StackOverflowError;", "call depth limit exceeded");
    if (arguments.size() != ref.argumentWords(kind != InvokeKind::static_))
        throw Error(ErrorCode::verification, "argument word count mismatch for " + ref.key());
    if (!depth_)
        instructions_ = 0;
    ++depth_;
    struct Scope {
        std::size_t& depth;
        ~Scope() {
            --depth;
        }
    } scope{depth_};
    tick();
    std::string owner = ref.owner;
    if (kind != InvokeKind::static_) {
        if (arguments.front().isNull())
            raise("Ljava/lang/NullPointerException;", ref.key());
        const auto& receiver = object(arguments.front());
        if (kind == InvokeKind::virtual_ || kind == InvokeKind::interface_)
            owner = receiver.type.starts_with('[') ? "Ljava/lang/Object;" : receiver.type;
    } else
        initialize(owner);
    std::unordered_set<std::string> visited;
    while (!owner.empty() && visited.insert(owner).second) {
        const auto key = owner + "->" + ref.name + ref.descriptor;
        if (const auto native = natives_.find(key); native != natives_.end())
            return native->second(*this, arguments);
        const auto item = classes_.find(owner);
        if (item == classes_.end())
            break;
        const auto& info = item->second;
        if (info.definition) {
            for (const auto* list : {&info.definition->directMethods, &info.definition->virtualMethods}) {
                for (const auto& method : *list) {
                    const auto& candidate = info.dex->method(method.index);
                    if (candidate.name != ref.name || candidate.descriptor != ref.descriptor)
                        continue;
                    if (bool(method.access & 8) != (kind == InvokeKind::static_))
                        raise("Ljava/lang/IncompatibleClassChangeError;", key);
                    const auto* code = info.dex->code(method);
                    if (!code)
                        raise(method.access & 0x100 ? "Ljava/lang/UnsatisfiedLinkError;"
                                                    : "Ljava/lang/AbstractMethodError;",
                              key);
                    try {
                        if (jitEnabled_)
                            if (auto result = jit_->execute(*this, *code, arguments))
                                return *result;
                        return execute(*info.dex, *code, arguments, key);
                    } catch (GuestException& error) {
                        error.frames.push_back(key);
                        throw;
                    }
                }
            }
        }
        if (ref.name == "<init>" || kind == InvokeKind::direct)
            break;
        owner = info.super;
    }
    raise("Ljava/lang/NoSuchMethodError;", ref.key());
}

Value Runtime::allocate(std::string type) {
    if (liveObjects_ >= limits_.objects)
        throw Error(ErrorCode::limit_exceeded, "guest object limit exceeded");
    auto object = std::make_unique<Object>();
    object->type = std::move(type);
    std::string current = object->type;
    std::unordered_set<std::string> visited;
    while (!current.empty() && visited.insert(current).second) {
        const auto klass = classes_.find(current);
        if (klass == classes_.end())
            break;
        for (const auto& [key, value] : klass->second.instanceDefaults)
            object->fields.emplace(key, value);
        if (klass->second.definition) {
            for (const auto& field : klass->second.definition->instanceFields)
                object->fields.emplace(klass->second.dex->field(field.index).key(), Value{});
        }
        current = klass->second.super;
    }
    std::uint32_t index;
    if (freeSlots_.empty()) {
        if (heap_.size() >= UINT32_MAX)
            throw Error(ErrorCode::limit_exceeded, "guest handle space exhausted");
        index = static_cast<std::uint32_t>(heap_.size());
        heap_.push_back({std::move(object), 0});
    } else {
        index = freeSlots_.back();
        freeSlots_.pop_back();
        heap_[index].object = std::move(object);
    }
    ++liveObjects_;
    ++allocationsSinceCollection_;
    return Value::object((std::uint64_t(heap_[index].generation) << 32) | (std::uint64_t(index) + 1));
}

Value Runtime::array(std::string type, std::int32_t size) {
    if (size < 0)
        raise("Ljava/lang/NegativeArraySizeException;", std::to_string(size));
    if (static_cast<std::size_t>(size) > limits_.arrayElements - arrayElements_)
        throw Error(ErrorCode::limit_exceeded, "guest array storage limit exceeded");
    if (type.size() < 2 || type.front() != '[')
        throw Error(ErrorCode::verification, "invalid array type");
    auto result = allocate(std::move(type));
    object(result).elements.resize(static_cast<std::size_t>(size));
    arrayElements_ += static_cast<std::size_t>(size);
    return result;
}

Value Runtime::string(std::string_view text, bool intern) {
    return string(utf16(text), intern);
}
Value Runtime::string(std::u16string text, bool intern) {
    std::u16string key;
    if (intern) {
        key = text;
        if (const auto it = interns_.find(key); it != interns_.end())
            return it->second;
    }
    const auto result = allocate("Ljava/lang/String;");
    object(result).text = std::move(text);
    if (intern)
        interns_.emplace(std::move(key), result);
    return result;
}
std::string Runtime::text(Value value) const {
    return value.isNull() ? "null" : utf8Replacing(object(value).text);
}
Value Runtime::classObject(std::string type) {
    if (const auto it = mirrors_.find(type); it != mirrors_.end())
        return it->second;
    const auto value = allocate("Ljava/lang/Class;");
    object(value).text = utf16(type);
    mirrors_.emplace(std::move(type), value);
    return value;
}

Object& Runtime::object(Value value) {
    if (value.isNull())
        raise("Ljava/lang/NullPointerException;", "null object reference");
    if (!isAlive(value))
        throw Error(ErrorCode::verification, "invalid guest reference");
    return *heap_[static_cast<std::uint32_t>(value.bits) - 1].object;
}
const Object& Runtime::object(Value value) const {
    if (!isAlive(value))
        throw Error(ErrorCode::verification, "invalid guest reference");
    return *heap_[static_cast<std::uint32_t>(value.bits) - 1].object;
}

bool Runtime::isAlive(Value value) const noexcept {
    const auto index = static_cast<std::uint32_t>(value.bits);
    return value.reference && index && index <= heap_.size() && heap_[index - 1].object &&
           heap_[index - 1].generation == static_cast<std::uint32_t>(value.bits >> 32);
}

std::size_t Runtime::collect(Arguments roots) {
    if (depth_)
        throw Error(ErrorCode::verification, "collection requires a quiescent runtime");
    std::vector<bool> marked(heap_.size());
    std::vector<std::uint32_t> pending;
    const std::function<void(Value)> visit = [&](Value value) {
        if (!value.reference || value.isNull())
            return;
        if (!isAlive(value))
            throw Error(ErrorCode::verification, "stale reference in managed graph");
        const auto index = static_cast<std::uint32_t>(value.bits) - 1;
        if (!marked[index]) {
            marked[index] = true;
            pending.push_back(index);
        }
    };
    for (const auto value : roots)
        visit(value);
    for (const auto& [name, value] : statics_)
        visit(value);
    for (const auto& [name, value] : interns_)
        visit(value);
    for (const auto& [name, value] : mirrors_)
        visit(value);
    for (const auto& [name, klass] : classes_)
        for (const auto& [field, value] : klass.instanceDefaults)
            visit(value);
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        const auto& object = *heap_[index].object;
        for (const auto& [name, value] : object.fields)
            visit(value);
        for (const auto value : object.elements)
            visit(value);
        if (object.native)
            object.native->visitReferences(visit);
    }
    freeSlots_.reserve(heap_.size());
    std::size_t reclaimed = 0;
    for (std::size_t index = 0; index < heap_.size(); ++index) {
        auto& slot = heap_[index];
        if (!slot.object || marked[index])
            continue;
        if (slot.object->type.starts_with('['))
            arrayElements_ -= slot.object->elements.size();
        slot.object.reset();
        --liveObjects_;
        ++reclaimed;
        if (slot.generation != UINT32_MAX) {
            ++slot.generation;
            freeSlots_.push_back(static_cast<std::uint32_t>(index));
        }
    }
    allocationsSinceCollection_ = 0;
    return reclaimed;
}

Value Runtime::initialValue(const DexFile& dex, const EncodedValue& value) {
    if (value.type == 0x17)
        return string(dex.string16(static_cast<std::uint32_t>(value.bits)), true);
    if (value.type == 0x18)
        return classObject(dex.type(static_cast<std::uint32_t>(value.bits)));
    if (value.type == 0x1c) {
        auto result = array("[Ljava/lang/Object;", static_cast<std::int32_t>(value.elements.size()));
        for (std::size_t n = 0; n < value.elements.size(); ++n)
            object(result).elements[n] = initialValue(dex, value.elements[n]);
        return result;
    }
    return {value.bits, false};
}

void Runtime::initialize(std::string_view type) {
    auto item = classes_.find(std::string(type));
    if (item == classes_.end())
        raise("Ljava/lang/NoClassDefFoundError;", std::string(type));
    auto& info = item->second;
    if (info.state == ClassInfo::State::ready || info.state == ClassInfo::State::initializing)
        return;
    if (info.state == ClassInfo::State::failed)
        raise("Ljava/lang/NoClassDefFoundError;", std::string(type));
    info.state = ClassInfo::State::initializing;
    try {
        if (!info.super.empty())
            initialize(info.super);
        if (info.definition) {
            for (const auto& field : info.definition->staticFields)
                statics_[info.dex->field(field.index).key()] = initialValue(*info.dex, field.initial);
            for (const auto& method : info.definition->directMethods) {
                const auto& ref = info.dex->method(method.index);
                if (ref.name == "<clinit>")
                    (void)invoke(ref, {}, InvokeKind::static_);
            }
        }
        info.state = ClassInfo::State::ready;
    } catch (...) {
        info.state = ClassInfo::State::failed;
        throw;
    }
}

std::string Runtime::fieldOwner(const FieldRef& ref) const {
    std::string owner = ref.owner;
    std::unordered_set<std::string> visited;
    while (!owner.empty() && visited.insert(owner).second) {
        if (statics_.contains(owner + "->" + ref.name + ":" + ref.type))
            return owner;
        const auto it = classes_.find(owner);
        if (it == classes_.end())
            break;
        if (it->second.definition) {
            for (const auto& field : it->second.definition->staticFields) {
                const auto& candidate = it->second.dex->field(field.index);
                if (candidate.name == ref.name && candidate.type == ref.type)
                    return owner;
            }
        }
        owner = it->second.super;
    }
    return ref.owner;
}
Value Runtime::getStatic(const FieldRef& field) {
    const auto owner = fieldOwner(field);
    initialize(owner);
    const auto key = owner + "->" + field.name + ":" + field.type;
    if (const auto it = statics_.find(key); it != statics_.end())
        return it->second;
    raise("Ljava/lang/NoSuchFieldError;", key);
}
void Runtime::putStatic(const FieldRef& field, Value value) {
    const auto owner = fieldOwner(field);
    initialize(owner);
    statics_[owner + "->" + field.name + ":" + field.type] = value;
}

void Runtime::defineField(std::string owner, std::string name, std::string type, Value initial) {
    auto item = classes_.find(owner);
    if (item == classes_.end())
        throw Error(ErrorCode::verification, "field owner is not defined: " + owner);
    item->second.instanceDefaults.emplace(owner + "->" + name + ":" + type, initial);
}

Value Runtime::getField(Value receiver, const FieldRef& field) {
    auto& instance = object(receiver);
    std::string owner = field.owner;
    std::unordered_set<std::string> visited;
    while (!owner.empty() && visited.insert(owner).second) {
        const auto key = owner + "->" + field.name + ":" + field.type;
        if (const auto item = instance.fields.find(key); item != instance.fields.end())
            return item->second;
        const auto klass = classes_.find(owner);
        if (klass == classes_.end())
            break;
        owner = klass->second.super;
    }
    raise("Ljava/lang/NoSuchFieldError;", field.key());
}

void Runtime::putField(Value receiver, const FieldRef& field, Value value) {
    auto& instance = object(receiver);
    std::string owner = field.owner;
    std::unordered_set<std::string> visited;
    while (!owner.empty() && visited.insert(owner).second) {
        const auto key = owner + "->" + field.name + ":" + field.type;
        if (const auto item = instance.fields.find(key); item != instance.fields.end()) {
            item->second = value;
            return;
        }
        const auto klass = classes_.find(owner);
        if (klass == classes_.end())
            break;
        owner = klass->second.super;
    }
    raise("Ljava/lang/NoSuchFieldError;", field.key());
}

[[noreturn]] void Runtime::raise(std::string type, std::string message) {
    const auto value = allocate(type);
    object(value).text = utf16(message);
    throw GuestException(value, type + ": " + message);
}
void Runtime::tick() {
    if (cancelled_.load(std::memory_order_relaxed))
        throw Error(ErrorCode::cancelled, "execution cancelled");
    if (++instructions_ > limits_.instructions)
        throw Error(ErrorCode::limit_exceeded, "instruction budget exceeded");
}

} // namespace gutenberg
