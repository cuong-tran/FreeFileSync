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
| M2 Sentry | 1. Interface, empty implementation, sentry-native build, Makefile switch, init | in progress | | |
| M2 Sentry | 2. Scrubbing, opt-out, first-start notice, test crash, symbol upload | | | |
| M3 macOS | | | | |
| M4 Windows | | | | |

## How the work runs

- The coordinator plans, writes task instructions and reviews. Coding is done by sub-agents (Opus, high effort).
- After each coding step: `/code-review medium <range>`, findings go to a fix agent, and the findings are listed below with their outcome.
- Commit and push to `dev` after every step. One PR from `dev` to `main` per milestone; work pauses when the PR is opened.

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

## Needed from the repository owner

- Repository secrets for Sentry (builds work without them, reporting stays off): `SENTRY_DSN`, `SENTRY_AUTH_TOKEN`, `SENTRY_ORG`, `SENTRY_PROJECT`.
- Later, for notarized Mac builds: an Apple Developer ID certificate and app-specific password.

## Upstream bugs found by the tests

Marked `KNOWN_ISSUE` in `Tests/zen`, not fixed (upstream code):

- `zen/sys_error.cpp` `formatGlibError()`: treats `G_FILE_ERROR` codes as errno values, so the message is wrong (e.g. `G_FILE_ERROR_NOENT` prints as `EINTR`).
- `zen/sys_info.cpp` `getUserDownloadsPath()`: throws instead of falling back to `~/Downloads` when `xdg-user-dir` is not installed (non-root users only).

## Open issues

- `Resources/Animal.dat` is not in the upstream source zip, so every build from these sources (the Linux packages included) shows "Failed to load image" on first start, when the welcome dialog opens. Found by the new start check. CI uses a placeholder image for now. To do in M3 packaging: ship an own image in its place (decided over copying it from the official binaries, whose artwork license is unclear).

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
