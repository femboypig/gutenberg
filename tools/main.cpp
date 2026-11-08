#include "gutenberg/dex.hpp"
#include "gutenberg/java.hpp"
#include <charconv>
#include <iostream>

using namespace gutenberg;

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            std::cerr << "usage: gutenberg inspect-dex <file> | run-dex <file> <class> <method> <descriptor> [integer arguments]\n";
            return 2;
        }
        const std::string command(argv[1]);
        auto dex = std::make_shared<DexFile>(readFile(argv[2]));
        if (command == "inspect-dex") {
            std::cout << "dex " << dex->version() << ": " << dex->classes().size() << " classes, " << dex->methods().size() << " method references\n";
            for (const auto& klass : dex->classes()) {
                std::cout << klass.type << " extends " << klass.super << '\n';
                for (const auto* list : {&klass.directMethods, &klass.virtualMethods})
                    for (const auto& method : *list) std::cout << "  " << dex->method(method.index).key() << (method.code ? "" : " [external]") << '\n';
            }
            return 0;
        }
        if (command != "run-dex" || argc < 6) throw Error(ErrorCode::verification, "invalid command");
        Runtime vm;
        installJavaRuntime(vm);
        vm.logger = [](std::string_view message) { std::cout << message << '\n'; };
        vm.load(dex);
        std::vector<Value> args;
        for (int n = 6; n < argc; ++n) {
            const std::string_view text(argv[n]); std::int32_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size()) throw Error(ErrorCode::verification, "argument is not a 32-bit integer");
            args.push_back(Value::integer(value));
        }
        const auto ref = methodRef(argv[3], argv[4], argv[5]);
        const auto result = vm.invoke(ref, args, InvokeKind::static_);
        std::cout << "result: ";
        if (ref.returnType == "V") std::cout << "void";
        else if (ref.returnType == "J") std::cout << result.i64();
        else if (ref.returnType == "D") std::cout << result.f64();
        else if (ref.returnType == "F") std::cout << result.f32();
        else if (ref.returnType.front() == 'L' || ref.returnType.front() == '[') std::cout << javaString(vm, result);
        else std::cout << result.i32();
        std::cout << "\ninstructions: " << vm.instructionCount() << "\nobjects: " << vm.heapSize() << '\n';
        return 0;
    } catch (const GuestException& error) {
        std::cerr << error.what() << '\n';
        for (const auto& frame : error.frames) std::cerr << "  at " << frame << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
