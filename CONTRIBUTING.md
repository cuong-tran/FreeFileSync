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

The source ships Linux makefiles only (`FreeFileSync/Source/Makefile` and `FreeFileSync/Source/RealTimeSync/Makefile`); there are no Windows or macOS project files. The only automated test is the standalone ZIP test described at the end of this section. Upstream recommends building against current library versions with the patches from `Bugs.txt` applied, and that file names the versions it was written for (for 14.13: wxWidgets 3.3.4, libcurl 8.21.0, a libssh2 1.11.2 development snapshot).

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

- A g++ that accepts `-std=c++26`, which means GCC 14 or newer (GCC 14 is the oldest that takes the flag; whether all of 14.13 compiles with it is untested here). The makefiles also pass GCC-only warning flags such as `-Wlogical-op`.
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

### ZIP archive test

`Tests/zip_afs/run.sh` builds the read-only ZIP file system (`afs/zip.cpp`) with the zen sources it needs, without wxWidgets, and checks it against archives it generates with Python's `zipfile` and Info-ZIP `zip`: listing, sizes, timestamps and content, the `zip:<archive>|<folder>` syntax, read-only behavior, a missing archive, a corrupted entry and an archive with more than 65535 entries. It needs a C++26 g++, the zlib and glib dev packages, `python3` and `zip`; set `CXX` to pick the compiler:

```bash
bash Tests/zip_afs/run.sh
```

The `ZIP AFS test` workflow runs it in a `gcc:15` container on pull requests and pushes to `dev` and `main` that touch `afs/`, `zen/` or the test.
