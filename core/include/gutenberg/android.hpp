#pragma once

#include "graphics.hpp"
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
    std::int32_t layoutGravity = -1;
    bool enabled = true, visible = true, checked = false;
    bool customDrawing = false;
    float measuredWidth = 0, measuredHeight = 0;
    std::shared_ptr<const DisplayList> drawing;
    std::vector<std::uint64_t> children;
};

struct Scene {
    std::uint64_t root = 0;
    std::string title;
    std::map<std::uint64_t, ViewNode> views;
    bool finished = false;
};

struct ViewSize {
    std::uint64_t handle;
    float width, height;
};

struct AndroidHost {
    std::filesystem::path dataDirectory;
    HttpTransport http;
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
    void resizeViews(const std::vector<ViewSize>& sizes);
    void touch(std::uint64_t handle, int action, float x, float y, float pressure, std::int64_t timeMs);
    void key(int code, bool down);
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
    void installDrawing();
    void installResources();
    void installMessages();
    void enqueueMessage(Value handler, Value message, std::int64_t delayMs);
    void drawPending();
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
    std::map<std::uint64_t, Value> touchListeners_;
    std::map<std::uint64_t, Value> canvases_;
    std::unordered_set<std::uint64_t> dirtyViews_;
    Value motionEvent_;
    Value keyEvent_;
    std::uint64_t focusedView_ = 0;
    bool drawing_ = false;
    struct Task {
        std::chrono::steady_clock::time_point due;
        Value message;
        Value handler;
    };
    std::vector<Task> tasks_;
    std::map<std::string, std::string> preferences_;
    enum class State { created, resumed, paused, stopped } state_ = State::created;
};

} // namespace gutenberg
