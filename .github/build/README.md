# Building the patched libraries

FreeFileSync 14.13 doesn't build against the libraries most Linux distributions ship:

- wxWidgets must be built with `wxUSE_EXCEPTIONS` off (`application.cpp` stops with `#error` otherwise) and with the dark mode hook from `Bugs.txt` (`wx+/darkmode.cpp` uses `wxColorHook`). `Bugs.txt` names wxWidgets 3.3.4; distributions mostly ship 3.2.
- `afs/sftp.cpp` needs `MAX_SFTP_READ_SIZE`/`MAX_SFTP_OUTGOING_SIZE` in libssh2's public header, which stock libssh2 doesn't have.
- The libcurl FTP fixes in `Bugs.txt` aren't upstream yet.

The scripts here download those libraries, apply the `Bugs.txt` fixes from [patches/](patches/) and install them into `.build/deps` in the repository, without touching the system. Then they build FreeFileSync and RealTimeSync with the upstream makefiles against them.

## Usage

Install a C++26 compiler (g++ 14 or newer) and the system packages they still use. On Debian or Ubuntu:

```bash
sudo apt-get install build-essential g++-14 pkg-config perl patch curl xz-utils bzip2 libgtk-3-dev libxtst-dev libidn2-dev zlib1g-dev
```

Build the libraries (OpenSSL, libssh2, libcurl, wxWidgets; about 15 minutes on 4 cores):

```bash
CC=gcc-14 CXX=g++-14 .github/build/build.sh
```

Build FreeFileSync and RealTimeSync into `FreeFileSync/Build/Bin`:

```bash
CXX=g++-14 .github/build/build-free-file-sync.sh
```

Each library has its own script (`build-openssl.sh`, `build-libssh2.sh`, `build-curl.sh`, `build-wxwidgets.sh`), so one can be rebuilt alone. `build.sh` skips a library whose version and patch are already installed.

| Variable | Default | Meaning |
| --- | --- | --- |
| `OPENSSL_VERSION`, `LIBSSH2_VERSION`, `CURL_VERSION`, `WXWIDGETS_VERSION` | 3.5.9, 1.11.1, 8.21.0, 3.3.4 | Library versions; also accepted as the first argument of each script |
| `FFS_BUILD_DIR` | `<repo>/.build` | Downloads, unpacked sources and build trees |
| `FFS_DEPS_PREFIX` | `$FFS_BUILD_DIR/deps` | Install prefix of the libraries |
| `FFS_SKIP_PATCHES` | unset | Set to `1` to build a version that has no patch in `patches/` |
| `JOBS` | `nproc` | Parallel make jobs |

wxWidgets is linked statically. OpenSSL, libssh2 and libcurl are shared libraries found through an rpath to `.build/deps/lib`, so the binaries only run on the machine that built them unless those libraries are copied along.

## Updating for a new release

When an upstream release changes the library versions in `Bugs.txt`, update the defaults in the scripts and the checksums in `common.sh`, and port the `Bugs.txt` fixes to the new sources as `patches/<name>-<version>.patch` (a unified diff applied with `patch -p1` in the unpacked source tree). The scripts refuse to build a version without a patch, so a forgotten patch shows up as an error rather than a subtly broken build.

The [Linux CI workflow](../workflows/linux-patched-libs.yml) runs these scripts on Ubuntu 24.04.
