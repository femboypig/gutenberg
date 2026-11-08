#include "gutenberg/android.hpp"
#include <algorithm>
#include <cmath>

namespace gutenberg {
namespace {
constexpr auto layout = "Landroid/view/ViewGroup$LayoutParams;";
constexpr auto linearLayout = "Landroid/widget/LinearLayout$LayoutParams;";
float dimension(const ResourceValue& value) {
    if (value.type != 5) return static_cast<float>(std::bit_cast<std::int32_t>(value.data));
    constexpr float multipliers[]{1.0f / 256, 1.0f / 32768, 1.0f / 8388608, 1.0f / 2147483648.0f};
    return static_cast<float>(std::bit_cast<std::int32_t>(value.data & 0xffffff00)) * multipliers[(value.data >> 4) & 3];
}
}

void AndroidSession::installViews() {
    vm_.defineClass("Landroid/util/AttributeSet;");
    vm_.defineClass("Landroid/view/View;");
    vm_.defineClass("Landroid/view/ViewGroup;", "Landroid/view/View;");
    vm_.defineClass("Landroid/view/View$OnClickListener;");
    vm_.defineClass("Landroid/widget/CompoundButton$OnCheckedChangeListener;");
    vm_.defineClass(layout);
    vm_.defineClass("Landroid/view/ViewGroup$MarginLayoutParams;", layout);
    vm_.defineClass(linearLayout, "Landroid/view/ViewGroup$MarginLayoutParams;");
    vm_.defineClass("Landroid/widget/FrameLayout$LayoutParams;", "Landroid/view/ViewGroup$MarginLayoutParams;");
    vm_.defineClass("Landroid/widget/RelativeLayout$LayoutParams;", "Landroid/view/ViewGroup$MarginLayoutParams;");
    vm_.defineField(layout, "width", "I", Value::integer(-2));
    vm_.defineField(layout, "height", "I", Value::integer(-2));
    vm_.defineField(linearLayout, "weight", "F");
    const std::pair<const char*, const char*> views[] = {
        {"LinearLayout", "Landroid/view/ViewGroup;"}, {"FrameLayout", "Landroid/view/ViewGroup;"},
        {"RelativeLayout", "Landroid/view/ViewGroup;"}, {"ScrollView", "Landroid/widget/FrameLayout;"},
        {"HorizontalScrollView", "Landroid/widget/FrameLayout;"}, {"TextView", "Landroid/view/View;"},
        {"Button", "Landroid/widget/TextView;"}, {"EditText", "Landroid/widget/TextView;"},
        {"CompoundButton", "Landroid/widget/Button;"}, {"CheckBox", "Landroid/widget/CompoundButton;"},
        {"Switch", "Landroid/widget/CompoundButton;"}, {"ToggleButton", "Landroid/widget/CompoundButton;"},
        {"RadioButton", "Landroid/widget/CompoundButton;"}, {"ImageView", "Landroid/view/View;"},
        {"ImageButton", "Landroid/widget/ImageView;"}, {"ProgressBar", "Landroid/view/View;"}
    };
    for (const auto& [name, parent] : views) vm_.defineClass("Landroid/widget/" + std::string(name) + ";", parent);
    std::vector<std::string> constructors{"Landroid/view/View;", "Landroid/view/ViewGroup;"};
    for (const auto& [name, parent] : views) { (void)parent; constructors.push_back("Landroid/widget/" + std::string(name) + ";"); }
    for (const auto& owner : constructors) {
        for (const auto* descriptor : {"(Landroid/content/Context;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;)V", "(Landroid/content/Context;Landroid/util/AttributeSet;I)V"}) {
            vm_.bind(owner, "<init>", descriptor, [this, owner](Runtime& r, Arguments a) {
                auto& node = view(a[0]); node.kind = owner; r.object(a[0]).fields["context"] = a[1]; return Value{};
            });
        }
    }
    for (const auto* owner : {layout, "Landroid/view/ViewGroup$MarginLayoutParams;", linearLayout, "Landroid/widget/FrameLayout$LayoutParams;", "Landroid/widget/RelativeLayout$LayoutParams;"}) {
        vm_.bind(owner, "<init>", "(II)V", [](Runtime& r, Arguments a) {
            auto& fields = r.object(a[0]).fields;
            fields["Landroid/view/ViewGroup$LayoutParams;->width:I"] = a[1];
            fields["Landroid/view/ViewGroup$LayoutParams;->height:I"] = a[2]; return Value{};
        });
    }
    vm_.bind(linearLayout, "<init>", "(IIF)V", [](Runtime& r, Arguments a) {
        auto& fields = r.object(a[0]).fields;
        fields["Landroid/view/ViewGroup$LayoutParams;->width:I"] = a[1];
        fields["Landroid/view/ViewGroup$LayoutParams;->height:I"] = a[2];
        fields["Landroid/widget/LinearLayout$LayoutParams;->weight:F"] = a[3]; return Value{};
    });
    vm_.bind("Landroid/view/ViewGroup$MarginLayoutParams;", "setMargins", "(IIII)V", [](Runtime& r, Arguments a) {
        for (unsigned n = 0; n < 4; ++n) r.object(a[0]).fields["margin" + std::to_string(n)] = a[n + 1];
        return Value{};
    });
    const std::string base = "Landroid/view/View;";
    vm_.bind(base, "setId", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).androidId = a[1].i32(); return Value{}; });
    vm_.bind(base, "getId", "()I", [this](Runtime&, Arguments a) { return Value::integer(view(a[0]).androidId); });
    vm_.bind(base, "setBackgroundColor", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).background = static_cast<std::uint32_t>(a[1].bits); return Value{}; });
    vm_.bind(base, "setPadding", "(IIII)V", [this](Runtime&, Arguments a) {
        view(a[0]).padding = {static_cast<float>(a[1].i32()), static_cast<float>(a[2].i32()), static_cast<float>(a[3].i32()), static_cast<float>(a[4].i32())}; return Value{};
    });
    vm_.bind(base, "setVisibility", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).visible = a[1].i32() == 0; return Value{}; });
    vm_.bind(base, "getVisibility", "()I", [this](Runtime&, Arguments a) { return Value::integer(view(a[0]).visible ? 0 : 8); });
    vm_.bind(base, "setEnabled", "(Z)V", [this](Runtime&, Arguments a) { view(a[0]).enabled = a[1].bits != 0; return Value{}; });
    vm_.bind(base, "isEnabled", "()Z", [this](Runtime&, Arguments a) { return Value::integer(view(a[0]).enabled); });
    vm_.bind(base, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V", [this](Runtime&, Arguments a) {
        if (a[1].isNull()) clickListeners_.erase(a[0].bits); else clickListeners_[a[0].bits] = a[1]; return Value{};
    });
    vm_.bind(base, "performClick", "()Z", [this](Runtime&, Arguments a) { const bool found = clickListeners_.contains(a[0].bits); click(a[0].bits); return Value::integer(found); });
    vm_.bind(base, "getContext", "()Landroid/content/Context;", [this](Runtime& r, Arguments a) { const auto it = r.object(a[0]).fields.find("context"); return it == r.object(a[0]).fields.end() ? activity_ : it->second; });
    vm_.bind(base, "getResources", "()Landroid/content/res/Resources;", [](Runtime& r, Arguments) { return r.allocate("Landroid/content/res/Resources;"); });
    vm_.bind(base, "setTag", "(Ljava/lang/Object;)V", [](Runtime& r, Arguments a) { r.object(a[0]).fields["tag"] = a[1]; return Value{}; });
    vm_.bind(base, "getTag", "()Ljava/lang/Object;", [](Runtime& r, Arguments a) { return r.object(a[0]).fields["tag"]; });
    for (const auto* name : {"invalidate", "requestLayout"}) vm_.bind(base, name, "()V", [](Runtime&, Arguments) { return Value{}; });
    vm_.bind(base, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", [this](Runtime& r, Arguments a) {
        auto& node = view(a[0]); auto& fields = r.object(a[1]).fields;
        node.width = static_cast<float>(fields["Landroid/view/ViewGroup$LayoutParams;->width:I"].i32());
        node.height = static_cast<float>(fields["Landroid/view/ViewGroup$LayoutParams;->height:I"].i32());
        node.weight = fields["Landroid/widget/LinearLayout$LayoutParams;->weight:F"].f32();
        r.object(a[0]).fields["layoutParams"] = a[1]; return Value{};
    });
    vm_.bind(base, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;", [](Runtime& r, Arguments a) { return r.object(a[0]).fields["layoutParams"]; });
    vm_.bind(base, "findViewById", "(I)Landroid/view/View;", [this](Runtime&, Arguments a) {
        std::vector<std::uint64_t> pending{a[0].bits};
        while (!pending.empty()) {
            const auto current = pending.back(); pending.pop_back(); const auto& node = view(Value::object(current));
            if (node.androidId == a[1].i32()) return Value::object(current);
            pending.insert(pending.end(), node.children.begin(), node.children.end());
        }
        return Value{};
    });
    auto addView = [this](Runtime& r, Arguments a) {
        auto& parent = view(a[0]); (void)view(a[1]);
        if (a[0] == a[1] || r.object(a[1]).fields.contains("parent")) r.raise("Ljava/lang/IllegalStateException;", "view already has a parent");
        parent.children.push_back(a[1].bits); r.object(a[1]).fields["parent"] = a[0];
        if (a.size() == 3) {
            const Value args[]{a[1], a[2]}; (void)r.invoke("Landroid/view/View;", "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V", args);
        }
        return Value{};
    };
    vm_.bind("Landroid/view/ViewGroup;", "addView", "(Landroid/view/View;)V", addView);
    vm_.bind("Landroid/view/ViewGroup;", "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V", addView);
    vm_.bind("Landroid/view/ViewGroup;", "removeAllViews", "()V", [this](Runtime& r, Arguments a) {
        auto& node = view(a[0]); for (auto child : node.children) r.object(Value::object(child)).fields.erase("parent"); node.children.clear(); return Value{};
    });
    vm_.bind("Landroid/view/ViewGroup;", "getChildCount", "()I", [this](Runtime&, Arguments a) { return Value::integer(static_cast<std::int32_t>(view(a[0]).children.size())); });
    vm_.bind("Landroid/view/ViewGroup;", "getChildAt", "(I)Landroid/view/View;", [this](Runtime&, Arguments a) {
        const auto index = a[1].i32(); const auto& children = view(a[0]).children;
        return index < 0 || static_cast<std::size_t>(index) >= children.size() ? Value{} : Value::object(children[static_cast<std::size_t>(index)]);
    });
    vm_.bind("Landroid/widget/LinearLayout;", "setOrientation", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).orientation = a[1].i32(); return Value{}; });
    for (const auto* owner : {"Landroid/widget/LinearLayout;", "Landroid/widget/TextView;"})
        vm_.bind(owner, "setGravity", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).gravity = a[1].i32(); return Value{}; });
    const std::string text = "Landroid/widget/TextView;";
    vm_.bind(text, "setText", "(Ljava/lang/CharSequence;)V", [this](Runtime& r, Arguments a) { view(a[0]).text = a[1].isNull() ? "" : javaString(r, a[1]); return Value{}; });
    vm_.bind(text, "setText", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).text = package_->resources().string(static_cast<std::uint32_t>(a[1].bits)); return Value{}; });
    vm_.bind(text, "getText", "()Ljava/lang/CharSequence;", [this](Runtime& r, Arguments a) { return r.string(view(a[0]).text); });
    vm_.bind(text, "setTextSize", "(F)V", [this](Runtime&, Arguments a) { view(a[0]).textSize = a[1].f32(); return Value{}; });
    vm_.bind(text, "setTextSize", "(IF)V", [this](Runtime&, Arguments a) { view(a[0]).textSize = a[2].f32(); return Value{}; });
    vm_.bind(text, "setTextColor", "(I)V", [this](Runtime&, Arguments a) { view(a[0]).textColor = static_cast<std::uint32_t>(a[1].bits); return Value{}; });
    vm_.bind(text, "setHint", "(Ljava/lang/CharSequence;)V", [this](Runtime& r, Arguments a) { view(a[0]).hint = a[1].isNull() ? "" : javaString(r, a[1]); return Value{}; });
    vm_.bind(text, "setSingleLine", "(Z)V", [](Runtime& r, Arguments a) { r.object(a[0]).fields["singleLine"] = a[1]; return Value{}; });
    vm_.bind(text, "setInputType", "(I)V", [](Runtime& r, Arguments a) { r.object(a[0]).fields["inputType"] = a[1]; return Value{}; });
    vm_.defineClass("Landroid/text/Editable;");
    vm_.defineClass("Landroid/text/SpannableStringBuilder;", "Ljava/lang/StringBuilder;", {"Landroid/text/Editable;"});
    vm_.bind("Landroid/widget/EditText;", "getText", "()Landroid/text/Editable;", [this](Runtime& r, Arguments a) {
        const auto value = r.allocate("Landroid/text/SpannableStringBuilder;"); r.object(value).text = utf16(view(a[0]).text); return value;
    });
    vm_.bind("Landroid/widget/CompoundButton;", "setChecked", "(Z)V", [this](Runtime&, Arguments a) { view(a[0]).checked = a[1].bits != 0; return Value{}; });
    vm_.bind("Landroid/widget/CompoundButton;", "isChecked", "()Z", [this](Runtime&, Arguments a) { return Value::integer(view(a[0]).checked); });
    vm_.bind("Landroid/widget/CompoundButton;", "setOnCheckedChangeListener", "(Landroid/widget/CompoundButton$OnCheckedChangeListener;)V", [this](Runtime&, Arguments a) {
        if (a[1].isNull()) checkListeners_.erase(a[0].bits); else checkListeners_[a[0].bits] = a[1]; return Value{};
    });
    vm_.defineClass("Landroid/view/LayoutInflater;");
    vm_.bind("Landroid/view/LayoutInflater;", "from", "(Landroid/content/Context;)Landroid/view/LayoutInflater;", [](Runtime& r, Arguments) { return r.allocate("Landroid/view/LayoutInflater;"); });
    vm_.bind("Landroid/app/Activity;", "getLayoutInflater", "()Landroid/view/LayoutInflater;", [](Runtime& r, Arguments) { return r.allocate("Landroid/view/LayoutInflater;"); });
    vm_.bind("Landroid/view/LayoutInflater;", "inflate", "(ILandroid/view/ViewGroup;Z)Landroid/view/View;", [this](Runtime&, Arguments a) { return inflate(static_cast<std::uint32_t>(a[1].bits), a[2], a[3].bits != 0); });
    vm_.bind("Landroid/view/LayoutInflater;", "inflate", "(ILandroid/view/ViewGroup;)Landroid/view/View;", [this](Runtime&, Arguments a) { return inflate(static_cast<std::uint32_t>(a[1].bits), a[2], !a[2].isNull()); });
}

Value AndroidSession::inflate(std::uint32_t resource, Value parent, bool attach) {
    const auto path = package_->resources().string(resource);
    const auto value = inflateNode(package_->xml(path), parent);
    if (attach && !parent.isNull()) {
        const Value args[]{parent, value}; (void)vm_.invoke("Landroid/view/ViewGroup;", "addView", "(Landroid/view/View;)V", args); return parent;
    }
    return value;
}

Value AndroidSession::inflateNode(const XmlNode& xml, Value parent) {
    (void)parent;
    std::string type;
    if (xml.name == "view") type = xml.stringAttribute("class");
    else if (xml.name == "View" || xml.name == "ViewGroup") type = "android.view." + xml.name;
    else type = xml.name.find('.') == xml.name.npos ? "android.widget." + xml.name : xml.name;
    type = classDescriptor(type, package_->info().id);
    vm_.initialize(type);
    const auto value = vm_.allocate(type);
    const Value args[]{value, activity_};
    (void)vm_.invoke(type, "<init>", "(Landroid/content/Context;)V", args, InvokeKind::direct);
    auto& node = view(value);
    for (const auto& attribute : xml.attributes) {
        const auto& name = attribute.name;
        auto data = attribute.value;
        if (data.isReference() && name != "id") {
            const auto resolved = package_->resources().resolve(data.data);
            if (resolved) data = *resolved;
        }
        if (name == "id") node.androidId = std::bit_cast<std::int32_t>(data.data);
        else if (name == "text") node.text = data.text;
        else if (name == "hint") node.hint = data.text;
        else if (name == "layout_width") node.width = dimension(data);
        else if (name == "layout_height") node.height = dimension(data);
        else if (name == "layout_weight") node.weight = std::bit_cast<float>(data.data);
        else if (name == "orientation") node.orientation = std::bit_cast<std::int32_t>(data.data);
        else if (name == "gravity") node.gravity = std::bit_cast<std::int32_t>(data.data);
        else if (name == "textSize") node.textSize = dimension(data);
        else if (name == "textColor") node.textColor = data.data;
        else if (name == "background" && data.type >= 0x1c && data.type <= 0x1f) node.background = data.data;
        else if (name == "enabled") node.enabled = data.data != 0;
        else if (name == "visibility") node.visible = data.data == 0;
        else if (name == "checked") node.checked = data.data != 0;
        else if (name == "padding") { const auto padding = dimension(data); node.padding = {padding, padding, padding, padding}; }
        else if (name == "paddingLeft" || name == "paddingStart") node.padding.left = dimension(data);
        else if (name == "paddingRight" || name == "paddingEnd") node.padding.right = dimension(data);
        else if (name == "paddingTop") node.padding.top = dimension(data);
        else if (name == "paddingBottom") node.padding.bottom = dimension(data);
    }
    for (const auto& child : xml.children) {
        const auto childValue = inflateNode(child, value);
        const Value childArgs[]{value, childValue};
        (void)vm_.invoke("Landroid/view/ViewGroup;", "addView", "(Landroid/view/View;)V", childArgs);
    }
    return value;
}

}
