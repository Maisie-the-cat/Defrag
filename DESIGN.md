# Defragmentation Tool Design Document

## 1. Program Architecture

The defragmentation tool will be a command-line utility written in C++. It will follow a modular design to support various filesystems. The core components will include:

-   **`main` function**: Handles command-line argument parsing, initializes the defragmentation process, and dispatches tasks to appropriate filesystem handlers.
-   **`FilesystemDetector` class/module**: Responsible for identifying the filesystem type of a given path.
-   **`Defragmenter` base class/interface**: Defines a common interface for defragmentation operations.
-   **Filesystem-specific `Defragmenter` implementations**: Classes like `Ext4Defragmenter`, `BtrfsDefragmenter`, `XfsDefragmenter`, `JfsDefragmenter`, `GenericDefragmenter` (for FAT/NTFS/Ext2/3 fallback).
-   **`Logger` module**: For outputting status, warnings, and errors.

## 2. Command-Line Interface (CLI)

The program will accept the following arguments:

-   `<path>`: The target file or directory to defragment. If a directory, it will be processed recursively.
-   `-c`, `--check`: Perform a fragmentation analysis only, without defragmenting.
-   `-r`, `--recursive`: (Default for directories) Recursively process subdirectories.
-   `-v`, `--verbose`: Enable verbose output.
-   `-h`, `--help`: Display usage information.

Example usage:
```bash
sudo ./defrag /mnt/data
sudo ./defrag /home/user/large_file.iso --check
```

## 3. Filesystem Detection Logic

To determine the filesystem type, the program will use `statfs` or `fstatfs` system calls, which provide information about a mounted filesystem. The `f_type` field in the `struct statfs` will be used to identify the filesystem. Common `f_type` values include:

-   `EXT4_SUPER_MAGIC` (for Ext2/3/4)
-   `BTRFS_SUPER_MAGIC`
-   `XFS_SUPER_MAGIC`
-   `JFS_SUPER_MAGIC`
-   `MSDOS_SUPER_MAGIC` (for FAT16/32)
-   `NTFS_SB_MAGIC` (for NTFS, though this might require `libblkid` or similar for robust detection as `statfs` might report FUSE for `ntfs-3g` mounts).

If `statfs` is insufficient for NTFS or other types, `libblkid` or parsing `/etc/fstab` and `/proc/mounts` will be considered as alternatives.

## 4. Defragmentation Strategies per Filesystem

### Ext4 (and Ext2/3 fallback)
-   **Primary**: Use `EXT4_IOC_MOVE_EXT` ioctl. This requires finding a contiguous block of free space (a 
donor file) and moving extents. This is an online defragmentation method.
-   **Fallback**: For Ext2/3 or if `EXT4_IOC_MOVE_EXT` fails, use the generic file-by-file copy method.

### Btrfs
-   **Primary**: Use `BTRFS_IOC_DEFRAG` ioctl. This ioctl can defragment a file or a range within a file. It can also be applied recursively to a directory.

### XFS
-   **Primary**: Use `XFS_IOC_SWAPEXT` ioctl. This ioctl swaps extents between two files. Similar to Ext4, it requires a donor file or a mechanism to create contiguous space.

### JFS
-   **Primary**: Research suggests `JFS_IOC_DEFRAG` exists, but documentation is scarce. If a direct ioctl is not feasible or well-documented, the generic file-by-file copy method will be used.

### FAT16/32, NTFS, and other filesystems (Generic Fallback)
-   **Strategy**: For these filesystems, a generic defragmentation approach will be implemented:
    1.  **Read File**: Read the content of the fragmented file into memory or a temporary buffer.
    2.  **Create New File**: Create a new temporary file on the same filesystem, attempting to allocate contiguous blocks (e.g., using `fallocate` if supported by the underlying filesystem and kernel).
    3.  **Write Data**: Write the content from the buffer to the new temporary file.
    4.  **Replace Original**: Atomically replace the original fragmented file with the newly created contiguous file (e.g., using `rename`).
    5.  **Delete Original**: Delete the original fragmented file.

## 5. Error Handling and Logging

-   The tool will provide clear error messages for failed operations, including `errno` values where appropriate.
-   Logging levels (e.g., verbose, info, error) will be supported.
-   Permissions checks will be performed (e.g., root access for most defragmentation operations).

## 6. Build System (Makefile)

The `Makefile` will be updated to compile the C++ source code, link against necessary libraries (e.g., `libblkid` for robust filesystem detection, `libncurses` for GUI if implemented later), and create the executable.

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS = -lrt # For fallocate, if used

TARGET = defrag
SRCS = main.cpp FilesystemDetector.cpp Defragmenter.cpp Ext4Defragmenter.cpp BtrfsDefragmenter.cpp XfsDefragmenter.cpp JfsDefragmenter.cpp GenericDefragmenter.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
```

This design provides a roadmap for implementing a robust and multi-filesystem defragmentation tool.
