# Changelog

All notable changes to Universal Defrag are recorded here.

## v0.1.0-rc.1 — 2026-11-12

### Added

- Per-file XFS processing through `xfs_fsr` from xfsprogs. Whole-filesystem defragmentation is not supported.
- Extended-attribute regression coverage for empty, binary, long-name, multiple, and larger values, plus safe refusal of multiply-linked files.

### XFS verification

- Verified the published Linux x86_64 release archive inside a fresh Ubuntu 24.04.5 LTS virtual machine under QEMU TCG, booted from the official Ubuntu Noble cloud image. The guest verified the release archive's SHA-256 before extracting it.
- Created and mounted a fresh 512 MiB XFS filesystem in the guest, then ran the packaged application against `xfs file;literal.bin` on that filesystem.
- The UI reported `Completed`. An `execve` trace confirmed that the packaged application launched `/usr/sbin/xfs_fsr` with the filename passed as one argument.
- The 4,587,520-byte test file's SHA-256 remained `9d7d7352ad49d3bc64a990f90aaf3808bb99d23dd6a41800b765db931c31289b`, and its `user.defrag.release-check` extended attribute was preserved.

### Limitations

- The test file was not deliberately fragmented. This verifies XFS detection, utility invocation, successful completion, and preservation of file data and the tested xattr; it does not measure extent reduction, defragmentation effectiveness, or performance.
- XFS processing requires `xfsprogs` with `xfs_fsr` on `PATH`; the utility is not bundled.
