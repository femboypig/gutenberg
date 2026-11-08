#include "gutenberg/package.hpp"
#include <algorithm>
#include <charconv>

namespace gutenberg {

std::string classDescriptor(std::string name, std::string_view package) {
    if (name.empty()) return {};
    if (name.front() == '.') name = std::string(package) + name;
    else if (name.find('.') == name.npos && name.front() != 'L') name = std::string(package) + "." + name;
    if (name.front() == 'L' && name.back() == ';') return name;
    std::replace(name.begin(), name.end(), '.', '/');
    return "L" + name + ";";
}

Package::Package(Bytes bytes) : archive_(std::move(bytes)) {
    if (archive_.contains("resources.arsc")) resources_ = Resources(archive_.read("resources.arsc"));
    const auto manifest = xml("AndroidManifest.xml");
    if (manifest.name != "manifest") throw Error(ErrorCode::malformed_xml, "missing manifest root");
    info_.id = manifest.stringAttribute("package");
    if (info_.id.empty()) throw Error(ErrorCode::malformed_xml, "manifest has no package name");
    info_.versionCode = static_cast<std::uint32_t>(manifest.intAttribute("versionCode"));
    if (const auto* version = manifest.attribute("versionName")) info_.version = resourceText(*version);
    info_.label = info_.id;
    for (const auto& child : manifest.children) {
        if (child.name == "uses-sdk") {
            info_.minimumSdk = child.intAttribute("minSdkVersion", 1);
            info_.targetSdk = child.intAttribute("targetSdkVersion", info_.minimumSdk);
        } else if (child.name == "uses-permission" || child.name == "uses-permission-sdk-23") {
            info_.permissions.push_back(child.stringAttribute("name"));
        } else if (child.name == "application") {
            if (const auto* label = child.attribute("label")) info_.label = resourceText(*label);
            info_.applicationClass = classDescriptor(child.stringAttribute("name"), info_.id);
            if (const auto* icon = child.attribute("icon")) info_.iconResource = icon->data;
            const bool applicationEnabled = child.intAttribute("enabled", 1) != 0;
            for (const auto& activity : child.children) {
                if (activity.name != "activity" && activity.name != "activity-alias") continue;
                Component component;
                component.name = classDescriptor(activity.stringAttribute("name"), info_.id);
                component.target = activity.name == "activity-alias" ? classDescriptor(activity.stringAttribute("targetActivity"), info_.id) : component.name;
                component.enabled = applicationEnabled && activity.intAttribute("enabled", 1) != 0;
                for (const auto& filter : activity.children) {
                    if (filter.name != "intent-filter") continue;
                    bool main = false, launcher = false;
                    for (const auto& intent : filter.children) {
                        if (intent.name == "action" && intent.stringAttribute("name") == "android.intent.action.MAIN") main = true;
                        if (intent.name == "category" && intent.stringAttribute("name") == "android.intent.category.LAUNCHER") launcher = true;
                    }
                    component.launcher = component.launcher || (main && launcher);
                }
                if (component.enabled && component.launcher && info_.launcher.empty()) info_.launcher = component.target;
                info_.activities.push_back(std::move(component));
            }
        }
    }
    std::vector<std::pair<unsigned, std::string>> dexEntries;
    for (const auto& [name, entry] : archive_.entries()) {
        (void)entry;
        if (name.starts_with("lib/") && name.ends_with(".so")) info_.nativeLibraries.push_back(name);
        if (name == "classes.dex") dexEntries.emplace_back(1, name);
        else if (name.starts_with("classes") && name.ends_with(".dex")) {
            const auto number = std::string_view(name).substr(7, name.size() - 11);
            unsigned index = 0;
            const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), index);
            if (error == std::errc{} && end == number.data() + number.size() && index >= 2) dexEntries.emplace_back(index, name);
        }
    }
    std::sort(dexEntries.begin(), dexEntries.end());
    for (const auto& [number, name] : dexEntries) {
        (void)number;
        dexFiles_.push_back(std::make_shared<DexFile>(archive_.read(name)));
    }
}

XmlNode Package::xml(std::string_view path) const { return parseBinaryXml(archive_.read(path)); }
Bytes Package::asset(std::string_view path) const { return archive_.read("assets/" + std::string(path)); }
std::string Package::resourceText(const ResourceValue& value) const {
    return value.isReference() ? resources_.string(value.data) : value.text;
}

}
