# Progress

Running log for the multi-platform port. The plan is in [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Status

| Milestone | Step | State | Commits | Review |
|---|---|---|---|---|
| M1 Foundation | Plan and architecture | done | | n/a (docs) |
| M1 Foundation | 1. Platform macros, `Tests/zen` | done | `d1e9037^..128099a`, fixes `584a353^..90c338a` | 8 findings: 7 fixed, 1 kept |
| M1 Foundation | 2. Clang + libc++ on Linux | done | `9a0f61e^..8f3c1fd`, fixes `f36f54a`, `62e5860`, `8b96471`, `bbfdcb8`, `b12fa57`, `a2ac5e0`, `a3a3741` | 6 findings: 5 fixed, 1 kept |
| M1 Foundation | 3. macOS dependency builds in CI | done | `8f3c1fd..5ef2d42`, fixes `93c562e^..c28f8f2` | 7 findings: all fixed |
| M1 Foundation | Pull request | merged as [#11](https://github.com/cuong-tran/FreeFileSync/pull/11) (squash, `9b6221a`); `dev` restarted from `main` | | bot review: 1 doc fix (`1cf3840`), 3 findings not valid |
| M2 Sentry | 1. Interface, empty implementation, sentry-native build, Makefile switch, init | done | `cf27254^..304f86f`, fixes `7c0f9bc`, `07ce91b`, `c48509d`, `51829b2` | 10 findings: all fixed |
| M2 Sentry | 2. Scrubbing, opt-out, first-start notice, test crash, symbol upload | done | `304f86f..51829b2`, fixes `c2b91b5^..b80eb4e` | 9 findings: all fixed |
| M2 Sentry | Pull request | merged as [#12](https://github.com/cuong-tran/FreeFileSync/pull/12) (squash, `fea5409`) | | no review threads |
| M3 macOS | 1. `zen` core compiles and `Tests/zen` passes on macOS | done | `a0b5c3f..50065e4`, fixes in `50065e4..292907c` | 8 findings: all fixed |
| M3 macOS | 2. `zen` features: FSEvents watcher, recycle bin, shutdown, network | done | `50065e4..292907c`, fixes `292907c..f5c9b53` | 8 findings: all fixed |
| M3 macOS | 3. `afs/native`, `base/`, paths; engine builds, `Tests/sync` | done | `b4b35bb..3289e88`, fixes `f3e2601..bd9f1bf` | 7 findings: 6 fixed, 1 kept |
| M3 macOS | 4. `wx+/`, `ui/` without GTK; app bundles | done | `bd9f1bf..1152282`, fixes `1152282..83f890e` | 9 findings: all fixed |
| M3 macOS | 5. DMG job, Sentry on macOS, UI smoke test | done | `83f890e..bf0ef72`, fixes `d284031..8420f50` | 7 findings: all fixed |
| M3 macOS | Pull request | merged as [#13](https://github.com/cuong-tran/FreeFileSync/pull/13) (squash, `5b2dbe1`) after all checks passed | | no review threads |
| CI | macOS jobs on the owner's self-hosted runner (label `self-hosted`) | done, merged as [#15](https://github.com/cuong-tran/FreeFileSync/pull/15) (squash, `b4617dc`) after all checks passed | `79879b9..2d06b25` | two reviews, 16 findings, all fixed |
| Review | [#14](https://github.com/cuong-tran/FreeFileSync/pull/14) `mac-test-fixes` (fixed by another session of the owner's) | a reviewer sub-session reviews it with `/code-review medium` after every push until clean and green; then an approval | | |
| M4 Windows | | not started: paused by the owner for testing on a real Mac | | |

## Paused

M4 (Windows) is paused by the owner (2026-11-25) until the Mac test findings are dealt with. Running meanwhile: the review loop on #14 (see Status). To resume M4: the owner says so; then start M4 step 1 (toolchain and dependency cross builds with llvm-mingw, wxMSW) per `docs/PORTING_PLAN.md` with a fresh coder session.

## Mac test report (2026-11-25/26)

`docs/MAC_TEST_REPORT.md` and 47 screenshots are on branch `mac-test-report` (`89bf541`), from a manual test of `main` (`5b2dbe1`) on the owner's Mac (macOS 27.2, Xcode 27.2). Most of the checklist passed (batch from Finder, modal-dialog guards, Dock progress, real syncs, exFAT, RealTimeSync, Sentry tests). Findings F1 to F18. Another session of the owner's fixes them on branch `mac-test-fixes` ([#14](https://github.com/cuong-tran/FreeFileSync/pull/14), based on `main`): F1 to F8, F12, F15, F17. It overlaps with `dev` (self-hosted runner work) and a trial merge conflicts in 8 files: `build-libidn2.sh`, `common.sh`, `check-mac-app-starts.sh`, `macos-build.yml`, `base/db_file.cpp`, `CLAUDE.md`, `README.md`, `PROGRESS.md`. Whichever lands second merges `main` and resolves them (F1 and F12 are fixed on both). That branch also contains eight stray `FreeFileSync/Source/*.d` dependency files that should not be committed.

- Blocker with Xcode 27: F1 `base/db_file.cpp` needs `<sys/types.h>` (fixed in the self-hosted runner work, since CI on the Mac needs it).
- Major: F2 RealTimeSync cannot find FreeFileSync when macOS runs it translocated; F3 Gatekeeper (ad-hoc signing: "Open Anyway" in System Settings, right-click Open no longer works); F4 message boxes drop wrapped detail paragraphs (update notice, crash-report notice); F5 stale colors after a live light/dark switch; F6 no Edit menu, Cmd+V fails in dialogs.
- Minor and cosmetic: F7 default exclude filter lacks macOS entries (`.DS_Store`); F8 Windows/Linux texts; F9 untranslated standard menu items; F10 small default window; F11 dark-mode details; F12 libidn2 mirror with an expired certificate (fixed with the runner work); F13 icon; F14 unsaved config lost after a language change; F15 to F18 tooling, release name, accessibility.

## How the work runs

- The coordinator plans, writes task instructions and reviews. Coding is done by sub-agents (Opus, high effort).
- After each coding step: `/code-review medium <range>`, findings go to a fix agent, and the findings are listed below with their outcome.
- Commit and push to `dev` after every step. One PR from `dev` to `main` per milestone. The coordinator watches the PR, fixes CI failures and valid review findings, squash-merges it once all checks pass, recreates `dev` from `main` and continues with the next milestone (owner's instruction of 2026-11-24; before that, work paused at each PR).

## Decisions

| Date | Decision | Why |
|---|---|---|
| 2026-11-23 | Order: foundation, Sentry on Linux, macOS, Windows, engine library, Slint | The first Mac build to be tested should already report crashes; Sentry can be built and verified locally on Linux first. |
| 2026-11-23 | macOS is built on a macOS runner (arm64 native, x86_64 via `-arch`), not cross-built from Linux | Cross-compiling needs Apple's SDK, which cannot be redistributed. Musikku's Linux cross build works because it is a JVM app; its macOS DMG job is reused as the model for packaging. |
| 2026-11-23 | Windows is cross-built from Linux with llvm-mingw and tested under Wine plus a Windows runner | Verifiable in the development container; one toolchain family (clang + libc++) for both new platforms. |
| 2026-11-23 | Platform macros `ZEN_LINUX`/`ZEN_MAC`/`ZEN_WIN`, derived in `zen/build_info.h` | Upstream's own names; no build flag to forget. |
| 2026-11-23 | Linux code stays in the upstream files; other platforms go in `*_mac.*`/`*_win.*` files or small `#if` branches | Keeps upstream release merges workable. |
| 2026-11-23 | Sentry is opt-in at build time (`FFS_SENTRY=1` plus DSN), with an empty implementation otherwise | Same full/FOSS split as Musikku; a build without the secret has no tracking code. |
| 2026-11-23 | Apps are ad-hoc signed, not notarized | No Apple Developer ID available yet. |
| 2026-11-23 | Local `dev` was replaced by `origin/dev` (the old local commits are the pre-rewrite copies of PRs 1–7, kept on local branch `dev-local-stale`) | The remote branch is the current history. |
| 2026-11-23 | macOS compiler: Apple clang from Xcode 26.x (libc++ 20), not Homebrew LLVM | The CI probe showed it accepts `-std=c++26` and compiles the portable `zen` headers unchanged; no C++ runtime has to ship in the app. Xcode 16.4's libc++ 19 lacks `std::jthread`. |
| 2026-11-23 | macOS deployment target 13.3 | Oldest version Apple's libc++ allows for `std::to_chars(double)` and `std::format`, which `zen` uses. |
| 2026-11-23 | Third-party libraries on macOS are static: OpenSSL 3.5.9, libssh2 (pinned commit), libidn2 2.3.8, curl 8.21.0, wxWidgets 3.3.4 | Simple app bundle; nothing can leak in from Homebrew (host architecture only). curl verifies certificates against the macOS trust store. |
| 2026-11-23 | libidn2 is built for both Mac architectures instead of using Apple's IDN in curl | `zen/socket.h` calls libidn2 directly, so one library serves both and the Linux code path is kept. |
| 2026-11-23 | `Resources/Animal.dat`: ship an own image, do not copy it from the official binaries | It is missing from the source zip; the license of upstream's artwork in the binary packages is unclear. |
| 2026-11-23 | Commits are authored as `Cuong-Tran <16017808+cuong-tran@users.noreply.github.com>` | Asked by the owner; the address of their existing commits on `main`. |
| 2026-11-24 | After each merged milestone PR, `dev` is recreated from `main` | The PRs are squash-merged and the remote `dev` branch is deleted on merge. |
| 2026-11-24 | Crash reports: nothing is uploaded until the user has answered the first-start notice; its default choice is to send. Batch runs with no stored answer send nothing. `FFS_NO_CRASH_REPORT=1` always wins | A Sentry build must not upload before the user knows; stricter than the plan's plain opt-out. |
| 2026-11-24 | sentry-native 0.17.6, static, Crashpad backend, curl transport, in its own prefix `.build/sentry` | Cached separately from the other libraries; built only for `FFS_SENTRY=1`. |
| 2026-11-24 | macOS path comparison is always case- and Unicode-normalization-insensitive, whatever the volume | APFS/HFS+ default and what upstream's header documents; deterministic. A case-sensitive APFS volume is treated as insensitive. |
| 2026-11-24 | Case mapping on macOS is per code point, as on Linux ("ß" stays "ß") | Changed during the review fixes from Core Foundation's full mapping, so comparisons behave the same on both platforms. |
| 2026-11-24 | macOS file copies use the portable read/write loop, not `clonefile`; extended attributes and ACLs are copied separately | Keeps the progress callback and byte counts identical to Linux. |
| 2026-11-24 | Floating-point `from_chars` falls back to `strtod_l` in the C locale where libc++ lacks it (macOS before 26) | Chosen by libc++'s availability macro; matches libstdc++ and libc++ on 53 edge inputs. |
| 2026-11-24 | Milestone PRs are squash-merged by the coordinator once all checks pass, without waiting for the owner | Asked by the owner. |
| 2026-11-24 | Work was paused by the owner at the end of M3 step 2 and resumed the same day | |
| 2026-11-24 | macOS: extended attributes are copied only to volumes that support them natively | Otherwise macOS writes visible `._name` sidecar files, which the next comparison would treat as extra files. |
| 2026-11-24 | macOS: copying permissions copies the source's explicit ACL entries and keeps entries the target inherited from its folder | Replacing the whole ACL would remove access the target folder grants by inheritance. |
| 2026-11-24 | The engine has no slot for a file's creation time, so it is not copied on macOS | The AFS interface carries only the modification time; nothing was invented. |
| 2026-11-24 | Files that include AppKit are compiled as C++23, behind headers with standard types | The macOS 26 SDK's CoreGraphics headers are not valid C++26. |
| 2026-11-24 | Mac bundle ids are `io.github.cuong-tran.freefilesync` and `.realtimesync` | This is a fork; upstream's ids are not used. |
| 2026-11-24 | Scheduled pause at 23:00 UTC (06:00 UTC+7 on 2026-11-25) | Asked by the owner. |
| 2026-11-24 | The Mac apps are two bundles side by side, `FreeFileSync.app` and `RealTimeSync.app` | Same layout as the official Mac package. |
| 2026-11-24 | `Resources/Animal.dat` is an own image (a drawn cartoon cat, `.github/build/resources`), used by the Mac bundles and the Linux packages | It is missing from the source zip; upstream's artwork is not copied. |
| 2026-11-24 | macOS: a `.ffs_batch` opened from Finder while the app runs starts a new process that runs the job | macOS runs one instance per bundle; loading the job into the open window would not run it. |
| 2026-11-24 | macOS builds of this fork do not use upstream's online update check | It would report this fork's builds to upstream's server with values it does not expect and point users at upstream's package. Linux keeps upstream's behavior. |
| 2026-11-25 | Work resumed after the pause of 2026-11-24 23:00 UTC | Owner said continue. |
| 2026-11-25 | macOS crash reports while reporting is off: uploads are disabled and stored dumps are deleted at start and when "send" is given later | sentry-native has no hook on macOS to discard a crash; a dump made while the user had opted out must never be uploaded. |
| 2026-11-25 | Pause before M4 so the owner can test the Mac apps on a real Mac | Asked by the owner. |
| 2026-11-25 | The macOS CI jobs run on the owner's self-hosted runner (their own Mac, label `self-hosted`); hosted runners stay as fallback and for pull requests from forks | Asked by the owner; hosted macOS runners were short of capacity. Rules for that machine: no fork code, nothing from the screen or home folder in logs or artifacts, no software installs or `sudo`, tests clean up after themselves. |
| 2026-11-25 | macOS: on volumes without native extended attributes (FAT, exFAT, some network shares), a `._X` file next to an existing `X` is not reported as an item by the native file system | On macOS 27 every file a provenance-tracked process creates (any downloaded app opened through Gatekeeper) gets `com.apple.provenance`, which such volumes store as `._X`; the app cannot remove it. Without this, each sync to a FAT or exFAT volume would show phantom target-only files. Finder treats these files the same way. Orphaned `._X` files stay visible. |
| 2026-11-26 | Only real AppleDouble files (checked by their header) are hidden as companions on volumes without native extended attributes | A user's ordinary file named `._X` must stay visible. A real AppleDouble `._X` beside `X` on an APFS side still cannot be mirrored to FAT/exFAT (macOS owns that name there); the exclude filter `*/._*` is the remedy. |
| 2026-11-26 | This session's reviewer sub-session reviews #14 and never pushes to its branch or merges it; an approval is given when the loop ends clean and green | Asked by the owner. GitHub may refuse the approval, since this session acts as the PR's author; a comment then states the result. |

## Needed from the repository owner

- For the self-hosted Mac runner (a Mac mini, arm64, macOS 27.2, Xcode 27.2), before any build or test can run there:
  1. `brew install bash coreutils flock pkgconf make cmake` (optional: `ccache create-dmg`).
  2. Remove `SessionCreate` from `~/Library/LaunchAgents/actions.runner.*.plist` (or set it to false) and restart the runner service (`./svc.sh stop`, `./svc.sh start`): with it, jobs cannot open app windows.
  3. Free disk space: 10 to 12 GB are free, and the jobs refuse to start below 10 GB.
  4. Optional: Rosetta 2 (`softwareupdate --install-rosetta --agree-to-license`) so the x86_64 builds can be run, not only compiled.
  5. Delete the logs of the four workflow runs for commit `5b0e321` (macOS build, zen tests, sync tests, ZIP AFS test): their checkout step shows the Mac's login name. Later runs mask it.

- Repository secrets for Sentry (builds work without them, reporting stays off): `SENTRY_DSN`, `SENTRY_AUTH_TOKEN`, `SENTRY_ORG`, `SENTRY_PROJECT`.
- Later, for notarized Mac builds: an Apple Developer ID certificate and app-specific password.

## Upstream bugs found by the tests

Marked `KNOWN_ISSUE` in `Tests/zen`, not fixed (upstream code):

- `zen/sys_error.cpp` `formatGlibError()`: treats `G_FILE_ERROR` codes as errno values, so the message is wrong (e.g. `G_FILE_ERROR_NOENT` prints as `EINTR`).
- `zen/sys_info.cpp` `getUserDownloadsPath()`: throws instead of falling back to `~/Downloads` when `xdg-user-dir` is not installed (non-root users only).

## Open issues

- `Resources/Animal.dat` is not in the upstream source zip, so every build from these sources (the Linux packages included) shows "Failed to load image" on first start, when the welcome dialog opens. Found by the new start check. CI uses a placeholder image for now. To do in M3 packaging: ship an own image in its place (decided over copying it from the official binaries, whose artwork license is unclear).
- FreeFileSync sometimes does not end on SIGTERM (about 1 in 50 in the test loop): upstream's signal handler is not async-signal-safe and the main thread can block in it. Present before the Sentry work. The tests fall back to SIGKILL after 10 s; upstream's handler is left alone.
- Not exercised yet, because the repository secrets are missing: a real upload to Sentry and `sentry-cli debug-files upload`.
- Untested on macOS: shut down and sleep, the "no recycle bin on this volume" mapping, socket code.
- Natural sort of 100,000 mixed names takes about 3.5 s on macOS arm64 versus 1.8 s on Linux (normalization per comparison); accepted for now.
- GitHub's macOS arm64 runners are short of capacity: jobs wait 10 minutes or more, and some are cancelled as "not acquired by Runner". A re-run fixes it.
- Network file systems (FTP, SFTP, Google Drive) are only compiled on macOS, not run.
- `afs/gdrive.cpp` (upstream): the OAuth login loop never closes the accepted client socket. Left as is.
- The Mac UI has only been seen through CI screenshots. To check on a real Mac: main window layout (fonts and sizes were set for GTK), dark mode, Dock icon, tray item, the application menu, opening files from Finder, drag and drop, folder picker, sounds, "Show in Finder". Dock progress (`wx+/taskbar_mac.cpp`, `dock_tile_mac.mm`) is implemented but has never been seen. Not testable in CI: Finder opens, the modal-dialog guards, starting a batch process from a running app. The app icon is upscaled from a 128 px image.
- Crash capture for x86_64 is tested on a real Intel runner only on manual dispatch (`sentry_dry_run`); under Rosetta 2 on the arm64 runners Crashpad catches no crashes, so those scenarios are skipped there with a printed reason.
- The first-start crash-report notice may not render its explanatory text on macOS (a blank area in the CI screenshot). To check on a real Mac.
- FreeFileSync can crash by itself when it gets SIGTERM right after starting (seen on hosted macOS runners as "Illegal instruction" or "Killed"): upstream's SIGTERM handler is not async-signal-safe. This was the cause of the flaky Sentry scenarios (a second dump from that crash, not a duplicate upload); the tests now stop those runs with SIGKILL. The crash itself is open; its exact frame was never captured.
- On macOS 27, a file named `._X` beside `X` on an APFS side cannot be mirrored to FAT/exFAT by a downloaded (provenance-tracked) FreeFileSync: macOS keeps `X`'s attributes under that name there. It fails or shows as a difference on each run; the exclude filter `*/._*` is the remedy.
- The dSYM path for the Mac apps is exercised by the dry run, but a real upload to Sentry has never happened (no secrets).
- The DMGs (`FreeFileSync-14.13-macos-<arch>.dmg`, about 19 MB, both apps plus an Applications link) have no background picture; their window layout has only been checked by the script, not by eye.

## Review findings

### M1 step 1 (`d1e9037^..128099a`, `/code-review medium`)

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | `Tests/zen/run.sh` drops the caller's `CXXFLAGS` and has no glib 2.76 cap; fails to compile with glib 2.78+ | important | fixed in `584a353` (also in `Tests/zip_afs/run.sh`) |
| 2 | `Tests/zen/run.sh` can miss a failed compile job and link stale objects | important | fixed in `584a353` |
| 3 | Running `zen_test` directly uses the real recycle bin (sandbox only in `run.sh`) | important | fixed in `50448c1`: those tests are skipped without `ZEN_TEST_SANDBOX` |
| 4 | `test_paths.cpp` dereferences an empty optional when `HOME` is unset | important | fixed in `50448c1` |
| 5 | `zen/build_info.h` `#error` on platforms other than Linux, macOS, Windows | important | no change needed: kept on purpose; the message now names the supported platforms (`90c338a`) |
| 6 | Timeout test leaves a `sleep 10` process behind | nit | fixed in `c22d105` |
| 7 | A `KNOWN_ISSUE` that no longer reproduces does not fail the run | nit | fixed in `50448c1` |
| 8 | Test helpers lack the `//throw FileError` comment | nit | fixed in `c22d105` |

### M1 step 2 (`9a0f61e^..8f3c1fd`, `/code-review medium`)

Result of the step: both apps compile, link and start with clang 20 + libc++ on Linux. Two source fixes (`#include <exception>` in `zen/scope_guard.h`, an `int64_t` cast in `application.cpp`), three clang-only `-Wno-` flags for style warnings in upstream code. The g++ command lines are byte-identical to before.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | Object folder is shared between g++ and `STDLIB=libc++` builds, so switching toolchains fails at link | important | fixed in `f36f54a`: non-default toolchains get their own binary name and object folder (e.g. `FreeFileSync_x86_64-clang-libc++`) |
| 2 | Compiler family detection matches "clang" anywhere in `--version` and treats everything else as GCC | nit | fixed in `f36f54a`, `62e5860`: detection from `__clang__`/`__GNUC__` |
| 3 | CI start check passes when the app shows an error dialog | nit | fixed in `8b96471` and follow-ups: new `.github/build/check-app-starts.sh` (window title, error popups, stderr) |
| 4 | Cache key of the clang job misses the inline wxWidgets flags | nit | fixed in `8b96471` |
| 5 | Stale docs (job count, g++-only wording) | nit | fixed in `bbfdcb8` |
| 6 | g++ test entries run as root, clang entries as a normal user | nit | no change needed: both variants are useful coverage; a comment is added |

### M1 step 3 (`8f3c1fd..5ef2d42`, `/code-review medium`)

Result of the step: `.github/workflows/macos-build.yml` builds and caches the libraries for arm64 and x86_64 (cold about 10 to 12 minutes per architecture, cached about 1 minute). Work list for M3 step 1 from the compile probe: 17 of 21 `zen/*.cpp` stop at `#include <glib.h>` in `zen/sys_error.h`; `file_path.cpp` needs `environ`, `legacy_compiler.cpp` a floating-point `from_chars` fallback (Apple's needs macOS 26), `thread.cpp` a replacement for `sys/prctl.h`.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | `package.sh` does not know the toolchain-suffixed binary names | important | fixed in `93c562e`: shared `toolchain_suffix()` in `common.sh`; default package names unchanged |
| 2 | Compiler detection in `build-free-file-sync.sh` can fail randomly (SIGPIPE under `pipefail`) | important | fixed in `93c562e`, `e939de1`; all M1 scripts and workflows audited for the same pattern |
| 3 | `otool | awk ... exit` can abort a local macOS library build silently | important | fixed in `93c562e`, `e939de1` |
| 4 | Start check does not report the exit code of an app that dies after showing its window | nit | fixed in `147bdfd` |
| 5 | Start check can time out when run as root with saved settings | nit | fixed in `147bdfd`: as root, the app runs as `nobody` |
| 6 | Hard-coded Xcode 26.3 path in the macOS workflow | nit | fixed in `e939de1`: newest installed Xcode 26.x is selected at run time |
| 7 | `CLAUDE.md` describes the old compiler detection | nit | fixed in `c28f8f2` |

### M2 step 1 (`cf27254^..304f86f`, `/code-review medium`)

Result of the step: `zen/crash_report.h` with a sentry-native and an empty implementation; `make FFS_SENTRY=1 SENTRY_DSN=...`; the DSN goes through a generated header, never a command line. The default build gains one empty object file and contains no Sentry code. Size with Sentry: about +0.9 MB per app plus a 1.3 MB `crashpad_handler`. sentry-native also builds for both macOS targets in CI.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | Consent is given unconditionally at start, so every Sentry build uploads without asking | important | fixed in `7c0f9bc`: consent follows the stored answer; none yet means reports are kept, not sent |
| 2 | Concurrent instances share one database folder and reset each other's consent state | important | fixed in `7c0f9bc`: the consent file is no longer deleted; verified in the sentry-native and Crashpad sources that processes lock their own run folder and reports |
| 3 | Resetting consent before init probably discards reports stored by an earlier run | important | fixed in `7c0f9bc`: reports are kept until consent is known, then sent or deleted; covered by a test |
| 4 | Reports are not flushed on exits through `terminateProcess()` | important | fixed in `7c0f9bc` with `std::at_quick_exit`, no edit to upstream files |
| 5 | A non-Sentry build deletes the crash handler a Sentry binary of another toolchain needs | important | fixed in `c48509d` |
| 6 | `FFS_TEST_CRASH` can crash release builds on start | important | fixed in `7c0f9bc`: only while reporting is active and the environment is not `release` |
| 7 | Sentry uses libcurl outside FreeFileSync's curl init/cleanup order | important | fixed in `07ce91b`: shutdown before `teardownAfs()`; curl's global init is reference-counted, so the init order is safe |
| 8 | Confusing build error when the pkg-config file has no handler path | nit | fixed in `51829b2` |
| 9 | `make clean` fails or leaves files behind for Sentry builds | nit | fixed in `51829b2` |
| 10 | `crash_report_init.h` relies on transitive includes | nit | fixed in `7c0f9bc` |

### M2 step 2 (`304f86f..51829b2`, `/code-review medium`)

Result of the step: `zen/crash_scrub.*` removes paths, URLs, user and host names from report texts; setting `<CrashReports Enabled=.../>` in `GlobalSettings.xml`, a checkbox in the options dialog and a first-start notice; `Tests/sentry/run.sh` crashes the apps against a fake Sentry endpoint in nine scenarios; a "Sentry build" CI job; packages are built with Sentry and debug files uploaded only when the secrets exist. Minidumps contain module paths and stack memory, which cannot be scrubbed on the client; the README says so.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | OK in the options dialog stores consent "on" for a user who never saw the notice | important | fixed in `48bf917`: the checkbox starts unticked without an answer and is stored only when the user changes it; covered by a test |
| 2 | Scrubber leaks paths after an unmatched quote or containing an apostrophe | important | fixed in `c2b91b5`: quotes are boundaries, unterminated quoted paths are scrubbed to the end of the line, linear time; adversarial and 10^6-character tests added |
| 3 | The "no Sentry code in default builds" check can pass a binary full of it (SIGPIPE under `pipefail`) | important | fixed in `2083164`; CI now proves the check fails on a Sentry binary |
| 4 | RealTimeSync keeps sending after the user opts out in FreeFileSync | important | fixed in `48bf917`: both apps watch `GlobalSettings.xml` (every 5 s) and apply a changed answer; covered by a test |
| 5 | The "Default" button turns reporting back on after an opt-out | important | fixed in `48bf917` |
| 6 | A second process can re-apply an old consent value while the GUI holds an unsaved change | important | fixed in `48bf917`: the answer is written to `GlobalSettings.xml` at once; a process without an answer withholds its own reports and leaves the shared state alone; two-process test added |
| 7 | `make clean FFS_SENTRY=1` for one app removes the crash handler of the other | nit | fixed in `b80eb4e` |
| 8 | `make clean all FFS_SENTRY=1` builds without the Sentry makefile part | nit | fixed in `b80eb4e` |
| 9 | `FFS_SENTRY=0` skips the stale-handler cleanup | nit | fixed in `b80eb4e` |

### M3 step 1 (`a0b5c3f..50065e4`, `/code-review medium`)

Result of the step: all of `zen/` compiles on macOS with Apple clang, warning-free, for arm64 and x86_64; `Tests/zen` passes on both (x86_64 under Rosetta) in about a minute per job. The Linux command lines are unchanged. Directory watcher and recycle bin were marked "not ported yet" for this step only.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | Copying permissions can leave a read-only target writable (`copyfile(COPYFILE_ACL)` after `chmod`) | important | fixed in `8f7308a` and a follow-up: ACLs are copied with `acl_get_file`/`acl_set_file` before owner and mode (symlinks through a descriptor); tested with read-only files, folders and symlinks as non-root |
| 2 | A file copy fails entirely when its extended attributes cannot be copied | important | fixed in `8f7308a`: an extended-attribute error never fails or discards a copy; unexpected errors go to the sync log. Tested against a FAT disk image; the logging branch could not be provoked |
| 3 | Opening a file for reading fails where read-ahead cannot be set | important | fixed in `f8d316a` |
| 4 | A new socket can leak into a child process started by another thread | important | fixed in `511d95a`: one `lockFdInheritance()` guard for `consoleExecute` and socket creation; other descriptor sites audited |
| 5 | A failed sleep request can be reported as success | nit | fixed in `f8d316a` |
| 6 | Natural sort and path comparison allocate on every non-ASCII comparison | nit | fixed in `5a7a838`, `292907c`: case mapping through a table built once, normalization skipped where not needed. 100k mixed names: natural sort 3.5 s on macOS arm64 versus 1.8 s on Linux |
| 7 | `Tests/zen/run.sh` accepts bash 4.3 but needs 4.4 | nit | fixed in `4a14e9e` |
| 8 | A deployment-target change does not re-run the zen tests | nit | fixed in `4a14e9e` |

### M3 step 2 (`50065e4..292907c`, `/code-review medium`)

Result of the step: FSEvents directory watcher (`zen/dir_watcher_mac.cpp`), recycle bin through `NSFileManager` (`zen/recycler_mac.mm`), shut down and sleep; every `Tests/zen` test runs on macOS arm64 and x86_64 (38 cases, 1750 checks), the watcher test 20 times in a row.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | Copying extended attributes to FAT/exFAT/SMB creates visible `._name` sidecar files | important | fixed: extended attributes are copied only to volumes that store them natively; on the FAT test image no `._*` files appear |
| 2 | Ejecting a disk mounted inside the watched folder is reported as the watched folder being unavailable | important | fixed: only an unmount at or above the watched folder reports it unavailable. The runner showed FSEvents sends nothing when the watched folder's own volume is force-detached, so `fetchChanges()` also checks once per second that the folder exists (`4dcb578`) |
| 3 | Watcher drops all events when the watched path and the on-disk path differ in Unicode form | important | fixed: paths are compared component by component; tested with NFC/NFD and different-case spellings |
| 4 | ACL copy fails on volumes that reject ACL calls with anything but `ENOTSUP`, even with no ACL to copy | important | fixed: nothing is written when neither side has explicit entries; volumes without ACL support are skipped |
| 5 | ACL copy removes entries the target inherited from its folder | important | fixed: the new ACL is the source's explicit entries followed by the target's inherited ones; tested with an inheritable entry on the parent |
| 6 | Folder attribute copy throws when the folder cannot be opened for reading | nit | fixed: never throws on macOS; unreadable folders are skipped, other errors logged |
| 7 | Modified files can be reported as created (FSEvents accumulates flags) | nit | fixed: the watcher remembers paths already reported as created; exactly one `create` per new file in the test |
| 8 | An empty trash path is returned silently after a successful move | nit | fixed: falls back to the URL's path; documented in `recycler.h` |

### M3 step 3 (`b4b35bb..3289e88`, `/code-review medium`)

Result of the step: `make engine` builds `libffs_engine.a` (everything but the UI) on Linux and macOS through the new `FreeFileSync/Source/platform.mk`; `Tests/sync` runs real comparisons and synchronizations headlessly (19 cases on Linux, 22 on macOS) in the new `sync-tests.yml`; the ZIP test runs on macOS; `crash_report_sentry.cpp` compiles on macOS. The engine needs no wx initialization. Files outside the UI that still compile against wx: `config.*`, `localization.*`, `icon_buffer.*`, `base_tools.cpp`, the icon loader, `afs/native.cpp` (inventory in `CONTRIBUTING.md`).

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | Google Drive login on macOS holds the process-wide descriptor lock across a blocking `accept()` | important | fixed in `f3e2601`: the listening socket is non-blocking, so the lock is never held across a wait; other users of the lock audited |
| 2 | Icon loading can crash on a file name that is not valid UTF-8 (nil path into AppKit) | important | fixed in `081a69e`: nil paths and Objective-C exceptions become errors; tested with a real invalid-UTF-8 name on the FAT image |
| 3 | The macOS recycle bin sync test looks only in `~/.Trash` and can leave items in a real trash | important | fixed in `deff9b5`: both possible trash locations are checked and always cleaned up |
| 4 | `sync-tests.yml` does not run for UI and `wx+` headers the engine includes | nit | fixed in `bd9f1bf` |
| 5 | `afs/ftp.cpp` platform branches have no `#else`, so Windows would compile functions without a return | nit | fixed in `42df8c0`: `#else #error` added to every platform branch that must produce a value |
| 6 | `Tests/zip_afs/run.sh` needs bash 4.4 on macOS but does not check | nit | fixed in `bd9f1bf` |
| 7 | The makefiles stop on hosts other than Linux and macOS | nit | no change needed: intended, like the `#error` in `zen/build_info.h` |

### M3 step 4 (`bd9f1bf..1152282`, `/code-review medium`)

Result of the step: `FreeFileSync.app` and `RealTimeSync.app` build for arm64 and x86_64 in `macos-build.yml`, ad-hoc signed and linked against system libraries only; they start on the runner and show a window, and FreeFileSync runs a mirror job in batch mode. Artifacts: `FreeFileSync-14.13-macos-<arch>.zip` and screenshots. Known defect at the end of the step: buttons with icon and text draw their text twice on macOS.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | The attempted fix for doubled button text clears the label, so Compare and Synchronize lose their text after the first refresh | important | fixed in `81b5a8f`: wxOSX's `SetFont()` re-applies the stored label as the native button title; the two button classes now hide the label only during `SetFont()`. Screenshots show each text once, also after a refresh |
| 2 | A config opened from Finder replaces the configuration underneath an open modal dialog | important | fixed in `d3d229c`: Finder opens are queued while a dialog is open or an operation runs, and loaded afterwards |
| 3 | Quit, Preferences and About from the application menu are not blocked while a modal dialog is open (Cmd+Q can destroy the main window under it) | important | fixed in `d3d229c`: ignored with a beep while a modal dialog is open, in both apps |
| 4 | Double-clicking a `.ffs_batch` while the app runs loads it instead of running it | important | fixed in `d3d229c`: a new process of the app runs the job; failures show an error dialog |
| 5 | Files passed by Finder at launch skip the checks command-line arguments get | nit | fixed in `d3d229c`, `b3f0612` |
| 6 | The start check can pass for an app that only shows an error popup | nit | fixed in `fb2797e`: expected window title required; error windows, early exits and crash reports fail the check |
| 7 | The bundle makefile rule misses inputs (own image, deployment target, crash handler) | nit | fixed in `b3f0612` |
| 8 | `mac_bundle.sh` and its recipes break with spaces in paths | nit | fixed in `b3f0612` |
| 9 | The Mac build posts unexpected values to upstream's update server | nit | fixed in `d84514f`: no update check on macOS; the menu item shows a notice pointing to this repository's releases |

### M3 step 5 (`83f890e..bf0ef72`, `/code-review medium`)

Result of the step: a `DMG` job wraps both apps into `FreeFileSync-<version>-macos-<arch>.dmg` with `.github/build/wrap-mac-dmg.sh` (create-dmg, signatures verified inside the mounted image); the Mac apps build with `FFS_SENTRY=1` with `crashpad_handler` in each bundle, and `Tests/sentry/run.sh` runs on macOS.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | macOS keeps crash dumps made while reporting is off; a later "send" could upload them (two test scenarios red) | important | fixed in `06a16f9`, `e4c17e5`: a revoked-period marker in the report database; all reports are deleted at start with "don't send" and before "send" applies again; new scenario `c-revoked-then-send` |
| 2 | The build path for shipped Mac apps with the real Sentry secret has never run | important | fixed in `be9d11c`, `838a356`: `workflow_dispatch` input `sentry_dry_run` runs the same path with a fake DSN; the first dry run exposed two breakages, both fixed |
| 3 | Sentry Mac executables ship their debug map with build paths | nit | fixed in `be9d11c`: executables are stripped after `dsymutil` and re-signed; UUIDs match and `sentry-cli` reports them usable |
| 4 | `wrap-mac-dmg.sh` force-detaches a same-named volume the user may have open | nit | fixed in `73ba448`: the script stops with a message instead |
| 5 | Crash capture for x86_64 is never tested (skipped under Rosetta) | nit | fixed in `be9d11c`, `8420f50`: a "Sentry tests (Intel)" job on `macos-15-intel` (on dispatch); every scenario passes there, crash capture included |
| 6 | `Tests/sentry/run.sh` accepts bash older than 4.4 on macOS | nit | fixed in `e4c17e5` |
| 7 | The "sentry-native not found" message gives Linux advice on macOS | nit | fixed in `94b5392` |

### Self-hosted macOS runner (`79879b9..074c8df`, `/code-review medium`)

Result so far: the macOS jobs of all four workflows choose the owner's Mac for pushes, manual runs and same-repository pull requests, and `macos-15` for fork pull requests, with `MACOS_RUNNER=hosted` or the `hosted` input; `.github/build/mac-runner.sh` checks tools without installing, masks private names, keeps a local cache and cleans up; screenshots are window-only there. Nothing has been built on the Mac yet (tools missing, no window session).

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | The toolchain report can be uploaded unscrubbed when its step fails | important | fixed in `3375ed1`: scrubbing is its own `if: always()` step before every upload |
| 2 | Window screenshots from the Mac can show the owner's login and are uploaded | important | fixed in `c3c6a65`, `3375ed1`: no screenshots at all off GitHub's runners |
| 3 | Cleanup misses leftover apps that were started by relative path | important | fixed in `53af6bf`, `3375ed1`: matched by resolved executable; apps start by absolute path; verified on the Mac |
| 4 | Crash-report filter misses reports when the bundle path contains a symlink | important | fixed: both path spellings are matched |
| 5 | Title and error-popup checks may silently turn off on hosted runners | important | fixed: decided from the window list; hosted logs show "window titles: readable" |
| 6 | Disk-image cleanup silently does nothing without a usable python3 | important | fixed: `PlistBuddy`; an unreadable list is a warning |
| 7 | The local cache is never trimmed when the work folder path contains a blank | nit | fixed; self-test uses a path with blanks |
| 8 | The trash cleanup's path check can be bypassed with `..` | nit | fixed: strict validation with 23 hostile and benign self-test cases |
| 9 | The diagnostics job skips setup/cleanup and prints FileVault, SIP and admin status | nit | fixed: manual runs only, with setup and cleanup; the security-posture lines are gone |

### Self-hosted macOS runner, second round (`074c8df..0b6138c`, `/code-review medium`)

Result: everything builds and passes on the owner's Mac (macOS 27.2, Xcode 27.2; about 27 minutes for all four workflows, one job after another) and on GitHub's runners. Fixed on the way: F1 and F12 of the Mac test report, build-machine paths in the libraries and apps (`check-binaries` is now an error), `mktemp -d` ignoring `TMPDIR` on macOS 27, and the AppleDouble companions on FAT/exFAT (`e9ee443`). The kernel renames and removes a companion with its item, so the engine only has to hide it from listings.

| # | Finding | Severity | Outcome |
|---|---|---|---|
| 1 | A second `mac-runner.sh cleanup` in a job fails, so the diagnostics workflow ends red and skips cleanup stages | important | fixed in `3a44fa9`: eight independent stages, no dependence on `TMPDIR`, self-tested twice. Also found: a failed preferences read made cleanup delete the owner's real preferences domain; fixed |
| 2 | Any `._X` next to `X` is hidden on FAT/exFAT/network volumes, also an ordinary user file of that name | important | fixed in `c07e4e8`: only regular files with the AppleDouble header are hidden (about 14 µs per candidate); new test on FAT and exFAT |
| 3 | A failed local cache restore is reported as a hit | important | fixed in `3a44fa9`: a failed restore removes the partial folder and reports a miss |
| 4 | The diagnostics FAT probe mounts an image outside the runner's temp folder | nit | fixed in `c0518e9`: probes removed; DMGs on the Mac are built with `hdiutil` inside the work folder (create-dmg mounted under `/Volumes`) |
| 5 | The start check can pass on hosted runners without testing the window title | nit | fixed in `b240e6d` |
| 6 | `check-binaries` can report a binary clean when the home path contains special characters | nit | fixed in `3a44fa9` |
| 7 | Usage text omits `self-test` | nit | fixed |
