# Progress

Running log for the multi-platform port. The plan is in [`docs/PORTING_PLAN.md`](docs/PORTING_PLAN.md).

## Status

| Milestone | Step | State | Commits | Review |
|---|---|---|---|---|
| M1 Foundation | Plan and architecture | done | | n/a (docs) |
| M1 Foundation | 1. Platform macros, `Tests/zen` | next | | |
| M1 Foundation | 2. Clang + libc++ on Linux | | | |
| M1 Foundation | 3. macOS dependency builds in CI | | | |
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

## Review findings

None yet.
