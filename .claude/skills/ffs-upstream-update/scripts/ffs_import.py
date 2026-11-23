#!/usr/bin/env python3
"""Import FreeFileSync source releases from freefilesync.org as git commits.

Releases come from the archive page (all past releases) and the download page
(the current release, which the archive page lists without a source link).

Each release becomes one commit whose tree is exactly the contents of
FreeFileSync_<ver>_Source.zip (plus any preserved paths such as .claude/):

  subject : "FreeFileSync <ver>"
  body    : the release notes from the website, one "- " bullet per item
  author  : Zenju <zenju@freefilesync.org>  (committer too)
  date    : release date from the website (author + committer date)

Only releases newer than the newest "FreeFileSync <ver>" commit already on the
current branch are imported, oldest first. Run with --dry-run to just list them.
"""

import argparse
import html
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ARCHIVE_URL = "https://freefilesync.org/archive.php"
DOWNLOAD_URL = "https://freefilesync.org/download.php"
DOWNLOAD_BASE = "https://freefilesync.org"
AUTHOR_NAME = "Zenju"
AUTHOR_EMAIL = "zenju@freefilesync.org"
USER_AGENT = "Mozilla/5.0 (compatible; ffs-import)"
PRESERVE = {".git", ".claude"}  # top-level entries never touched by an import
SUBJECT_RE = re.compile(r"^FreeFileSync (\d+(?:\.\d+)*)$")


def vkey(version):
    return tuple(int(x) for x in version.split("."))


def fetch(url, dest=None, referer=ARCHIVE_URL, tries=3):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Referer": referer})
    for attempt in range(1, tries + 1):
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                if dest is None:
                    return resp.read().decode("utf-8")
                tmp = dest + ".part"
                with open(tmp, "wb") as out:
                    shutil.copyfileobj(resp, out)
                os.replace(tmp, dest)
                return dest
        except Exception as exc:  # network hiccups: retry a few times
            permanent = isinstance(exc, urllib.error.HTTPError) and exc.code < 500
            if permanent or attempt == tries:
                raise RuntimeError(f"download failed: {url}: {exc}") from exc
            time.sleep(3 * attempt)


def change_items(block):
    """Text of each <div class="change-item">, up to its matching </div> (items may nest divs)."""
    items = []
    for start in re.finditer(r'<div class="change-item">', block):
        depth, pos = 1, start.end()
        for tag in re.finditer(r"<(/?)div\b[^>]*>", block[pos:]):
            depth += -1 if tag.group(1) else 1
            if depth == 0:
                inner = block[pos:pos + tag.start()]
                items.append(" ".join(html.unescape(re.sub(r"<[^>]+>", " ", inner)).split()))
                break
    return [i for i in items if i]


def parse_releases(page, page_url, source_anywhere=False):
    """Return [{version, date, notes, source_url, assets, referer}] for every release block on the page.

    `assets` lists the absolute URLs of all of the release's downloads (source, installers, binaries).

    The archive page keeps each release's source link inside its block; the download page puts
    the current release's links above its block, so there `source_anywhere` searches the page.
    """
    releases = []
    for block in page.split('<div class="changelog-header">')[1:]:
        m = re.match(r"FreeFileSync (\d+(?:\.\d+)*)\s*<", block)
        if not m:
            continue
        version = m.group(1)
        date = re.search(r'<time datetime="([^"]+)"', block)
        scope = page if source_anywhere else block
        src = re.search(r'href="([^"]*FreeFileSync_' + re.escape(version) + r'_Source\.zip)"', scope)
        assets = []
        for href in re.findall(r'href="([^"]*/FreeFileSync_' + re.escape(version) + r'_[^"/]+)"', scope):
            url = urllib.parse.urljoin(DOWNLOAD_BASE, href)
            if url not in assets:
                assets.append(url)
        releases.append({
            "version": version,
            "date": date.group(1) if date else None,
            "notes": change_items(block),
            "source_url": urllib.parse.urljoin(DOWNLOAD_BASE, src.group(1)) if src else None,
            "assets": assets,
            "referer": page_url,
        })
    return releases


def load_page(path, url):
    if path:
        with open(path, encoding="utf-8") as fh:
            return fh.read()
    return fetch(url, referer=DOWNLOAD_BASE + "/")


def git(repo, *args, env=None, capture=True, input=None):
    res = subprocess.run(["git", "-C", repo, *args], env=env, input=input, text=True,
                         stdout=subprocess.PIPE if capture else None)
    if res.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed ({res.returncode})")
    return res.stdout if capture else ""


def imported_versions(repo):
    has_head = subprocess.run(["git", "-C", repo, "rev-parse", "--verify", "-q", "HEAD"],
                              stdout=subprocess.DEVNULL).returncode == 0
    if not has_head:
        return []
    out = git(repo, "log", "--format=%s", f"--author={AUTHOR_EMAIL}")
    return [m.group(1) for m in (SUBJECT_RE.match(s) for s in out.splitlines()) if m]


def safe_extract(zip_path, dest):
    """Extract zip into dest, refusing absolute/parent paths; keep unix exec bits and symlinks."""
    root = os.path.realpath(dest)
    with zipfile.ZipFile(zip_path) as zf:
        bad = zf.testzip()
        if bad:
            raise RuntimeError(f"corrupt member {bad} in {zip_path}")
        for info in zf.infolist():
            name = info.filename.replace("\\", "/")
            if ".git" in name.split("/"):
                raise RuntimeError(f"archive contains a .git entry (would become a gitlink): {info.filename!r}")
            target = os.path.realpath(os.path.join(root, name))
            if os.path.isabs(name) or not (target == root or target.startswith(root + os.sep)):
                raise RuntimeError(f"unsafe path in archive: {info.filename!r}")
            mode = info.external_attr >> 16
            if name.endswith("/"):
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if stat.S_ISLNK(mode):
                link = zf.read(info).decode("utf-8")
                resolved = os.path.realpath(os.path.join(os.path.dirname(target), link))
                if os.path.isabs(link) or not resolved.startswith(root + os.sep):
                    raise RuntimeError(f"unsafe symlink in archive: {info.filename!r} -> {link!r}")
                os.symlink(link, target)
                continue
            with zf.open(info) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out)
            if mode & 0o111:
                os.chmod(target, 0o755)


def replace_worktree(repo, src_dir):
    for entry in os.listdir(repo):
        if entry in PRESERVE:
            continue
        path = os.path.join(repo, entry)
        if os.path.isdir(path) and not os.path.islink(path):
            shutil.rmtree(path)
        else:
            os.remove(path)
    for entry in os.listdir(src_dir):
        if entry in PRESERVE:
            print(f"  warning: archive contains top-level {entry!r}; skipped", file=sys.stderr)
            continue
        shutil.move(os.path.join(src_dir, entry), os.path.join(repo, entry))


def commit_release(repo, rel, cache_dir):
    version = rel["version"]
    zip_path = os.path.join(cache_dir, f"FreeFileSync_{version}_Source.zip")
    if not (os.path.isfile(zip_path) and zipfile.is_zipfile(zip_path)):
        print(f"  downloading {rel['source_url']}")
        fetch(rel["source_url"], zip_path, referer=rel["referer"])
    with tempfile.TemporaryDirectory(prefix=f"ffs-{version}-") as tmp:
        safe_extract(zip_path, tmp)
        replace_worktree(repo, tmp)

    exclude = [f":(exclude){p}" for p in sorted(PRESERVE - {".git"})]
    # Clear the index first so case-only renames are picked up on case-insensitive filesystems.
    git(repo, "rm", "-r", "-q", "--cached", "--ignore-unmatch", "--", ".", *exclude)
    git(repo, "add", "-A", "-f", "--", ".", *exclude)

    message = f"FreeFileSync {version}\n"
    if rel["notes"]:
        message += "\n" + "\n".join(f"- {n}" for n in rel["notes"]) + "\n"
    env = dict(os.environ,
               GIT_AUTHOR_NAME=AUTHOR_NAME, GIT_AUTHOR_EMAIL=AUTHOR_EMAIL,
               GIT_COMMITTER_NAME=AUTHOR_NAME, GIT_COMMITTER_EMAIL=AUTHOR_EMAIL,
               GIT_AUTHOR_DATE=rel["date"], GIT_COMMITTER_DATE=rel["date"])
    git(repo, "-c", "commit.gpgsign=false", "commit", "-q", "--allow-empty", "--no-verify",
        "--cleanup=verbatim", "-F", "-", env=env, input=message)
    print(f"  committed FreeFileSync {version} ({rel['date'][:10]}, {len(rel['notes'])} notes)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".", help="git repository (default: cwd)")
    ap.add_argument("--branch", required=True, help="branch that must be checked out, e.g. upstream")
    ap.add_argument("--cache-dir", help="where source zips are kept (default: a temp dir)")
    ap.add_argument("--archive-html", help="use a saved copy of archive.php instead of fetching it")
    ap.add_argument("--download-html", help="use a saved copy of download.php instead of fetching it")
    ap.add_argument("--dry-run", action="store_true", help="only list releases that would be imported")
    args = ap.parse_args()

    try:
        run(args)
    except RuntimeError as exc:
        sys.exit(f"error: {exc}\n(the working tree may be half-replaced; see `git status`)")


def run(args):
    repo = os.path.realpath(args.repo)
    top = subprocess.run(["git", "-C", repo, "rev-parse", "--show-toplevel"], text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.strip()
    if not top or os.path.realpath(top) != repo:
        sys.exit(f"error: {repo} is not the top of a git work tree")
    branch = subprocess.run(["git", "-C", repo, "symbolic-ref", "--short", "-q", "HEAD"], text=True,
                            stdout=subprocess.PIPE).stdout.strip()
    if branch != args.branch:
        sys.exit(f"error: on {branch or 'a detached HEAD'!r}, expected branch {args.branch!r} "
                 f"(git switch {args.branch})")
    # Any tracked change (staged ones would end up in the import commit) blocks; outside PRESERVE,
    # untracked and ignored files block too, because an import deletes them.
    exclude = [f":(exclude){p}" for p in sorted(PRESERVE - {".git"})]
    dirty = git(repo, "status", "--porcelain", "--untracked-files=no")
    dirty += git(repo, "status", "--porcelain", "--untracked-files=all", "--ignored", "--", ".", *exclude)
    if dirty.strip():
        sys.exit("error: working tree is not clean (untracked and ignored files count, since an "
                 "import deletes them):\n" + dirty.rstrip())

    archive = parse_releases(load_page(args.archive_html, ARCHIVE_URL), ARCHIVE_URL)
    if not archive:
        sys.exit("error: no releases parsed from archive page (layout changed?)")
    current = [r for r in parse_releases(load_page(args.download_html, DOWNLOAD_URL), DOWNLOAD_URL,
                                         source_anywhere=True) if r["source_url"]]
    if not current:
        print("warning: no current release with a source link found on the download page "
              "(layout changed?); using the archive page only", file=sys.stderr)

    # The archive entry wins when it has a source link; otherwise the download page supplies it.
    by_version = {r["version"]: r for r in archive}
    for r in current:
        if not by_version.get(r["version"], {}).get("source_url"):
            by_version[r["version"]] = r
    releases = list(by_version.values())

    done = imported_versions(repo)
    latest = max(done, key=vkey) if done else None
    newer = [r for r in releases if latest is None or vkey(r["version"]) > vkey(latest)]
    pending = sorted((r for r in newer if r["source_url"]), key=lambda r: vkey(r["version"]))
    waiting = [r["version"] for r in newer if not r["source_url"]]

    print(f"latest imported: {latest or 'none'}; newest on archive page: "
          f"{max((r['version'] for r in archive), key=vkey)}; current on download page: "
          f"{', '.join(r['version'] for r in current) or 'not found'}")
    if waiting:
        print(f"listed without a source archive on either page (skipped): {', '.join(waiting)}")
    if not pending:
        print("nothing to import")
        return
    print(f"to import ({len(pending)}): {', '.join(r['version'] for r in pending)}")
    if args.dry_run:
        return
    for r in pending:
        if not r["date"] or not r["notes"]:
            sys.exit(f"error: no release date or notes parsed for {r['version']} (layout changed?)")

    own_cache = args.cache_dir is None
    cache_dir = args.cache_dir or tempfile.mkdtemp(prefix="ffs-zips-")
    os.makedirs(cache_dir, exist_ok=True)
    try:
        for r in pending:
            print(f"FreeFileSync {r['version']}")
            commit_release(repo, r, cache_dir)
    finally:
        if own_cache:
            shutil.rmtree(cache_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
