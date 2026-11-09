#include "gutenberg/android.hpp"
#include "gutenberg/dex.hpp"
#include "gutenberg/java.hpp"
#include "gutenberg/jit.hpp"
#include "gutenberg/layout.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace gutenberg;

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "usage: gutenberg inspect-dex <file> | run-dex <file> <class> <method> <descriptor> "
                         "[integer arguments]\n";
            return 2;
        }
        const std::string command(argv[1]);
        if (command == "inspect-apk" || command == "run-apk") {
            auto package = std::make_shared<Package>(readFile(argv[2]));
            const auto& info = package->info();
            std::cout << "package: " << info.id << "\nlabel: " << info.label << "\nversion: " << info.version
                      << "\nlauncher: " << info.launcher << "\ndex files: " << package->dexCount() << '\n';
            if (command == "inspect-apk") {
                const auto& code = package->codeInfo();
                std::cout << "classes: " << code.classes << "\nmethods: " << code.methods << '\n';
                return 0;
            }
            AndroidHost host;
            host.log = [](std::string_view message) { std::cout << "log: " << message << '\n'; };
            host.toast = [](std::string_view message) { std::cout << "toast: " << message << '\n'; };
            host.present = [](const Scene& scene) {
                std::cout << "scene: " << std::quoted(scene.title) << '\n';
                for (const auto& [handle, node] : scene.views) {
                    std::cout << "view " << handle << ' ' << node.androidId << ' ' << node.kind << ' '
                              << std::quoted(node.text) << '\n';
                    if (node.drawing)
                        std::cout << "drawing " << handle << " commands: " << node.drawing->commands.size()
                                  << " work: " << node.drawing->replayCost << '\n';
                }
            };
            AndroidSession session(package, host);
            session.launch();
            const auto layout =
                layoutScene(session.scene(), {host.screenWidth, host.screenHeight},
                            [](const ViewNode& node, Size available) {
                                return Size{std::min(available.width, static_cast<float>(node.text.size()) *
                                                                          node.textSize * 0.5f),
                                            node.text.empty() ? 0 : node.textSize * 1.2f};
                            });
            std::vector<ViewSize> sizes;
            for (const auto& [handle, box] : layout)
                sizes.push_back({handle, box.frame.width, box.frame.height});
            session.resizeViews(sizes);
            for (int n = 3; n < argc; ++n) {
                const std::string option(argv[n]);
                if (option == "--tick")
                    session.pumpTasks();
                else if ((option == "--key" || option == "--wait") && n + 1 < argc) {
                    const std::string_view text(argv[++n]);
                    int value = 0;
                    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (error != std::errc{} || end != text.data() + text.size() || value < 0 || value > 5000)
                        throw Error(ErrorCode::verification, "invalid key or wait value");
                    if (option == "--key") {
                        session.key(value, true);
                        session.key(value, false);
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(value));
                        session.pumpTasks();
                    }
                } else if (option == "--click" && n + 1 < argc) {
                    const std::string text(argv[++n]);
                    std::uint64_t target = 0;
                    for (const auto& [handle, node] : session.scene().views)
                        if (node.text == text) {
                            target = handle;
                            break;
                        }
                    if (!target)
                        throw Error(ErrorCode::verification, "no view with text: " + text);
                    session.click(target);
                } else if (option == "--input" && n + 2 < argc) {
                    const std::string_view number(argv[++n]);
                    std::size_t index = 0;
                    const auto [end, error] =
                        std::from_chars(number.data(), number.data() + number.size(), index);
                    if (error != std::errc{} || end != number.data() + number.size())
                        throw Error(ErrorCode::verification, "invalid input index");
                    const std::string text(argv[++n]);
                    std::uint64_t target = 0;
                    for (const auto& [handle, node] : session.scene().views)
                        if (node.kind == "Landroid/widget/EditText;") {
                            if (!index--) {
                                target = handle;
                                break;
                            }
                        }
                    if (!target)
                        throw Error(ErrorCode::verification, "input index not found");
                    session.changeText(target, text);
                } else
                    throw Error(ErrorCode::verification, "unknown apk option: " + option);
            }
            session.stop();
            return 0;
        }
        auto dex = std::make_shared<DexFile>(readFile(argv[2]));
        if (command == "inspect-dex") {
            std::cout << "dex " << dex->version() << ": " << dex->classes().size() << " classes, "
                      << dex->methods().size() << " method references\n";
            for (const auto& klass : dex->classes()) {
                std::cout << klass.type << " extends " << klass.super << '\n';
                for (const auto* list : {&klass.directMethods, &klass.virtualMethods})
                    for (const auto& method : *list)
                        std::cout << "  " << dex->method(method.index).key()
                                  << (method.codeOffset ? "" : " [external]") << '\n';
            }
            return 0;
        }
        if (command != "run-dex" || argc < 6)
            throw Error(ErrorCode::verification, "invalid command");
        Runtime vm;
        installJavaRuntime(vm);
        vm.logger = [](std::string_view message) { std::cout << message << '\n'; };
        vm.load(dex);
        std::vector<Value> args;
        for (int n = 6; n < argc; ++n) {
            const std::string_view text(argv[n]);
            std::int32_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
                throw Error(ErrorCode::verification, "argument is not a 32-bit integer");
            args.push_back(Value::integer(value));
        }
        const auto ref = methodRef(argv[3], argv[4], argv[5]);
        const auto result = vm.invoke(ref, args, InvokeKind::static_);
        std::cout << "result: ";
        if (ref.returnType == "V")
            std::cout << "void";
        else if (ref.returnType == "J")
            std::cout << result.i64();
        else if (ref.returnType == "D")
            std::cout << result.f64();
        else if (ref.returnType == "F")
            std::cout << result.f32();
        else if (ref.returnType.front() == 'L' || ref.returnType.front() == '[')
            std::cout << javaString(vm, result);
        else
            std::cout << result.i32();
        std::cout << "\ninstructions: " << vm.instructionCount() << "\nobjects: " << vm.heapSize() << '\n';
        std::cout << "jit methods: " << vm.jit().statistics().compiledMethods
                  << "\njit executions: " << vm.jit().statistics().executions << '\n';
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
