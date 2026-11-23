# Changelog

Changes made in this repository on top of the official FreeFileSync releases. Upstream's own release notes are in `Changelog.txt` and in the `FreeFileSync <ver>` release commits, and are not repeated here.

Entries are grouped by the upstream release they were made against.

## FreeFileSync 14.13

- Added project docs: `README.md`, `FAQ.md`, `MANUAL.md`, `CONTRIBUTING.md` and `CLAUDE.md`, with images in `docs/images/`.
- Added the `ffs-upstream-update` skill to import new upstream releases into the `upstream` branch and publish GitHub releases.
- Fixed a missing line continuation in `FreeFileSync/Source/Makefile` and `FreeFileSync/Source/RealTimeSync/Makefile` that made GNU make stop with "missing separator" before compiling anything.
- Fixed the malformed `#if  def __GNUC__` conditionals in `zen/argon2.cpp` and `xBRZ/src/xbrz.cpp`, which fail to preprocess as soon as `__GNUC__` expands to a number.
- Changed the multi-arch Linux build workflow to build the checked-in tree: FreeFileSync and RealTimeSync for amd64 and arm64 on native GitHub runners in an Ubuntu 26.04 container, with the `Bugs.txt` library patches applied (wxWidgets 3.3.4 built from source with the `settings.h` color hook and without exception handling, libssh2 built from a pinned `1.11.2_DEV` commit with the `MAX_SFTP_*` constants made public), and with the glib API capped at 2.76 so the `::g_free()` calls compile against glib 2.78 and later. It runs on manual dispatch and on pull requests that change it or the Makefiles, caches the wxWidgets build and uploads the binaries as a tarball artifact instead of publishing prereleases.
- Expanded the Linux build instructions in `CONTRIBUTING.md`: Debian/Ubuntu packages, building libssh2 from a development commit and wxWidgets 3.3 with `--disable-exceptions` from source with the `Bugs.txt` patches, building FreeFileSync and RealTimeSync (with the glib 2.76 API cap needed on glib 2.78 and later), and choosing a compiler through `CXX` in the environment.
- Corrected the build requirements in `CLAUDE.md`: 14.13 builds with `-std=c++26` (GCC 14 or newer), wxWidgets 3.3 and a development libssh2.
- The FreeFileSync and RealTimeSync Makefiles check `wx/settings.h` for the `wxColorHook` patch from `Bugs.txt` and define `ZEN_WX_NO_COLOR_HOOK` only if the check succeeds and finds no hook, so `wx+/darkmode.cpp` declares the hook itself only for a stock wxWidgets (e.g. the 3.3 release tarball or distro packages). A failed check defines nothing, so the compiler reports the real error. Builds against a patched wxWidgets are unchanged; with a stock wxWidgets the makefiles print a warning naming `Bugs.txt` and the higher-contrast gray text in dark mode is lost. `CONTRIBUTING.md` notes that the color hook is the only wxWidgets patch the build needs.

## FreeFileSync 14.9

- Added a GitHub Actions workflow (`.github/workflows/build-arm64.yml`) that builds FreeFileSync and RealTimeSync for Linux ARM64 on every push and pull request to `main` and `dev`, in an Ubuntu 26.04 container on a native arm64 runner. It builds wxWidgets 3.3.4 with the `wxColorHook` patch from `Bugs.txt` and `--disable-exceptions` (`.github/scripts/build-wxwidgets.sh`, cached) and an unpatched libssh2 from a pinned development commit, and packages the result as a `.deb` that installs `Bin/` (with the bundled libssh2), `Resources/` and launchers under `/opt/FreeFileSync`, plus a portable `tar.gz`.
- `libcurl/curl_wrap.cpp` builds against libcurl older than 8.11.1 (checked down to 8.5): `LIBCURL_VERSION_NUM` checks guard `CURLE_TOO_LARGE` (8.6.0), `CURLE_ECH_REQUIRED` (8.8.0) and `CURLE_OBSOLETE34`/`41` (8.11.1, with the older names used before that).
- `afs/sftp.cpp` falls back to libssh2's value of 30000 for `MAX_SFTP_READ_SIZE` and `MAX_SFTP_OUTGOING_SIZE` when stock libssh2 headers lack them. A libssh2 patched as described in `Bugs.txt` still uses its own definitions.

## FreeFileSync 14.8

- Added `.github/workflows/linux-multiarch-build.yml`, a manually run Linux build of FreeFileSync for a chosen architecture that builds libssh2, libcurl and wxWidgets from source and uploads the binary as a workflow artifact.

## FreeFileSync 14.5

- `wx+/darkmode.cpp` declares `wxColorHook` and `refGlobalColorHook()` itself so that it compiles against a wxWidgets without the `Bugs.txt` patch.
