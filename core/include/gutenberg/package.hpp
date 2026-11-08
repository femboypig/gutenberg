#pragma once

#include "archive.hpp"
#include "dex.hpp"
#include "resources.hpp"

namespace gutenberg {

struct Component {
    std::string name;
    std::string target;
    bool enabled = true;
    bool launcher = false;
};

struct PackageInfo {
    std::string id;
    std::string label;
    std::string version;
    std::uint32_t versionCode = 0;
    std::int32_t minimumSdk = 1;
    std::int32_t targetSdk = 1;
    std::string applicationClass;
    std::string launcher;
    std::vector<Component> activities;
    std::vector<std::string> permissions;
    std::vector<std::string> nativeLibraries;
    std::uint32_t iconResource = 0;
    bool allowsCleartext = true;
};

class Package {
  public:
    explicit Package(Bytes bytes);
    [[nodiscard]] const PackageInfo& info() const noexcept {
        return info_;
    }
    [[nodiscard]] const Resources& resources() const noexcept {
        return resources_;
    }
    [[nodiscard]] const Archive& archive() const noexcept {
        return archive_;
    }
    [[nodiscard]] const std::vector<std::shared_ptr<const DexFile>>& dexFiles() const noexcept {
        return dexFiles_;
    }
    [[nodiscard]] XmlNode xml(std::string_view path) const;
    [[nodiscard]] Bytes asset(std::string_view path) const;
    [[nodiscard]] std::string resourceText(const ResourceValue& value) const;

  private:
    Archive archive_;
    Resources resources_;
    PackageInfo info_;
    std::vector<std::shared_ptr<const DexFile>> dexFiles_;
};

[[nodiscard]] std::string classDescriptor(std::string name, std::string_view package);

} // namespace gutenberg
