#include "gutenberg/filesystem.hpp"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

namespace gutenberg {
namespace {
bool below(const std::filesystem::path& path, const std::filesystem::path& root) {
    auto part = path.begin();
    for (const auto& expected : root) {
        if (part == path.end() || *part != expected)
            return false;
        ++part;
    }
    return true;
}
bool prefix(std::string_view path, std::string_view base) {
    return path == base || (path.starts_with(base) && path.size() > base.size() && path[base.size()] == '/');
}
} // namespace

FileSandbox::FileSandbox(std::filesystem::path root, std::string package) {
    if (!root.is_absolute() || package.empty() || package.find('/') != package.npos ||
        package.find('\\') != package.npos)
        throw Error(ErrorCode::verification, "invalid file sandbox configuration");
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
        throw Error(ErrorCode::io, "cannot create app storage");
    root_ = std::filesystem::canonical(root, error);
    if (error)
        throw Error(ErrorCode::io, "cannot resolve app storage");
    guestRoot_ = "/data/user/0/" + package;
    legacyRoot_ = "/data/data/" + package;
    for (const auto* child : {"files", "cache", "shared"}) {
        std::filesystem::create_directories(root_ / child, error);
        if (error)
            throw Error(ErrorCode::io, "cannot create app data directories");
    }
}

std::string FileSandbox::normalize(std::string_view path) const {
    if (path.find('\0') != path.npos)
        throw Error(ErrorCode::verification, "nul byte in file path");
    std::filesystem::path guest(path);
    if (!guest.is_absolute())
        guest = std::filesystem::path(filesDirectory()) / guest;
    auto result = guest.lexically_normal().generic_string();
    if (prefix(result, legacyRoot_))
        result = guestRoot_ + result.substr(legacyRoot_.size());
    if (prefix(result, "/storage/emulated/0"))
        result = "/sdcard" + result.substr(std::string_view("/storage/emulated/0").size());
    if (!prefix(result, guestRoot_) && !prefix(result, "/sdcard"))
        throw Error(ErrorCode::verification, "file path is outside app storage");
    return result;
}

std::filesystem::path FileSandbox::resolve(std::string_view path) const {
    const auto normalized = normalize(path);
    auto candidate =
        prefix(normalized, "/sdcard")
            ? root_ / "shared" / normalized.substr(normalized.size() > 7 ? 8 : 7)
            : root_ / normalized.substr(guestRoot_.size() + (normalized.size() > guestRoot_.size() ? 1 : 0));
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(candidate, error);
    if (error || !below(canonical, root_))
        throw Error(ErrorCode::verification, "file path escapes app storage");
    candidate = candidate.lexically_normal();
    if (candidate.filename().empty())
        candidate = candidate.parent_path();
    return candidate;
}
Bytes FileSandbox::read(std::string_view path, std::size_t limit) const {
    return readFile(resolve(path), limit);
}
void FileSandbox::write(std::string_view path, std::span<const std::uint8_t> bytes, bool append) const {
    std::ofstream file(resolve(path), std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!file)
        throw Error(ErrorCode::io, "cannot open app file for writing");
    if (!bytes.empty())
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
        throw Error(ErrorCode::io, "cannot write app file");
}
bool FileSandbox::exists(std::string_view path) const {
    std::error_code error;
    return std::filesystem::exists(resolve(path), error);
}
bool FileSandbox::isDirectory(std::string_view path) const {
    std::error_code error;
    return std::filesystem::is_directory(resolve(path), error);
}
std::uint64_t FileSandbox::length(std::string_view path) const {
    std::error_code error;
    const auto size = std::filesystem::file_size(resolve(path), error);
    return error ? 0 : size;
}
bool FileSandbox::createFile(std::string_view path) const {
    const auto file = resolve(path);
    const int descriptor = open(file.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        if (errno == EEXIST)
            return false;
        throw Error(ErrorCode::io, "cannot create app file");
    }
    close(descriptor);
    return true;
}
bool FileSandbox::createDirectories(std::string_view path) const {
    std::error_code error;
    return std::filesystem::create_directories(resolve(path), error);
}
bool FileSandbox::remove(std::string_view path) const {
    const auto file = resolve(path);
    if (file == root_)
        return false;
    std::error_code error;
    return std::filesystem::remove(file, error);
}
bool FileSandbox::rename(std::string_view from, std::string_view to) const {
    const auto source = resolve(from), destination = resolve(to);
    if (source == root_ || destination == root_)
        return false;
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
}
std::vector<std::string> FileSandbox::list(std::string_view path) const {
    const auto directory = resolve(path);
    std::error_code error;
    std::vector<std::string> names;
    for (const auto& item : std::filesystem::directory_iterator(directory, error))
        names.push_back(item.path().filename().string());
    if (error)
        throw Error(ErrorCode::io, "cannot list app directory");
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace gutenberg
