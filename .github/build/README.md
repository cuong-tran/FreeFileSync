# Building the patched libraries

FreeFileSync 14.13 doesn't build against the libraries most Linux distributions ship; the Building section of [CONTRIBUTING.md](../../CONTRIBUTING.md#building) explains why and how to build them by hand.

The scripts here download OpenSSL, libssh2, libcurl and wxWidgets, apply the `Bugs.txt` fixes from [patches/](patches/) and install them into `.build/deps` in the repository, without touching the system. Then they build FreeFileSync and RealTimeSync with the upstream makefiles against them.

## Usage

Install a C++26 compiler (g++ 14 or newer; clang 20 or newer also works, see [Building with clang and libc++](../../CONTRIBUTING.md#building-with-clang-and-libc)) and the system packages they still use. On Debian or Ubuntu:

```bash
sudo apt-get install build-essential g++-14 cmake pkg-config perl patch curl xz-utils bzip2 libgtk-3-dev libxtst-dev libidn2-dev zlib1g-dev
```

Build the libraries (OpenSSL, libssh2, libcurl, wxWidgets; under 10 minutes on 4 cores):

```bash
CC=gcc-14 CXX=g++-14 .github/build/build.sh
```

Build FreeFileSync and RealTimeSync into `FreeFileSync/Build/Bin`:

```bash
CXX=g++-14 .github/build/build-free-file-sync.sh
```

Its object files go to `.build/obj`, not the makefiles' `FreeFileSync/Build/Obj`, so they never mix with objects from the Docker or manual builds, and the binaries are relinked on every run. With `BUILD=debug` in the environment it builds the makefiles' debug FreeFileSync into `FreeFileSync/Build/Debug/Bin` (RealTimeSync has no debug mode).

Each library has its own script (`build-openssl.sh`, `build-libssh2.sh`, `build-curl.sh`, `build-wxwidgets.sh`), so one can be rebuilt alone. `build.sh` skips a library whose version and patch are already installed.

| Variable | Default | Meaning |
| --- | --- | --- |
| `OPENSSL_VERSION`, `CURL_VERSION`, `WXWIDGETS_VERSION` | 3.5.9, 8.21.0, 3.3.4 | Library versions; also accepted as the first argument of each script |
| `LIBSSH2_COMMIT` | `83284a05…` | libssh2 commit to build; 14.13 needs the unreleased 1.11.2_DEV API, so this is a pinned master snapshot from just before the 14.13 release |
| `FFS_TARGET` | `linux` on Linux, `macos-<host arch>` on macOS | What the libraries are built for: `linux`, `macos-arm64` or `macos-x86_64` (see [macOS](#macos)) |
| `LIBIDN2_VERSION` | 2.3.8 | macOS only: libidn2 version |
| `FFS_BUILD_DIR` | `<repo>/.build` | Downloads, unpacked sources and build trees |
| `FFS_DEPS_PREFIX` | `$FFS_BUILD_DIR/deps` (Linux), `$FFS_BUILD_DIR/deps/<target>` (macOS) | Install prefix of the libraries |
| `FFS_SKIP_PATCHES` | unset | Set to `1` to build a version that has no patch in `patches/`. An unpatched wxWidgets still compiles: the makefiles detect the missing `wxColorHook`, warn, and build without the dark mode contrast fix |
| `FFS_UNPATCHED` | unset | Set to `1` to build every library without its patch, even when `patches/` has one. CI uses it to check that FreeFileSync still compiles against stock libraries |
| `FFS_SYSTEM_LIBS` | unset | `build-free-file-sync.sh` only: space-separated pkg-config names (`openssl`, `libssh2`, `libcurl`) that may come from the system instead of the prefix, e.g. `"openssl libcurl"` to build against the distribution's packages. The script refuses to build if one of them is also installed in the prefix, since the prefix's copy would be used |
| `JOBS` | `nproc` | Parallel make jobs |

wxWidgets is linked statically. OpenSSL, libssh2 and libcurl are shared libraries found through an rpath to `.build/deps/lib`, so the binaries only run on the machine that built them unless those libraries are copied along.

## macOS

The libraries, `zen/`, the engine library (`libffs_engine.a`, see [CONTRIBUTING.md](../../CONTRIBUTING.md#the-engine-library)) and the two apps ([macOS apps](../../CONTRIBUTING.md#macos-apps)) build on macOS (milestone M3 of [docs/PORTING_PLAN.md](../../docs/PORTING_PLAN.md); DMGs and crash reporting in the Mac apps come with its step 5); the libraries with the same scripts and `FFS_TARGET=macos-arm64` or `macos-x86_64`:

```bash
brew install bash coreutils flock pkgconf make   # bash 4+, sha256sum, realpath -m, nproc, flock; GNU make 4 as gmake
FFS_TARGET=macos-arm64  .github/build/build.sh
FFS_TARGET=macos-x86_64 .github/build/build.sh   # cross-compiled with -arch x86_64 on an Apple silicon Mac
gmake -C FreeFileSync/Source engine              # the engine against .build/deps/macos-<host arch>; ARCH=x86_64 for the other one
gmake -C FreeFileSync/Source && gmake -C FreeFileSync/Source/RealTimeSync   # the apps: Build/Bin/macos-<arch>/*.app
```

Each target is installed into `.build/deps/<target>`, as static libraries only, for one architecture and the deployment target `FFS_MACOS_DEPLOYMENT_TARGET` in `common.sh`, which every library is checked against (`lipo -archs`, `otool -l`). The compiler is Xcode's clang (`CC=clang CXX=clang++`, override through the environment; CI selects the newest Xcode 26.x on the runner image with `DEVELOPER_DIR`, the toolchain the apps will be built with); pkg-config sees only the prefix, so nothing from Homebrew ends up in a library. What differs from Linux:

- **OpenSSL**: `no-shared` with the `darwin64-arm64-cc`/`darwin64-x86_64-cc` target.
- **libssh2**: static, against that OpenSSL and the SDK's zlib, with the `Bugs.txt` patch.
- **libidn2** (`build-libidn2.sh`, macOS only): static, with libidn2's bundled copy of the libunistring parts it needs and without gettext. FreeFileSync calls libidn2 itself (`zen/socket.h`), not only through libcurl, so the port keeps the same IDN implementation as Linux; curl's `--with-apple-idn` would cover curl only. The archive's checksum in `common.sh` is the one Homebrew's libidn2 formula pins.
- **libcurl**: static, with the `Bugs.txt` patch, OpenSSL plus `--with-apple-sectrust` (certificates are verified with the macOS trust store, so no CA bundle has to ship), libssh2, libidn2 and the SDK's zlib; every other optional dependency is switched off explicitly.
- **wxWidgets**: wxOSX/Cocoa with the same feature set as on Linux, `--disable-exceptions`, wx's bundled image libraries (`--disable-sys-libs`) and the SDK's zlib. Of the `Bugs.txt` fixes in the patch, `wx/settings.h` (the color hook), `src/aui` and `src/common` take effect on macOS; the `src/gtk` and `src/unix` hunks apply but aren't compiled there.

`macos-report.sh toolchain` is an informational report (compiler and libc++ versions of Xcode's clang and Homebrew LLVM, which `-std=` they accept, a syntax check of zen code without platform-specific includes, and the oldest deployment target per C++ library feature). `macos-report.sh zen-compile <arch> <prefix>` syntax-checks every `zen/*.cpp` and `zen/*.mm` (and `zen/socket.h`, which only the AFS code includes) for macOS against the libraries in the prefix, with the makefiles' warning flags, printing every error and failing if a file doesn't compile; `crash_report_sentry.cpp` is left out (it needs sentry-native, M3 step 5).

Toolchain findings so far (macos-15 runner): Xcode 16.4 (Apple clang 17.0.0, libc++ 19) and Xcode 26.3 (Apple clang 17.0.0, libc++ 20) both accept `-std=c++26`, as does Homebrew LLVM 23. Apple's libc++ makes floating-point `std::to_chars` and `std::format` available from macOS 13.3 on, hence the deployment target 13.3 (zen uses `to_chars(double)`); floating-point `std::from_chars` is deleted in libc++ 19 and needs macOS 26 in libc++ 20 (also with Homebrew LLVM against the system libc++), so `zen/legacy_compiler.cpp` falls back to `strtod_l` in the C locale when libc++'s availability macro `_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT` is 0 (with the same results as libstdc++ and libc++ for every input the tests try); `std::jthread` exists from libc++ 20 on. Apple clang needs no `-Wno-*` flag beyond the makefiles' clang set for `zen/`.

The [macOS build workflow](../workflows/macos-build.yml) runs both on a `macos-15` runner (arm64; x86_64 is cross-compiled there) and caches each target's prefix under a key of compiler, SDK, scripts and patches. Then it compiles all of `zen/` against each target's prefix (`zen-compile`, uploaded as `zen-compile-<arch>.txt`), builds `libffs_engine.a` with the makefile (`gmake engine ARCH=<arch>`, log uploaded as `engine-<arch>.txt`), compiles `zen/crash_report_sentry.cpp` through the makefile's `FFS_SENTRY=1` rules against the cached sentry-native, runs `Tests/zen/run.sh` for the architecture (x86_64 under Rosetta 2), and builds, checks, zips and starts both apps and runs a batch job (see [macOS apps](../../CONTRIBUTING.md#macos-apps)); artifacts per architecture: `FreeFileSync-<version>-macos-<arch>.zip`, `screenshots-macos-<arch>` and the logs. `resources/` holds files the source zip lacks (`Animal.dat`, see its README). The `sync tests` and `ZIP AFS test` workflows restore the same library cache (same key) for their macOS entries. It uploads per architecture `macos-libraries-<arch>.tar.gz` (build log and a manifest with versions, architectures and deployment targets).

## sentry-native (crash reporting)

`build-sentry-native.sh` builds sentry-native (default 0.17.6, `SENTRY_NATIVE_VERSION`; the release asset `sentry-native.zip`, which includes the Crashpad sources, checksum in `common.sh`) for builds with `FFS_SENTRY=1` (see [CONTRIBUTING.md](../../CONTRIBUTING.md#crash-reporting)); `build.sh` runs it only when `FFS_SENTRY=1` is set. It installs static libraries, `bin/crashpad_handler` and a `lib/pkgconfig/sentry-native.pc` for the makefiles into its own prefix, `FFS_SENTRY_PREFIX` (default `.build/sentry`, `.build/sentry/<target>` on macOS), with its own stamps, so CI caches it apart from the other libraries. Backend Crashpad, transport curl: on Linux against the libcurl pkg-config finds (the prefix's after `build.sh`, the distribution's with `FFS_SYSTEM_LIBS`), which `crashpad_handler` also loads at run time with `dlopen("libcurl.so.4")`; on macOS against the prefix's static `libcurl.a` (`crashpad_handler` uploads with the system's NSURLSession there). It uses `CC`/`CXX`, and `STDLIB=libc++` adds `-stdlib=libc++`; on macOS it builds for the target architecture and deployment target and is checked like the other libraries. `fetch-sentry-cli.sh` downloads a pinned, checksummed `sentry-cli` for the CI symbol upload.

## Packaging

`package.sh` turns the build into a portable `FreeFileSync_<version>_Linux_<arch>.tar.gz` and a `freefilesync_<version>_<arch>.deb` in `.build/dist` (it needs `patchelf`, `dpkg-dev` and `unzip`):

```bash
.github/build/package.sh
```

It copies the libraries the binaries load from `.build/deps` next to them in `Bin/`, sets an `$ORIGIN` rpath on the binaries and those libraries, and checks that nothing is still loaded from `.build/deps`. The `.deb` installs to `/opt/FreeFileSync` with `freefilesync` and `realtimesync` launchers and desktop entries; its other dependencies come from `dpkg-shlibdeps`, so it installs on the distribution release it was built on and newer ones. `DEB_VERSION_SUFFIX` is appended to the `.deb` version. With `FFS_SENTRY=1` (a build with crash reporting) it also ships `crashpad_handler` and moves the binaries' debug info into `.build/debug-files` (`<exe>.debug` and the shipped `<exe>`, with matching build IDs) for the upload to Sentry; the shipped binaries keep their symbol table as before. The `.deb` names the GitHub repository of the `origin` remote as its homepage; set `GITHUB_REPOSITORY=<owner>/<name>` to name another.

## Updating for a new release

When an upstream release changes the library versions in `Bugs.txt`, update the defaults in the scripts and the checksums in `common.sh`, and port the `Bugs.txt` fixes to the new sources as `patches/<name>-<version>.patch` (a unified diff applied with `patch -p1` in the unpacked source tree). The scripts refuse to build a version without a patch, so a forgotten patch shows up as an error rather than a subtly broken build.

## CI

The [Linux build workflow](../workflows/linux-build.yml) runs these scripts in four jobs:

- **Packages**: x86_64 and aarch64 on native Ubuntu 24.04 runners with g++ 14, against all the patched libraries, then `package.sh`. The `.deb` and tar.gz of each architecture are uploaded as workflow artifacts for 30 days.
- **Stock libraries**: an Ubuntu 26.04 container that builds libssh2 and wxWidgets with `FFS_UNPATCHED=1` and takes OpenSSL and libcurl from the distribution (`FFS_SYSTEM_LIBS="openssl libcurl"`). It only compiles, to keep the fallbacks for unpatched libraries working: the `wxColorHook` declaration in `wx+/darkmode.cpp`, the `MAX_SFTP_*` values in `afs/sftp.cpp` and the libcurl version checks in `libcurl/curl_wrap.cpp`.
- **clang + libc++**: Ubuntu 24.04 with clang 20 and `STDLIB=libc++`, against the patched libssh2 and a wxWidgets built with `CXXFLAGS=LDFLAGS=-stdlib=libc++` (cached under a key that includes the compiler and those flags), and the distribution's OpenSSL and libcurl. It links both apps as `<app>_x86_64-clang-libc++`, checks that they use libc++ and runs `check-app-starts.sh` on each: under Xvfb with an empty `HOME`, the main window must appear, no window may be titled like an error or warning, and stderr must not report an error.
- **Sentry build**: g++ 14 against the Packages job's libraries and sentry-native (cached on its own), both apps built with `FFS_SENTRY=1` and a fake DSN, then `Tests/sentry/run.sh` on the build and on its package, and the split debug files checked with `sentry-cli debug-files check`. With the repository secret `SENTRY_DSN` the Packages jobs build with crash reporting too, and upload the debug files when `SENTRY_AUTH_TOKEN`, `SENTRY_ORG` and `SENTRY_PROJECT` are set.

With `CXX` set to clang or `STDLIB` set, `build-free-file-sync.sh` uses the same name suffix as the makefiles (`-clang`, `-libc++`) for the binaries and object folders. Use a separate `FFS_BUILD_DIR` per toolchain anyway: wxWidgets in `.build/deps` is built for one C++ library.

These scripts are the only place the CI pins library versions. The root `Dockerfile` has its own copy of the libssh2 commit and the libcurl and wxWidgets versions, so update it as well when a new release changes them.
