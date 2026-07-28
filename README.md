# Defrag

Filesystem defragmentation tool for ext4 on Linux. This C++ program checks and defragments ext4 filesystems, assuming root access.

## Two Versions Available

- **ext4_defrag** - Command-line version for scripting and automation
- **ext4_defrag_gui** - MS-DOS style GUI version with visual disk representation

## Features

### CLI Version
- Fragmentation Analysis: Checks current fragmentation level of ext4 filesystems
- Automatic Defragmentation: Defragments when fragmentation exceeds threshold
- Multiple Methods: Uses e4defrag, online defrag ioctl, and file-by-file approaches
- SSD Optimization: Includes fstrim support for SSD devices

### GUI Version
- Retro MS-DOS Style Interface: Visual disk representation with colored blocks
- Real-time Progress: Animated progress bars and status messages
- Interactive Controls: Press any key to start, Q to cancel
- Visual Feedback: Shows fragmentation analysis and defragmentation in progress

## Requirements

- Linux system with ext4 filesystem
- Root access (sudo)
- Required packages: g++, e2fsprogs, util-linux, libncurses5-dev

## Building

```bash
cd defrag
make
```

This will compile both:
- ext4_defrag - Command-line version
- ext4_defrag_gui - GUI version (linked with ncurses)

## Usage

### CLI Version
```bash
sudo ./ext4_defrag /
sudo ./ext4_defrag / --check
sudo ./ext4_defrag /home -t 20
```

### GUI Version
```bash
sudo ./ext4_defrag_gui /
sudo ./ext4_defrag_gui /home
```

## License

GNU General Public License v3 - see LICENSE file for details.