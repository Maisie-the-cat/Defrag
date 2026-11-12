# Universal Defrag

A Linux C++17 utility with an ncurses interface for processing files. It uses native filesystem interfaces where implemented and refuses some unsafe or unsupported replacements rather than risking user data.

## Implemented methods and limits

- **Ext4**: attempts online extent movement with the kernel `EXT4_IOC_MOVE_EXT` interface. It can fail when the filesystem lacks suitable donor space or permissions.
- **Btrfs**: requests file defragmentation with the kernel `BTRFS_IOC_DEFRAG_RANGE` interface.
- **XFS**: calls `xfs_fsr` from xfsprogs for the specified regular file. It does not run a whole-filesystem pass. `xfs_fsr` may report no improvement or fail if there is insufficient free space, quota, or access.
- **Other filesystems**: uses a best-effort same-directory copy-and-rename fallback for regular files with one link. That does not guarantee contiguous allocation or reduced fragmentation.
- The copy fallback refuses symlinks, non-regular files, and files with multiple hard links. It checks read/write/metadata errors, copies extended attributes, synchronizes the replacement, and atomically renames it only after completion. Filesystem-specific metadata that cannot be copied causes the operation to fail without replacing the source.
- The progress map is a UI visualization, not a physical disk map or fragmentation measurement.

Defragmentation may change file layout and should be used only with a current backup. Do not run this utility against raw devices or directories whose contents you do not intend to process.

## Requirements

- Linux
- C++17 compiler and GNU Make
- ncurses development headers and library (`libncurses-dev` on Debian/Ubuntu)
- xfsprogs installed to process XFS files (`xfs_fsr` must be on `PATH`)
- Permissions appropriate for the target file and filesystem operation

## Build and test

```bash
make
make test
```

The `make test` target runs focused tests for content preservation, the old temporary-name collision case, refusal to replace multiply-linked files, and safe XFS utility invocation.

## Usage

```bash
./defrag /path/to/file
./defrag /path/to/directory
```

The program scans directories recursively. Press `q` to leave the UI; an in-progress file operation is allowed to finish before the program exits.

## License

GNU General Public License v3; see [LICENSE](LICENSE).
