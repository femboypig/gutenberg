#include "gutenberg/android.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace gutenberg;

namespace {
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("gutenberg-apk-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::uint64_t find(const Scene& scene, std::string_view text, std::string_view kind = {}) {
    for (const auto& [handle, node] : scene.views)
        if (node.text == text && (kind.empty() || node.kind == kind))
            return handle;
    throw std::runtime_error("view not found: " + std::string(text));
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected apk path");
        auto package = std::make_shared<Package>(readFile(argv[1]));
        require(package->info().id == "org.gutenberg.conformance", "package id");
        require(package->info().label == "Runtime Conformance", "resource label");
        require(package->info().launcher == "Lorg/gutenberg/conformance/MainActivity;", "launcher class");
        require(package->info().minimumSdk == 26, "minimum sdk");
        std::map<std::string, std::string> preferences;
        unsigned presentations = 0;
        AndroidHost host;
        Directory directory;
        host.dataDirectory = directory.path;
        host.http = [](const HttpRequest& request, const std::function<bool()>& cancelled) {
            require(request.url == "http://127.0.0.1:8765/message", "HTTP target");
            require(!cancelled(), "unexpected cancellation");
            const std::string body = "network message\n";
            return HttpResponse{
                200, {{"Content-Type", "text/plain"}}, Bytes(body.begin(), body.end()), request.url};
        };
        host.present = [&](const Scene& scene) {
            require(scene.root != 0, "missing content view");
            ++presentations;
        };
        host.log = [](std::string_view text) { std::cout << text << '\n'; };
        host.readPreference = [&](std::string_view key) { return preferences[std::string(key)]; };
        host.writePreference = [&](std::string_view key, std::string_view value) {
            preferences[std::string(key)] = value;
        };
        AndroidSession session(package, host);
        session.launch();
        (void)find(session.scene(), "Count: 0");
        const auto button = find(session.scene(), "Increment");
        const auto input = find(session.scene(), "", "Landroid/widget/EditText;");
        session.changeText(input, "Gutenberg");
        session.click(button);
        (void)find(session.scene(), "Gutenberg: 1");
        require(preferences.at("counter/value") == "I:1", "preference write");
        session.pumpTasks();
        require(session.scene().title == "Handler ready", "handler callback");
        session.click(find(session.scene(), "Fetch network"));
        (void)find(session.scene(), "HTTP 200: network message");
        session.pause();
        session.resume();
        session.stop();
        AndroidSession restored(package, host);
        restored.launch();
        (void)find(restored.scene(), "Count: 1");
        require(presentations >= 5, "scene publication");
        std::cout << "apk conformance passed\n";
        return 0;
    } catch (const GuestException& error) {
        std::cerr << error.what() << '\n';
        for (const auto& frame : error.frames)
            std::cerr << "  at " << frame << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
