#include "gutenberg/android.hpp"
#include <algorithm>

namespace gutenberg {
namespace {
constexpr auto handlerType = "Landroid/os/Handler;", messageType = "Landroid/os/Message;";
std::int64_t uptime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
Value message(Runtime& vm, Value target, Arguments fields = {}) {
    const auto result = vm.allocate(messageType);
    auto& values = vm.object(result).fields;
    values["target"] = target;
    if (!fields.empty()) {
        values["Landroid/os/Message;->what:I"] = fields[0];
        if (fields.size() == 2)
            values["Landroid/os/Message;->obj:Ljava/lang/Object;"] = fields[1];
        if (fields.size() >= 3) {
            values["Landroid/os/Message;->arg1:I"] = fields[1];
            values["Landroid/os/Message;->arg2:I"] = fields[2];
        }
        if (fields.size() == 4)
            values["Landroid/os/Message;->obj:Ljava/lang/Object;"] = fields[3];
    }
    return result;
}
} // namespace

void AndroidSession::enqueueMessage(Value handler, Value value, std::int64_t delayMs) {
    auto& fields = vm_.object(value).fields;
    if (fields["queued"].bits)
        vm_.raise("Ljava/lang/IllegalStateException;", "message is already in use");
    if (tasks_.size() >= 10000)
        throw Error(ErrorCode::limit_exceeded, "handler queue limit exceeded");
    const auto bounded = std::clamp<std::int64_t>(delayMs, 0, 365LL * 24 * 60 * 60 * 1000);
    const auto due = std::chrono::steady_clock::now() + std::chrono::milliseconds(bounded);
    fields["target"] = handler;
    fields["queued"] = Value::integer(1);
    fields["when"] =
        Value::wide(std::chrono::duration_cast<std::chrono::milliseconds>(due.time_since_epoch()).count());
    const auto at = std::upper_bound(tasks_.begin(), tasks_.end(), due,
                                     [](const auto& time, const Task& task) { return time < task.due; });
    tasks_.insert(at, {due, value, handler});
}

void AndroidSession::installMessages() {
    vm_.defineClass(messageType);
    vm_.defineClass("Landroid/os/Handler$Callback;");
    vm_.defineClass("Landroid/os/SystemClock;");
    for (const auto* field : {"what", "arg1", "arg2"})
        vm_.defineField(messageType, field, "I");
    vm_.defineField(messageType, "obj", "Ljava/lang/Object;");
    const auto looper = vm_.allocate("Landroid/os/Looper;");
    vm_.putStatic({"Landroid/os/Looper;", "mainLooper", "Landroid/os/Looper;"}, looper);
    for (const auto* name : {"getMainLooper", "myLooper"})
        vm_.bind("Landroid/os/Looper;", name, "()Landroid/os/Looper;",
                 [looper](Runtime&, Arguments) { return looper; });
    for (const auto* name : {"uptimeMillis", "elapsedRealtime"})
        vm_.bind("Landroid/os/SystemClock;", name, "()J",
                 [](Runtime&, Arguments) { return Value::wide(uptime()); });
    for (const std::string descriptor : {"()V", "(Landroid/os/Looper;)V", "(Landroid/os/Handler$Callback;)V",
                                         "(Landroid/os/Looper;Landroid/os/Handler$Callback;)V"})
        vm_.bind(handlerType, "<init>", descriptor, [looper, descriptor](Runtime& r, Arguments a) {
            const bool explicitLooper = descriptor.starts_with("(Landroid/os/Looper;");
            const auto targetLooper = explicitLooper ? a[1] : looper;
            if (targetLooper.isNull())
                r.raise("Ljava/lang/NullPointerException;", "null handler looper");
            if (targetLooper != looper)
                r.raise("Ljava/lang/UnsupportedOperationException;", "handler requires the main looper");
            r.object(a[0]).fields["looper"] = targetLooper;
            if (descriptor.find("Callback") != descriptor.npos)
                r.object(a[0]).fields["callback"] = a.back();
            return Value{};
        });
    vm_.bind(handlerType, "getLooper", "()Landroid/os/Looper;",
             [looper](Runtime&, Arguments) { return looper; });
    vm_.bind(handlerType, "handleMessage", "(Landroid/os/Message;)V",
             [](Runtime&, Arguments) { return Value{}; });
    vm_.bind(handlerType, "dispatchMessage", "(Landroid/os/Message;)V", [](Runtime& r, Arguments a) {
        const auto runnable = r.object(a[1]).fields["callback"];
        if (!runnable.isNull()) {
            const Value args[]{runnable};
            return r.invoke("Ljava/lang/Runnable;", "run", "()V", args, InvokeKind::interface_);
        }
        const auto callback = r.object(a[0]).fields["callback"];
        if (!callback.isNull()) {
            const Value args[]{callback, a[1]};
            if (r.invoke("Landroid/os/Handler$Callback;", "handleMessage", "(Landroid/os/Message;)Z", args,
                         InvokeKind::interface_)
                    .bits)
                return Value{};
        }
        return r.invoke(handlerType, "handleMessage", "(Landroid/os/Message;)V", a);
    });
    vm_.bind(messageType, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind(messageType, "obtain", "()Landroid/os/Message;",
             [](Runtime& r, Arguments) { return message(r, {}); });
    for (const std::string tail : {"", "I", "ILjava/lang/Object;", "III", "IIILjava/lang/Object;"}) {
        vm_.bind(messageType, "obtain", "(Landroid/os/Handler;" + tail + ")Landroid/os/Message;",
                 [](Runtime& r, Arguments a) { return message(r, a[0], a.subspan(1)); });
        vm_.bind(handlerType, "obtainMessage", "(" + tail + ")Landroid/os/Message;",
                 [](Runtime& r, Arguments a) { return message(r, a[0], a.subspan(1)); });
    }
    vm_.bind(messageType, "getTarget", "()Landroid/os/Handler;",
             [](Runtime& r, Arguments a) { return r.object(a[0]).fields["target"]; });
    vm_.bind(messageType, "getWhen", "()J",
             [](Runtime& r, Arguments a) { return r.object(a[0]).fields["when"]; });
    vm_.bind(messageType, "sendToTarget", "()V", [this](Runtime& r, Arguments a) {
        const auto target = r.object(a[0]).fields["target"];
        (void)r.object(target);
        enqueueMessage(target, a[0], 0);
        return Value{};
    });
    vm_.bind(messageType, "recycle", "()V", [](Runtime& r, Arguments a) {
        auto& fields = r.object(a[0]).fields;
        if (fields["queued"].bits)
            r.raise("Ljava/lang/IllegalStateException;", "cannot recycle a queued message");
        fields.clear();
        for (const auto* name : {"what", "arg1", "arg2"})
            fields["Landroid/os/Message;->" + std::string(name) + ":I"] = Value::integer(0);
        fields["Landroid/os/Message;->obj:Ljava/lang/Object;"] = {};
        return Value{};
    });
    for (const std::string name : {"sendMessage", "sendMessageDelayed", "sendMessageAtTime"})
        vm_.bind(handlerType, name,
                 name == "sendMessage" ? "(Landroid/os/Message;)Z" : "(Landroid/os/Message;J)Z",
                 [this, name](Runtime& r, Arguments a) {
                     auto delay = a.size() == 4 ? std::bit_cast<std::int64_t>(argumentWide(a, 2)) : 0;
                     if (name == "sendMessageAtTime") {
                         const auto now = uptime();
                         delay = delay <= now ? 0 : delay - now;
                     }
                     (void)r.object(a[1]);
                     enqueueMessage(a[0], a[1], delay);
                     return Value::integer(1);
                 });
    for (const std::string name : {"sendEmptyMessage", "sendEmptyMessageDelayed", "sendEmptyMessageAtTime"})
        vm_.bind(handlerType, name, name == "sendEmptyMessage" ? "(I)Z" : "(IJ)Z",
                 [this, name](Runtime& r, Arguments a) {
                     auto delay = a.size() == 4 ? std::bit_cast<std::int64_t>(argumentWide(a, 2)) : 0;
                     if (name == "sendEmptyMessageAtTime") {
                         const auto now = uptime();
                         delay = delay <= now ? 0 : delay - now;
                     }
                     enqueueMessage(a[0], message(r, a[0], a.subspan(1, 1)), delay);
                     return Value::integer(1);
                 });
    for (const std::string name : {"post", "postDelayed", "postAtTime"})
        vm_.bind(handlerType, name, name == "post" ? "(Ljava/lang/Runnable;)Z" : "(Ljava/lang/Runnable;J)Z",
                 [this, name](Runtime& r, Arguments a) {
                     if (a[1].isNull())
                         r.raise("Ljava/lang/NullPointerException;", "null runnable");
                     const auto value = message(r, a[0]);
                     r.object(value).fields["callback"] = a[1];
                     auto delay = a.size() == 4 ? std::bit_cast<std::int64_t>(argumentWide(a, 2)) : 0;
                     if (name == "postAtTime") {
                         const auto now = uptime();
                         delay = delay <= now ? 0 : delay - now;
                     }
                     enqueueMessage(a[0], value, delay);
                     return Value::integer(1);
                 });
    for (const std::string name : {"removeMessages", "hasMessages"})
        for (const bool token : {false, true})
            vm_.bind(handlerType, name,
                     "(I" + std::string(token ? "Ljava/lang/Object;" : "") + ")" +
                         (name == "hasMessages" ? "Z" : "V"),
                     [this, name, token](Runtime& r, Arguments a) {
                         bool found = false;
                         for (auto it = tasks_.begin(); it != tasks_.end();) {
                             auto& fields = r.object(it->message).fields;
                             const bool matches =
                                 it->handler == a[0] && fields["Landroid/os/Message;->what:I"] == a[1] &&
                                 fields["callback"].isNull() &&
                                 (!token || a[2].isNull() ||
                                  fields["Landroid/os/Message;->obj:Ljava/lang/Object;"] == a[2]);
                             if (matches) {
                                 found = true;
                                 if (name == "removeMessages") {
                                     fields["queued"] = {};
                                     it = tasks_.erase(it);
                                     continue;
                                 }
                             }
                             ++it;
                         }
                         return name == "hasMessages" ? Value::integer(found) : Value{};
                     });
    for (const std::string name : {"removeCallbacks", "hasCallbacks", "removeCallbacksAndMessages"})
        vm_.bind(handlerType, name,
                 name == "removeCallbacksAndMessages" ? "(Ljava/lang/Object;)V"
                 : name == "hasCallbacks"             ? "(Ljava/lang/Runnable;)Z"
                                                      : "(Ljava/lang/Runnable;)V",
                 [this, name](Runtime& r, Arguments a) {
                     bool found = false;
                     for (auto it = tasks_.begin(); it != tasks_.end();) {
                         auto& fields = r.object(it->message).fields;
                         const bool matches =
                             it->handler == a[0] &&
                             (name == "removeCallbacksAndMessages"
                                  ? (a[1].isNull() ||
                                     fields["Landroid/os/Message;->obj:Ljava/lang/Object;"] == a[1])
                                  : fields["callback"] == a[1]);
                         if (matches) {
                             found = true;
                             if (name != "hasCallbacks") {
                                 fields["queued"] = {};
                                 it = tasks_.erase(it);
                                 continue;
                             }
                         }
                         ++it;
                     }
                     return name == "hasCallbacks" ? Value::integer(found) : Value{};
                 });
}

void AndroidSession::pumpTasks() {
    if (state_ == State::stopped)
        return;
    if (vm_.allocationsSinceCollection() >= 4096 ||
        vm_.heapSize() >= vm_.limits().objects - vm_.limits().objects / 4)
        (void)collectGarbage();
    const auto now = std::chrono::steady_clock::now();
    bool changed = false;
    for (unsigned n = 0; n < 64 && !tasks_.empty() && tasks_.front().due <= now; ++n) {
        const auto task = tasks_.front();
        tasks_.erase(tasks_.begin());
        const Value args[]{task.handler, task.message};
        try {
            (void)vm_.invoke(handlerType, "dispatchMessage", "(Landroid/os/Message;)V", args);
        } catch (...) {
            vm_.object(task.message).fields["queued"] = {};
            throw;
        }
        vm_.object(task.message).fields["queued"] = {};
        changed = true;
    }
    if (changed || !dirtyViews_.empty())
        publish();
}
} // namespace gutenberg
