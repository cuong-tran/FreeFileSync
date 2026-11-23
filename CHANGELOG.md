# Changelog

Changes made in this repository and merged from other forks, on top of the official FreeFileSync releases. Upstream's own release notes are in `Changelog.txt` and in the `FreeFileSync <ver>` release commits, and are not repeated here.

Entries are grouped by the upstream release they were made against. Changes taken from another fork name it and link to it.

## FreeFileSync 14.13

### This repository

- Added project docs: `README.md`, `FAQ.md`, `MANUAL.md`, `CONTRIBUTING.md` and `CLAUDE.md`, with images in `docs/images/`.
- Added the `ffs-upstream-update` skill to import new upstream releases into the `upstream` branch and publish GitHub releases.
- Fixed a missing line continuation in `FreeFileSync/Source/Makefile` and `FreeFileSync/Source/RealTimeSync/Makefile` that made GNU make stop with "missing separator" before compiling anything.
- Changed the multi-arch Linux build workflow to build the checked-in tree: FreeFileSync and RealTimeSync for amd64 and arm64 on native GitHub runners in an Ubuntu 26.04 container, with the `Bugs.txt` library patches applied (wxWidgets 3.3.4 built from source with the `settings.h` color hook and without exception handling, libssh2 built from a pinned `1.11.2_DEV` commit with the `MAX_SFTP_*` constants made public), and with the glib API capped at 2.76 so the `::g_free()` calls compile against glib 2.78 and later. It runs on manual dispatch and on pull requests that change it or the Makefiles, caches the wxWidgets build and uploads the binaries as a tarball artifact instead of publishing prereleases.

## FreeFileSync 14.8

- Added `.github/workflows/linux-multiarch-build.yml`, a manually run Linux build of FreeFileSync for a chosen architecture that builds libssh2, libcurl and wxWidgets from source and uploads the binary as a workflow artifact.
