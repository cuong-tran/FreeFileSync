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

FreeFileSync doesn't build on macOS yet (that is milestone M3 of [docs/PORTING_PLAN.md](../../docs/PORTING_PLAN.md)), but the libraries do, with the same scripts and `FFS_TARGET=macos-arm64` or `macos-x86_64`:

```bash
brew install bash coreutils flock pkgconf   # bash 4+, sha256sum, realpath -m, nproc, flock
FFS_TARGET=macos-arm64  .github/build/build.sh
FFS_TARGET=macos-x86_64 .github/build/build.sh   # cross-compiled with -arch x86_64 on an Apple silicon Mac
```

Each target is installed into `.build/deps/<target>`, as static libraries only, for one architecture and the deployment target `FFS_MACOS_DEPLOYMENT_TARGET` in `common.sh`, which every library is checked against (`lipo -archs`, `otool -l`). The compiler is Xcode's clang (`CC=clang CXX=clang++`, override through the environment; CI selects Xcode 26.3 with `DEVELOPER_DIR`, the toolchain the apps will be built with); pkg-config sees only the prefix, so nothing from Homebrew ends up in a library. What differs from Linux:

- **OpenSSL**: `no-shared` with the `darwin64-arm64-cc`/`darwin64-x86_64-cc` target.
- **libssh2**: static, against that OpenSSL and the SDK's zlib, with the `Bugs.txt` patch.
- **libidn2** (`build-libidn2.sh`, macOS only): static, with libidn2's bundled copy of the libunistring parts it needs and without gettext. FreeFileSync calls libidn2 itself (`zen/socket.h`), not only through libcurl, so the port keeps the same IDN implementation as Linux; curl's `--with-apple-idn` would cover curl only. The archive's checksum in `common.sh` is the one Homebrew's libidn2 formula pins.
- **libcurl**: static, with the `Bugs.txt` patch, OpenSSL plus `--with-apple-sectrust` (certificates are verified with the macOS trust store, so no CA bundle has to ship), libssh2, libidn2 and the SDK's zlib; every other optional dependency is switched off explicitly.
- **wxWidgets**: wxOSX/Cocoa with the same feature set as on Linux, `--disable-exceptions`, wx's bundled image libraries (`--disable-sys-libs`) and the SDK's zlib. Of the `Bugs.txt` fixes in the patch, `wx/settings.h` (the color hook), `src/aui` and `src/common` take effect on macOS; the `src/gtk` and `src/unix` hunks apply but aren't compiled there.

`macos-report.sh` writes two informational reports: `toolchain` (compiler and libc++ versions of Xcode's clang and Homebrew LLVM, which `-std=` they accept, a syntax check of zen code without platform-specific includes, and the oldest deployment target per C++ library feature) and `port-todo <arch> <prefix>` (a syntax check of every `zen/*.cpp` for macOS, with the first error per file: the work list for porting `zen/`).

Toolchain findings so far (macos-15 runner): Xcode 16.4 (Apple clang 17.0.0, libc++ 19) and Xcode 26.3 (Apple clang 17.0.0, libc++ 20) both accept `-std=c++26`, as does Homebrew LLVM 23. Apple's libc++ makes floating-point `std::to_chars` and `std::format` available from macOS 13.3 on, hence the deployment target 13.3 (zen uses `to_chars(double)`); floating-point `std::from_chars` is deleted in libc++ 19 and needs macOS 26 in libc++ 20 (also with Homebrew LLVM against the system libc++), so `zen/legacy_compiler.cpp` will need a fallback; `std::jthread` exists from libc++ 20 on.

The [macOS build workflow](../workflows/macos-build.yml) runs both on a `macos-15` runner (arm64; x86_64 is cross-compiled there) and caches each target's prefix under a key of compiler, SDK, scripts and patches. It uploads per architecture `macos-libraries-<arch>.tar.gz` (build log and a manifest with versions, architectures and deployment targets) and `macos-port-todo-<arch>.txt`.

## Packaging

`package.sh` turns the build into a portable `FreeFileSync_<version>_Linux_<arch>.tar.gz` and a `freefilesync_<version>_<arch>.deb` in `.build/dist` (it needs `patchelf`, `dpkg-dev` and `unzip`):

```bash
.github/build/package.sh
```

It copies the libraries the binaries load from `.build/deps` next to them in `Bin/`, sets an `$ORIGIN` rpath on the binaries and those libraries, and checks that nothing is still loaded from `.build/deps`. The `.deb` installs to `/opt/FreeFileSync` with `freefilesync` and `realtimesync` launchers and desktop entries; its other dependencies come from `dpkg-shlibdeps`, so it installs on the distribution release it was built on and newer ones. `DEB_VERSION_SUFFIX` is appended to the `.deb` version. The `.deb` names the GitHub repository of the `origin` remote as its homepage; set `GITHUB_REPOSITORY=<owner>/<name>` to name another.

## Updating for a new release

When an upstream release changes the library versions in `Bugs.txt`, update the defaults in the scripts and the checksums in `common.sh`, and port the `Bugs.txt` fixes to the new sources as `patches/<name>-<version>.patch` (a unified diff applied with `patch -p1` in the unpacked source tree). The scripts refuse to build a version without a patch, so a forgotten patch shows up as an error rather than a subtly broken build.

## CI

The [Linux build workflow](../workflows/linux-build.yml) runs these scripts in three jobs:

- **Packages**: x86_64 and aarch64 on native Ubuntu 24.04 runners with g++ 14, against all the patched libraries, then `package.sh`. The `.deb` and tar.gz of each architecture are uploaded as workflow artifacts for 30 days.
- **Stock libraries**: an Ubuntu 26.04 container that builds libssh2 and wxWidgets with `FFS_UNPATCHED=1` and takes OpenSSL and libcurl from the distribution (`FFS_SYSTEM_LIBS="openssl libcurl"`). It only compiles, to keep the fallbacks for unpatched libraries working: the `wxColorHook` declaration in `wx+/darkmode.cpp`, the `MAX_SFTP_*` values in `afs/sftp.cpp` and the libcurl version checks in `libcurl/curl_wrap.cpp`.
- **clang + libc++**: Ubuntu 24.04 with clang 20 and `STDLIB=libc++`, against the patched libssh2 and a wxWidgets built with `CXXFLAGS=LDFLAGS=-stdlib=libc++` (cached under a key that includes the compiler and those flags), and the distribution's OpenSSL and libcurl. It links both apps as `<app>_x86_64-clang-libc++`, checks that they use libc++ and runs `check-app-starts.sh` on each: under Xvfb with an empty `HOME`, the main window must appear, no window may be titled like an error or warning, and stderr must not report an error.

With `CXX` set to clang or `STDLIB` set, `build-free-file-sync.sh` uses the same name suffix as the makefiles (`-clang`, `-libc++`) for the binaries and object folders. Use a separate `FFS_BUILD_DIR` per toolchain anyway: wxWidgets in `.build/deps` is built for one C++ library.

These scripts are the only place the CI pins library versions. The root `Dockerfile` has its own copy of the libssh2 commit and the libcurl and wxWidgets versions, so update it as well when a new release changes them.
