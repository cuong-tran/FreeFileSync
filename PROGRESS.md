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
| M3 macOS | 4. `wx+/`, `ui/` without GTK; app bundles | in progress | | |
| M3 macOS | 5. DMG job, Sentry on macOS, UI smoke test | | | |
| M4 Windows | | | | |

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

## Needed from the repository owner

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
