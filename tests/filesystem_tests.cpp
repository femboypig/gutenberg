#include "gutenberg/filesystem.hpp"
#include "test.hpp"
#include <chrono>

using namespace gutenberg;
namespace {
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("gutenberg-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory() {
        std::filesystem::create_directories(path);
    }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace

TEST(file_sandbox_normalizes_guest_aliases) {
    Directory directory;
    FileSandbox sandbox(directory.path / "app", "org.example");
    CHECK(sandbox.resolve("a.txt") == sandbox.resolve("/data/data/org.example/files/a.txt"));
    CHECK(sandbox.resolve("/sdcard/test.txt") == sandbox.resolve("/storage/emulated/0/test.txt"));
    CHECK(sandbox.isDirectory("/sdcard"));
    test::rejects([&] { (void)sandbox.resolve("../../../../../../etc/passwd"); });
    test::rejects([&] { (void)sandbox.resolve("/data/user/0/org.example.evil/files/a.txt"); });
}

TEST(file_sandbox_rejects_symlink_escape) {
    Directory directory;
    FileSandbox sandbox(directory.path / "app", "org.example");
    std::filesystem::create_directory_symlink(directory.path, directory.path / "app" / "files" / "escape");
    test::rejects([&] { (void)sandbox.resolve("escape/secret.txt"); });
}

TEST(file_sandbox_reads_writes_and_renames) {
    Directory directory;
    FileSandbox sandbox(directory.path / "app", "org.example");
    CHECK(sandbox.createFile("state.bin"));
    CHECK(!sandbox.createFile("state.bin"));
    sandbox.write("state.bin", Bytes{1, 2, 3});
    sandbox.write("state.bin", Bytes{4}, true);
    CHECK(sandbox.read("state.bin") == Bytes({1, 2, 3, 4}));
    CHECK(sandbox.length("state.bin") == 4);
    CHECK(sandbox.rename("state.bin", "renamed.bin"));
    CHECK(!sandbox.exists("state.bin"));
    CHECK(sandbox.list(sandbox.filesDirectory()) == std::vector<std::string>({"renamed.bin"}));
    CHECK(sandbox.remove("renamed.bin"));
}
