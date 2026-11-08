#include "gutenberg/android.hpp"
#include <algorithm>

namespace gutenberg {

AndroidSession::AndroidSession(std::shared_ptr<const Package> package, AndroidHost host, RuntimeLimits limits)
    : package_(std::move(package)), host_(std::move(host)), vm_(limits) {
    installJavaRuntime(vm_);
    vm_.logger = host_.log;
    installFramework();
    installViews();
    installServices();
    for (const auto& dex : package_->dexFiles())
        vm_.load(dex);
}
AndroidSession::~AndroidSession() = default;

void AndroidSession::installFramework() {
    vm_.defineClass("Landroid/content/Context;");
    vm_.defineClass("Landroid/content/ContextWrapper;", "Landroid/content/Context;");
    vm_.defineClass("Landroid/view/ContextThemeWrapper;", "Landroid/content/ContextWrapper;");
    vm_.defineClass("Landroid/app/Application;", "Landroid/content/ContextWrapper;");
    vm_.defineClass("Landroid/app/Activity;", "Landroid/view/ContextThemeWrapper;");
    vm_.defineClass("Landroid/content/res/Resources;");
    vm_.defineClass("Landroid/content/res/AssetManager;");
    vm_.defineClass("Landroid/content/res/Resources$Theme;");
    vm_.defineClass("Landroid/content/res/Configuration;");
    vm_.defineClass("Landroid/util/DisplayMetrics;");
    vm_.defineClass("Landroid/view/Window;");
    vm_.defineClass("Landroid/view/WindowManager;");
    vm_.defineClass("Landroid/view/Display;");
    for (const auto* field : {"density", "scaledDensity", "xdpi", "ydpi"})
        vm_.defineField("Landroid/util/DisplayMetrics;", field, "F", Value::floating(1.0f));
    for (const auto* field : {"widthPixels", "heightPixels", "densityDpi"})
        vm_.defineField("Landroid/util/DisplayMetrics;", field, "I");
    for (const auto* owner : {"Landroid/app/Activity;", "Landroid/app/Application;",
                              "Landroid/content/ContextWrapper;", "Landroid/view/ContextThemeWrapper;"})
        vm_.bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    for (const auto* method :
         {"onStart", "onResume", "onPause", "onStop", "onDestroy", "onRestart", "onPostResume"})
        vm_.bind("Landroid/app/Activity;", method, "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Activity;", "onCreate", "(Landroid/os/Bundle;)V",
             [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Activity;", "onSaveInstanceState", "(Landroid/os/Bundle;)V",
             [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Application;", "onCreate", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Application;", "onTerminate", "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Activity;", "setContentView", "(Landroid/view/View;)V",
             [this](Runtime& r, Arguments a) {
                 scene_.root = a[1].bits;
                 (void)view(a[1]);
                 r.object(a[0]).fields["contentView"] = a[1];
                 return Value{};
             });
    vm_.bind("Landroid/app/Activity;", "setContentView", "(I)V", [this](Runtime& r, Arguments a) {
        const auto root = inflate(static_cast<std::uint32_t>(a[1].bits));
        scene_.root = root.bits;
        r.object(a[0]).fields["contentView"] = root;
        return Value{};
    });
    vm_.bind("Landroid/app/Activity;", "findViewById", "(I)Landroid/view/View;",
             [this](Runtime&, Arguments a) {
                 for (const auto& [handle, node] : scene_.views)
                     if (node.androidId == a[1].i32())
                         return Value::object(handle);
                 return Value{};
             });
    vm_.bind("Landroid/app/Activity;", "setTitle", "(Ljava/lang/CharSequence;)V",
             [this](Runtime& r, Arguments a) {
                 scene_.title = a[1].isNull() ? "" : javaString(r, a[1]);
                 return Value{};
             });
    vm_.bind("Landroid/app/Activity;", "setTitle", "(I)V", [this](Runtime&, Arguments a) {
        scene_.title = package_->resources().string(static_cast<std::uint32_t>(a[1].bits));
        return Value{};
    });
    vm_.bind("Landroid/app/Activity;", "getTitle", "()Ljava/lang/CharSequence;",
             [this](Runtime& r, Arguments) { return r.string(scene_.title); });
    vm_.bind("Landroid/app/Activity;", "finish", "()V", [this](Runtime&, Arguments) {
        scene_.finished = true;
        return Value{};
    });
    vm_.bind("Landroid/app/Activity;", "isFinishing", "()Z",
             [this](Runtime&, Arguments) { return Value::integer(scene_.finished); });
    vm_.bind("Landroid/app/Activity;", "onBackPressed", "()V", [this](Runtime&, Arguments) {
        scene_.finished = true;
        return Value{};
    });
    vm_.bind("Landroid/app/Activity;", "getApplication", "()Landroid/app/Application;",
             [this](Runtime&, Arguments) { return application_; });
    vm_.bind("Landroid/content/Context;", "getApplicationContext", "()Landroid/content/Context;",
             [this](Runtime&, Arguments) { return application_; });
    vm_.bind("Landroid/content/Context;", "getPackageName", "()Ljava/lang/String;",
             [this](Runtime& r, Arguments) { return r.string(package_->info().id); });
    vm_.bind("Landroid/content/Context;", "getString", "(I)Ljava/lang/String;",
             [this](Runtime&, Arguments a) { return textValue(a[1].i32()); });
    vm_.bind("Landroid/content/Context;", "getText", "(I)Ljava/lang/CharSequence;",
             [this](Runtime&, Arguments a) { return textValue(a[1].i32()); });
    vm_.bind("Landroid/content/Context;", "getResources", "()Landroid/content/res/Resources;",
             [](Runtime& r, Arguments) { return r.allocate("Landroid/content/res/Resources;"); });
    vm_.bind("Landroid/content/Context;", "getAssets", "()Landroid/content/res/AssetManager;",
             [](Runtime& r, Arguments) { return r.allocate("Landroid/content/res/AssetManager;"); });
    vm_.bind("Landroid/content/Context;", "getTheme", "()Landroid/content/res/Resources$Theme;",
             [](Runtime& r, Arguments) { return r.allocate("Landroid/content/res/Resources$Theme;"); });
    vm_.bind("Landroid/content/res/Resources;", "getString", "(I)Ljava/lang/String;",
             [this](Runtime&, Arguments a) { return textValue(a[1].i32()); });
    vm_.bind("Landroid/content/res/Resources;", "getText", "(I)Ljava/lang/CharSequence;",
             [this](Runtime&, Arguments a) { return textValue(a[1].i32()); });
    vm_.bind("Landroid/content/res/Resources;", "getColor", "(I)I", [this](Runtime&, Arguments a) {
        const auto value = package_->resources().resolve(static_cast<std::uint32_t>(a[1].bits));
        if (!value)
            throw Error(ErrorCode::verification, "missing color resource");
        return Value{value->data, false};
    });
    vm_.bind("Landroid/content/res/Resources;", "getDisplayMetrics", "()Landroid/util/DisplayMetrics;",
             [this](Runtime& r, Arguments) {
                 const auto metrics = r.allocate("Landroid/util/DisplayMetrics;");
                 auto& fields = r.object(metrics).fields;
                 fields["Landroid/util/DisplayMetrics;->density:F"] = Value::floating(1.0f);
                 fields["Landroid/util/DisplayMetrics;->scaledDensity:F"] = Value::floating(1.0f);
                 fields["Landroid/util/DisplayMetrics;->densityDpi:I"] = Value::integer(160);
                 fields["Landroid/util/DisplayMetrics;->widthPixels:I"] =
                     Value::integer(static_cast<std::int32_t>(host_.screenWidth));
                 fields["Landroid/util/DisplayMetrics;->heightPixels:I"] =
                     Value::integer(static_cast<std::int32_t>(host_.screenHeight));
                 return metrics;
             });
    vm_.bind("Landroid/app/Activity;", "getWindow", "()Landroid/view/Window;",
             [](Runtime& r, Arguments) { return r.allocate("Landroid/view/Window;"); });
    vm_.bind("Landroid/view/Window;", "setFlags", "(II)V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/view/Window;", "addFlags", "(I)V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/view/Window;", "setStatusBarColor", "(I)V",
             [](Runtime&, Arguments) { return Value{}; });
    vm_.bind("Landroid/app/Activity;", "requestWindowFeature", "(I)Z",
             [](Runtime&, Arguments) { return Value::integer(0); });
}

void AndroidSession::launch(std::string activity) {
    if (!activity_.isNull())
        throw Error(ErrorCode::verification, "session is already running");
    vm_.resetCancellation();
    if (activity.empty())
        activity = package_->info().launcher;
    if (activity.empty())
        throw Error(ErrorCode::verification, "apk has no launcher activity");
    scene_.title = package_->info().label;
    const auto applicationType = package_->info().applicationClass.empty()
                                     ? "Landroid/app/Application;"
                                     : package_->info().applicationClass;
    vm_.initialize(applicationType);
    application_ = vm_.allocate(applicationType);
    const Value appArgs[]{application_};
    (void)vm_.invoke(applicationType, "<init>", "()V", appArgs, InvokeKind::direct);
    (void)vm_.invoke(applicationType, "onCreate", "()V", appArgs);
    vm_.initialize(activity);
    activity_ = vm_.allocate(activity);
    const Value args[]{activity_};
    (void)vm_.invoke(activity, "<init>", "()V", args, InvokeKind::direct);
    lifecycle("onCreate", "(Landroid/os/Bundle;)V", {Value{}});
    lifecycle("onStart");
    lifecycle("onResume");
    state_ = State::resumed;
    publish();
}
void AndroidSession::lifecycle(std::string name, std::string descriptor, std::vector<Value> extra) {
    if (activity_.isNull())
        return;
    extra.insert(extra.begin(), activity_);
    (void)vm_.invoke(vm_.object(activity_).type, std::move(name), std::move(descriptor), extra);
}
void AndroidSession::resume() {
    if (state_ != State::paused)
        return;
    lifecycle("onResume");
    state_ = State::resumed;
    publish();
}
void AndroidSession::pause() {
    if (state_ != State::resumed)
        return;
    lifecycle("onPause");
    state_ = State::paused;
}
void AndroidSession::stop() {
    if (state_ == State::stopped)
        return;
    pause();
    lifecycle("onStop");
    lifecycle("onDestroy");
    state_ = State::stopped;
}
void AndroidSession::back() {
    lifecycle("onBackPressed");
    publish();
}
void AndroidSession::publish() {
    if (host_.present)
        host_.present(scene_);
}
ViewNode& AndroidSession::view(Value value) {
    const auto& obj = vm_.object(value);
    auto [entry, inserted] = scene_.views.try_emplace(value.bits);
    if (inserted) {
        entry->second.handle = value.bits;
        entry->second.kind = obj.type;
    }
    return entry->second;
}
Value AndroidSession::textValue(std::int32_t id) {
    return vm_.string(package_->resources().string(static_cast<std::uint32_t>(id)));
}
void AndroidSession::click(std::uint64_t handle) {
    const auto node = scene_.views.find(handle);
    if (node == scene_.views.end() || !node->second.enabled || !node->second.visible)
        return;
    const auto listener = clickListeners_.find(handle);
    if (listener != clickListeners_.end()) {
        const Value args[]{listener->second, Value::object(handle)};
        (void)vm_.invoke(vm_.object(listener->second).type, "onClick", "(Landroid/view/View;)V", args,
                         InvokeKind::interface_);
    }
    publish();
}
void AndroidSession::changeText(std::uint64_t handle, std::string text) {
    view(Value::object(handle)).text = std::move(text);
    publish();
}
void AndroidSession::changeChecked(std::uint64_t handle, bool checked) {
    view(Value::object(handle)).checked = checked;
    if (const auto it = checkListeners_.find(handle); it != checkListeners_.end()) {
        const Value args[]{it->second, Value::object(handle), Value::integer(checked)};
        (void)vm_.invoke(vm_.object(it->second).type, "onCheckedChanged",
                         "(Landroid/widget/CompoundButton;Z)V", args, InvokeKind::interface_);
    }
    publish();
}

} // namespace gutenberg
