#include "gutenberg/java.hpp"
#include "java_internal.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace gutenberg {
namespace {
constexpr std::size_t bufferLimit = 16 * 1024 * 1024;

struct Input : NativeObject {
    virtual std::size_t read(std::span<std::uint8_t> bytes) = 0;
    virtual std::size_t available() = 0;
    virtual void close() = 0;
};
struct MemoryInput final : Input {
    Bytes bytes;
    std::size_t cursor = 0;
    explicit MemoryInput(Bytes value) : bytes(std::move(value)) {}
    std::size_t read(std::span<std::uint8_t> out) override {
        const auto count = std::min(out.size(), bytes.size() - cursor);
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), count, out.begin());
        cursor += count;
        return count;
    }
    std::size_t available() override {
        return bytes.size() - cursor;
    }
    void close() override {}
};
struct FileInput final : Input {
    std::ifstream file;
    bool closed = false;
    explicit FileInput(const std::filesystem::path& path) : file(path, std::ios::binary) {
        if (!file)
            throw Error(ErrorCode::io, "cannot open input file");
    }
    std::size_t read(std::span<std::uint8_t> out) override {
        if (closed)
            throw Error(ErrorCode::io, "input stream is closed");
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (file.bad())
            throw Error(ErrorCode::io, "cannot read input file");
        return static_cast<std::size_t>(file.gcount());
    }
    std::size_t available() override {
        if (closed)
            throw Error(ErrorCode::io, "input stream is closed");
        if (file.eof())
            return 0;
        const auto position = file.tellg();
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        file.seekg(position);
        return end >= position && position >= 0 ? static_cast<std::size_t>(end - position) : 0;
    }
    void close() override {
        file.close();
        closed = true;
    }
};
struct Output : NativeObject {
    virtual void write(std::span<const std::uint8_t> bytes) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
};
struct MemoryOutput final : Output {
    Bytes bytes;
    void write(std::span<const std::uint8_t> data) override {
        if (data.size() > bufferLimit - bytes.size())
            throw Error(ErrorCode::limit_exceeded, "output buffer limit exceeded");
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    void flush() override {}
    void close() override {}
};
struct FileOutput final : Output {
    std::ofstream file;
    bool closed = false;
    FileOutput(const std::filesystem::path& path, bool append)
        : file(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc)) {
        if (!file)
            throw Error(ErrorCode::io, "cannot open output file");
    }
    void write(std::span<const std::uint8_t> bytes) override {
        if (closed)
            throw Error(ErrorCode::io, "output stream is closed");
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
            throw Error(ErrorCode::io, "cannot write output file");
    }
    void flush() override {
        if (closed)
            throw Error(ErrorCode::io, "output stream is closed");
        file.flush();
        if (!file)
            throw Error(ErrorCode::io, "cannot flush output file");
    }
    void close() override {
        if (!closed) {
            flush();
            file.close();
            closed = true;
        }
    }
};
struct BufferedOutput final : Output {
    std::shared_ptr<Output> wrapped;
    Bytes buffer;
    bool closed = false;
    explicit BufferedOutput(std::shared_ptr<Output> value) : wrapped(std::move(value)) {
        buffer.reserve(8192);
    }
    void write(std::span<const std::uint8_t> bytes) override {
        if (closed)
            throw Error(ErrorCode::io, "output stream is closed");
        if (bytes.size() > 8192 - buffer.size())
            flush();
        if (bytes.size() >= 8192)
            wrapped->write(bytes);
        else
            buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    }
    void flush() override {
        if (closed)
            throw Error(ErrorCode::io, "output stream is closed");
        if (!buffer.empty()) {
            wrapped->write(buffer);
            buffer.clear();
        }
        wrapped->flush();
    }
    void close() override {
        if (!closed) {
            flush();
            wrapped->close();
            closed = true;
        }
    }
};
struct TextInput final : NativeObject {
    std::u16string text;
    std::size_t cursor = 0;
    bool closed = false;
    std::shared_ptr<Input> input;
    Bytes pending;
    bool eof = false;
    void requireOpen() const {
        if (closed)
            throw Error(ErrorCode::io, "reader is closed");
    }
    bool ensure(Runtime& vm) {
        requireOpen();
        while (cursor == text.size() && input && !eof) {
            vm.tick();
            if (cursor) {
                text.clear();
                cursor = 0;
            }
            std::array<std::uint8_t, 8192> block;
            const auto count = input->read(block);
            eof = count == 0;
            pending.insert(pending.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
            std::size_t complete = pending.size();
            if (!eof && complete) {
                auto start = complete - 1;
                while (start && (pending[start] & 0xc0) == 0x80 && complete - start < 4)
                    --start;
                const auto first = pending[start];
                const unsigned expected = first >= 0xc2 && first <= 0xdf   ? 2
                                          : first >= 0xe0 && first <= 0xef ? 3
                                          : first >= 0xf0 && first <= 0xf4 ? 4
                                                                           : 1;
                if (complete - start < expected)
                    complete = start;
            }
            text += utf16Replacing(std::string_view(reinterpret_cast<const char*>(pending.data()), complete));
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(complete));
        }
        return cursor < text.size();
    }
};

template <class T> std::shared_ptr<T> resource(Runtime& vm, Value value) {
    auto result = std::dynamic_pointer_cast<T>(vm.object(value).native);
    if (!result)
        vm.raise("Ljava/io/IOException;", "stream is not initialized");
    return result;
}
FileSandbox& storage(Runtime& vm, const JavaServices& services) {
    if (!services.files)
        vm.raise("Ljava/lang/SecurityException;", "file storage is not available");
    return *services.files;
}
std::pair<std::size_t, std::size_t> slice(Runtime& vm, std::size_t length, std::int32_t offset,
                                          std::int32_t count) {
    if (offset < 0 || count < 0 || static_cast<std::size_t>(offset) > length ||
        static_cast<std::size_t>(count) > length - static_cast<std::size_t>(offset))
        vm.raise("Ljava/lang/IndexOutOfBoundsException;", "invalid stream buffer range");
    return {static_cast<std::size_t>(offset), static_cast<std::size_t>(count)};
}
Bytes arrayBytes(Runtime& vm, Value array, std::int32_t offset, std::int32_t count) {
    const auto& object = vm.object(array);
    if (object.type != "[B")
        vm.raise("Ljava/lang/ClassCastException;", "expected byte array");
    const auto [start, length] = slice(vm, object.elements.size(), offset, count);
    Bytes result(length);
    for (std::size_t n = 0; n < length; ++n)
        result[n] = static_cast<std::uint8_t>(object.elements[start + n].bits);
    return result;
}
Bytes arrayBytes(Runtime& vm, Value array) {
    return arrayBytes(vm, array, 0, static_cast<std::int32_t>(vm.object(array).elements.size()));
}
Value byteArray(Runtime& vm, std::span<const std::uint8_t> bytes) {
    const auto value = vm.array("[B", static_cast<std::int32_t>(bytes.size()));
    auto& elements = vm.object(value).elements;
    for (std::size_t n = 0; n < bytes.size(); ++n)
        elements[n] = Value::integer(std::bit_cast<std::int8_t>(bytes[n]));
    return value;
}
std::shared_ptr<TextInput> reader(std::shared_ptr<Input> input) {
    auto result = std::make_shared<TextInput>();
    result->input = std::move(input);
    return result;
}
template <class F> Value translate(Runtime& vm, F body) {
    try {
        return body();
    } catch (const Error& error) {
        if (error.code() == ErrorCode::cancelled)
            throw;
        vm.raise(error.code() == ErrorCode::verification ? "Ljava/lang/SecurityException;"
                                                         : "Ljava/io/IOException;",
                 error.what());
    }
}
std::string normalizeFilePath(std::string_view path) {
    std::string result;
    for (char c : path)
        if (c != '/' || result.empty() || result.back() != '/')
            result += c;
    if (result.size() > 1 && result.back() == '/')
        result.pop_back();
    return result;
}
} // namespace

Value memoryInputStream(Runtime& vm, Bytes bytes) {
    const auto value = vm.allocate("Ljava/io/ByteArrayInputStream;");
    vm.object(value).native = std::make_shared<MemoryInput>(std::move(bytes));
    return value;
}
Value memoryOutputStream(Runtime& vm) {
    const auto value = vm.allocate("Ljava/io/ByteArrayOutputStream;");
    vm.object(value).native = std::make_shared<MemoryOutput>();
    return value;
}
Bytes outputStreamBytes(Runtime& vm, Value stream) {
    return resource<MemoryOutput>(vm, stream)->bytes;
}

void installJavaIo(Runtime& vm, JavaServices services) {
    vm.defineClass("Ljava/io/Closeable;");
    vm.defineClass("Ljava/io/Flushable;");
    vm.defineClass("Ljava/io/IOException;", "Ljava/lang/Exception;");
    for (const auto* type : {"FileNotFoundException", "EOFException", "UnsupportedEncodingException"})
        vm.defineClass("Ljava/io/" + std::string(type) + ";", "Ljava/io/IOException;");
    vm.defineClass("Ljava/io/File;", "Ljava/lang/Object;",
                   {"Ljava/io/Serializable;", "Ljava/lang/Comparable;"});
    vm.defineClass("Ljava/io/InputStream;", "Ljava/lang/Object;", {"Ljava/io/Closeable;"});
    vm.defineClass("Ljava/io/OutputStream;", "Ljava/lang/Object;",
                   {"Ljava/io/Closeable;", "Ljava/io/Flushable;"});
    for (const auto* name : {"ByteArrayInputStream", "FileInputStream", "FilterInputStream"})
        vm.defineClass("Ljava/io/" + std::string(name) + ";", "Ljava/io/InputStream;");
    vm.defineClass("Ljava/io/BufferedInputStream;", "Ljava/io/FilterInputStream;");
    for (const auto* name : {"ByteArrayOutputStream", "FileOutputStream", "FilterOutputStream"})
        vm.defineClass("Ljava/io/" + std::string(name) + ";", "Ljava/io/OutputStream;");
    vm.defineClass("Ljava/io/BufferedOutputStream;", "Ljava/io/FilterOutputStream;");
    vm.defineClass("Ljava/io/Reader;", "Ljava/lang/Object;", {"Ljava/io/Closeable;"});
    vm.defineClass("Ljava/io/InputStreamReader;", "Ljava/io/Reader;");
    vm.defineClass("Ljava/io/FileReader;", "Ljava/io/InputStreamReader;");
    vm.defineClass("Ljava/io/StringReader;", "Ljava/io/Reader;");
    vm.defineClass("Ljava/io/BufferedReader;", "Ljava/io/Reader;");
    auto bind = [&](std::string owner, std::string name, std::string descriptor, NativeMethod method) {
        vm.bind(std::move(owner), std::move(name), std::move(descriptor),
                [method = std::move(method)](Runtime& r, Arguments a) {
                    return translate(r, [&] { return method(r, a); });
                });
    };
    for (const auto* name :
         {"IOException", "FileNotFoundException", "EOFException", "UnsupportedEncodingException"}) {
        const auto owner = "Ljava/io/" + std::string(name) + ";";
        bind(owner, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
        bind(owner, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
            r.object(a[0]).text = a[1].isNull() ? u"" : r.object(a[1]).text;
            return Value{};
        });
    }
    const std::string file = "Ljava/io/File;";
    bind(file, "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        if (a[1].isNull())
            r.raise("Ljava/lang/NullPointerException;", "null file path");
        r.object(a[0]).text = utf16(normalizeFilePath(r.text(a[1])));
        return Value{};
    });
    for (const auto* parent : {"Ljava/io/File;", "Ljava/lang/String;"})
        bind(file, "<init>", "(" + std::string(parent) + "Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
            if (a[2].isNull())
                r.raise("Ljava/lang/NullPointerException;", "null child path");
            const auto child = r.text(a[2]);
            const auto path = a[1].isNull() ? child : r.text(a[1]) + "/" + child;
            r.object(a[0]).text = utf16(normalizeFilePath(path));
            return Value{};
        });
    for (const auto* name : {"getPath", "toString"})
        bind(file, name, "()Ljava/lang/String;",
             [](Runtime& r, Arguments a) { return r.string(r.object(a[0]).text); });
    for (const auto* name : {"getAbsolutePath", "getCanonicalPath"})
        bind(file, name, "()Ljava/lang/String;", [services](Runtime& r, Arguments a) {
            return r.string(storage(r, services).normalize(r.text(a[0])));
        });
    bind(file, "getName", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        return r.string(std::filesystem::path(r.text(a[0])).filename().string());
    });
    bind(file, "getParent", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto parent = std::filesystem::path(r.text(a[0])).parent_path();
        return parent.empty() ? Value{} : r.string(parent.generic_string());
    });
    bind(file, "getParentFile", "()Ljava/io/File;", [](Runtime& r, Arguments a) {
        const auto parent = std::filesystem::path(r.text(a[0])).parent_path();
        if (parent.empty())
            return Value{};
        const auto value = r.allocate("Ljava/io/File;");
        r.object(value).text = utf16(parent.generic_string());
        return value;
    });
    bind(file, "exists", "()Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).exists(r.text(a[0])));
    });
    bind(file, "isDirectory", "()Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).isDirectory(r.text(a[0])));
    });
    bind(file, "isFile", "()Z", [services](Runtime& r, Arguments a) {
        const auto path = r.text(a[0]);
        auto& files = storage(r, services);
        return Value::integer(files.exists(path) && !files.isDirectory(path));
    });
    bind(file, "length", "()J", [services](Runtime& r, Arguments a) {
        return Value{storage(r, services).length(r.text(a[0])), false};
    });
    bind(file, "createNewFile", "()Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).createFile(r.text(a[0])));
    });
    bind(file, "mkdirs", "()Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).createDirectories(r.text(a[0])));
    });
    bind(file, "delete", "()Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).remove(r.text(a[0])));
    });
    bind(file, "renameTo", "(Ljava/io/File;)Z", [services](Runtime& r, Arguments a) {
        return Value::integer(storage(r, services).rename(r.text(a[0]), r.text(a[1])));
    });
    bind(file, "list", "()[Ljava/lang/String;", [services](Runtime& r, Arguments a) {
        const auto names = storage(r, services).list(r.text(a[0]));
        const auto result = r.array("[Ljava/lang/String;", static_cast<std::int32_t>(names.size()));
        for (std::size_t n = 0; n < names.size(); ++n)
            r.object(result).elements[n] = r.string(names[n]);
        return result;
    });
    vm.putStatic({file, "separator", "Ljava/lang/String;"}, vm.string("/"));
    vm.putStatic({file, "separatorChar", "C"}, Value::integer('/'));
    vm.putStatic({file, "pathSeparator", "Ljava/lang/String;"}, vm.string(":"));
    bind(file, "equals", "(Ljava/lang/Object;)Z", [](Runtime& r, Arguments a) {
        return Value::integer(!a[1].isNull() && r.object(a[1]).type == "Ljava/io/File;" &&
                              r.object(a[0]).text == r.object(a[1]).text);
    });

    const std::string input = "Ljava/io/InputStream;", output = "Ljava/io/OutputStream;";
    for (const auto* type : {"Ljava/io/InputStream;", "Ljava/io/OutputStream;", "Ljava/io/Reader;"})
        bind(type, "<init>", "()V", [](Runtime&, Arguments) { return Value{}; });
    bind("Ljava/io/ByteArrayInputStream;", "<init>", "([B)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<MemoryInput>(arrayBytes(r, a[1]));
        return Value{};
    });
    bind("Ljava/io/ByteArrayInputStream;", "<init>", "([BII)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<MemoryInput>(arrayBytes(r, a[1], a[2].i32(), a[3].i32()));
        return Value{};
    });
    bind("Ljava/io/ByteArrayOutputStream;", "<init>", "()V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = std::make_shared<MemoryOutput>();
        return Value{};
    });
    bind("Ljava/io/ByteArrayOutputStream;", "<init>", "(I)V", [](Runtime& r, Arguments a) {
        if (a[1].i32() < 0)
            r.raise("Ljava/lang/IllegalArgumentException;", "negative buffer size");
        r.object(a[0]).native = std::make_shared<MemoryOutput>();
        return Value{};
    });
    for (const auto* pathType : {"Ljava/lang/String;", "Ljava/io/File;"}) {
        bind("Ljava/io/FileInputStream;", "<init>", "(" + std::string(pathType) + ")V",
             [services](Runtime& r, Arguments a) {
                 r.object(a[0]).native =
                     std::make_shared<FileInput>(storage(r, services).resolve(r.text(a[1])));
                 return Value{};
             });
        bind("Ljava/io/FileOutputStream;", "<init>", "(" + std::string(pathType) + ")V",
             [services](Runtime& r, Arguments a) {
                 r.object(a[0]).native =
                     std::make_shared<FileOutput>(storage(r, services).resolve(r.text(a[1])), false);
                 return Value{};
             });
        bind("Ljava/io/FileOutputStream;", "<init>", "(" + std::string(pathType) + "Z)V",
             [services](Runtime& r, Arguments a) {
                 r.object(a[0]).native =
                     std::make_shared<FileOutput>(storage(r, services).resolve(r.text(a[1])), a[2].bits != 0);
                 return Value{};
             });
        bind("Ljava/io/FileReader;", "<init>", "(" + std::string(pathType) + ")V",
             [services](Runtime& r, Arguments a) {
                 r.object(a[0]).native =
                     reader(std::make_shared<FileInput>(storage(r, services).resolve(r.text(a[1]))));
                 return Value{};
             });
    }
    for (const auto* type : {"Ljava/io/FilterInputStream;", "Ljava/io/BufferedInputStream;"}) {
        bind(type, "<init>", "(Ljava/io/InputStream;)V", [](Runtime& r, Arguments a) {
            r.object(a[0]).native = resource<Input>(r, a[1]);
            return Value{};
        });
        bind(type, "<init>", "(Ljava/io/InputStream;I)V", [](Runtime& r, Arguments a) {
            if (a[2].i32() <= 0)
                r.raise("Ljava/lang/IllegalArgumentException;", "invalid buffer size");
            r.object(a[0]).native = resource<Input>(r, a[1]);
            return Value{};
        });
    }
    for (const auto* type : {"Ljava/io/FilterOutputStream;", "Ljava/io/BufferedOutputStream;"}) {
        const bool buffered = std::string_view(type) == "Ljava/io/BufferedOutputStream;";
        bind(type, "<init>", "(Ljava/io/OutputStream;)V", [buffered](Runtime& r, Arguments a) {
            auto stream = resource<Output>(r, a[1]);
            r.object(a[0]).native = buffered ? std::make_shared<BufferedOutput>(stream)
                                             : std::static_pointer_cast<NativeObject>(stream);
            return Value{};
        });
    }
    bind(input, "read", "()I", [](Runtime& r, Arguments a) {
        std::uint8_t byte = 0;
        return Value::integer(resource<Input>(r, a[0])->read(std::span(&byte, 1)) ? byte : -1);
    });
    auto readBytes = [](Runtime& r, Arguments a) {
        auto& array = r.object(a[1]);
        const auto [start, length] =
            slice(r, array.elements.size(), a.size() == 4 ? a[2].i32() : 0,
                  a.size() == 4 ? a[3].i32() : static_cast<std::int32_t>(array.elements.size()));
        if (array.type != "[B")
            r.raise("Ljava/lang/ClassCastException;", "expected byte array");
        if (!length)
            return Value::integer(0);
        Bytes bytes(length);
        const auto count = resource<Input>(r, a[0])->read(bytes);
        for (std::size_t n = 0; n < count; ++n)
            array.elements[start + n] = Value::integer(std::bit_cast<std::int8_t>(bytes[n]));
        return Value::integer(count ? static_cast<std::int32_t>(count) : -1);
    };
    bind(input, "read", "([B)I", readBytes);
    bind(input, "read", "([BII)I", readBytes);
    bind(input, "available", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(std::min<std::size_t>(
            resource<Input>(r, a[0])->available(), std::numeric_limits<std::int32_t>::max())));
    });
    bind(input, "skip", "(J)J", [](Runtime& r, Arguments a) {
        const auto amount = std::bit_cast<std::int64_t>(argumentWide(a, 1));
        if (amount <= 0)
            return Value::wide(0);
        std::array<std::uint8_t, 8192> bytes;
        std::uint64_t skipped = 0;
        auto stream = resource<Input>(r, a[0]);
        while (skipped < static_cast<std::uint64_t>(amount)) {
            r.tick();
            const auto count = stream->read(std::span(bytes).first(
                std::min<std::size_t>(bytes.size(), static_cast<std::uint64_t>(amount) - skipped)));
            if (!count)
                break;
            skipped += count;
        }
        return Value{skipped, false};
    });
    bind(input, "close", "()V", [](Runtime& r, Arguments a) {
        resource<Input>(r, a[0])->close();
        return Value{};
    });
    bind(output, "write", "(I)V", [](Runtime& r, Arguments a) {
        const auto byte = static_cast<std::uint8_t>(a[1].bits);
        resource<Output>(r, a[0])->write(std::span(&byte, 1));
        return Value{};
    });
    auto writeBytes = [](Runtime& r, Arguments a) {
        const auto bytes = a.size() == 4 ? arrayBytes(r, a[1], a[2].i32(), a[3].i32()) : arrayBytes(r, a[1]);
        resource<Output>(r, a[0])->write(bytes);
        return Value{};
    };
    bind(output, "write", "([B)V", writeBytes);
    bind(output, "write", "([BII)V", writeBytes);
    bind(output, "flush", "()V", [](Runtime& r, Arguments a) {
        resource<Output>(r, a[0])->flush();
        return Value{};
    });
    bind(output, "close", "()V", [](Runtime& r, Arguments a) {
        resource<Output>(r, a[0])->close();
        return Value{};
    });
    bind("Ljava/io/ByteArrayOutputStream;", "size", "()I", [](Runtime& r, Arguments a) {
        return Value::integer(static_cast<std::int32_t>(resource<MemoryOutput>(r, a[0])->bytes.size()));
    });
    bind("Ljava/io/ByteArrayOutputStream;", "reset", "()V", [](Runtime& r, Arguments a) {
        resource<MemoryOutput>(r, a[0])->bytes.clear();
        return Value{};
    });
    bind("Ljava/io/ByteArrayOutputStream;", "toByteArray", "()[B",
         [](Runtime& r, Arguments a) { return byteArray(r, resource<MemoryOutput>(r, a[0])->bytes); });
    bind("Ljava/io/ByteArrayOutputStream;", "toString", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        const auto& bytes = resource<MemoryOutput>(r, a[0])->bytes;
        return r.string(
            utf16Replacing(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
    });
    bind("Ljava/io/InputStreamReader;", "<init>", "(Ljava/io/InputStream;)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = reader(resource<Input>(r, a[1]));
        return Value{};
    });
    bind("Ljava/io/StringReader;", "<init>", "(Ljava/lang/String;)V", [](Runtime& r, Arguments a) {
        auto input = std::make_shared<TextInput>();
        input->text = r.object(a[1]).text;
        r.object(a[0]).native = std::move(input);
        return Value{};
    });
    bind("Ljava/io/BufferedReader;", "<init>", "(Ljava/io/Reader;)V", [](Runtime& r, Arguments a) {
        r.object(a[0]).native = resource<TextInput>(r, a[1]);
        return Value{};
    });
    bind("Ljava/io/Reader;", "read", "()I", [](Runtime& r, Arguments a) {
        auto input = resource<TextInput>(r, a[0]);
        return Value::integer(input->ensure(r) ? input->text[input->cursor++] : -1);
    });
    bind("Ljava/io/Reader;", "close", "()V", [](Runtime& r, Arguments a) {
        auto input = resource<TextInput>(r, a[0]);
        if (input->input)
            input->input->close();
        input->closed = true;
        return Value{};
    });
    auto readChars = [](Runtime& r, Arguments a) {
        auto input = resource<TextInput>(r, a[0]);
        input->requireOpen();
        auto& array = r.object(a[1]);
        const auto [start, length] =
            slice(r, array.elements.size(), a.size() == 4 ? a[2].i32() : 0,
                  a.size() == 4 ? a[3].i32() : static_cast<std::int32_t>(array.elements.size()));
        if (array.type != "[C")
            r.raise("Ljava/lang/ClassCastException;", "expected char array");
        if (length)
            (void)input->ensure(r);
        const auto count = std::min(length, input->text.size() - input->cursor);
        for (std::size_t n = 0; n < count; ++n)
            array.elements[start + n] = Value::integer(input->text[input->cursor++]);
        return Value::integer(!length ? 0 : count ? static_cast<std::int32_t>(count) : -1);
    };
    bind("Ljava/io/Reader;", "read", "([C)I", readChars);
    bind("Ljava/io/Reader;", "read", "([CII)I", readChars);
    bind("Ljava/io/BufferedReader;", "readLine", "()Ljava/lang/String;", [](Runtime& r, Arguments a) {
        auto input = resource<TextInput>(r, a[0]);
        if (!input->ensure(r))
            return Value{};
        std::u16string line;
        while (input->ensure(r)) {
            const auto c = input->text[input->cursor++];
            if (c == '\n')
                break;
            if (c == '\r') {
                if (input->ensure(r) && input->text[input->cursor] == '\n')
                    ++input->cursor;
                break;
            }
            if (line.size() >= bufferLimit)
                throw Error(ErrorCode::limit_exceeded, "line length limit exceeded");
            line += c;
        }
        return r.string(std::move(line));
    });
    if (services.asset) {
        bind("Landroid/content/res/AssetManager;", "open", "(Ljava/lang/String;)Ljava/io/InputStream;",
             [services](Runtime& r, Arguments a) {
                 const auto value = r.allocate("Ljava/io/ByteArrayInputStream;");
                 r.object(value).native = std::make_shared<MemoryInput>(services.asset(r.text(a[1])));
                 return value;
             });
    }
    auto fileObject = [](Runtime& r, std::string path) {
        const auto value = r.allocate("Ljava/io/File;");
        r.object(value).text = utf16(path);
        return value;
    };
    bind("Landroid/content/Context;", "getFilesDir", "()Ljava/io/File;",
         [services, fileObject](Runtime& r, Arguments) {
             return fileObject(r, storage(r, services).filesDirectory());
         });
    bind("Landroid/content/Context;", "getCacheDir", "()Ljava/io/File;",
         [services, fileObject](Runtime& r, Arguments) {
             return fileObject(r, storage(r, services).cacheDirectory());
         });
    auto fileName = [](Runtime& r, Value value) {
        if (value.isNull())
            r.raise("Ljava/lang/NullPointerException;", "null filename");
        const auto name = r.text(value);
        if (name.find('/') != name.npos)
            r.raise("Ljava/lang/IllegalArgumentException;", "filename contains a path separator");
        return name;
    };
    bind("Landroid/content/Context;", "openFileInput", "(Ljava/lang/String;)Ljava/io/FileInputStream;",
         [services, fileName](Runtime& r, Arguments a) {
             const auto path = fileName(r, a[1]);
             const auto value = r.allocate("Ljava/io/FileInputStream;");
             r.object(value).native = std::make_shared<FileInput>(storage(r, services).resolve(path));
             return value;
         });
    bind("Landroid/content/Context;", "openFileOutput", "(Ljava/lang/String;I)Ljava/io/FileOutputStream;",
         [services, fileName](Runtime& r, Arguments a) {
             if (a[2].bits & 3)
                 r.raise("Ljava/lang/SecurityException;", "world accessible file modes are not supported");
             const auto path = fileName(r, a[1]);
             const auto value = r.allocate("Ljava/io/FileOutputStream;");
             r.object(value).native =
                 std::make_shared<FileOutput>(storage(r, services).resolve(path), (a[2].bits & 0x8000) != 0);
             return value;
         });
    bind("Landroid/content/Context;", "deleteFile", "(Ljava/lang/String;)Z",
         [services, fileName](Runtime& r, Arguments a) {
             return Value::integer(storage(r, services).remove(fileName(r, a[1])));
         });
}

} // namespace gutenberg
