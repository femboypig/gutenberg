#include "gutenberg/java.hpp"
#include "test.hpp"
#include <chrono>

using namespace gutenberg;
namespace {
Value call(Runtime& vm, std::string owner, std::string name, std::string descriptor,
           std::initializer_list<Value> values, InvokeKind kind = InvokeKind::virtual_) {
    return vm.invoke(std::move(owner), std::move(name), std::move(descriptor),
                     Arguments(values.begin(), values.size()), kind);
}
struct Directory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("gutenberg-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
} // namespace

TEST(java_byte_streams_preserve_binary_data_and_eof) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto output = vm.allocate("Ljava/io/ByteArrayOutputStream;");
    (void)call(vm, "Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {output}, InvokeKind::direct);
    for (int value : {0, 255, 128, 42})
        (void)call(vm, "Ljava/io/OutputStream;", "write", "(I)V", {output, Value::integer(value)});
    const auto bytes = call(vm, "Ljava/io/ByteArrayOutputStream;", "toByteArray", "()[B", {output});
    const auto input = vm.allocate("Ljava/io/ByteArrayInputStream;");
    (void)call(vm, "Ljava/io/ByteArrayInputStream;", "<init>", "([B)V", {input, bytes}, InvokeKind::direct);
    for (int value : {0, 255, 128, 42, -1})
        CHECK(call(vm, "Ljava/io/InputStream;", "read", "()I", {input}).i32() == value);
    const auto empty = vm.array("[B", 0);
    CHECK(call(vm, "Ljava/io/InputStream;", "read", "([B)I", {input, empty}).i32() == 0);
}

TEST(java_buffered_output_flushes_to_wrapped_stream) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto output = vm.allocate("Ljava/io/ByteArrayOutputStream;"),
               buffered = vm.allocate("Ljava/io/BufferedOutputStream;");
    (void)call(vm, "Ljava/io/ByteArrayOutputStream;", "<init>", "()V", {output}, InvokeKind::direct);
    (void)call(vm, "Ljava/io/BufferedOutputStream;", "<init>", "(Ljava/io/OutputStream;)V",
               {buffered, output}, InvokeKind::direct);
    (void)call(vm, "Ljava/io/OutputStream;", "write", "(I)V", {buffered, Value::integer(65)});
    CHECK(call(vm, "Ljava/io/ByteArrayOutputStream;", "size", "()I", {output}).i32() == 0);
    (void)call(vm, "Ljava/io/OutputStream;", "flush", "()V", {buffered});
    CHECK(vm.text(call(vm, "Ljava/io/ByteArrayOutputStream;", "toString", "()Ljava/lang/String;",
                       {output})) == "A");
}

TEST(java_file_streams_use_persistent_app_storage) {
    Directory directory;
    Runtime vm;
    JavaServices services;
    services.files = std::make_shared<FileSandbox>(directory.path, "org.example");
    installJavaRuntime(vm, services);
    const auto path = vm.string("state.bin"), output = vm.allocate("Ljava/io/FileOutputStream;");
    (void)call(vm, "Ljava/io/FileOutputStream;", "<init>", "(Ljava/lang/String;)V", {output, path},
               InvokeKind::direct);
    (void)call(vm, "Ljava/io/OutputStream;", "write", "(I)V", {output, Value::integer(255)});
    (void)call(vm, "Ljava/io/OutputStream;", "close", "()V", {output});
    CHECK(services.files->read("state.bin") == Bytes({255}));
    const auto input = vm.allocate("Ljava/io/FileInputStream;");
    (void)call(vm, "Ljava/io/FileInputStream;", "<init>", "(Ljava/lang/String;)V", {input, path},
               InvokeKind::direct);
    CHECK(call(vm, "Ljava/io/InputStream;", "read", "()I", {input}).i32() == 255);
    (void)call(vm, "Ljava/io/InputStream;", "close", "()V", {input});
    try {
        (void)call(vm, "Ljava/io/InputStream;", "read", "()I", {input});
        CHECK(false);
    } catch (const GuestException& error) {
        CHECK(vm.assignable(vm.object(error.value).type, "Ljava/io/IOException;"));
    }
}

TEST(java_reader_handles_cr_lf_and_unicode) {
    Runtime vm;
    installJavaRuntime(vm);
    auto input = vm.allocate("Ljava/io/StringReader;"), buffered = vm.allocate("Ljava/io/BufferedReader;");
    (void)call(vm, "Ljava/io/StringReader;", "<init>", "(Ljava/lang/String;)V",
               {input, vm.string("one\r\nмир 😀\rthree\n")}, InvokeKind::direct);
    (void)call(vm, "Ljava/io/BufferedReader;", "<init>", "(Ljava/io/Reader;)V", {buffered, input},
               InvokeKind::direct);
    for (auto text : {"one", "мир 😀", "three"})
        CHECK(vm.text(call(vm, "Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", {buffered})) ==
              text);
    CHECK(call(vm, "Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", {buffered}).isNull());
}

TEST(java_unicode_replacement_preserves_utf16_identity) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto value = vm.string(std::u16string{0xd800}, true);
    CHECK(vm.string(std::u16string{0xd800}, true) == value);
    CHECK(!(vm.string(std::u16string{0xd801}, true) == value));
    CHECK(vm.text(value) == "�");
    const auto bytes = call(vm, "Ljava/lang/String;", "getBytes", "()[B", {value});
    CHECK(vm.object(bytes).elements.size() == 1);
    CHECK(vm.object(bytes).elements[0].i32() == '?');
    CHECK(utf16Replacing(std::string("\xe1\x80", 2)) == u"�");
    CHECK(utf16Replacing(std::string("\xe0\x80\x80", 3)) == u"���");
}

TEST(java_stream_reader_preserves_split_utf8_sequences) {
    Runtime vm;
    installJavaRuntime(vm);
    const auto line = std::string(8191, 'a') + "😀";
    const auto text = vm.string(line + "\nnext");
    const auto bytes = call(vm, "Ljava/lang/String;", "getBytes", "()[B", {text});
    const auto input = vm.allocate("Ljava/io/ByteArrayInputStream;"),
               reader = vm.allocate("Ljava/io/InputStreamReader;"),
               buffered = vm.allocate("Ljava/io/BufferedReader;");
    (void)call(vm, "Ljava/io/ByteArrayInputStream;", "<init>", "([B)V", {input, bytes}, InvokeKind::direct);
    (void)call(vm, "Ljava/io/InputStreamReader;", "<init>", "(Ljava/io/InputStream;)V", {reader, input},
               InvokeKind::direct);
    (void)call(vm, "Ljava/io/BufferedReader;", "<init>", "(Ljava/io/Reader;)V", {buffered, reader},
               InvokeKind::direct);
    CHECK(vm.text(call(vm, "Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", {buffered})) ==
          line);
    CHECK(vm.text(call(vm, "Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", {buffered})) ==
          "next");
}
