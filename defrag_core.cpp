#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/magic.h>

namespace fs = std::filesystem;

// Filesystem Magic Numbers (if not defined in linux/magic.h)
#ifndef BTRFS_SUPER_MAGIC
#define BTRFS_SUPER_MAGIC 0x9123683E
#endif
#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif
#ifndef JFS_SUPER_MAGIC
#define JFS_SUPER_MAGIC 0x3153464a
#endif
#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC 0x5346544e
#endif
#ifndef MSDOS_SUPER_MAGIC
#define MSDOS_SUPER_MAGIC 0x4d44
#endif

// Ext4 Defragmentation Structures
struct move_extent {
    uint32_t reserved;
    uint32_t donor_fd;
    uint64_t orig_start;
    uint64_t donor_start;
    uint64_t len;
    uint64_t moved_len;
};
#ifndef EXT4_IOC_MOVE_EXT
#define EXT4_IOC_MOVE_EXT _IOWR("f", 15, struct move_extent)
#endif

// Btrfs Defragmentation Structures
struct btrfs_ioctl_defrag_range_args {
    uint64_t start;
    uint64_t len;
    uint64_t flags;
    uint32_t extent_thresh;
    uint32_t compress_type;
    uint32_t unused[4];
};
#ifndef BTRFS_IOC_DEFRAG
#define BTRFS_IOC_DEFRAG _IOW("9", 2, struct btrfs_ioctl_defrag_range_args)
#endif

// XFS Defragmentation Structures (Simplified)
#ifndef XFS_IOC_SWAPEXT
#define XFS_IOC_SWAPEXT _IOWR("X", 109, void*)
#endif

namespace DefragCore {

    // Forward declaration of progress callback type
    using ProgressCallback = void (*)(int, int, const std::string&);

    class FilesystemDetector {
    public:
        static long get_fs_type(const fs::path& path) {
            struct statfs sfs;
            if (statfs(path.c_str(), &sfs) != 0) {
                return 0;
            }
            return sfs.f_type;
        }
        
        static std::string get_fs_name(long type) {
            switch (type) {
                case EXT4_SUPER_MAGIC: return "Ext2/3/4";
                case BTRFS_SUPER_MAGIC: return "Btrfs";
                case XFS_SUPER_MAGIC: return "XFS";
                case JFS_SUPER_MAGIC: return "JFS";
                case MSDOS_SUPER_MAGIC: return "FAT16/32";
                case NTFS_SB_MAGIC: return "NTFS";
                default: return "Unknown";
            }
        }
    };

    class Defragmenter {
    public:
        virtual ~Defragmenter() {}
        virtual bool defragment(const fs::path& path, ProgressCallback callback) = 0;
    };

    class GenericDefragmenter : public Defragmenter {
    public:
        bool defragment(const fs::path& path, ProgressCallback callback) override {
            std::cout << "Using generic defragmentation for: " << path << std::endl;
            
            try {
                fs::path temp_path = path;
                temp_path.replace_extension(".defrag_tmp");
                
                std::ifstream src(path, std::ios::binary);
                std::ofstream dst(temp_path, std::ios::binary);

                if (!src.is_open() || !dst.is_open()) {
                    std::cerr << "Failed to open files for generic defrag: " << std::strerror(errno) << std::endl;
                    return false;
                }

                src.seekg(0, std::ios::end);
                long long file_size = src.tellg();
                src.seekg(0, std::ios::beg);

                const int buffer_size = 4096;
                std::vector<char> buffer(buffer_size);
                long long bytes_copied = 0;

                while (src.read(buffer.data(), buffer_size)) {
                    dst.write(buffer.data(), buffer_size);
                    bytes_copied += buffer_size;
                    if (callback) {
                        callback(static_cast<int>((bytes_copied * 100) / file_size), 100, path.filename().string());
                    }
                }
                dst.write(buffer.data(), src.gcount());
                bytes_copied += src.gcount();
                if (callback) {
                    callback(100, 100, path.filename().string());
                }

                src.close();
                dst.close();
                
                // 2. Preserve metadata
                struct stat st;
                if (stat(path.c_str(), &st) == 0) {
                    chmod(temp_path.c_str(), st.st_mode);
                    chown(temp_path.c_str(), st.st_uid, st.st_gid);
                }
                
                // 3. Atomically rename the temp file to the original file
                fs::rename(temp_path, path);
                
                std::cout << "Successfully defragmented: " << path << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Error during generic defragmentation: " << e.what() << std::endl;
                return false;
            }
        }
    };

    class Ext4Defragmenter : public Defragmenter {
    public:
        bool defragment(const fs::path& path, ProgressCallback callback) override {
            std::cout << "Using Ext4 online defragmentation for: " << path << std::endl;
            
            int fd = open(path.c_str(), O_RDWR);
            if (fd < 0) {
                std::cerr << "Failed to open file: " << std::strerror(errno) << std::endl;
                return false;
            }
            
            // Create a donor file
            fs::path donor_path = path;
            donor_path.replace_extension(".defrag_donor");
            int donor_fd = open(donor_path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
            if (donor_fd < 0) {
                std::cerr << "Failed to create donor file: " << std::strerror(errno) << std::endl;
                close(fd);
                return false;
            }
            
            // Pre-allocate space for the donor file
            struct stat st;
            fstat(fd, &st);
            if (posix_fallocate(donor_fd, 0, st.st_size) != 0) {
                std::cerr << "Failed to pre-allocate donor file space" << std::endl;
                close(fd);
                close(donor_fd);
                unlink(donor_path.c_str());
                return false;
            }
            
            struct move_extent me;
            std::memset(&me, 0, sizeof(me));
            me.donor_fd = donor_fd;
            me.len = st.st_size;
            
            if (ioctl(fd, EXT4_IOC_MOVE_EXT, &me) < 0) {
                std::cerr << "Ext4 ioctl failed: " << std::strerror(errno) << ". Falling back to generic method." << std::endl;
                close(fd);
                close(donor_fd);
                unlink(donor_path.c_str());
                GenericDefragmenter generic;
                return generic.defragment(path, callback);
            }
            
            close(fd);
            close(donor_fd);
            unlink(donor_path.c_str());
            std::cout << "Successfully defragmented: " << path << std::endl;
            if (callback) {
                callback(100, 100, path.filename().string());
            }
            return true;
        }
    };

    class BtrfsDefragmenter : public Defragmenter {
    public:
        bool defragment(const fs::path& path, ProgressCallback callback) override {
            std::cout << "Using Btrfs online defragmentation for: " << path << std::endl;
            
            int fd = open(path.c_str(), O_RDWR);
            if (fd < 0) {
                std::cerr << "Failed to open file: " << std::strerror(errno) << std::endl;
                return false;
            }
            
            struct btrfs_ioctl_defrag_range_args args;
            std::memset(&args, 0, sizeof(args));
            args.start = 0;
            args.len = (uint64_t)-1; // Defragment the entire file
            
            if (ioctl(fd, BTRFS_IOC_DEFRAG, &args) < 0) {
                std::cerr << "Btrfs ioctl failed: " << std::strerror(errno) << std::endl;
                close(fd);
                return false;
            }
            
            close(fd);
            std::cout << "Successfully defragmented: " << path << std::endl;
            if (callback) {
                callback(100, 100, path.filename().string());
            }
            return true;
        }
    };

    class XfsDefragmenter : public Defragmenter {
    public:
        bool defragment(const fs::path& path, ProgressCallback callback) override {
            std::cout << "XFS defragmentation (using generic fallback for simplicity): " << path << std::endl;
            GenericDefragmenter generic;
            return generic.defragment(path, callback);
        }
    };

    // Main defragmentation function to be called by GUI
    bool defragment_file(const fs::path& path, ProgressCallback callback) {
        long fs_type = FilesystemDetector::get_fs_type(path);
        std::cout << "Processing " << path << " (Filesystem: " << FilesystemDetector::get_fs_name(fs_type) << ")" << std::endl;
        
        Defragmenter* defrag = nullptr;
        switch (fs_type) {
            case EXT4_SUPER_MAGIC: defrag = new Ext4Defragmenter(); break;
            case BTRFS_SUPER_MAGIC: defrag = new BtrfsDefragmenter(); break;
            case XFS_SUPER_MAGIC: defrag = new XfsDefragmenter(); break;
            default: defrag = new GenericDefragmenter(); break;
        }
        
        bool result = false;
        if (defrag) {
            result = defrag->defragment(path, callback);
            delete defrag;
        }
        return result;
    }

    // Expose filesystem detection functions
    long get_fs_type(const fs::path& path) {
        return FilesystemDetector::get_fs_type(path);
    }

    std::string get_fs_name(long type) {
        return FilesystemDetector::get_fs_name(type);
    }

} // namespace DefragCore
