# Multi-platform port: plan and architecture

Goal: make this fork build and run on macOS and Windows as well as Linux, add crash reporting, and then replace the wxWidgets UI with a Slint UI. The upstream source zips are Linux-only (no `_WIN32`/`__APPLE__` code anywhere), so the platform layer has to be written here.

Status and decisions are tracked in [`PROGRESS.md`](../PROGRESS.md). This file is the design; update it when the design changes.

## Order of work

| # | Milestone | Ends with a PR when |
|---|---|---|
| M1 | Foundation: platform macros, `zen` unit tests, clang/libc++ portability, macOS dependency builds in CI | Linux still builds with g++; the same sources compile with clang + libc++ on Linux; `Tests/zen` passes; macOS CI builds the third-party libraries |
| M2 | Sentry crash reporting (Linux first, platform-neutral design) | A Linux build with `FFS_SENTRY=1` reports a test crash; builds without it contain no Sentry code |
| M3 | macOS port | `FreeFileSync.app` and `RealTimeSync.app` build in CI for arm64 and x86_64, `Tests/zen` and the headless sync test pass on macOS, DMGs are uploaded as artifacts |
| M4 | Windows port | `FreeFileSync.exe` and `RealTimeSync.exe` cross-build from Linux, tests pass under Wine and on a Windows runner, a zip is uploaded |
| M5 | Engine without wxWidgets | `base/`, `afs/`, `zen/` build as a library that does not link wx |
| M6+ | Slint UI (spike, main window, dialogs, platform gaps) | Planned in detail after M5 |

Each milestone is one pull request from `dev` to `main`. The coordinator watches it, fixes what CI and reviews turn up, squash-merges it once all checks pass, and continues with the next milestone. A milestone that grows too large is split at the step boundaries listed below, each split getting its own PR.

## Architecture

### Platform selection

- Three macros, one always defined: `ZEN_LINUX`, `ZEN_MAC`, `ZEN_WIN`. They are set in `zen/build_info.h` from the compiler's predefined macros (`__linux__`, `__APPLE__`, `_WIN32`), so no build flag is needed. These are the names upstream uses in its own (unpublished) platform code.
- No code outside `zen/`, `wx+/` and the few known spots in `FreeFileSync/Source` may test compiler macros directly.

### Where platform code goes

Upstream changes almost every file in every release, so the rule is to keep the diff against upstream small and regular:

- **Small differences** (a flag, a different call): `#if defined ZEN_LINUX / #elif defined ZEN_MAC / #elif defined ZEN_WIN` in place, with the existing Linux code untouched inside its branch.
- **Whole features that differ** (directory watching, recycle bin, icon loading, shutdown): the existing file keeps the Linux implementation, wrapped in one `#ifdef ZEN_LINUX`. New files hold the others: `zen/dir_watcher_mac.cpp`, `zen/recycler_mac.mm`, `zen/dir_watcher_win.cpp`, and so on. The header stays shared.
- **Objective-C++** only in `*_mac.mm` files, behind plain C++ headers. No Cocoa types in headers.
- **POSIX code shared by Linux and macOS** stays in the existing file; only Linux-only calls (`statfs` from `sys/vfs.h`, `copy_file_range`, `fallocate`, `posix_fadvise`, `pipe2`, `accept4`, `SOCK_CLOEXEC`, `MSG_NOSIGNAL`, `/proc`, `prctl`, SELinux, inotify, GIO/GLib) are branched.

### Platform feature map

| Feature | Linux (today) | macOS | Windows |
|---|---|---|---|
| File I/O, attributes, times | POSIX, `utimensat` | POSIX, `fcopyfile`/`clonefile`, `setattrlist` for creation time | Win32 `CreateFileW` with `\\?\` long paths |
| `Zstring` | UTF-8, case-sensitive | UTF-8, NFC/NFD-aware comparison, case-insensitive volumes | UTF-8 inside, UTF-16 at the API boundary |
| Permissions, extra data | mode, owner, SELinux, xattr | mode, owner, xattr, ACLs via `copyfile` | attributes; ACLs and alternate streams later |
| Symlinks | `readlink`/`symlink` | same | `CreateSymbolicLinkW`, junctions |
| Directory watching | inotify | FSEvents | `ReadDirectoryChangesW` |
| Recycle bin | GIO `g_file_trash` | `NSFileManager trashItemAtURL` | `IFileOperation` |
| Error text | `errno`, GLib errors | `errno`, `NSError`/`OSStatus` | `GetLastError`, `HRESULT` |
| System info, paths | `/proc`, XDG dirs, `getpwuid_r` | `sysctl`, `~/Library/Application Support/FreeFileSync`, app bundle `Resources` | registry/`SHGetKnownFolderPath`, `%APPDATA%` |
| Shutdown, sleep, open file | `systemctl`, `xdg-open` | `osascript`/IOKit, `open` | `ExitWindowsEx`, `ShellExecuteW` |
| File icons, thumbnails | GTK icon theme | `NSWorkspace` | `SHGetFileInfoW`/`IShellItemImageFactory` |
| UI toolkit (until Slint) | wxGTK 3 | wxOSX/Cocoa | wxMSW |
| Volume Shadow Copy | n/a | n/a | deferred, noted as missing |

### Build system

- The two Makefiles stay the entry points. They detect the host with `uname -s` and accept `TARGET_OS=linux|mac|win` and `ARCH=` for cross builds. Platform file lists and flags are separate blocks; the Linux block stays as upstream wrote it.
- Compilers: g++ 14+ on Linux (unchanged). macOS uses clang with libc++ (Apple clang if it accepts the code, otherwise Homebrew LLVM with its libc++ bundled into the app). Windows uses llvm-mingw (clang + libc++, UCRT) cross-compiling from Linux.
- GCC-only warning flags move into a GCC block; clang gets an equivalent set.
- Third-party libraries are built by the existing scripts in `.github/build/`, extended with a target parameter. Outputs stay under `.build/deps/<target>`.

### macOS builds in CI

Musikku cross-builds its JVM app for every OS from Linux with Conveyor and only wraps the DMG on a macOS runner. A C++ app cannot follow that fully: cross-compiling for macOS from Linux needs Apple's SDK, which may not be redistributed or downloaded outside Apple hardware. So:

- One macOS arm64 runner builds arm64 natively and cross-compiles x86_64 with `-arch x86_64` (same SDK), dependencies included.
- A second job, modelled on Musikku's `wrap-mac-dmg`, wraps each `.app` into a `.dmg` with `create-dmg` and uploads one artifact per architecture with `archive: false`.
- Apps are ad-hoc signed (`codesign -s -`). Notarization needs an Apple Developer ID and is left for later; the PR text explains how to open an unnotarized build.
- Windows keeps the Musikku shape: cross-built from Linux, tested under Wine, with a Windows runner job that runs the unit tests natively.

### Tests

- `Tests/zen/`: unit tests for the platform layer (create/copy/move/delete, symlinks, times, permissions, traversal, long and Unicode names, `Zstring` comparison, directory watcher events, recycle bin). No framework dependency: one small header with `CHECK` macros, the same style as `Tests/zip_afs`. They run on every platform in CI.
- `Tests/sync/`: a headless driver that links the engine, compares and synchronizes two temporary folder trees in mirror, two-way and update mode, and checks the result. It runs on every platform.
- UI smoke test in CI for packaged apps: start the app, check it stays up for a few seconds and exits cleanly, keep a screenshot as an artifact.

### Sentry

Same shape as Musikku's `crashlytics` / `crashlytics-empty` pair: one interface, a real implementation and an empty one, chosen at build time.

- `zen/crash_report.h`: `initCrashReporting(config)`, `addBreadcrumb()`, `captureError()`, `shutdownCrashReporting()`. `zen/crash_report_sentry.cpp` implements it with sentry-native (Crashpad backend, handler shipped next to the binary); `zen/crash_report_none.cpp` is empty.
- `make FFS_SENTRY=1 SENTRY_DSN=...` selects the real one. The default build, and any build without a DSN, links the empty one and contains no Sentry code. CI passes the DSN from the `SENTRY_DSN` repository secret, as Musikku does through `local.properties`.
- Privacy: only crashes and unexpected internal errors are sent, never sync logs. Paths, URLs, user and host names are scrubbed from report texts (`zen/crash_scrub.*`); minidumps still carry module paths and stack memory, which cannot be removed on the client.
- Consent: nothing is uploaded before the user has answered the first-start notice. Until then reports are kept; the answer (stored as `<CrashReports Enabled=.../>` in `GlobalSettings.xml`, written at once) sends or deletes them. Batch runs without a stored answer send nothing, running apps pick up a changed answer within seconds, and `FFS_NO_CRASH_REPORT=1` always turns reporting off.
- Release name `freefilesync@<ffsVersion>+<short sha>`. CI uploads debug symbols (ELF debug files, dSYM, PDB) with `sentry-cli` using `SENTRY_AUTH_TOKEN`, `SENTRY_ORG` and `SENTRY_PROJECT`.

## Steps

Each step is one coding task, followed by review, fixes, a commit and a push.

### M1 Foundation
1. `ZEN_LINUX/ZEN_MAC/ZEN_WIN` in `zen/build_info.h`; `Tests/zen` with a Linux baseline; CI job for it.
2. Clang + libc++ on Linux: Makefile compiler blocks, source fixes, CI compile job. This catches most macOS compiler errors where they can be reproduced locally.
3. `.github/build` scripts take a target; macOS workflow builds and caches OpenSSL, curl, libssh2 and wxWidgets for arm64 and x86_64.

### M2 Sentry
1. Interface, empty implementation, sentry-native build script, Makefile switch, init in both apps.
2. Path scrubbing, opt-out setting, first-start notice, test-crash switch for CI, symbol upload in the Linux workflow.

### M3 macOS
1. `zen` core (file access, I/O, traversal, paths, errors, system info, threads, processes, `Zstring`): `Tests/zen` green on macOS.
2. `zen` features: FSEvents watcher, recycle bin, shutdown, network bits.
3. `afs/native`, `base/` (icons, directory lock), `ffs_paths`: engine builds, `Tests/sync` green on macOS.
4. `wx+/` and `ui/` without GTK calls; app bundles for FreeFileSync and RealTimeSync.
5. DMG job, Sentry on macOS with dSYM upload, UI smoke test.

### M4 Windows
1. Toolchain and dependency cross builds (llvm-mingw, wxMSW).
2. `zen` core with UTF-16 boundary and long paths: tests under Wine and on a Windows runner.
3. `zen` features: watcher, recycle bin, shutdown, sockets.
4. `afs/native`, `base/`, paths, engine and `Tests/sync`.
5. `wx+/`, `ui/`, resources (icon, manifest, version info), zip package, Sentry with PDB upload.

### M5 Engine library
Move the wx types out of `config.h`, `localization.*` and `icon_buffer.*`; build `libffs_engine` without wx; point `Tests/sync` at it.

## Known risks

- **No Mac or Windows machine in the development container.** macOS code is verified only by CI runs, which makes each iteration slow. Step M1.2 exists to shrink that loop.
- **C++26 with libc++.** The code targets libstdc++; some library features may be missing in libc++ and need small fallbacks in `zen/legacy_compiler.h`.
- **Upstream merges.** Every platform branch added to an upstream file can conflict on the next release import. The file rules above keep that to the edges of each file.
- **Behavior upstream never published** (how the official Mac and Windows builds treat permissions, case sensitivity, locked files). Where it matters, behavior is checked against the official binaries and the manual, and recorded in `PROGRESS.md`.
