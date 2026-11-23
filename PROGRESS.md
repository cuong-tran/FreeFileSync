# Progress

Running log for the multi-platform port. The plan is in [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Status

| Milestone | Step | State | Commits | Review |
|---|---|---|---|---|
| M1 Foundation | Plan and architecture | done | | n/a (docs) |
| M1 Foundation | 1. Platform macros, `Tests/zen` | done | `d1e9037^..128099a`, fixes `584a353^..90c338a` | 8 findings: 7 fixed, 1 kept |
| M1 Foundation | 2. Clang + libc++ on Linux | done, review fixes in progress | `9a0f61e^..8f3c1fd` | 6 findings, see below |
| M1 Foundation | 3. macOS dependency builds in CI | in progress | | |
| M2 Sentry | | | | |
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

## Needed from the repository owner

- Repository secrets for Sentry (builds work without them, reporting stays off): `SENTRY_DSN`, `SENTRY_AUTH_TOKEN`, `SENTRY_ORG`, `SENTRY_PROJECT`.
- Later, for notarized Mac builds: an Apple Developer ID certificate and app-specific password.

## Upstream bugs found by the tests

Marked `KNOWN_ISSUE` in `Tests/zen`, not fixed (upstream code):

- `zen/sys_error.cpp` `formatGlibError()`: treats `G_FILE_ERROR` codes as errno values, so the message is wrong (e.g. `G_FILE_ERROR_NOENT` prints as `EINTR`).
- `zen/sys_info.cpp` `getUserDownloadsPath()`: throws instead of falling back to `~/Downloads` when `xdg-user-dir` is not installed (non-root users only).

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
| 1 | Object folder is shared between g++ and `STDLIB=libc++` builds, so switching toolchains fails at link | important | sent for fixing |
| 2 | Compiler family detection matches "clang" anywhere in `--version` and treats everything else as GCC | nit | sent for fixing |
| 3 | CI start check passes when the app shows an error dialog | nit | sent for fixing |
| 4 | Cache key of the clang job misses the inline wxWidgets flags | nit | sent for fixing |
| 5 | Stale docs (job count, g++-only wording) | nit | sent for fixing |
| 6 | g++ test entries run as root, clang entries as a normal user | nit | no change needed: both variants are useful coverage; a comment is added |
