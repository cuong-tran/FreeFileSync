# CLAUDE.md

Git mirror of the FreeFileSync source code (C++26, wxWidgets/GTK 3), rebuilt from the official source zips at https://freefilesync.org/archive.php (past releases) and https://freefilesync.org/download.php (current release). `README.md` describes the app; `FAQ.md` and `MANUAL.md` summarize the official FAQ and manual (images in `docs/images/`, copied from freefilesync.org); `CONTRIBUTING.md` covers branches, importing releases, the source layout and building.

## Repository rules

- Release commits (`FreeFileSync <ver>`, authored by `Zenju <zenju@freefilesync.org>`, dated to the release) are exact snapshots of the upstream zips. Never amend, rebase, reword or squash them, and never commit local changes under Zenju's name.
- `upstream` holds release commits and the `.claude/` tooling only. Don't commit fixes, docs or experiments there; do that on `main` or a feature branch. These docs and `docs/` live only on `main`.
- New releases come in through the `ffs-upstream-update` skill (`.claude/skills/ffs-upstream-update/`), which commits to `upstream`, pushes it, and publishes a GitHub release per version (tag `v<ver>` on the release commit, commit subject/body as title/notes, the website's prebuilt downloads attached, never the source zip, which is the commit itself). Tags `v<ver>` must always point at the matching release commit. Never force-push `upstream` or move release tags. Merge `upstream` into `main` only when the user asks.
- The importer deletes everything in the working tree except `.git/` and `.claude/` before unpacking a release, so keep anything that must survive imports inside `.claude/` on `upstream`.
- `zen/`, `wx+/`, `zenXml/`, `libcurl/`, `libssh2/` and `xBRZ/` are part of the upstream snapshot, not vendored copies to update separately.

## Code structure

- `FreeFileSync/Source/`: app entry point (`application.cpp`), global config (`config.*`), paths, logging, localization (`localization.*`, `parse_lng.h`, `parse_plural.h`), status handling. `version/version.h` holds `ffsVersion`.
- `FreeFileSync/Source/base/`: the engine, free of UI code. `comparison.*` compares folders, `algorithm.*` and `synchronization.*` decide and run sync operations, `file_hierarchy.*` is the in-memory folder tree model, `db_file.*` stores the last-sync database for two-way sync, `path_filter.*`/`soft_filter.h` are include/exclude filters, `versioning.*` handles deletion versioning, `parallel_scan.*` scans folders in parallel.
- `FreeFileSync/Source/afs/`: `AbstractFileSystem` (`abstract.h`) with the `native`, `ftp`, `sftp` and `gdrive` implementations. Engine code reaches files through `AbstractPath`/`AFS`, never through OS APIs directly.
- `FreeFileSync/Source/ui/`: wxWidgets GUI. `main_dlg.*` is the main window, `file_grid.*`/`tree_grid.*` the comparison views, `sync_cfg.*` the sync settings dialog, `batch_*` the batch jobs.
- `FreeFileSync/Source/RealTimeSync/`: separate app that watches folders (`monitor.*`) and launches a command; it has its own Makefile.
- `zen/`: platform layer and utilities in namespace `zen` (file access and I/O, `Zstring`, `FileError`/`SysError`, threads, HTTP, OpenSSL, zlib). `wx+/`: wxWidgets helpers (custom `Grid`, graphs, dark mode, popups). `zenXml/`: header-only XML serialization for config files.
- Generated code: `ui/gui_generated.*`, `RealTimeSync/gui_generated.*` and `wx+/popup_dlg_generated.*` come from wxFormBuilder, but the `.fbp` projects are not in the source zip. Edit them by hand only when unavoidable, keeping `.h` and `.cpp` in sync.

## Conventions in the upstream code

- App code is in namespace `fff`, the library in `zen`. Files start with the GPL header block and use `#pragma once`.
- 4-space indentation, no tabs, Allman braces, `//` comments, camelCase functions and variables, PascalCase types.
- Errors are reported by throwing `zen::FileError` (user-facing, with context) or `zen::SysError` (low level). Functions that throw note it in a trailing comment, e.g. `void f(); //throw FileError`. Follow that convention in any change.
- Strings shown to users go through `_("...")`/`_P(...)` from `zen/i18n.h` (force-included by the Makefile).

## Building and checking

- Linux only from these sources: `make -C FreeFileSync/Source -j"$(nproc)"` (and `make -C FreeFileSync/Source/RealTimeSync`). It needs g++ 14+ (`-std=c++26`, GCC-only warning flags) plus wxWidgets 3.3 (GTK 3, usually built from source), OpenSSL, libcurl, libssh2 and libidn2 dev packages; see `CONTRIBUTING.md` for packages and steps. There are no Windows/macOS project files and no test suite.
- `Bugs.txt` lists patches upstream applies to its third-party libraries; builds against unpatched libraries may misbehave.
- To see what changed in a release, diff the release commits: `git diff <prev-release-commit> <release-commit> -- FreeFileSync/Source`.
