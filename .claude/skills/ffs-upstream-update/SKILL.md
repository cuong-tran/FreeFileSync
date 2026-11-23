---
name: ffs-upstream-update
description: Check https://freefilesync.org/archive.php and https://freefilesync.org/download.php for FreeFileSync releases newer than the last one imported into this repo, import each new source archive as a commit on the "upstream" branch (title "FreeFileSync x.y", release notes as body, release date as commit date, author Zenju <zenju@freefilesync.org>), push it, and publish a GitHub release (tag vx.y, same title and notes, with the prebuilt binaries from the website attached; the source zip is not attached because it is the commit itself). Use when the user asks to check for, pull in, sync, import or release new FreeFileSync versions/releases/upstream source.
---

# FreeFileSync upstream update

This repository mirrors FreeFileSync source releases. Branch `upstream` holds one commit per release, each an exact snapshot of `FreeFileSync_<ver>_Source.zip` from freefilesync.org. This skill adds the releases published since the last import, pushes `upstream`, and publishes a matching GitHub release for each one.

The import is done by `scripts/ffs_import.py` (next to this file). It parses the archive page (all past releases) and the download page (the current release, which the archive lists without a source link), finds releases newer than the newest `FreeFileSync <ver>` commit authored by `zenju@freefilesync.org` on the current branch, downloads each source zip, replaces the working tree with its contents (keeping `.git/` and `.claude/`), and commits it with:

- subject: `FreeFileSync <ver>`
- body: the release notes, one `- ` bullet per item
- author and committer: `Zenju <zenju@freefilesync.org>`, unsigned
- author and committer date: the release date shown on the website

The GitHub releases are made by `scripts/ffs_release.py`. For every release commit on `upstream` that has no complete, published GitHub release yet, it:

- creates the lightweight tag `v<ver>` on that commit and pushes it
- downloads the prebuilt files the website lists for that version (Windows installer, macOS and Linux packages). It skips `*_Source.zip`, which is already in git as the release commit. A version with no prebuilt downloads gets a release without attachments
- creates the release with the commit subject as title and the commit body as description, with those files attached, and marks the newest version as latest

It works one release at a time and deletes the downloads after each upload. Reruns skip finished releases and complete partial or draft ones.

## Prerequisites

- `gh auth status` shows a logged-in account with write access to the GitHub repo.
- `git remote get-url origin` points at that repo, e.g. `git@github.com:cuong-tran/FreeFileSync.git`.

If either is missing, stop and tell the user; don't log in or add remotes yourself.

## Steps

Run every command from the repository root.

1. Note the current branch (`git branch --show-current`) so you can return to it. Make sure the working tree is clean (`git status --porcelain` is empty). If it isn't, stop and ask the user; do not stash or discard their changes yourself.

2. Copy the scripts out of the current branch before switching, so the versions that match these instructions run, not possibly older copies committed on `upstream`. Copy both, because `ffs_release.py` loads `ffs_import.py` from its own folder:

   ```bash
   mkdir -p "${TMPDIR:-/tmp}/ffs-skill" && cp .claude/skills/ffs-upstream-update/scripts/*.py "${TMPDIR:-/tmp}/ffs-skill/"
   ```

3. Switch to the upstream branch: `git switch upstream`. If it does not exist, stop and tell the user; do not create it.

4. Dry run:

   ```bash
   python3 -I "${TMPDIR:-/tmp}/ffs-skill/ffs_import.py" --repo . --branch upstream --dry-run
   ```

   The script refuses to run if anything outside `.claude/` is untracked or ignored, because an import deletes everything except `.git/` and `.claude/`. If it lists such files, stop and show them to the user; do not delete them yourself.

   It prints the latest imported version, the newest version on the archive page, the current version on the download page, and the versions it would import. If it says `nothing to import`, skip to step 8, which still finishes any release a previous run left incomplete.

5. Import (run it in the background if more than a couple of releases are pending, since each zip is several MB):

   ```bash
   python3 -I "${TMPDIR:-/tmp}/ffs-skill/ffs_import.py" --repo . --branch upstream
   ```

   The script stops on the first error. Commits made before the error are kept and a rerun resumes after them, but a failure in the middle of a release can leave the working tree half-replaced. Show the user `git status --short | head`, and once they agree the leftovers are just the failed import, restore `upstream` with `git reset --hard HEAD` followed by `git clean -fd -e .claude`, then rerun.

6. Verify the new commits:

   ```bash
   git log --format='%h %ad %an <%ae> %s' --date=short -n 5
   git show --stat --format=fuller HEAD | head -20
   git status --porcelain
   ```

   Each new commit should have the right title, date and author, the notes in its body, and the working tree should be clean.

7. Push the new commits: `git push origin upstream`. Never force-push. If the push is rejected, stop and tell the user, because the remote `upstream` has commits this clone doesn't.

8. Publish the GitHub releases, dry run first:

   ```bash
   python3 -I "${TMPDIR:-/tmp}/ffs-skill/ffs_release.py" --repo . --branch upstream --dry-run
   ```

   It lists the releases it would create or complete, with their file counts. If it lists none, everything is already published. Otherwise run it without `--dry-run`, in the background if more than a couple of releases are listed, since each release uploads tens of MB:

   ```bash
   python3 -I "${TMPDIR:-/tmp}/ffs-skill/ffs_release.py" --repo . --branch upstream
   ```

   It stops on the first network or GitHub API error; rerunning it resumes and completes a draft or partial release. A file is treated differently when its website link works but doesn't deliver it, i.e. the response is empty, truncated or an HTML page even after following a MediaFire landing page. The script retries such a file three times, then publishes the release without it and lists it at the end under "not attached". Some old files are broken this way on the hosting side, e.g. `FreeFileSync_5.17_Mac_OS_X_64-bit.zip`. Report the list to the user. Later runs retry those files, so the dry run keeps listing their releases as incomplete until the links work.

9. Verify each new release, for example `gh release view v<ver> --json name,body,isDraft,assets --jq '{name, isDraft, body, assets: [.assets[].name]}'`. The title and body should match `git log -1 --format=%B v<ver>`, it should not be a draft, and all of the version's prebuilt files should be attached, with no `*_Source.zip`.

10. Switch back to the original branch (`git switch <original>`) and report the imported versions with their release URLs. Do not merge `upstream` into `main` unless the user asks.

## Notes

- The archive page lists the current release without a source link ("Please find the current version in the Download section"). The script takes that release's date, notes and source zip from the download page instead, where the source link sits above the release block rather than inside it.
- If the script reports `no releases parsed` (archive page) or warns that no current release was found (download page; it then carries on with the archive only), the page layout changed: inspect the HTML of a release block (`<div class="changelog-header">`, `<time datetime=…>`, `<div class="change-item">`, the `_Source.zip` link) and update `parse_releases()` / `change_items()`.
- Use `--cache-dir <dir>` to keep downloaded zips between runs, and `--archive-html <file>` / `--download-html <file>` to parse saved copies of the pages. `ffs_release.py` accepts the same page options, plus `--versions <ver>…` to limit it to specific releases and `--gh-repo owner/name` if `origin` isn't the GitHub repo.
- Some downloads, notably the Windows installers, redirect to a MediaFire landing page instead of serving the file. `ffs_release.py` follows the direct download link on that page and checks each file's signature (zip, gzip, exe), so an HTML page is never uploaded as a release asset.
- GitHub shows a release's publish time, not the FreeFileSync release date. The date is in the tagged commit and is shown on the website.
