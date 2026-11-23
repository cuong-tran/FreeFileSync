# Building the patched libraries

FreeFileSync 14.13 doesn't build against the libraries most Linux distributions ship; the Building section of [CONTRIBUTING.md](../../CONTRIBUTING.md#building) explains why and how to build them by hand.

The scripts here download OpenSSL, libssh2, libcurl and wxWidgets, apply the `Bugs.txt` fixes from [patches/](patches/) and install them into `.build/deps` in the repository, without touching the system. Then they build FreeFileSync and RealTimeSync with the upstream makefiles against them.

## Usage

Install a C++26 compiler (g++ 14 or newer) and the system packages they still use. On Debian or Ubuntu:

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
| `FFS_BUILD_DIR` | `<repo>/.build` | Downloads, unpacked sources and build trees |
| `FFS_DEPS_PREFIX` | `$FFS_BUILD_DIR/deps` | Install prefix of the libraries |
| `FFS_SKIP_PATCHES` | unset | Set to `1` to build a version that has no patch in `patches/`. An unpatched wxWidgets still compiles: the makefiles detect the missing `wxColorHook`, warn, and build without the dark mode contrast fix |
| `FFS_UNPATCHED` | unset | Set to `1` to build every library without its patch, even when `patches/` has one. CI uses it to check that FreeFileSync still compiles against stock libraries |
| `FFS_SYSTEM_LIBS` | unset | `build-free-file-sync.sh` only: space-separated pkg-config names (`openssl`, `libssh2`, `libcurl`) that may come from the system instead of the prefix, e.g. `"openssl libcurl"` to build against the distribution's packages |
| `JOBS` | `nproc` | Parallel make jobs |

wxWidgets is linked statically. OpenSSL, libssh2 and libcurl are shared libraries found through an rpath to `.build/deps/lib`, so the binaries only run on the machine that built them unless those libraries are copied along.

## Packaging

`package.sh` turns the build into a portable `FreeFileSync_<version>_Linux_<arch>.tar.gz` and a `freefilesync_<version>_<arch>.deb` in `.build/dist` (it needs `patchelf`, `dpkg-dev` and `unzip`):

```bash
.github/build/package.sh
```

It copies the libraries the binaries load from `.build/deps` next to them in `Bin/`, sets an `$ORIGIN` rpath on the binaries and those libraries, and checks that nothing is still loaded from `.build/deps`. The `.deb` installs to `/opt/FreeFileSync` with `freefilesync` and `realtimesync` launchers and desktop entries; its other dependencies come from `dpkg-shlibdeps`, so it installs on the distribution release it was built on and newer ones. `DEB_VERSION_SUFFIX` is appended to the `.deb` version. The `.deb` names the GitHub repository of the `origin` remote as its homepage; set `GITHUB_REPOSITORY=<owner>/<name>` to name another.

## Updating for a new release

When an upstream release changes the library versions in `Bugs.txt`, update the defaults in the scripts and the checksums in `common.sh`, and port the `Bugs.txt` fixes to the new sources as `patches/<name>-<version>.patch` (a unified diff applied with `patch -p1` in the unpacked source tree). The scripts refuse to build a version without a patch, so a forgotten patch shows up as an error rather than a subtly broken build.

## CI

The [Linux build workflow](../workflows/linux-build.yml) runs these scripts in two jobs:

- **Packages**: x86_64 and aarch64 on native Ubuntu 24.04 runners with g++ 14, against all the patched libraries, then `package.sh`. The `.deb` and tar.gz of each architecture are uploaded as workflow artifacts for 30 days.
- **Stock libraries**: an Ubuntu 26.04 container that builds libssh2 and wxWidgets with `FFS_UNPATCHED=1` and takes OpenSSL and libcurl from the distribution (`FFS_SYSTEM_LIBS="openssl libcurl"`). It only compiles, to keep the fallbacks for unpatched libraries working: the `wxColorHook` declaration in `wx+/darkmode.cpp`, the `MAX_SFTP_*` values in `afs/sftp.cpp` and the libcurl version checks in `libcurl/curl_wrap.cpp`.

These scripts are the only place the CI pins library versions. The root `Dockerfile` has its own copy of the libssh2 commit and the libcurl and wxWidgets versions, so update it as well when a new release changes them.
