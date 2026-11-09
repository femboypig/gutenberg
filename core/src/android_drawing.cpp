#include "gutenberg/android.hpp"
#include <cmath>
#include <utility>

namespace gutenberg {
void AndroidSession::installDrawing() {
    installGraphics(vm_);
    const std::string base = "Landroid/view/View;", event = "Landroid/view/MotionEvent;";
    vm_.defineClass(event);
    vm_.defineClass("Landroid/view/KeyEvent;");
    for (const auto* owner : {"Landroid/view/View;", "Landroid/app/Activity;"}) {
        vm_.bind(owner, "onKeyDown", "(ILandroid/view/KeyEvent;)Z", [this, owner](Runtime&, Arguments a) {
            if (std::string_view(owner) == "Landroid/app/Activity;" && a[1].i32() == 4) {
                back();
                return Value::integer(1);
            }
            return Value::integer(0);
        });
        vm_.bind(owner, "onKeyUp", "(ILandroid/view/KeyEvent;)Z",
                 [](Runtime&, Arguments) { return Value::integer(0); });
    }
    for (const std::string name : {"getKeyCode", "getAction", "getRepeatCount"})
        vm_.bind("Landroid/view/KeyEvent;", name, "()I", [name](Runtime& r, Arguments a) {
            return name == "getRepeatCount" ? Value::integer(0) : r.object(a[0]).fields[name];
        });
    vm_.defineClass("Landroid/view/View$OnTouchListener;");
    for (const std::string name : {"setFocusable", "setFocusableInTouchMode"})
        vm_.bind(base, name, "(Z)V", [this, name](Runtime& r, Arguments a) {
            auto& fields = r.object(a[0]).fields;
            fields[name] = a[1];
            if (name == "setFocusableInTouchMode" && a[1].bits)
                fields["setFocusable"] = Value::integer(1);
            if (name == "setFocusable" && !a[1].bits && focusedView_ == a[0].bits)
                focusedView_ = 0;
            return Value{};
        });
    vm_.bind(base, "isFocusable", "()Z",
             [](Runtime& r, Arguments a) { return r.object(a[0]).fields["setFocusable"]; });
    vm_.bind(base, "requestFocus", "()Z", [this](Runtime& r, Arguments a) {
        const auto& node = view(a[0]);
        const bool focusable =
            r.object(a[0]).fields["setFocusable"].bits != 0 && node.enabled && node.visible;
        if (focusable)
            focusedView_ = a[0].bits;
        return Value::integer(focusable);
    });
    vm_.bind(base, "isFocused", "()Z",
             [this](Runtime&, Arguments a) { return Value::integer(focusedView_ == a[0].bits); });
    vm_.bind(base, "hasFocus", "()Z", [this](Runtime& r, Arguments a) {
        auto current = Value::object(focusedView_);
        std::unordered_set<std::uint64_t> visited;
        while (!current.isNull() && visited.insert(current.bits).second) {
            if (current == a[0])
                return Value::integer(1);
            current = r.object(current).fields["parent"];
        }
        return Value::integer(0);
    });
    vm_.bind(base, "clearFocus", "()V", [this](Runtime&, Arguments a) {
        if (focusedView_ == a[0].bits)
            focusedView_ = 0;
        return Value{};
    });
    vm_.bind(base, "onDraw", "(Landroid/graphics/Canvas;)V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind(base, "onSizeChanged", "(IIII)V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind(base, "onTouchEvent", "(Landroid/view/MotionEvent;)Z", [this](Runtime&, Arguments a) {
        const auto action = vm_.object(a[1]).fields["action"].i32();
        if (vm_.object(a[0]).fields["onClickListener"].isNull())
            return Value::integer(0);
        if (action == 1)
            click(a[0].bits);
        return Value::integer(1);
    });
    vm_.bind(base, "draw", "(Landroid/graphics/Canvas;)V", [](Runtime& r, Arguments a) {
        return r.invoke("Landroid/view/View;", "onDraw", "(Landroid/graphics/Canvas;)V", a);
    });
    for (const std::string name : {"getWidth", "getHeight", "getMeasuredWidth", "getMeasuredHeight"})
        vm_.bind(base, name, "()I", [this, name](Runtime&, Arguments a) {
            const auto& node = view(a[0]);
            return Value::integer(static_cast<std::int32_t>(
                name == "getWidth" || name == "getMeasuredWidth" ? node.measuredWidth : node.measuredHeight));
        });
    vm_.bind(base, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V",
             [](Runtime& r, Arguments a) {
                 r.object(a[0]).fields["onTouchListener"] = a[1];
                 return Value{};
             });
    vm_.bind(base, "setWillNotDraw", "(Z)V", [this](Runtime&, Arguments a) {
        view(a[0]).customDrawing = !a[1].bits;
        dirtyViews_.insert(a[0].bits);
        return Value{};
    });
    for (const std::string name : {"getAction", "getActionMasked", "getActionIndex", "getPointerCount"})
        vm_.bind(event, name, "()I", [name](Runtime& r, Arguments a) {
            return name == "getActionIndex"    ? Value::integer(0)
                   : name == "getPointerCount" ? Value::integer(1)
                                               : r.object(a[0]).fields["action"];
        });
    for (const std::string name : {"getX", "getY", "getPressure", "getRawX", "getRawY"}) {
        const auto field = name == "getX" || name == "getRawX"   ? "x"
                           : name == "getY" || name == "getRawY" ? "y"
                                                                 : "pressure";
        vm_.bind(event, name, "()F",
                 [field](Runtime& r, Arguments a) { return r.object(a[0]).fields[field]; });
        if (name != "getRawX" && name != "getRawY")
            vm_.bind(event, name, "(I)F", [field](Runtime& r, Arguments a) {
                if (a[1].i32() != 0)
                    r.raise("Ljava/lang/IllegalArgumentException;", "invalid pointer index");
                return r.object(a[0]).fields[field];
            });
    }
    vm_.bind(event, "getPointerId", "(I)I", [](Runtime& r, Arguments a) {
        if (a[1].i32() != 0)
            r.raise("Ljava/lang/IllegalArgumentException;", "invalid pointer index");
        return Value::integer(0);
    });
    for (const auto* name : {"getEventTime", "getDownTime"})
        vm_.bind(event, name, "()J", [name](Runtime& r, Arguments a) {
            return r.object(a[0]).fields[std::string_view(name) == "getDownTime" ? "downTime" : "time"];
        });
    vm_.bind(event, "getHistorySize", "()I", [](Runtime&, Arguments) { return Value::integer(0); });
}

void AndroidSession::resizeViews(const std::vector<ViewSize>& sizes) {
    bool changed = false;
    for (const auto& size : sizes) {
        if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 0 || size.height < 0 ||
            size.width > 1000000 || size.height > 1000000)
            throw Error(ErrorCode::verification, "invalid view geometry");
        const auto found = scene_.views.find(size.handle);
        if (found == scene_.views.end())
            continue;
        auto& node = found->second;
        const float width = std::floor(size.width), height = std::floor(size.height);
        if (node.measuredWidth == width && node.measuredHeight == height)
            continue;
        const auto oldWidth = node.measuredWidth, oldHeight = node.measuredHeight;
        node.measuredWidth = width;
        node.measuredHeight = height;
        dirtyViews_.insert(size.handle);
        changed = true;
        const Value args[]{Value::object(size.handle), Value::integer(static_cast<int>(width)),
                           Value::integer(static_cast<int>(height)),
                           Value::integer(static_cast<int>(oldWidth)),
                           Value::integer(static_cast<int>(oldHeight))};
        (void)vm_.invoke("Landroid/view/View;", "onSizeChanged", "(IIII)V", args);
    }
    if (changed)
        publish();
}

void AndroidSession::drawPending() {
    if (drawing_)
        return;
    drawing_ = true;
    try {
        const auto dirty = std::exchange(dirtyViews_, {});
        for (const auto handle : dirty) {
            const auto found = scene_.views.find(handle);
            if (found == scene_.views.end())
                continue;
            auto& node = found->second;
            if (!node.customDrawing || !node.visible)
                continue;
            if (node.measuredWidth <= 0 || node.measuredHeight <= 0) {
                dirtyViews_.insert(handle);
                continue;
            }
            auto& canvas = vm_.object(Value::object(handle)).fields["drawingCanvas"];
            if (canvas.isNull())
                canvas = createRecordingCanvas(vm_);
            beginRecording(vm_, canvas, node.measuredWidth, node.measuredHeight);
            const Value args[]{Value::object(handle), canvas};
            (void)vm_.invoke("Landroid/view/View;", "draw", "(Landroid/graphics/Canvas;)V", args);
            node.drawing = finishRecording(vm_, canvas);
        }
    } catch (...) {
        drawing_ = false;
        throw;
    }
    drawing_ = false;
}

void AndroidSession::touch(std::uint64_t handle, int action, float x, float y, float pressure,
                           std::int64_t timeMs) {
    const auto found = scene_.views.find(handle);
    if (found == scene_.views.end() || !found->second.enabled || !found->second.visible)
        return;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) || action < 0 || action > 3)
        throw Error(ErrorCode::verification, "invalid touch event");
    if (motionEvent_.isNull())
        motionEvent_ = vm_.allocate("Landroid/view/MotionEvent;");
    auto& fields = vm_.object(motionEvent_).fields;
    fields["action"] = Value::integer(action);
    fields["x"] = Value::floating(x);
    fields["y"] = Value::floating(y);
    fields["pressure"] = Value::floating(pressure);
    fields["time"] = Value::wide(timeMs);
    if (action == 0)
        fields["downTime"] = Value::wide(timeMs);
    bool consumed = false;
    const auto listener = vm_.object(Value::object(handle)).fields["onTouchListener"];
    if (!listener.isNull()) {
        const Value args[]{listener, Value::object(handle), motionEvent_};
        consumed =
            vm_.invoke("Landroid/view/View$OnTouchListener;", "onTouch",
                       "(Landroid/view/View;Landroid/view/MotionEvent;)Z", args, InvokeKind::interface_)
                .bits != 0;
    }
    if (!consumed) {
        const Value args[]{Value::object(handle), motionEvent_};
        (void)vm_.invoke("Landroid/view/View;", "onTouchEvent", "(Landroid/view/MotionEvent;)Z", args);
    }
    publish();
}

void AndroidSession::key(int code, bool down) {
    if (code < 0 || code > 1000)
        throw Error(ErrorCode::verification, "invalid key code");
    if (keyEvent_.isNull())
        keyEvent_ = vm_.allocate("Landroid/view/KeyEvent;");
    vm_.object(keyEvent_).fields["getKeyCode"] = Value::integer(code);
    vm_.object(keyEvent_).fields["getAction"] = Value::integer(down ? 0 : 1);
    auto target = focusedView_;
    if (!target && scene_.root) {
        std::vector<std::uint64_t> pending{scene_.root};
        std::unordered_set<std::uint64_t> visited;
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second)
                continue;
            const auto& node = scene_.views.at(current);
            if (!node.visible || !node.enabled)
                continue;
            if (vm_.object(Value::object(current)).fields["setFocusable"].bits) {
                target = current;
                break;
            }
            pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
        }
        focusedView_ = target;
    }
    const auto name = down ? "onKeyDown" : "onKeyUp";
    bool consumed = false;
    if (target) {
        const Value args[]{Value::object(target), Value::integer(code), keyEvent_};
        consumed = vm_.invoke("Landroid/view/View;", name, "(ILandroid/view/KeyEvent;)Z", args).bits != 0;
    }
    if (!consumed) {
        const Value args[]{activity_, Value::integer(code), keyEvent_};
        (void)vm_.invoke("Landroid/app/Activity;", name, "(ILandroid/view/KeyEvent;)Z", args);
    }
    publish();
}
} // namespace gutenberg
