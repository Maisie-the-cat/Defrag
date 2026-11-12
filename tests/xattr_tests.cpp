#include "../defrag_core.cpp"

#include <cerrno>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <sys/xattr.h>

namespace {
using XattrMap = std::map<std::string, std::vector<unsigned char>>;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        char pattern[] = "/tmp/defrag-xattr-tests-XXXXXX";
        char* result = mkdtemp(pattern);
        if (!result) throw std::runtime_error("mkdtemp failed");
        path_ = result;
    }
    ~TemporaryDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
};

bool write_file(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.close();
    return static_cast<bool>(out);
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool set_attribute(int fd, const std::string& name, const std::vector<unsigned char>& value) {
    const void* data = value.empty() ? nullptr : value.data();
    return fsetxattr(fd, name.c_str(), data, value.size(), 0) == 0;
}

bool read_attributes(int fd, XattrMap& attributes, std::string& error) {
    const ssize_t required = flistxattr(fd, nullptr, 0);
    if (required < 0) {
        error = std::string("flistxattr(size): ") + std::strerror(errno);
        return false;
    }
    std::vector<char> names(static_cast<std::size_t>(required));
    const ssize_t actual = flistxattr(fd, names.empty() ? nullptr : names.data(), names.size());
    if (actual < 0) {
        error = std::string("flistxattr(data): ") + std::strerror(errno);
        return false;
    }
    if (static_cast<std::size_t>(actual) > names.size()) {
        error = "flistxattr returned more bytes than the supplied buffer";
        return false;
    }

    for (std::size_t offset = 0; offset < static_cast<std::size_t>(actual);) {
        const char* start = names.data() + offset;
        const std::size_t remaining = static_cast<std::size_t>(actual) - offset;
        const void* terminator = std::memchr(start, '\0', remaining);
        if (!terminator) {
            error = "xattr name list contains an unterminated entry";
            return false;
        }
        const auto* end = static_cast<const char*>(terminator);
        if (end == start) {
            error = "xattr name list contains an empty entry";
            return false;
        }
        const std::string name(start, end);
        const ssize_t value_size = fgetxattr(fd, name.c_str(), nullptr, 0);
        if (value_size < 0) {
            error = std::string("fgetxattr(size) for ") + name + ": " + std::strerror(errno);
            return false;
        }
        std::vector<unsigned char> value(static_cast<std::size_t>(value_size));
        if (value_size > 0) {
            const ssize_t value_actual = fgetxattr(fd, name.c_str(), value.data(), value.size());
            if (value_actual != value_size) {
                error = std::string("fgetxattr(data) for ") + name + " returned an unexpected length";
                return false;
            }
        }
        attributes.emplace(name, std::move(value));
        offset += static_cast<std::size_t>(end - start) + 1;
    }
    return true;
}

bool get_attributes(const fs::path& path, XattrMap& attributes, std::string& error) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        error = std::string("open for xattr verification: ") + std::strerror(errno);
        return false;
    }
    const bool ok = read_attributes(fd, attributes, error);
    close(fd);
    return ok;
}

bool run_roundtrip(const fs::path& path, const std::string& contents, const XattrMap& additions) {
    if (!write_file(path, contents)) {
        std::cerr << "FAIL: cannot create test file " << path << '\n';
        return false;
    }
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        std::cerr << "FAIL: cannot open test file: " << std::strerror(errno) << '\n';
        return false;
    }
    XattrMap expected;
    std::string error;
    if (!read_attributes(fd, expected, error)) {
        std::cerr << "FAIL: cannot read baseline xattrs: " << error << '\n';
        close(fd);
        return false;
    }
    for (const auto& [name, value] : additions) {
        if (!set_attribute(fd, name, value)) {
            std::cerr << "FAIL: cannot set " << name << ": " << std::strerror(errno) << '\n';
            close(fd);
            return false;
        }
        expected[name] = value;
    }
    close(fd);

    DefragCore::GenericDefragmenter defragmenter;
    if (!defragmenter.defragment(path, nullptr)) {
        std::cerr << "FAIL: generic defragmentation refused " << path << '\n';
        return false;
    }
    if (read_file(path) != contents) {
        std::cerr << "FAIL: file contents changed during xattr round-trip\n";
        return false;
    }
    XattrMap actual;
    if (!get_attributes(path, actual, error)) {
        std::cerr << "FAIL: cannot read copied xattrs: " << error << '\n';
        return false;
    }
    if (actual != expected) {
        std::cerr << "FAIL: xattr map differs after defragmentation (expected " << expected.size()
                  << " attributes, found " << actual.size() << ")\n";
        return false;
    }
    return true;
}

bool test_no_user_attributes(const fs::path& path) {
    const std::string contents = "file with no user xattrs";
    if (!run_roundtrip(path, contents, {})) return false;
    XattrMap actual;
    std::string error;
    if (!get_attributes(path, actual, error)) {
        std::cerr << "FAIL: cannot verify empty xattr case: " << error << '\n';
        return false;
    }
    for (const auto& [name, value] : actual) {
        (void)value;
        if (name.rfind("user.defrag.edge.", 0) == 0) {
            std::cerr << "FAIL: unexpected test xattr remained on empty-case file\n";
            return false;
        }
    }
    std::cout << "PASS: file with no test xattrs\n";
    return true;
}

bool test_value_and_name_edges(const fs::path& path) {
    XattrMap additions;
    additions["user.defrag.edge.empty"] = {};
    additions["user.defrag.edge.one-byte"] = {0xA5};
    additions["user.defrag.edge.binary"] = {0x00, 0xFF, 0x80, 0x41, 0x00, 0x7F};

    std::vector<unsigned char> long_name_value(64, 0x5A);
    additions["user." + std::string(240, 'n')] = std::move(long_name_value);

    for (int i = 0; i < 24; ++i) {
        const std::string suffix = (i < 10 ? "0" : "") + std::to_string(i);
        additions["user.defrag.edge.many." + suffix] = {
            static_cast<unsigned char>(i), 0x00, static_cast<unsigned char>(255 - i)
        };
    }
    if (!run_roundtrip(path, std::string("content\0with\0NUL", 16), additions)) return false;
    std::cout << "PASS: zero-length, binary, long-name, and multi-attribute values\n";
    return true;
}

bool test_large_value_if_supported(const fs::path& path) {
    XattrMap additions;
    std::vector<unsigned char> large(2048);
    for (std::size_t i = 0; i < large.size(); ++i)
        large[i] = static_cast<unsigned char>((i * 37 + 11) & 0xFF);
    additions["user.defrag.edge.large"] = large;

    if (!write_file(path, "large xattr value")) return false;
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    if (!set_attribute(fd, additions.begin()->first, large)) {
        const int error = errno;
        close(fd);
        if (error == E2BIG || error == ENOSPC || error == EDQUOT || error == ERANGE ||
            error == EOPNOTSUPP || error == ENOTSUP) {
            std::cout << "SKIP: filesystem rejected a 2048-byte xattr value: " << std::strerror(error) << '\n';
            return true;
        }
        std::cerr << "FAIL: unexpected error setting large xattr: " << std::strerror(error) << '\n';
        return false;
    }
    close(fd);
    if (!run_roundtrip(path, "large xattr value", additions)) return false;
    std::cout << "PASS: 2048-byte xattr value\n";
    return true;
}

bool test_linked_file_failure_preserves_xattrs(const fs::path& path, const fs::path& alias) {
    const std::string contents = "linked file must not be replaced";
    if (!write_file(path, contents)) return false;
    int fd = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    const std::vector<unsigned char> value = {0x00, 0x42, 0xFF};
    if (!set_attribute(fd, "user.defrag.edge.linked", value)) {
        std::cerr << "FAIL: cannot set xattr on hard-link safety test: " << std::strerror(errno) << '\n';
        close(fd);
        return false;
    }
    XattrMap expected;
    std::string error;
    if (!read_attributes(fd, expected, error)) {
        std::cerr << "FAIL: cannot snapshot hard-link test xattrs: " << error << '\n';
        close(fd);
        return false;
    }
    close(fd);
    if (link(path.c_str(), alias.c_str()) != 0) {
        std::cerr << "FAIL: cannot create hard link: " << std::strerror(errno) << '\n';
        return false;
    }

    DefragCore::GenericDefragmenter defragmenter;
    if (defragmenter.defragment(path, nullptr)) {
        std::cerr << "FAIL: generic defragmenter accepted a multiply-linked file\n";
        return false;
    }
    if (read_file(path) != contents || read_file(alias) != contents) {
        std::cerr << "FAIL: linked file contents changed after refusal\n";
        return false;
    }
    for (const auto& candidate : {path, alias}) {
        XattrMap actual;
        if (!get_attributes(candidate, actual, error)) {
            std::cerr << "FAIL: xattr not preserved after refusal for " << candidate << ": " << error << '\n';
            return false;
        }
        if (actual != expected) {
            std::cerr << "FAIL: xattr map changed after refusal for " << candidate << '\n';
            return false;
        }
    }
    std::cout << "PASS: refusal leaves hard-linked file xattrs intact\n";
    return true;
}
}

int main() {
    TemporaryDirectory temporary;
    const fs::path probe = temporary.path() / "probe";
    if (!write_file(probe, "xattr capability probe")) {
        std::cerr << "FAIL: cannot create xattr capability probe\n";
        return 1;
    }
    int probe_fd = open(probe.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (probe_fd < 0) {
        std::cerr << "FAIL: cannot open xattr capability probe\n";
        return 1;
    }
    const std::vector<unsigned char> probe_value = {0x01};
    if (!set_attribute(probe_fd, "user.defrag.edge.probe", probe_value)) {
        const int error = errno;
        close(probe_fd);
        if (error == ENOTSUP || error == EOPNOTSUPP) {
            std::cout << "SKIP: filesystem does not support user extended attributes\n";
            return 0;
        }
        std::cerr << "FAIL: xattr capability probe failed: " << std::strerror(error) << '\n';
        return 1;
    }
    close(probe_fd);

    const bool passed =
        test_no_user_attributes(temporary.path() / "no-user-attrs") &&
        test_value_and_name_edges(temporary.path() / "value-and-name-edges") &&
        test_large_value_if_supported(temporary.path() / "large-value") &&
        test_linked_file_failure_preserves_xattrs(temporary.path() / "linked", temporary.path() / "linked-alias");
    if (!passed) return 1;
    std::cout << "Xattr edge-case suite passed\n";
    return 0;
}
