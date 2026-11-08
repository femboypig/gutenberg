#pragma once

#include "java.hpp"
#include "package.hpp"
#include <chrono>
#include <map>

namespace gutenberg {

struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct ViewNode {
    std::uint64_t handle = 0;
    std::int32_t androidId = -1;
    std::string kind;
    std::string text;
    std::string hint;
    float width = -2, height = -2, weight = 0;
    float textSize = 16;
    std::uint32_t textColor = 0xff18181b;
    std::uint32_t background = 0;
    Insets padding;
    std::int32_t orientation = 0;
    std::int32_t gravity = 0;
    bool enabled = true, visible = true, checked = false;
    std::vector<std::uint64_t> children;
};

struct Scene {
    std::uint64_t root = 0;
    std::string title;
    std::map<std::uint64_t, ViewNode> views;
    bool finished = false;
};

struct AndroidHost {
    std::filesystem::path dataDirectory;
    std::function<void(const Scene&)> present;
    std::function<void(std::string_view)> log;
    std::function<void(std::string_view)> toast;
    std::function<void(std::string_view)> openUrl;
    std::function<std::string(std::string_view)> readPreference;
    std::function<void(std::string_view, std::string_view)> writePreference;
    float screenWidth = 390, screenHeight = 844;
};

class AndroidSession {
  public:
    AndroidSession(std::shared_ptr<const Package> package, AndroidHost host, RuntimeLimits limits = {});
    ~AndroidSession();
    void launch(std::string activity = {});
    void resume();
    void pause();
    void stop();
    void click(std::uint64_t handle);
    void changeText(std::uint64_t handle, std::string text);
    void changeChecked(std::uint64_t handle, bool checked);
    void back();
    void pumpTasks();
    void cancel() noexcept {
        vm_.cancel();
    }
    [[nodiscard]] const Scene& scene() const noexcept {
        return scene_;
    }
    [[nodiscard]] Runtime& runtime() noexcept {
        return vm_;
    }
    [[nodiscard]] const Package& package() const noexcept {
        return *package_;
    }
    [[nodiscard]] ViewNode& view(Value value);
    [[nodiscard]] Value inflate(std::uint32_t resource, Value parent = {}, bool attach = false);
    void publish();

  private:
    void installFramework();
    void installViews();
    void installServices();
    void lifecycle(std::string name, std::string descriptor = "()V", std::vector<Value> extra = {});
    [[nodiscard]] Value inflateNode(const XmlNode& node, Value parent);
    [[nodiscard]] Value textValue(std::int32_t id);
    std::shared_ptr<const Package> package_;
    AndroidHost host_;
    Runtime vm_;
    Scene scene_;
    Value activity_, application_;
    std::map<std::uint64_t, Value> clickListeners_;
    std::map<std::uint64_t, Value> checkListeners_;
    struct Task {
        std::chrono::steady_clock::time_point due;
        Value runnable;
        Value handler;
    };
    std::vector<Task> tasks_;
    std::map<std::string, std::string> preferences_;
    enum class State { created, resumed, paused, stopped } state_ = State::created;
};

} // namespace gutenberg
