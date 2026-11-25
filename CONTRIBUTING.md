# Contributing

This repository mirrors the FreeFileSync source releases from [freefilesync.org](https://freefilesync.org). Upstream development happens elsewhere; changes here are limited to the mirror's own tooling and docs, plus any local work on top of the release snapshots.

## Branches

| Branch | Contents |
| --- | --- |
| `main` | All release commits, the update skill, the monthly update workflow (`.github/workflows/`), and this repository's own docs (`README.md`, `FAQ.md`, `MANUAL.md`, `CONTRIBUTING.md`, `CLAUDE.md`, images in `docs/images/`). |
| `upstream` | Release commits only, plus the update skill. New releases are imported here. |

Release commits (`FreeFileSync <version>`, authored by Zenju and dated to the release) are exact snapshots of the upstream source zips. Don't amend or rebase them, and don't commit local changes to `upstream`.

## Getting new releases

New releases are imported automatically by the [FreeFileSync upstream update](.github/workflows/ffs-upstream-update.yml) GitHub Actions workflow, which runs on the 1st of every month (06:17 UTC) and does what the skill below does: it imports any new releases into `upstream`, pushes it, and publishes the matching GitHub releases. It runs the scripts from `main` against a checkout of `upstream`. Start it by hand from the Actions tab or with `gh workflow run ffs-upstream-update.yml` (add `-f dry_run=true` to only list what it would do); each run's summary lists the imported versions, their release URLs and any files the website failed to deliver.

The same steps are available as the Claude Code skill in [.claude/skills/ffs-upstream-update](.claude/skills/ffs-upstream-update/SKILL.md). Ask Claude Code to "check for new FreeFileSync versions", or run the importer yourself on a clean `upstream` checkout:

```bash
git switch upstream
```

```bash
python3 -I .claude/skills/ffs-upstream-update/scripts/ffs_import.py --repo . --branch upstream --dry-run
```

Drop `--dry-run` to import, then push with `git push origin upstream`. Past releases come from the [archive page](https://freefilesync.org/archive.php). The current release is taken from the [download page](https://freefilesync.org/download.php), because the archive page lists it without a source link until the next release ships.

Each release also gets a GitHub release: tag `v<version>` on its release commit, the commit subject as title, the commit body as description, and the prebuilt files the website offers for that version (Windows installer, macOS and Linux packages) attached. The source zip isn't attached, since the release commit already contains it. After pushing, publish any missing releases with:

```bash
python3 -I .claude/skills/ffs-upstream-update/scripts/ffs_release.py --repo . --branch upstream --dry-run
```

Drop `--dry-run` to publish. It needs an authenticated `gh` CLI, skips releases that are already complete, and finishes partial or draft ones.

## Source layout

| Path | What it is |
| --- | --- |
| `FreeFileSync/Source/` | The FreeFileSync application: entry point, config, logging, localization |
| `FreeFileSync/Source/base/` | Core engine: folder comparison, sync algorithm, database files, filters, versioning, parallel scanning |
| `FreeFileSync/Source/afs/` | Abstract file system with local, FTP, SFTP, Google Drive and read-only ZIP archive back ends |
| `FreeFileSync/Source/ui/` | wxWidgets GUI: main dialog, grids, config dialogs, progress and batch handling |
| `FreeFileSync/Source/RealTimeSync/` | RealTimeSync, the companion tool that watches folders and triggers syncs |
| `FreeFileSync/Build/Resources/` | Runtime resources: icons, languages, sounds, GTK styles, CA certificates |
| `zen/` | Zenju's general-purpose C++ library: file I/O, paths, strings, threads, HTTP, crypto wrappers |
| `wx+/` | wxWidgets helpers: custom grid, graphs, dark mode, popup dialogs, image tools |
| `zenXml/` | Header-only XML library used for config files |
| `libcurl/`, `libssh2/` | Thin C++ wrappers around libcurl and libssh2 |
| `xBRZ/` | xBRZ image scaling, used for high-DPI icons |
| `Bugs.txt` | Patches upstream applies to third-party libraries (libcurl, wxWidgets, …) |
| `Changelog.txt`, `License.txt` | Upstream changelog and license terms |

## Building

The source ships Linux makefiles only (`FreeFileSync/Source/Makefile` and `FreeFileSync/Source/RealTimeSync/Makefile`); there are no Windows or macOS project files (on macOS, only the libraries and `zen/` build so far, see [macOS libraries and zen](#macos-libraries-and-zen)). The automated tests are the standalone ZIP test and the zen unit tests described at the end of this section. Upstream recommends building against current library versions with the patches from `Bugs.txt` applied, and that file names the versions it was written for (for 14.13: wxWidgets 3.3.4, libcurl 8.21.0, a libssh2 1.11.2 development snapshot).

### Build with the scripts in .github/build

The scripts in [.github/build](.github/build/README.md) download OpenSSL, the pinned libssh2 snapshot, libcurl and wxWidgets, apply all the `Bugs.txt` patches, and install them into `.build/deps` in the repository, without root and without touching the system. They then build FreeFileSync and RealTimeSync against those libraries with the glib cap described below, keeping their objects in `.build/obj`. `.github/build/package.sh` packages the result as a `.deb` and a portable tar.gz with those libraries bundled. [.github/workflows/linux-build.yml](.github/workflows/linux-build.yml) runs them in CI: packages for x86_64 and aarch64, plus a compile-only build against the distribution's OpenSSL and libcurl and an unpatched libssh2 and wxWidgets.

### Build in Docker

The `Dockerfile` at the repository root sets all of this up in an Ubuntu 26.04 image: GCC 15, libssh2 from the same development commit the CI builds use, libcurl 8.21.0 and wxWidgets 3.3.4 with the `Bugs.txt` `settings.h` patch and `--disable-exceptions`, downloads checked against pinned SHA-256 values, and the glib API cap described below. Build the image once, then compile the checked-out tree with it; by default it builds FreeFileSync and RealTimeSync:

```bash
docker build -t freefilesync-build .
```

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" freefilesync-build
```

Object files stay in the mounted tree, so later runs only rebuild what changed. To run something else, append a command, quoted so the container's shell expands `$(nproc)` (macOS has no `nproc`, and a bare `-j` runs unlimited jobs):

```bash
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" freefilesync-build sh -c 'make -C FreeFileSync/Source BUILD=debug -j"$(nproc)"'
```

libssh2 and libcurl are shared libraries inside the image, so the binaries run there, not necessarily on the host. The rest of this section describes the manual setup.

### Requirements

- A compiler that accepts `-std=c++26`: GCC 14 or newer (GCC 14 is the oldest that takes the flag), or clang 20 or newer (see [Building with clang and libc++](#building-with-clang-and-libc)). The makefiles detect the compiler from its predefined macros and pass GCC-only warning flags such as `-Wlogical-op` only to GCC.
- `pkg-config` and the development packages for GTK 3, OpenSSL, libcurl, libidn2 and zlib. libselinux is optional: the makefile enables SELinux support when `pkg-config` finds it.
- libssh2 built from a recent development commit, as below. The released 1.11.x versions that distributions package are not enough.
- wxWidgets 3.3 built for GTK 3 with `--disable-exceptions`, as below, with `wx-config` on the `PATH`.

On Debian or Ubuntu:

```bash
sudo apt install build-essential cmake pkgconf libgtk-3-dev libssl-dev libcurl4-openssl-dev libidn2-dev zlib1g-dev
```

Add `libselinux1-dev` for SELinux support. GTK 2 (`libgtk2.0-dev`) is not needed, and neither is the distribution's `libssh2-1-dev`.

### libssh2

`libssh2/libssh2_wrap.h` uses `LIBSSH2_ERROR_STORE_OVERFLOW`, which only libssh2's development branch has (`Bugs.txt` names `1.11.2_DEV`). `afs/sftp.cpp` also uses `MAX_SFTP_OUTGOING_SIZE` and `MAX_SFTP_READ_SIZE`, which libssh2 keeps in its private `src/sftp.h`; it falls back to libssh2's value of 30000 when the headers don't define them, so adding the two `#define` lines from `Bugs.txt` to `include/libssh2_sftp.h` is optional. Download a recent commit from [libssh2](https://github.com/libssh2/libssh2), then build and install it with OpenSSL:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCRYPTO_BACKEND=OpenSSL -DENABLE_ZLIB_COMPRESSION=ON -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF
```

```bash
cmake --build build -j"$(nproc)" && sudo cmake --install build && sudo ldconfig
```

`pkg-config --modversion libssh2` should then report the development version. `.github/build/build-libssh2.sh` pins the commit the CI builds use.

### wxWidgets

Distributions mostly package wxWidgets 3.2, which is older than upstream supports, so build wxWidgets from source. Download the release from [wxWidgets releases](https://github.com/wxWidgets/wxWidgets/releases) (the `wxWidgets-<version>.tar.bz2` archive already includes the bundled third-party libraries that a plain git clone needs submodules for), then unpack it:

```bash
tar xf wxWidgets-<version>.tar.bz2 && cd wxWidgets-<version>
```

Apply the wxWidgets patches from `Bugs.txt` to that tree, then configure and build in a subfolder:

```bash
mkdir buildgtk && cd buildgtk
```

```bash
../configure --with-gtk=3 --disable-exceptions
```

```bash
make -j"$(nproc)" && sudo make install && sudo ldconfig
```

`wx-config --version` should then report the version you built, and `wx-config --selected-config` should start with `gtk3`. `--disable-exceptions` is required: `application.cpp` stops the build with an `#error` if wxWidgets was built with its uncaught-exception handling (`wxUSE_EXCEPTIONS`). Don't use `--enable-no_exceptions` instead. It also turns that handling off, but it puts `-fno-exceptions` into `wx-config --cxxflags`, and FreeFileSync reports errors through C++ exceptions. See [wxWidgets' GTK install notes](https://github.com/wxWidgets/wxWidgets/blob/master/docs/gtk/install.md) for other options.

Of the wxWidgets patches, only the `include/wx/settings.h` color hook (`wxColorHook`, used by `wx+/darkmode.cpp`) is needed to compile; the others change runtime behavior only. Without it, the makefiles print "wxWidgets lacks the wxColorHook patch from Bugs.txt" and build anyway with `-DZEN_WX_NO_COLOR_HOOK`, so dark mode loses the higher-contrast gray text.

### FreeFileSync and RealTimeSync

Check the glib version first with `pkg-config --modversion glib-2.0`. From glib 2.78 (Ubuntu 24.04, Debian 13, Fedora 40 and later), `g_free` can be a function-like macro, and the `::g_free(...)` calls in `afs/ftp.cpp`, `base/icon_loader.cpp` and `zen/zstring.cpp` don't compile. So cap the glib API at 2.76 through the environment, which the makefiles' `CXXFLAGS +=` appends to:

```bash
export CXXFLAGS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_76"
```

Skip that line only if glib is older than 2.78; glib older than 2.76 doesn't define `GLIB_VERSION_2_76`. Then build both programs in the same shell:

```bash
make -C FreeFileSync/Source -j"$(nproc)"
```

```bash
make -C FreeFileSync/Source/RealTimeSync -j"$(nproc)"
```

The binaries are written to `FreeFileSync/Build/Bin/FreeFileSync_<arch>` and `FreeFileSync/Build/Bin/RealTimeSync_<arch>`, and object files go to `FreeFileSync/Build/Obj/<exe>` in the checkout, not to the shared temp directory, so separate checkouts never mix their objects (`make clean` removes both).

For a debug build of FreeFileSync, run `make -C FreeFileSync/Source BUILD=debug -j"$(nproc)"` (or `make -C FreeFileSync/Source debug`). It compiles with `-g3 -O0` (keeping `NDEBUG`, so `assert()` stays off) into `FreeFileSync/Build/Debug/Bin/`, keeps its objects in `FreeFileSync/Build/Debug/Obj/<exe>`, and copies `Build/Resources` next to it on every run; `make clean BUILD=debug` removes only the debug build. Objects are compiled with `-MMD -MP`, so editing a header rebuilds the files that include it. RealTimeSync's makefile has no debug mode.

To use a compiler other than the default `g++`, set `CXX` in the environment the same way, for example `export CXX=g++-14` before running make. Don't pass it as a make argument (`make CXX=g++-14`): a command-line variable overrides the makefiles' `CXX += -pthread`.

### Building with clang and libc++

The macOS and Windows ports are built with clang and LLVM's libc++, so the sources must also compile with them; on Linux this is checked with clang 20 (Ubuntu 24.04: `sudo apt-get install clang-20 libc++-20-dev libc++abi-20-dev`). The makefiles detect the compiler from its predefined macros (`$(CXX) -dM -E`): for GCC they pass the GCC-only warning flags as before, for clang `-Wtautological-overlap-compare` instead (neither set for an unknown compiler), plus `-Wno-potentially-evaluated-expression`, `-Wno-unused-const-variable` and `-Wno-unused-lambda-capture` for three clang-only style warnings on valid upstream code (`typeid` operands evaluated on purpose, constants of features this edition doesn't compile in, captures of constants). `STDLIB=libc++` adds `-stdlib=libc++` to compiling and linking. A toolchain other than g++ with libstdc++ gets its own binary and object folder names, e.g. `FreeFileSync_x86_64-clang-libc++` and `Build/Obj/FreeFileSync_x86_64-clang-libc++`, so one checkout can switch between toolchains without mixing objects or keeping a stale binary:

```bash
export CXX=clang++-20
make -C FreeFileSync/Source -j"$(nproc)" STDLIB=libc++
make -C FreeFileSync/Source/RealTimeSync -j"$(nproc)" STDLIB=libc++
```

wxWidgets is C++, so it has to be built against the same C++ library: with the scripts in `.github/build`, `CC=clang-20 CXX=clang++-20 CXXFLAGS=-stdlib=libc++ LDFLAGS=-stdlib=libc++ .github/build/build-wxwidgets.sh`, then `CXX=clang++-20 STDLIB=libc++ .github/build/build-free-file-sync.sh` (use a separate `FFS_BUILD_DIR` if you also build with g++; OpenSSL, libssh2 and libcurl are C libraries and work with either). The `clang + libc++` job of the Linux build workflow does exactly this, links both apps, checks that they use libc++ and starts each one with `.github/build/check-app-starts.sh <binary> <title regex>` (needs `xvfb` and `xdotool`), which fails unless the main window shows up without an error popup or error output. Both test scripts take the same `CXX` and `STDLIB` variables (`CXX=clang++-20 STDLIB=libc++ bash Tests/zen/run.sh`), and both test workflows run a clang + libc++ entry next to the g++ one.

### macOS libraries, zen and the engine

The libraries FreeFileSync needs, the platform layer `zen/`, the engine library and (see [macOS apps](#macos-apps)) the two apps build on macOS: `FFS_TARGET=macos-arm64 .github/build/build.sh` (or `FFS_TARGET=macos-x86_64`) builds static OpenSSL, libssh2, libidn2, libcurl and wxWidgets (wxOSX/Cocoa) for one architecture and macOS 13.3 or later into `.build/deps/<target>`, with Xcode's clang; x86_64 is cross-compiled on Apple silicon. It needs `brew install bash coreutils flock pkgconf`. `bash Tests/zen/run.sh` builds and runs the zen unit tests with Xcode's clang (see [zen unit tests](#zen-unit-tests)), `.github/build/macos-report.sh zen-compile <arch> .build/deps/macos-<arch>` compiles every zen source against the libraries, and `gmake -C FreeFileSync/Source engine` (add `ARCH=x86_64` for the other architecture) builds the [engine library](#the-engine-library) against them; `bash Tests/sync/run.sh` runs the [sync tests](#sync-tests) on it. Details, the toolchain findings and the CI workflow (`macos-build.yml`) are in [.github/build/README.md](.github/build/README.md#macos).

macOS code follows [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md#where-platform-code-goes): small differences are `#if defined ZEN_LINUX / #elif defined ZEN_MAC` branches in place, with the Linux code unchanged inside its branch; whole features live in `zen/*_mac.cpp` (C APIs: FSEvents, CoreFoundation) and `zen/*_mac.mm` (Objective-C++, compiled with `-fobjc-arc`), behind the shared C++ headers, while `dir_watcher.cpp` and `recycler.cpp` keep the Linux implementation inside `#if defined ZEN_LINUX`. Behavior that differs from Linux on purpose: paths compare case- and normalization-insensitively (`compareNativePath`, the APFS default, independent of the actual volume), `/Volumes/<name>` is a volume root, case conversion and normalization use CoreFoundation behind the same GLib-style calls as on Linux (simple 1:1 case mapping, so "ß" stays "ß" like on Linux), file and folder copies take extended attributes along on a best-effort basis and only to volumes that store them natively (`fcopyfile`; FAT, exFAT and many network shares would get AppleDouble `._` files instead; an attribute the target can't take never fails the copy, unexpected errors go to the sync log through `logExtraError()`), permission copies also copy the ACL (`acl_set_file`, before owner and mode: the source's explicit entries followed by the target's inherited ones, nothing written when neither has explicit entries, skipped on volumes without ACLs), symlinks get their own mode (`lchmod`), and descriptors created without an atomic close-on-exec flag (`pipe`, `socket`) are guarded by `lockFdInheritance()` (`zen/process_exec.h`), the directory watcher also covers subfolders created later, reports `create` once per item and `update` afterwards (FSEvents accumulates flags), and reports a removed, renamed or unmounted watched folder as `baseFolderUnavailable` (also through an existence check in `fetchChanges()` at most once per second, because FSEvents doesn't report every unmount), and the configuration folder is `~/Library/Application Support`.

The makefiles choose the platform in `FreeFileSync/Source/platform.mk`, which both include: `TARGET_OS` is `linux` or `mac` (default: the build host's, from `uname -s`; cross builds between them are not possible, macOS needs Apple's SDK), and on macOS `ARCH` is `arm64` or `x86_64` (default: the host's), the deployment target comes from `FFS_MACOS_DEPLOYMENT_TARGET` in `.github/build/common.sh`, and `wx-config` and `pkg-config` look only into `FFS_DEPS_PREFIX` (default `.build/deps/macos-<arch>`), never at Homebrew. The Linux block of each makefile is upstream's, unchanged; the macOS block links the static libraries from the prefix, zlib and the frameworks from the SDK, replaces the Linux-only sources by their `*_mac.*` counterparts (`zen/recycler_mac.mm`, `zen/dir_watcher_mac.cpp`, `zen/sys_error_mac.mm`, `base/icon_loader_mac.cpp` and `.mm`) and compiles `.mm` files with `-fobjc-arc`. The macOS 26 SDK's CoreGraphics headers (included by AppKit) combine enumerators of different enumeration types, which C++26 makes ill-formed (P2864), so a file that includes AppKit is compiled as C++23 (`base/icon_loader_mac.mm`) behind a header with standard types only (`base/icon_loader_mac.h`), and the C++26 code calls it through that header; the `zen/*_mac.mm` files include only Foundation and compile as C++26. macOS's own `make` is 3.81, too old for the makefiles: use Homebrew's GNU make (`brew install make`, run as `gmake`). Two quirks of the prefix are handled in `platform.mk`: the SDK's zlib has no `.pc` file, which the prefix's `libcurl.pc` and `libssh2.pc` require, so `.github/build/pkgconfig-macos/zlib.pc` stands in for it (a failing `pkg-config` query stops the build); and the static wxWidgets libraries are installed with a `-<host>` suffix (`libwx_baseu-3.3-aarch64-apple-darwin.a`, from `--host`) that `wx-config --libs` leaves out when configure could run its test programs (always, here), so `macWxLibs` maps names that don't exist to the installed files. The macOS code in the engine: `afs/native.cpp` and `afs/zip.cpp` read `st_mtimespec`; `afs/ftp.cpp` converts Latin-1 server names without glib (failing like `g_convert()` for characters outside Latin-1); `afs/gdrive.cpp` sets `FD_CLOEXEC` under `lockFdInheritance()` and `SO_NOSIGPIPE` on its OAuth sockets; `base/icon_loader_mac.mm` gets file and folder icons from `NSWorkspace` and thumbnails from ImageIO (image files only, like GTK's pixbuf loader), rendered on the worker thread, and `base/icon_loader_mac.cpp` puts them into the `FileIconHolder` of `wx+/image_holder.h`; `ffs_paths.cpp` finds `Contents/Resources` in the app bundle the same way the Linux build finds `Resources` next to `Bin` (and so works unbundled from `Build/Bin` too); the default external commands are `open -R` (show in Finder) and `open`. The engine has no slot for file creation times (the AFS interface carries modification times only), so they are not copied.

When something compiles with libstdc++ but not with libc++, the usual causes are a missing `#include` that libstdc++ happened to provide transitively, and integer types that differ between the libraries (`std::chrono` durations use `long long` in libc++ and `long` in libstdc++). Fix the source in standard form; for a library feature libc++ lacks, add a fallback to `zen/legacy_compiler.h` keyed on its `__cpp_lib_*` macro rather than on the compiler name.

### Crash reporting

The apps report crashes with [sentry-native](https://github.com/getsentry/sentry-native) when built with `FFS_SENTRY=1`; the default build compiles the empty implementation and contains no reporting code (same shape as Musikku's `crashlytics`/`crashlytics-empty` modules). The interface is `zen/crash_report.h`, implemented by `zen/crash_report_sentry.cpp` and `zen/crash_report_none.cpp`; `FreeFileSync/Source/crash_report_init.h` starts it in both apps, and `zen/crash_scrub.*` removes paths, user and host name from report texts. What is sent and how users turn it off is in the [README](README.md#crash-reporting).

```bash
CC=gcc-14 CXX=g++-14 FFS_SENTRY=1 .github/build/build.sh       # the libraries plus sentry-native (into .build/sentry)
export FFS_SENTRY=1 SENTRY_DSN='https://<key>@<org>.ingest.sentry.io/<project>' SENTRY_ENVIRONMENT=dev
CXX=g++-14 .github/build/build-free-file-sync.sh                 # also copies crashpad_handler to FreeFileSync/Build/Bin
FFS_SENTRY=1 .github/build/package.sh                            # ships crashpad_handler, splits the debug info off
```

- `SENTRY_DSN` is required with `FFS_SENTRY=1` and must come from the environment (or a make variable); it is compiled in through a header generated into the object folder (`FreeFileSync/Source/crash_report_header.sh`), so it doesn't show up in make's command echo. Never commit a DSN. `SENTRY_ENVIRONMENT` is `dev` (default), `ci` or `release`; `FFS_GIT_SHA` (default: the short hash of `HEAD`) goes into the release name `freefilesync@<version>+<sha>`.
- With plain `make`: `PKG_CONFIG_PATH=$PWD/.build/sentry/lib/pkgconfig make -C FreeFileSync/Source FFS_SENTRY=1` with `SENTRY_DSN` in the environment. On macOS `gmake -C FreeFileSync/Source FFS_SENTRY=1` finds sentry-native in `.build/sentry/macos-<arch>` by itself (`FFS_SENTRY_PREFIX` for another prefix) and puts `crashpad_handler` into each bundle's `Contents/MacOS`, signed before the bundle. Sentry builds keep the binary names but use their own object folders (`…-sentry`), add `-g`, and copy `crashpad_handler` next to the binaries; the apps look for it there and start without reporting if it is missing.
- `package.sh` with `FFS_SENTRY=1` moves the debug info out of the shipped binaries into `.build/debug-files` (`<exe>.debug` plus the shipped `<exe>`, checked to have the same build ID) for `sentry-cli debug-files upload`.
- `.github/build/build-sentry-native.sh` builds the pinned sentry-native (Crashpad backend, curl transport, static) with the apps' compiler and C++ library, also for `FFS_TARGET=macos-arm64|macos-x86_64`.
- Consent: the setting records the user's consent, not a preference: it changes only through the first-start notice or the options checkbox (unticked while there is no answer), and FreeFileSync writes it to `GlobalSettings.xml` (`<CrashReports Enabled="…"/>`) at once. Nothing is uploaded before there is an answer; until then reports are kept in the report database (sentry-native's `cache/`, Crashpad's `pending/`), sent after "send", deleted after "don't send". With "don't send", a crash leaves no dump. All processes of an app share `~/.config/FreeFileSync/CrashReports/<app>` (macOS: `~/Library/Application Support/FreeFileSync/CrashReports/<app>`): each applies the stored answer when it starts and follows changes while it runs (a few seconds' poll of the file); a process without an answer of its own (e.g. another `GlobalSettings.xml`) leaves the shared state alone and sends nothing of its own.
- For debugging, `FFS_CRASH_REPORT_DEBUG=1` turns on sentry-native's log on stderr, and `FFS_TEST_CRASH=segv|abort|internal-error` makes a Sentry build fail on purpose once its settings are read (not in builds with `SENTRY_ENVIRONMENT=release`, and only while reporting is on).

**CI and secrets.** The Packages jobs of the Linux build workflow and the app builds of the macOS build workflow build with crash reporting only when the repository secret `SENTRY_DSN` is set, and upload the debug files when `SENTRY_AUTH_TOKEN`, `SENTRY_ORG` and `SENTRY_PROJECT` are set too (a pinned `sentry-cli`, `.github/build/fetch-sentry-cli.sh`; on macOS one dSYM per executable made with `dsymutil`, after which the executables are stripped of their debug map with `strip -S` and signed again; the dSYM's UUID must equal the shipped binary's, `dwarfdump --uuid`, and `sentry-cli debug-files check` must find both usable). Started by hand with the input `sentry_dry_run`, the macOS build workflow takes the same path with a fake DSN and without upload; its artifacts are named `sentry-dry-run-…-NOT-FOR-RELEASE` and get no DMG. Without the secrets (forks, pull requests from forks) they build exactly as before and say so in the job summary. The secrets reach only the steps that need them, through their environment.

**Test.** `Tests/sentry/run.sh [<bin folder>]` runs a Sentry build against a fake Sentry endpoint on 127.0.0.1 (`Tests/sentry/fake_sentry.py`, Python standard library only): a crash of each app arrives as a minidump upload with the right release and environment and without user name, host name or home folder; an internal error arrives with its paths scrubbed; with the setting off a real crash sends nothing, and with `FFS_NO_CRASH_REPORT=1` there is no reporter at all; reports made before the notice is answered are kept, then sent or deleted according to the answer; a running RealTimeSync follows a changed answer; an answer given in a running FreeFileSync (keyboard-driven with `xdotool`) reaches a second process at once; OK in the options dialog without an answer stores none; without `crashpad_handler` the app starts normally. `SCREENSHOT_DIR` keeps screenshots of the notice and the options dialog (needs ImageMagick). Build for it with the DSN it expects, `SENTRY_DSN=http://testkey@127.0.0.1:38917/1 SENTRY_ENVIRONMENT=ci` (or set `SENTRY_TEST_DSN`), and install `python3`, `xvfb` and `xdotool`. `Tests/sentry/run.sh --default-build <binary>...` checks that a default build contains no sentry code and no DSN. On macOS it takes `Build/Bin/macos-<arch>` with the two bundles, starts the apps without Xvfb and uses `check-mac-app-starts.sh` for the start checks (for the missing handler: a copy of the bundle without it, signed again); the two scenarios driven by `xdotool` (two processes, options dialog) are skipped there with a note in the log. On macOS sentry-native's Crashpad backend has no first-chance handler, so `on_crash` never runs and a crash with reporting turned off is not discarded in the process: the app dies by the signal and Crashpad writes a dump. `crash_report_sentry.cpp` makes sure such a dump can never be uploaded: a revoked period is marked in the report database (`revoked-period`, written when the answer becomes "don't send" and at every start with it); with "don't send" all reports (sentry's `cache`, Crashpad's `new`/`pending`/`completed`) are deleted at start and when the answer changes; when "send" is given while the mark exists, all reports are deleted first and the mark only after a complete deletion, then uploads are enabled; a process started with "don't send" runs without `http_retry`, so Crashpad's uploads are disabled rather than paused. "No answer yet" keeps its reports until the answer, as on Linux. All processes share one stored answer, so they see the same transitions; a second deletion is harmless, and a process whose deletion fails leaves the mark for the next one. The test checks this on macOS with the exit code of the signal, "nothing kept" after the next start with "don't send", and the scenario `c-revoked-then-send` (crash while off, then "send": nothing arrives, nothing is left). Crashpad catches no crashes of x86_64 apps under Rosetta 2, so CI skips the scenarios that need a captured crash for x86_64 on the arm64 runner (`SENTRY_TEST_NO_CRASH_CAPTURE=<reason>`, printed) and runs them on an Intel runner (`Sentry tests (Intel)`, `macos-15-intel`) when the workflow is started by hand. The `Sentry build` job of the Linux build workflow runs the test on the build and on its package, the `Sentry build`/`Sentry tests` steps of the macOS build workflow on both architectures (x86_64 under Rosetta 2); the `clang + libc++` job, and the Packages jobs when they build without the secret, run the `--default-build` check. The scrubber has unit tests in `Tests/zen` (`crashScrub*`).

### macOS apps

With the libraries of the architecture in `.build/deps/macos-<arch>` (see above) and GNU make 4 (`brew install make`):

```bash
gmake -C FreeFileSync/Source -j"$(sysctl -n hw.ncpu)"                # FreeFileSync, ARCH=x86_64 for the other architecture
gmake -C FreeFileSync/Source/RealTimeSync -j"$(sysctl -n hw.ncpu)"
open FreeFileSync/Build/Bin/macos-arm64/FreeFileSync.app
```

Each makefile links its app (`Build/Bin/FreeFileSync_<arch>`, `RealTimeSync_<arch>`) and then builds its bundle in `Build/Bin/macos-<arch>/` with `FreeFileSync/Source/mac_bundle.sh`: the two apps sit side by side, as in the official Mac package, and RealTimeSync finds `FreeFileSync.app` next to it (`getFreeFileSyncLauncherPath()`). The bundle gets the executable in `Contents/MacOS`, the `Resources` folder in `Contents/Resources` (plus the welcome dialog's picture `Animal.dat` from `.github/build/resources`, which the source zip lacks), an `.icns` made from the 128-pixel PNG in `Icons.zip` with `sips` and `iconutil`, and an `Info.plist` with the bundle identifier (`io.github.cuong-tran.freefilesync`, `io.github.cuong-tran.realtimesync`: this is a fork, so not upstream's), the version from `version/version.h`, `LSMinimumSystemVersion` from the deployment target, the document types (`.ffs_gui` and `.ffs_batch` owned by FreeFileSync, `.ffs_real` owned by RealTimeSync and `.ffs_batch` as its alternative, opened from Finder through `wxApp::MacOpenFiles`), `NSHighResolutionCapable` and the usage descriptions macOS shows when the app asks for access (Apple Events for shutting down via System Events; Desktop, Documents, Downloads, removable and network volumes). It is signed ad hoc (`codesign -s -`; inner executables first, then the bundle, no `--deep`, without hardened runtime) and not notarized. A Sentry build (`FFS_SENTRY=1`) puts `crashpad_handler` into `Contents/MacOS`, where the apps look for it (`mac_bundle.sh` takes such helpers as trailing arguments, one each).

The UI code builds against wxOSX/Cocoa: GTK calls in `wx+/`, `ui/`, `application.cpp` and RealTimeSync are in `ZEN_LINUX` branches, with the macOS equivalent beside them (scroll bars through `ShowScrollbars()`, the DPI scale from the content scale factor, no GTK CSS). About, Preferences (Cmd+,) and Quit sit in the application menu as on any Mac app: wxOSX moves the `wxID_ABOUT`/`wxID_PREFERENCES`/`wxID_EXIT` items there and sends their events to the frame, where the apps bind them. Dark mode follows the system appearance (`wxSystemSettings::GetAppearance()`). While a modal dialog is open (or the window is disabled), About, Preferences and Quit from the application menu only beep, so that Quit cannot destroy the window underneath the dialog. Files opened from Finder while the app runs (macOS keeps one process per bundle) get the same checks as command line arguments: a `.ffs_batch` starts a new process of the app's own executable, which runs the job as a batch job, as on Linux; a `.ffs_gui` loads into the main window, queued while a dialog is open or a comparison/synchronization runs (the latest request wins); errors are shown, nothing is dropped silently. Bitmap buttons whose text is part of the image (`zen::BitmapTextButton`, `zen::ToggleButton`) hide their label from `SetFont()` on macOS: wxOSX would otherwise set it as the native button title despite `wxBU_NOTEXT` and draw the text twice. The online update check is off in the macOS builds (automatic and "Check for updates now", which says where new builds are published instead): upstream's update server knows neither the platform nor this fork's builds; Linux keeps upstream's update check unchanged. Progress of a comparison or synchronization shows as a bar on the Dock icon (`wx+/taskbar_mac.cpp` on top of `wx+/dock_tile_mac.mm`, an `NSDockTile` content view; color by status: blue, gray when indeterminate, yellow, red, orange when paused).

CI (`macos-build.yml`, per architecture): builds both bundles, checks the signature (`codesign --verify --strict`), the architecture (`lipo`), the minimum macOS version (`otool -l`) and that the executables link only system libraries and frameworks (`otool -L`); uploads `FreeFileSync-<version>-macos-<arch>.zip` (made with `ditto -c -k --keepParent`, which keeps the signatures); starts each app with a fresh `HOME` (`.github/build/check-mac-app-starts.sh <bundle> <title regex> <screenshot> [<argument>...]`: waits for a window of the process with the expected title through `CGWindowListCopyWindowInfo`, saves a screenshot as artifact `screenshots-macos-<arch>` and a small copy in the log, ends it with SIGTERM, and fails if the app ends by itself, a window is titled like an error or warning, its output reports an error, or it crashed: crash signal or crash report); FreeFileSync starts twice, once fresh (About dialog over the main window) and once with a configuration of two folders and global settings without first-start dialogs written by `sync_test --write-gui` and `--write-global`, so the main window shows a comparison result; and runs FreeFileSync in batch mode on a mirror job written by `sync_test --write-batch` (exit code 0, right folder equal to the left one). x86_64 runs under Rosetta 2. The `DMG` job then wraps each architecture's zip into `FreeFileSync-<version>-macos-<arch>.dmg` with `.github/build/wrap-mac-dmg.sh <folder with both apps> <output.dmg>` (also usable on a Mac by hand; `brew install create-dmg`): both apps and an *Applications* link, volume name `FreeFileSync <version>`, the app icon as volume icon, no background picture. It removes AppleDouble (`._*`) files and Finder metadata first (they would break `codesign --strict`), stops with a message if a volume named like the image is mounted (it never detaches one it did not mount), lays out the window with `create-dmg`, falls back to `create-dmg --skip-jenkins` (no Finder scripting) and then to plain `hdiutil create -format UDZO`, prints which was used (also in the job summary), and finally mounts the image read-only, lists it and verifies both apps' signatures in it.

### The engine library

`make -C FreeFileSync/Source engine` builds `libffs_engine.a` in the object folder (`Build/Obj/<exe name>/libffs_engine.a`, or `<objPath>/libffs_engine.a` with `objPath=` on the make command line) from the same objects as the app: `afs/`, `base/`, the non-UI files of `FreeFileSync/Source` (`base_tools`, `config`, `ffs_paths`, `icon_buffer`, `localization`, `log_file`, `status_handler`), `libcurl/curl_wrap.cpp` and the `zen/` sources the app uses, but not `application.cpp`, `ui/`, `wx+/`, `xBRZ/` or a crash reporting implementation (the program that links it picks `zen/crash_report_none.cpp` or the Sentry one). `make engine-flags` prints the compiler, compiler flags, linker flags and library path a program linking it needs, one `NAME=value` line each (`Tests/sync/run.sh` uses it). It builds on Linux (g++ and clang + libc++) and macOS (arm64, x86_64). A headless program needs no wxWidgets initialization: `fff::initAfs()` before and `fff::teardownAfs()` after `compare()`/`synchronize()` is all, and without `localizationInit()` the texts stay English.

The engine still needs wxWidgets, which M5 of [docs/PORTING_PLAN.md](docs/PORTING_PLAN.md) removes. Compiled against wx headers (and so against `wx-config --cxxflags`): `config.cpp`/`config.h` (`wxSize`, `wxLanguage`, dark mode and grid settings from `wx+/` and `ui/` headers), `localization.cpp`/`.h` (`wxLanguage`, `wxTranslations`, `wxUILocale`, the `.lng` files read through `wxZipInputStream`), `icon_buffer.cpp`/`.h` (`wxImage`, `wx+/dc.h`, `wx+/image_resources.h`), `base_tools.cpp` (through `config.h`), `base/icon_loader.*` (`wxImage` in `extractWxImage()`) and `afs/native.cpp` (through `base/icon_loader.h`). Of these, the objects that reference wx (or GTK) symbols at link time are `config`, `localization`, `icon_buffer`, `base_tools` and `base/icon_loader` (Linux: also GTK/gdk-pixbuf), so a program linking the engine links wxWidgets too (`wx-config --libs`, as `make engine-flags` prints); `icon_buffer.o` also needs `wx+/image_resources.cpp` and is never pulled in by `Tests/sync`. All other files of `afs/` and `base/` need glib/gio headers on Linux only, through `wx+/image_holder.h` (GIcon) and `zen/`.

### Sync tests

`Tests/sync/run.sh` builds the engine library with `make engine` and the tests in `Tests/sync/` against it, then runs them: each test makes temporary folder trees and runs the real `compare()` and `synchronize()` like a batch job, with a non-interactive `ProcessCallback` that records every error and warning (errors are answered "ignore"), and checks the result with `std::filesystem` (names, types, content, sizes, modification times). Covered: mirror, update and two-way, including a second and third two-way run that use the database `.sync.ffs_db` (a change, a deletion, a move and a new item on each side, moves carried out as moves; conflicts left alone and reported again); include/exclude, time span and size filters; versioning with the replace, time stamp folder and time stamp file styles; the recycle bin and permanent deletion; the three symlink modes; copying permissions; comparing by content and by time and size; case-only renames and NFC/NFD names (canonically equivalent names are one name, on every platform); unreadable files and folders (reported, the rest synchronized, nothing deleted below a folder that couldn't be read; skipped as root); a ZIP archive as a read-only source; and on macOS extended attributes on APFS, a FAT disk image (no `._` files, equal on the next comparison) and the icon loader on a worker thread. It uses the harness of the zen tests (`Tests/zen/test.h`), and the same rules: portable test code, `ZEN_*` branches where the behavior differs, upstream bugs as `KNOWN_ISSUE`.

It needs what the FreeFileSync build needs (the libraries from `.github/build`, found in `.build/deps` or through `FFS_DEPS_PREFIX`, else the system's) and `python3` for the ZIP fixture; `CXX` (may include a wrapper, e.g. `ccache g++-14`), `STDLIB`, `CXXFLAGS`, `LDFLAGS` and on macOS `ARCH` work as for the other tests. By default it builds the engine in a temporary folder, from scratch; `SYNC_TEST_OBJ=<folder>` keeps the objects between runs, `SYNC_TEST_BIN=<folder>` keeps the test binary. HOME and `XDG_DATA_HOME` point into the temporary folder, so on Linux the recycle bin test fills a sandboxed trash; on macOS the trash can't be redirected, and the test deletes the items it moved there (unique names, at the paths the trash gives them). Nothing is left behind.

```bash
CXX=g++-14 bash Tests/sync/run.sh                     # all tests, against .build/deps
CXX=g++-14 SYNC_TEST_OBJ=$PWD/.build/sync-obj bash Tests/sync/run.sh syncTwoWayDatabase   # one test, objects kept
```

The `sync tests` workflow runs it on pull requests and pushes to `dev` and `main` that touch the engine, `zen/`, the tests or the build scripts: g++ 14 and clang 20 + libc++ on Ubuntu (as the unprivileged runner user, so the permission tests run), Xcode's clang for arm64 and x86_64 (Rosetta 2) on a `macos-15` runner. The libraries come from the caches of the build workflows (same keys, built on a miss) and the compiler output from ccache.

### ZIP archive test

`Tests/zip_afs/run.sh` builds the read-only ZIP file system (`afs/zip.cpp`) with the zen sources it needs, without wxWidgets, and checks it against archives it generates with Python's `zipfile` and Info-ZIP `zip`: listing, sizes, timestamps and content, the `zip:<archive>|<folder>` syntax, read-only behavior, a missing archive, a corrupted entry and an archive with more than 65535 entries. It needs a C++26 compiler (g++ 14+ or clang 20+), the zlib and glib dev packages on Linux (Xcode on macOS), `python3` and `zip`; set `CXX` to pick the compiler and `STDLIB=libc++` to use libc++, on macOS `ARCH` for the architecture:

```bash
bash Tests/zip_afs/run.sh
```

The `ZIP AFS test` workflow runs it with g++ in a `gcc:15` container, with clang 20 + libc++ on the Ubuntu runner and with Xcode's clang for arm64 and x86_64 (Rosetta 2) on a `macos-15` runner, on pull requests and pushes to `dev` and `main` that touch `afs/`, `zen/` or the test.

### zen unit tests

`Tests/zen/run.sh` builds the platform layer in `zen/` (only the sources the tests need, with `assert()` enabled) together with the tests in `Tests/zen/` and runs them. They cover file and folder creation, removal, moving and copying, modification times, sizes, item types, symlinks, permissions, free disk space, buffered and plain file I/O, folder traversal with Unicode and long names, the path helpers and macro expansion, `Zstring` comparison, case conversion, natural sorting and Unicode normalization, the directory watcher, the recycle bin, system and OS information, running a child process, the thread helpers and system error texts. Every test runs in a new temporary folder, and `run.sh` points `HOME` and `XDG_DATA_HOME` into its own temporary folder, so the recycle bin test never touches your trash on Linux; on macOS the trash can't be redirected, so the test deletes the items it moved there by the path the trash returns. Nothing is left behind. It needs a C++26 compiler (g++ 14+ or clang 20+) and the glib dev packages (`libglib2.0-dev pkg-config`) on Linux, Xcode (clang, SDK) and bash 4.4+ (`brew install bash`) on macOS; set `CXX` to pick the compiler and `STDLIB=libc++` to use libc++ (`CXXFLAGS` and `LDFLAGS` are appended), on macOS `ARCH=x86_64` or `ARCH=arm64` for the other architecture (cross-compiled, run under Rosetta 2) and `ZEN_TEST_COMPILE_ONLY=1` to only build; `ZEN_TEST_ALL_ERRORS=1` reports every compile error of a file, not only the first. Pass test names to run only those:

```bash
bash Tests/zen/run.sh              # all tests
bash Tests/zen/run.sh --list       # names
bash Tests/zen/run.sh dirWatcher   # one test
```

The summary line counts checks, failures and known issues. A known issue is an upstream bug the test documents with `KNOWN_ISSUE` instead of failing; the tests don't change `zen/` to make it pass. The tests use no Linux-only calls, so they run on macOS too and on Windows once `zen/` builds there; where the expected behavior differs per platform they branch on `ZEN_LINUX`, `ZEN_MAC` and `ZEN_WIN`, which `zen/build_info.h` derives from the compiler's predefined macros. Run directly from a kept build (`ZEN_TEST_BIN`), `zen_test` skips the tests that need `run.sh`'s sandbox (recycle bin, environment variables) and says so. `run.sh` also builds `zen_test_fallback`, which runs the `from_chars` test with the `strtod_l` fallback of `zen/legacy_compiler.cpp` forced on (`-DZEN_TEST_FROM_CHARS_FALLBACK`), so the fallback macOS uses is checked on Linux as well. While a platform is being ported, a test of a feature that isn't implemented there yet can be a `NOT_PORTED_YET_TEST_CASE`: it is counted and fails the run unless `ZEN_TEST_ALLOW_NOT_PORTED=1` is set (none is left). The `zen tests` workflow runs them on pull requests and pushes to `dev` and `main` that touch `zen/` or the tests, as a matrix of g++ in a `gcc:15` container, clang 20 + libc++ on the Ubuntu runner and Xcode's clang for arm64 and x86_64 (Rosetta 2) on a `macos-15` runner, where the directory watcher test runs 20 times in a row.

## Self-hosted macOS runner

The macOS CI jobs run on the repository owner's own Mac, registered as a self-hosted GitHub Actions runner (labels `self-hosted`, `macOS`), because GitHub's hosted macOS runners were often short of capacity. That Mac is a person's computer, not a throwaway VM, so the jobs follow stricter rules there. Everything below is implemented in `.github/build/mac-runner.sh` (setup, cache, cleanup, log scrubbing), `.github/build/select-xcode.sh` and the `runs-on` expressions of the workflows.

**What runs there, and when.** The macOS jobs of `macos-build.yml` (toolchain report, libraries, engine, apps, start checks, batch run, Sentry build and tests per architecture, DMG), `zen-tests.yml`, `sync-tests.yml` and `zip-sync.yml`, for pushes to `main` and `dev`, manual runs (`workflow_dispatch`) and pull requests from branches of this repository. The Linux jobs and `Sentry tests (Intel)` (on `macos-15-intel`, manual runs only; skipped when the Mac itself is an Intel one) stay on GitHub's runners. `macos-runner-diagnostics.yml` (manual runs only) prints what the Mac offers the jobs (architecture, macOS, Xcode, tools, Rosetta 2, window server session, permissions, disk space) without changing anything, and tests that `mac-runner.sh cleanup` ends a process started from the checkout by a relative path. One runner runs one job at a time, so the macOS jobs of a push queue up and run one after another (a push to `dev` that touches everything is 10 macOS jobs); a newer push cancels the runs it supersedes, as before. With Apple silicon and no Rosetta 2, the x86_64 entries build everything but run nothing (zen, sync and ZIP tests compiled only, apps not started); with Rosetta 2 they run under it. On an Intel Mac the roles flip: arm64 is compiled, not run, and the x86_64 Sentry tests run natively there.

**Trust rule.** Code from pull requests of forks never runs on that Mac: the runner is chosen in `runs-on` and is the Mac only for `push`, `workflow_dispatch` and `pull_request` events whose head repository is this repository (not for Dependabot); everything else gets `macos-15`, as before. Keep it that way when adding triggers (no `pull_request_target`, `workflow_run` or `issue_comment` jobs on the Mac) or macOS jobs (reuse the expression: `*macos-runner` in `macos-build.yml`, the matrix expression in the test workflows).

**Switching back to GitHub's runners.** A manual run with the input `hosted` uses `macos-15` for that run only. The repository variable `MACOS_RUNNER` (Settings, Secrets and variables, Actions, Variables) set to `hosted` sends every macOS job to `macos-15` without a commit; unset or any other value means the Mac. A job for the Mac waits in the queue while the Mac is off or asleep (GitHub cancels a job queued for 24 hours): set the variable, or cancel the run. Each job has a `timeout-minutes` (30 for tests and DMG, 90 for sync tests, 120 for a library build).

**What the Mac needs** (checked by `mac-runner.sh setup`, which fails with one `brew install ...` line for everything missing; the jobs install nothing):

- Xcode 26 or later in `/Applications`, opened once (license accepted, first launch done). `select-xcode.sh` takes the newest 26.x, else the newest later one, whose clang accepts `-std=c++26` with libc++ 20 or newer, and sets `DEVELOPER_DIR` for the job only (`xcode-select` is left alone). The Command Line Tools alone are not enough.
- Homebrew: `brew install bash coreutils flock pkgconf make cmake`; optional `ccache` (sync tests reuse compiler output) and `create-dmg` (DMG with a volume icon; without it plain `hdiutil`). python3 from Homebrew or Xcode.
- Optional: Rosetta 2 (`softwareupdate --install-rosetta --agree-to-license`, by the owner) to run the x86_64 tests and apps.
- About 10 GB of free disk space (checked; `FFS_CI_MIN_FREE_GB`): a cold library build of one architecture takes several GB, the local cache about 1 to 2 GB.
- The runner in the logged-in user's window server session. `svc.sh` installs a launch agent with `SessionCreate=true`, whose jobs have no window server session, so no app can start: then delete that key from `~/Library/LaunchAgents/actions.runner.*.plist` and restart the runner (`./svc.sh stop && ./svc.sh start`), or start it with `./run.sh` in a terminal. `mac-runner.sh require-gui` fails the app steps with that advice.
- No permission has to be granted. Screen Recording for the runner is optional: without it window titles are unreadable, so the start checks count the app's windows (`EXPECT_WINDOWS`, the counts GitHub's runners show) instead of matching titles; with it they match titles (the log says `window titles: readable`). No screenshot is ever taken on the Mac. Finder is never scripted (the DMG has no window layout there) unless the owner grants the runner the Automation permission for Finder and sets the repository variable `MACOS_DMG_FINDER_LAYOUT=1`. A permission dialog that appears during a job is a bug to report, not something to click through.

**What the jobs touch, and how they clean up.** Everything happens in the runner's `_work` folder: the checkout, `$RUNNER_TEMP` (also `TMPDIR`, so every `mktemp` folder and disk image of the tests is there) and the local cache `_work/FreeFileSync/ffs-ci-cache` (libraries per architecture, keyed like the `actions/cache` entries plus `-fpm`, restored as APFS clones; the two newest entries of each kind are kept, entries unused for 14 days go; the sync tests' ccache). The last step of every macOS job, `mac-runner.sh cleanup` (`if: always()`, so also after a failure or a cancellation), then

- ends processes still running whose executable is in the checkout or `$RUNNER_TEMP` (apps, `crashpad_handler`, test binaries): the resolved path of each process's executable (the first `txt` entry of `lsof`) or its command path, matched against both folders as given and resolved; the workflows start the apps by absolute path,
- detaches disk images whose image file is there (the tests' FAT and APFS images, the DMG checks; the list from `hdiutil info -plist`, read with PlistBuddy, and an unreadable list is a warning); other mounted images and volumes are left alone, and `wrap-mac-dmg.sh` refuses, without touching it, when a volume named like its image ("FreeFileSync <version>") is mounted,
- deletes what the tests moved to the real trash and could not delete themselves because the run was cancelled: the macOS trash can't be redirected, so the zen and sync tests use names unique to the run (`zen_test <n> ...`, `sync_test <n> ...`) and note each path before the move (`FFS_TEST_TRASH_LOG`); a logged path is deleted only if it is exactly one item named with those prefixes directly in `<home>/.Trash` or `<mount point>/.Trashes/<uid>`, without `.`/`..` components or symlinked trash folders (`trashItemOk`, tested with hostile paths by `mac-runner.sh self-test` in the toolchain job); nothing else in the trash is looked at (the runner has no Full Disk Access and doesn't need it),
- deletes crash reports in `~/Library/Logs/DiagnosticReports` written during the job by binaries from the checkout or `$RUNNER_TEMP` (the Sentry tests crash the apps on purpose); reports of other programs, the owner's own FreeFileSync included, are left alone,
- unregisters the test app bundles from Launch Services (started apps register themselves; the owner's own copy of the same bundle id elsewhere is not touched),
- puts back the apps' preferences (`defaults` domains `io.github.cuong-tran.freefilesync` and `.realtimesync`) and saved window state (`~/Library/Saved Application State/<id>.savedState`) as `setup` found them; the start checks, the batch run and the Sentry tests give the apps a fresh `HOME`, so the owner's `~/Library/Application Support/FreeFileSync` is never used,
- removes the build output (`.build`, `FreeFileSync/Build`, `$RUNNER_TEMP`'s folders) and old cache entries.

Not undone: Dock and "recent applications" entries macOS itself may add for a started app, and the owner's changes to those two preference domains made while a job runs (cleanup restores the state from the job's start).

**On the owner's screen.** The start checks, the batch run and the Sentry tests start FreeFileSync and RealTimeSync for a few seconds each (around 20 starts per arm64 run of `macos-build.yml`, none for x86_64 without Rosetta 2); their windows appear on the screen and may take the focus. They are started directly, not through `open`, because the checks need their exit codes; wxOSX does not activate a bundled app by itself.

**Privacy.** Nothing from the owner's machine may end up in public logs or artifacts (the repository is public):

- The user name (login and full name), host name and computer name are masked (`::add-mask::`) in every macOS job, in a first step before `actions/checkout`, which prints the work folder path.
- No screenshot at all on the Mac, and no image in the log: `check-mac-app-starts.sh` takes screenshots only on GitHub's runners (`RUNNER_ENVIRONMENT=github-hosted`; even an app window would show folder paths in the home folder), `SCREENSHOT_TO_LOG` works there only, and the screenshot artifact is uploaded from GitHub's runners only.
- Logs uploaded as artifacts (build logs, toolchain report, library manifest) go through `mac-runner.sh scrub` (home folder to `~`, private names to `<private>`) in an `if: always()` step of its own right before the uploads, which on the Mac run only after a successful scrub; the library log archive is made with `tar --numeric-owner`. Crash reports are deleted, never printed or uploaded. No step lists the home folder or `/Applications` (only Xcode bundles), dumps the environment or prints model strings.
- No path of the build machine in what is built, on any runner: source paths are mapped (`-ffile-prefix-map` of the work area in `common.sh`'s macOS flags for the libraries, of the repository in `platform.mk`'s `macPathFlags` for the apps: `__FILE__` of asserts, debug info); OpenSSL, which keeps its compiler flags as text in libcrypto, is built without that flag and without `CPPFLAGS`, and with its default built-in directories (`OPENSSLDIR` `/usr/local/ssl`, `ENGINESDIR`, `MODULESDIR` under `/usr/local/lib`) instead of the prefix's. `mac-runner.sh check-binaries` fails when a shipped executable, a dSYM or the Intel runner's copy of the x86_64 Sentry build (also stripped of its debug map) contains a path in the home folder; on the Mac the apps and dSYMs are then not uploaded. This changed the library cache keys once.

Residual exposure: GitHub prints the runner name and the machine name in "Set up job", before any step. Masking covers the job log, not file contents: anything new that uploads files must scrub them (logs) or pass `check-binaries` (binaries); images can't be scrubbed, so none is made on the Mac.

**Taking the runner out of use.** Set `MACOS_RUNNER=hosted` (immediate, reversible), or stop the runner (`./svc.sh stop`), or remove it (Settings, Actions, Runners; `./config.sh remove` on the Mac). Then delete `_work/FreeFileSync` in the runner folder (checkout and local cache).

**Rules for changes** (also in `CLAUDE.md`): no step that installs software (`brew install`, `softwareupdate`, `pip install`) or uses `sudo` on the Mac (keep such steps `if: runner.environment == 'github-hosted'`); no screenshots on the Mac, no images in logs, no home folder or `/Applications` listings, environment dumps or security settings in logs; no fork code; temporary files in `$RUNNER_TEMP`; every macOS job starts with the mask step and `mac-runner.sh setup` and ends with `mac-runner.sh cleanup`; a test that touches the real user environment (trash, mounted volumes, preferences) cleans up itself and leaves the cleanup step a way to finish the job after a cancellation.
