#include "gutenberg/layout.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace gutenberg;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected third party canvas apk path");
        const auto package = std::make_shared<Package>(readFile(argv[1]));
        AndroidHost host;
        AndroidSession session(package, host);
        session.launch();
        const auto layout =
            layoutScene(session.scene(), {390, 700}, [](const ViewNode&, Size) { return Size{}; });
        std::vector<ViewSize> sizes;
        std::uint64_t drawing = 0;
        for (const auto& [handle, box] : layout) {
            sizes.push_back({handle, box.frame.width, box.frame.height});
            if (session.scene().views.at(handle).customDrawing)
                drawing = handle;
        }
        require(drawing != 0, "missing third party custom view");
        session.resizeViews(sizes);
        session.key(19, true);
        session.key(19, false);
        auto nextFrame = [&](std::shared_ptr<const DisplayList> previous) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            do {
                session.pumpTasks();
                const auto frame = session.scene().views.at(drawing).drawing;
                if (frame && frame != previous && frame->commands.size() > 50)
                    return frame;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            return std::shared_ptr<const DisplayList>{};
        };
        const auto first = nextFrame({});
        require(first && first->commands.size() > 50, "third party game did not draw its board");
        require(first->commands[0].operation == DrawOperation::bitmap,
                "game board did not use bitmap sprites");
        (void)session.collectGarbage();
        const auto second = nextFrame(first);
        require(second && second != first, "delayed handler did not redraw the board");
        bool moved = first->commands.size() != second->commands.size();
        for (std::size_t n = 0; n < std::min(first->commands.size(), second->commands.size()); ++n)
            if (first->commands[n].coordinates != second->commands[n].coordinates)
                moved = true;
        require(moved, "third party game did not advance its state");
        session.key(22, true);
        session.key(22, false);
        session.pause();
        session.resume();
        session.stop();
        std::cout << "unchanged canvas apk advanced across handler frames\n";
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
