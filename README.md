# Universal Defrag

A comprehensive filesystem defragmentation tool written in C++ for Linux. This program supports a wide range of filesystems by leveraging native kernel ioctls where available and falling back to a robust generic defragmentation strategy for others.

## Supported Filesystems

- **Ext2, Ext3, Ext4**: Uses `EXT4_IOC_MOVE_EXT` for online defragmentation.
- **Btrfs**: Uses `BTRFS_IOC_DEFRAG` for native online defragmentation.
- **XFS**: Uses native extent swapping strategies.
- **JFS, FAT16, FAT32, NTFS**: Uses a robust file-by-file contiguous reallocation strategy.

## Features

- **Multi-Filesystem Support**: Automatically detects the underlying filesystem and chooses the best defragmentation method.
- **Online Defragmentation**: For Ext4 and Btrfs, defragments files while the filesystem is mounted and in use.
- **Recursive Processing**: Can scan and defragment entire directories recursively.
- **Metadata Preservation**: Ensures file permissions, ownership, and timestamps are preserved during the defragmentation process.
- **Safety First**: Uses atomic operations (like `rename`) for generic defragmentation to prevent data loss.

## Requirements

- Linux system
- Root access (sudo) for most operations
- C++17 compatible compiler (e.g., g++)

## Building

```bash
cd Defrag
make
```

This will compile the universal defragmenter:
- **defrag** - Universal command-line defragmentation tool (with MS-DOS style GUI)

## Usage

```bash
# Run the GUI defragmenter for a specific file or directory
sudo ./defrag /home/user/large_file.iso
```
# To exit the GUI, press 'q'.
```

## MS-DOS Style GUI

The tool now features an interactive MS-DOS style graphical user interface built with ncurses. It provides a visual representation of the disk map, real-time progress updates, and a status area, mimicking the classic defragmentation programs of the MS-DOS era.

## License

GNU General Public License v3 - see LICENSE file for details.
