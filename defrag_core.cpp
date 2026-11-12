#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <spawn.h>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/btrfs.h>
#include <linux/ext4.h>
#include <linux/magic.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <sys/wait.h>

namespace fs = std::filesystem;
extern char** environ;

#ifndef JFS_SUPER_MAGIC
#define JFS_SUPER_MAGIC 0x3153464a
#endif

namespace DefragCore {
using ProgressCallback = void (*)(int, int, const std::string&);

class FileDescriptor {
public:
    explicit FileDescriptor(int fd = -1) : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return fd_; }
    int release() { int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};

class FilesystemDetector {
public:
    static long get_fs_type(const fs::path& path) {
        struct statfs sfs{};
        if (statfs(path.c_str(), &sfs) != 0) return 0;
        return static_cast<long>(sfs.f_type);
    }

    static std::string get_fs_name(long type) {
        switch (type) {
            case EXT4_SUPER_MAGIC: return "Ext2/3/4";
            case BTRFS_SUPER_MAGIC: return "Btrfs";
            case XFS_SUPER_MAGIC: return "XFS";
            case JFS_SUPER_MAGIC: return "JFS";
            case MSDOS_SUPER_MAGIC: return "FAT16/32";
            case 0x5346544e: return "NTFS";
            default: return "Unknown";
        }
    }
};

class Defragmenter {
public:
    virtual ~Defragmenter() = default;
    virtual bool defragment(const fs::path&, ProgressCallback) = 0;
};

static bool copy_xattrs(int source_fd, int destination_fd) {
    const ssize_t size = flistxattr(source_fd, nullptr, 0);
    if (size < 0) return false;
    if (size == 0) return true;
    std::vector<char> names(static_cast<size_t>(size));
    const ssize_t actual = flistxattr(source_fd, names.data(), names.size());
    if (actual < 0) return false;
    for (size_t offset = 0; offset < static_cast<size_t>(actual);) {
        const char* name = names.data() + offset;
        const size_t length = std::strlen(name);
        if (length == 0 || offset + length >= static_cast<size_t>(actual)) {
            errno = EIO;
            return false;
        }
        const ssize_t value_size = fgetxattr(source_fd, name, nullptr, 0);
        if (value_size < 0) return false;
        std::vector<char> value(static_cast<size_t>(value_size));
        if (value_size > 0 && fgetxattr(source_fd, name, value.data(), value.size()) != value_size) return false;
        if (fsetxattr(destination_fd, name, value.data(), value.size(), 0) != 0) return false;
        offset += length + 1;
    }
    return true;
}

class GenericDefragmenter final : public Defragmenter {
public:
    bool defragment(const fs::path& path, ProgressCallback callback) override {
        FileDescriptor source(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (source.get() < 0) {
            std::cerr << "Cannot open source safely: " << std::strerror(errno) << '\n';
            return false;
        }
        struct stat original{};
        if (fstat(source.get(), &original) != 0 || !S_ISREG(original.st_mode) || original.st_nlink != 1) {
            std::cerr << "Refusing generic replacement of non-regular or multiply-linked file: " << path << '\n';
            return false;
        }

        fs::path pattern = path.parent_path() / ("." + path.filename().string() + ".defrag-XXXXXX");
        std::string pattern_string = pattern.string();
        std::vector<char> writable(pattern_string.begin(), pattern_string.end());
        writable.push_back('\0');
        FileDescriptor destination(mkstemp(writable.data()));
        if (destination.get() < 0) {
            std::cerr << "Cannot create temporary file beside source: " << std::strerror(errno) << '\n';
            return false;
        }
        const fs::path temporary(writable.data());
        bool renamed = false;
        auto cleanup = [&]() { if (!renamed) unlink(temporary.c_str()); };

        std::vector<char> buffer(128 * 1024);
        off_t bytes_copied = 0;
        int last_reported = -1;
        while (true) {
            const ssize_t count = read(source.get(), buffer.data(), buffer.size());
            if (count == 0) break;
            if (count < 0) {
                if (errno == EINTR) continue;
                std::cerr << "Read failed: " << std::strerror(errno) << '\n';
                cleanup();
                return false;
            }
            ssize_t written = 0;
            while (written < count) {
                const ssize_t result = write(destination.get(), buffer.data() + written, static_cast<size_t>(count - written));
                if (result < 0 && errno == EINTR) continue;
                if (result <= 0) {
                    std::cerr << "Write failed: " << std::strerror(errno) << '\n';
                    cleanup();
                    return false;
                }
                written += result;
            }
            bytes_copied += count;
            if (callback) {
                const int percent = original.st_size == 0 ? 100 : static_cast<int>((bytes_copied * 100) / original.st_size);
                if (percent != last_reported) {
                    callback(std::min(percent, 100), 100, path.filename().string());
                    last_reported = percent;
                }
            }
        }

        struct stat after_copy{};
        if (fstat(source.get(), &after_copy) != 0 || original.st_dev != after_copy.st_dev || original.st_ino != after_copy.st_ino ||
            original.st_size != after_copy.st_size || original.st_mtim.tv_sec != after_copy.st_mtim.tv_sec ||
            original.st_mtim.tv_nsec != after_copy.st_mtim.tv_nsec || original.st_ctim.tv_sec != after_copy.st_ctim.tv_sec ||
            original.st_ctim.tv_nsec != after_copy.st_ctim.tv_nsec) {
            std::cerr << "Source changed while it was being copied; leaving original untouched.\n";
            cleanup();
            return false;
        }
        if (fchown(destination.get(), original.st_uid, original.st_gid) != 0 ||
            fchmod(destination.get(), original.st_mode & 07777) != 0) {
            std::cerr << "Unable to preserve ownership or permissions: " << std::strerror(errno) << '\n';
            cleanup();
            return false;
        }
        if (!copy_xattrs(source.get(), destination.get())) {
            std::cerr << "Unable to preserve extended attributes: " << std::strerror(errno) << '\n';
            cleanup();
            return false;
        }
        struct timespec times[2] = {original.st_atim, original.st_mtim};
        if (futimens(destination.get(), times) != 0 || fsync(destination.get()) != 0) {
            std::cerr << "Unable to finalize temporary file: " << std::strerror(errno) << '\n';
            cleanup();
            return false;
        }

        struct stat current_path{};
        if (lstat(path.c_str(), &current_path) != 0 || current_path.st_dev != original.st_dev || current_path.st_ino != original.st_ino ||
            !S_ISREG(current_path.st_mode) || current_path.st_nlink != 1) {
            std::cerr << "Source path changed during copy; refusing replacement.\n";
            cleanup();
            return false;
        }
        if (rename(temporary.c_str(), path.c_str()) != 0) {
            std::cerr << "Atomic replacement failed: " << std::strerror(errno) << '\n';
            cleanup();
            return false;
        }
        renamed = true;
        const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
        FileDescriptor directory(open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (directory.get() < 0 || fsync(directory.get()) != 0) {
            std::cerr << "Warning: replacement completed but directory sync failed: " << std::strerror(errno) << '\n';
            return false;
        }
        if (callback) callback(100, 100, path.filename().string());
        return true;
    }
};

class Ext4Defragmenter final : public Defragmenter {
public:
    bool defragment(const fs::path& path, ProgressCallback callback) override {
        FileDescriptor file(open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW));
        if (file.get() < 0) return false;
        struct stat st{};
        if (fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) return false;
        fs::path donor_path = path.parent_path() / ("." + path.filename().string() + ".donor-XXXXXX");
        std::string pattern(donor_path.string());
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        FileDescriptor donor(mkstemp(writable.data()));
        if (donor.get() < 0) return false;
        const fs::path donor_name(writable.data());
        if (posix_fallocate(donor.get(), 0, st.st_size) != 0) {
            unlink(donor_name.c_str());
            return false;
        }
        move_extent request{};
        request.donor_fd = static_cast<__u32>(donor.get());
        request.len = static_cast<__u64>(st.st_size);
        const bool ok = ioctl(file.get(), EXT4_IOC_MOVE_EXT, &request) == 0;
        close(donor.release());
        unlink(donor_name.c_str());
        if (ok && callback) callback(100, 100, path.filename().string());
        return ok;
    }
};

class BtrfsDefragmenter final : public Defragmenter {
public:
    bool defragment(const fs::path& path, ProgressCallback callback) override {
        FileDescriptor file(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (file.get() < 0) return false;
        btrfs_ioctl_defrag_range_args args{};
        args.start = 0;
        args.len = UINT64_MAX;
        if (ioctl(file.get(), BTRFS_IOC_DEFRAG_RANGE, &args) != 0) return false;
        if (callback) callback(100, 100, path.filename().string());
        return true;
    }
};

class XfsDefragmenter final : public Defragmenter {
public:
    bool defragment(const fs::path& path, ProgressCallback callback) override {
        struct stat original{};
        if (lstat(path.c_str(), &original) != 0 || !S_ISREG(original.st_mode)) {
            std::cerr << "XFS defragmentation requires a regular, non-symlink file.\n";
            return false;
        }
        std::error_code ec;
        const fs::path absolute_path = fs::canonical(path, ec);
        if (ec) {
            std::cerr << "Cannot resolve XFS file path: " << ec.message() << '\n';
            return false;
        }
        struct stat canonical_stat{};
        if (stat(absolute_path.c_str(), &canonical_stat) != 0 || canonical_stat.st_dev != original.st_dev ||
            canonical_stat.st_ino != original.st_ino) {
            std::cerr << "XFS file path changed during validation.\n";
            return false;
        }

        posix_spawn_file_actions_t actions;
        int result = posix_spawn_file_actions_init(&actions);
        if (result != 0) {
            std::cerr << "Cannot prepare xfs_fsr invocation: " << std::strerror(result) << '\n';
            return false;
        }
        result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        if (result == 0) result = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        if (result != 0) {
            posix_spawn_file_actions_destroy(&actions);
            std::cerr << "Cannot configure xfs_fsr output: " << std::strerror(result) << '\n';
            return false;
        }

        std::string filename = absolute_path.string();
        char* arguments[] = {const_cast<char*>("xfs_fsr"), filename.data(), nullptr};
        pid_t child = -1;
        result = posix_spawnp(&child, "xfs_fsr", &actions, nullptr, arguments, environ);
        posix_spawn_file_actions_destroy(&actions);
        if (result != 0) {
            std::cerr << "Unable to start xfs_fsr (install xfsprogs): " << std::strerror(result) << '\n';
            return false;
        }

        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            std::cerr << "Unable to wait for xfs_fsr: " << std::strerror(errno) << '\n';
            return false;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::cerr << "xfs_fsr failed for " << path << '\n';
            return false;
        }
        if (callback) callback(100, 100, path.filename().string());
        return true;
    }
};

bool defragment_file(const fs::path& path, ProgressCallback callback) {
    const long fs_type = FilesystemDetector::get_fs_type(path);
    std::unique_ptr<Defragmenter> defrag;
    switch (fs_type) {
        case EXT4_SUPER_MAGIC: defrag = std::make_unique<Ext4Defragmenter>(); break;
        case BTRFS_SUPER_MAGIC: defrag = std::make_unique<BtrfsDefragmenter>(); break;
        case XFS_SUPER_MAGIC: defrag = std::make_unique<XfsDefragmenter>(); break;
        default: defrag = std::make_unique<GenericDefragmenter>(); break;
    }
    return defrag->defragment(path, callback);
}

long get_fs_type(const fs::path& path) { return FilesystemDetector::get_fs_type(path); }
std::string get_fs_name(long type) { return FilesystemDetector::get_fs_name(type); }
} // namespace DefragCore
