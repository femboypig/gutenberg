#include "gutenberg/android.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>

namespace gutenberg {

void AndroidSession::installServices() {
    for (const auto* type : {"Landroid/os/Bundle;", "Landroid/content/Intent;", "Landroid/net/Uri;", "Landroid/widget/Toast;",
                             "Landroid/util/Log;", "Landroid/graphics/Color;", "Landroid/os/Build;", "Landroid/os/Build$VERSION;",
                             "Landroid/os/Handler;", "Landroid/os/Looper;", "Landroid/content/SharedPreferences;", "Landroid/content/SharedPreferences$Editor;", "Landroid/preference/PreferenceManager;"})
        vm_.defineClass(type);
    vm_.bind("Landroid/os/Bundle;", "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    for (const auto* type : {"Int", "Boolean", "String"}) {
        const std::string name(type), descriptor = name == "String" ? "Ljava/lang/String;" : name == "Boolean" ? "Z" : "I";
        vm_.bind("Landroid/os/Bundle;", "put" + name, "(Ljava/lang/String;" + descriptor + ")V", [](Runtime& r, Arguments a) { r.object(a[0]).fields[r.text(a[1])] = a[2]; return Value{}; });
        vm_.bind("Landroid/os/Bundle;", "get" + name, "(Ljava/lang/String;)" + descriptor, [](Runtime& r, Arguments a) { return r.object(a[0]).fields[r.text(a[1])]; });
    }
    vm_.bind("Landroid/os/Bundle;", "containsKey", "(Ljava/lang/String;)Z", [](Runtime& r, Arguments a) { return Value::integer(r.object(a[0]).fields.contains(r.text(a[1]))); });
    vm_.bind("Landroid/content/Intent;", "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) { r.object(a[0]).fields["action"] = a[1]; return Value{}; });
    vm_.bind("Landroid/content/Intent;", "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V", [](Runtime& r, Arguments a) { r.object(a[0]).fields["action"] = a[1]; r.object(a[0]).fields["data"] = a[2]; return Value{}; });
    vm_.bind("Landroid/content/Intent;", "getAction", "()Ljava/lang/String;", [](Runtime& r, Arguments a) { return r.object(a[0]).fields["action"]; });
    vm_.bind("Landroid/content/Intent;", "getData", "()Landroid/net/Uri;", [](Runtime& r, Arguments a) { return r.object(a[0]).fields["data"]; });
    vm_.bind("Landroid/content/Intent;", "setData", "(Landroid/net/Uri;)Landroid/content/Intent;", [](Runtime& r, Arguments a) { r.object(a[0]).fields["data"] = a[1]; return a[0]; });
    vm_.bind("Landroid/net/Uri;", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", [](Runtime& r, Arguments a) { const auto uri = r.allocate("Landroid/net/Uri;"); r.object(uri).text = r.object(a[0]).text; return uri; });
    vm_.bind("Landroid/net/Uri;", "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    vm_.bind("Landroid/content/Context;", "startActivity", "(Landroid/content/Intent;)V", [this](Runtime& r, Arguments a) {
        const auto data = r.object(a[1]).fields["data"];
        if (!data.isNull() && host_.openUrl) { host_.openUrl(r.text(data)); return Value{}; }
        r.raise("Ljava/lang/UnsupportedOperationException;", "intent target is not supported");
    });
    vm_.bind("Landroid/app/Activity;", "getIntent", "()Landroid/content/Intent;", [](Runtime& r, Arguments a) {
        auto& intent = r.object(a[0]).fields["intent"];
        if (intent.isNull()) { intent = r.allocate("Landroid/content/Intent;"); r.object(intent).fields["action"] = r.string("android.intent.action.MAIN"); }
        return intent;
    });
    vm_.bind("Landroid/widget/Toast;", "makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;", [](Runtime& r, Arguments a) {
        const auto value = r.allocate("Landroid/widget/Toast;"); r.object(value).text = utf16(javaString(r, a[1])); return value;
    });
    vm_.bind("Landroid/widget/Toast;", "makeText", "(Landroid/content/Context;II)Landroid/widget/Toast;", [this](Runtime& r, Arguments a) {
        const auto value = r.allocate("Landroid/widget/Toast;"); r.object(value).text = utf16(package_->resources().string(static_cast<std::uint32_t>(a[1].bits))); return value;
    });
    vm_.bind("Landroid/widget/Toast;", "show", "()V", [this](Runtime& r, Arguments a) { if (host_.toast) host_.toast(r.text(a[0])); return Value{}; });
    for (const auto* level : {"v", "d", "i", "w", "e"}) {
        vm_.bind("Landroid/util/Log;", level, "(Ljava/lang/String;Ljava/lang/String;)I", [this, level](Runtime& r, Arguments a) {
            const auto text = std::string(level) + "/" + r.text(a[0]) + ": " + r.text(a[1]); if (host_.log) host_.log(text);
            return Value::integer(static_cast<std::int32_t>(text.size()));
        });
    }
    vm_.bind("Landroid/graphics/Color;", "rgb", "(III)I", [](Runtime&, Arguments a) { return Value{0xff000000U | ((a[0].bits & 255) << 16) | ((a[1].bits & 255) << 8) | (a[2].bits & 255), false}; });
    vm_.bind("Landroid/graphics/Color;", "argb", "(IIII)I", [](Runtime&, Arguments a) { return Value{((a[0].bits & 255) << 24) | ((a[1].bits & 255) << 16) | ((a[2].bits & 255) << 8) | (a[3].bits & 255), false}; });
    vm_.putStatic({"Landroid/os/Build;", "MODEL", "Ljava/lang/String;"}, vm_.string("Gutenberg"));
    vm_.putStatic({"Landroid/os/Build;", "MANUFACTURER", "Ljava/lang/String;"}, vm_.string("Gutenberg"));
    vm_.putStatic({"Landroid/os/Build$VERSION;", "SDK_INT", "I"}, Value::integer(26));
    vm_.putStatic({"Landroid/os/Build$VERSION;", "RELEASE", "Ljava/lang/String;"}, vm_.string("8.0"));
    vm_.bind("Landroid/os/Looper;", "getMainLooper", "()Landroid/os/Looper;", [](Runtime& r, Arguments) { return r.allocate("Landroid/os/Looper;"); });
    vm_.bind("Landroid/os/Looper;", "myLooper", "()Landroid/os/Looper;", [](Runtime& r, Arguments) { return r.allocate("Landroid/os/Looper;"); });
    vm_.bind("Landroid/os/Handler;", "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/os/Handler;", "<init>", "(Landroid/os/Looper;)V", [](Runtime&, Arguments) { return Value{}; });
    auto post = [this](Runtime& r, Arguments a) {
        if (a[1].isNull()) r.raise("Ljava/lang/NullPointerException;", "null runnable");
        if (tasks_.size() >= 10000) throw Error(ErrorCode::limit_exceeded, "handler queue limit exceeded");
        const auto delay = a.size() == 4 ? std::bit_cast<std::int64_t>(argumentWide(a, 2)) : 0;
        const auto bounded = std::clamp<std::int64_t>(delay, 0, 365LL * 24 * 60 * 60 * 1000);
        tasks_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(bounded), a[1], a[0]});
        return Value::integer(1);
    };
    vm_.bind("Landroid/os/Handler;", "post", "(Ljava/lang/Runnable;)Z", post);
    vm_.bind("Landroid/os/Handler;", "postDelayed", "(Ljava/lang/Runnable;J)Z", post);
    vm_.bind("Landroid/os/Handler;", "removeCallbacks", "(Ljava/lang/Runnable;)V", [this](Runtime&, Arguments a) {
        std::erase_if(tasks_, [&](const Task& task) { return task.handler == a[0] && task.runnable == a[1]; }); return Value{};
    });
    vm_.bind("Landroid/app/Activity;", "runOnUiThread", "(Ljava/lang/Runnable;)V", [](Runtime& r, Arguments a) {
        const Value args[]{a[1]}; (void)r.invoke("Ljava/lang/Runnable;", "run", "()V", args, InvokeKind::interface_); return Value{};
    });

    const std::string prefs = "Landroid/content/SharedPreferences;", editor = "Landroid/content/SharedPreferences$Editor;";
    vm_.bind("Landroid/content/ContextWrapper;", "getBaseContext", "()Landroid/content/Context;", [](Runtime&, Arguments a) { return a[0]; });
    vm_.bind("Landroid/preference/PreferenceManager;", "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;", [this](Runtime& r, Arguments) {
        const auto value = r.allocate("Landroid/content/SharedPreferences;"); r.object(value).text = utf16(package_->info().id + "_preferences"); return value;
    });
    vm_.bind("Landroid/content/Context;", "getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;", [](Runtime& r, Arguments a) {
        const auto value = r.allocate("Landroid/content/SharedPreferences;"); r.object(value).text = r.object(a[1]).text; return value;
    });
    vm_.bind(prefs, "edit", "()Landroid/content/SharedPreferences$Editor;", [](Runtime& r, Arguments a) {
        const auto value = r.allocate("Landroid/content/SharedPreferences$Editor;"); r.object(value).text = r.object(a[0]).text; return value;
    });
    for (const auto* name : {"String", "Int", "Boolean"}) {
        const std::string kind(name), descriptor = kind == "String" ? "Ljava/lang/String;" : kind == "Int" ? "I" : "Z";
        vm_.bind(prefs, "get" + kind, "(Ljava/lang/String;" + descriptor + ")" + descriptor, [this, kind](Runtime& r, Arguments a) {
            const auto key = r.text(a[0]) + "/" + r.text(a[1]);
            const auto value = host_.readPreference ? host_.readPreference(key) : preferences_[key];
            if (value.size() < 2 || value[0] != kind[0] || value[1] != ':') return a[2];
            if (kind == "String") return r.string(std::string_view(value).substr(2));
            std::int32_t n = 0; const auto [end, error] = std::from_chars(value.data() + 2, value.data() + value.size(), n);
            return error == std::errc{} && end == value.data() + value.size() ? Value::integer(n) : a[2];
        });
        vm_.bind(editor, "put" + kind, "(Ljava/lang/String;" + descriptor + ")Landroid/content/SharedPreferences$Editor;", [kind](Runtime& r, Arguments a) {
            const auto key = r.text(a[1]); const auto value = kind + ":" + (kind == "String" ? r.text(a[2]) : std::to_string(a[2].i32()));
            r.object(a[0]).fields[key] = r.string(std::string(1, kind[0]) + value.substr(kind.size())); return a[0];
        });
    }
    vm_.bind(editor, "remove", "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;", [](Runtime& r, Arguments a) { r.object(a[0]).fields[r.text(a[1])] = {}; return a[0]; });
    auto commit = [this](Runtime& r, Arguments a) {
        for (const auto& [name, value] : r.object(a[0]).fields) {
            const auto key = r.text(a[0]) + "/" + name; const auto text = value.isNull() ? "" : r.text(value);
            if (host_.writePreference) host_.writePreference(key, text); else preferences_[key] = text;
        }
        r.object(a[0]).fields.clear(); return Value::integer(1);
    };
    vm_.bind(editor, "commit", "()Z", commit);
    vm_.bind(editor, "apply", "()V", [commit](Runtime& r, Arguments a) { (void)commit(r, a); return Value{}; });
}

void AndroidSession::pumpTasks() {
    if (state_ != State::resumed) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<Task> ready;
    for (auto it = tasks_.begin(); it != tasks_.end() && ready.size() < 64;) {
        if (it->due <= now) { ready.push_back(*it); it = tasks_.erase(it); } else ++it;
    }
    for (const auto& task : ready) {
        const Value args[]{task.runnable};
        (void)vm_.invoke("Ljava/lang/Runnable;", "run", "()V", args, InvokeKind::interface_);
    }
    if (!ready.empty()) publish();
}

}
