# Contributing

This repository mirrors the FreeFileSync source releases from [freefilesync.org](https://freefilesync.org). Upstream development happens elsewhere; changes here are limited to the mirror's own tooling and docs, plus any local work on top of the release snapshots.

## Branches

| Branch | Contents |
| --- | --- |
| `main` | All release commits, the update skill, and this repository's own docs (`README.md`, `FAQ.md`, `MANUAL.md`, `CONTRIBUTING.md`, `CLAUDE.md`, images in `docs/images/`). |
| `upstream` | Release commits only, plus the update skill. New releases are imported here. |

Release commits (`FreeFileSync <version>`, authored by Zenju and dated to the release) are exact snapshots of the upstream source zips. Don't amend or rebase them, and don't commit local changes to `upstream`.

## Getting new releases

New releases are imported by the Claude Code skill in [.claude/skills/ffs-upstream-update](.claude/skills/ffs-upstream-update/SKILL.md). Ask Claude Code to "check for new FreeFileSync versions", or run the importer yourself on a clean `upstream` checkout:

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
| `FreeFileSync/Source/afs/` | Abstract file system with local, FTP, SFTP and Google Drive back ends |
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

The source ships Linux makefiles only (`FreeFileSync/Source/Makefile` and `FreeFileSync/Source/RealTimeSync/Makefile`); there are no Windows or macOS project files and no test suite. Upstream recommends building against current library versions with the patches from `Bugs.txt` applied, and that file names the versions it was written for (for 14.13: wxWidgets 3.3.4, libcurl 8.21.0, libssh2 1.11.2).

### Requirements

- A g++ that accepts `-std=c++26` (GCC 14 or newer). The makefiles also pass GCC-only warning flags such as `-Wlogical-op`.
- `pkg-config` and the development packages for GTK 3, OpenSSL, libcurl, libssh2 and libidn2. libselinux is optional: the makefile enables SELinux support when `pkg-config` finds it.
- wxWidgets 3.3 built for GTK 3, with `wx-config` on the `PATH`.

On Debian or Ubuntu:

```bash
sudo apt install build-essential pkgconf libgtk-3-dev libssl-dev libcurl4-openssl-dev libssh2-1-dev libidn2-dev
```

Add `libselinux1-dev` for SELinux support. GTK 2 (`libgtk2.0-dev`) is not needed.

### wxWidgets

Distributions mostly package wxWidgets 3.2, which is older than upstream supports, so build wxWidgets from source. Download the release from [wxWidgets releases](https://github.com/wxWidgets/wxWidgets/releases) (the `wxWidgets-<version>.tar.bz2` archive already includes the bundled third-party libraries that a plain git clone needs submodules for), apply the wxWidgets patches from `Bugs.txt`, then:

```bash
mkdir buildgtk && cd buildgtk
```

```bash
../configure --with-gtk=3
```

```bash
make -j"$(nproc)" && sudo make install && sudo ldconfig
```

`wx-config --version` should then report the version you built, and `wx-config --selected-config` should start with `gtk3`. Don't configure wxWidgets with `--enable-no_exceptions`: it puts `-fno-exceptions` into `wx-config --cxxflags`, and FreeFileSync reports errors through C++ exceptions. See [wxWidgets' GTK install notes](https://github.com/wxWidgets/wxWidgets/blob/master/docs/gtk/install.md) for other options.

### FreeFileSync and RealTimeSync

```bash
make -C FreeFileSync/Source -j"$(nproc)"
```

```bash
make -C FreeFileSync/Source/RealTimeSync -j"$(nproc)"
```

The binaries are written to `FreeFileSync/Build/Bin/FreeFileSync_<arch>` and `FreeFileSync/Build/Bin/RealTimeSync_<arch>`, and object files go to a `<exe>_Make` folder in the system temp directory (`make clean` removes both).

To use a compiler other than the default `g++`, set `CXX` in the environment, for example `CXX=g++-14 make -C FreeFileSync/Source -j"$(nproc)"`. Don't pass it as a make argument (`make CXX=g++-14`): a command-line variable overrides the makefiles' `CXX += -pthread`.
