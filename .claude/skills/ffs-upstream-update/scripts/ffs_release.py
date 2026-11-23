#!/usr/bin/env python3
"""Publish GitHub releases for the FreeFileSync release commits of a branch.

For every "FreeFileSync <ver>" commit authored by Zenju on --branch (default: upstream):

  tag     : v<ver>, a lightweight tag on that commit, created and pushed if missing
  release : title = commit subject, description = commit body
  assets  : the prebuilt downloads the website lists for that version (Windows installer,
            macOS/Linux packages), fetched from freefilesync.org. The source zip is left
            out: its contents are the release commit itself. A version with no prebuilt
            downloads gets a release without attachments.

Releases that already exist on GitHub with all their assets are skipped, and missing
assets are added to existing releases, so the script can be rerun after a failure.
Assets are downloaded one release at a time into a temporary directory and deleted
after upload. The newest version is marked as the repository's latest release.

Requires an authenticated `gh` CLI and a remote whose URL points at the GitHub repo.
"""

import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True  # don't leave a __pycache__ in the repo when loading the importer
_spec = importlib.util.spec_from_file_location("ffs_import", os.path.join(HERE, "ffs_import.py"))
ffs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ffs)

# First bytes of the file types the website serves; anything else (e.g. an HTML error page) is rejected.
MAGIC = {".zip": b"PK", ".gz": b"\x1f\x8b", ".exe": b"MZ", ".dmg": None, ".pkg": b"xar!"}


def run(cmd, capture=True, check=True):
    res = subprocess.run(cmd, text=True, stdout=subprocess.PIPE if capture else None)
    if check and res.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd[:4])} … failed ({res.returncode})")
    return res.stdout if capture else ""


def gh_repo_from_remote(repo, remote):
    url = run(["git", "-C", repo, "remote", "get-url", remote]).strip()
    m = re.search(r"github\.com[:/]([^/]+)/(.+?)(?:\.git)?/?$", url)
    if not m:
        sys.exit(f"error: remote {remote!r} ({url}) is not a GitHub URL; pass --gh-repo owner/name")
    return f"{m.group(1)}/{m.group(2)}"


def release_commits(repo, branch):
    """{version: (sha, subject, body)} for the Zenju release commits on branch."""
    out = run(["git", "-C", repo, "log", "-z", "--format=%H%x00%s%x00%b", f"--author={ffs.AUTHOR_EMAIL}",
               branch, "--"])
    fields = out.split("\0")
    commits = {}
    for i in range(0, len(fields) - 2, 3):
        sha, subject, body = fields[i].strip(), fields[i + 1], fields[i + 2]
        m = ffs.SUBJECT_RE.match(subject)
        if m:
            commits[m.group(1)] = (sha, subject, body.strip("\n"))
    return commits


def existing_releases(gh_repo):
    """{tag: {"assets": set(names), "draft": bool}} for all releases (drafts included) of the repo.

    `gh release create` uploads into a draft and publishes it at the end, so an interrupted run
    leaves a draft behind; it is completed and published on the next run.
    """
    out = run(["gh", "api", "--paginate", f"repos/{gh_repo}/releases?per_page=100", "--jq",
               ".[] | {tag: .tag_name, draft: .draft, assets: [.assets[] | select(.state == \"uploaded\") | .name]}"])
    rels = {}
    for line in out.splitlines():
        if line.strip():
            r = json.loads(line)
            rels[r["tag"]] = {"assets": set(r["assets"]), "draft": r["draft"]}
    return rels


def ensure_tag(repo, remote, tag, sha):
    have = subprocess.run(["git", "-C", repo, "rev-parse", "-q", "--verify", f"refs/tags/{tag}^{{commit}}"],
                          text=True, stdout=subprocess.PIPE).stdout.strip()
    if have and have != sha:
        raise RuntimeError(f"tag {tag} exists but points at {have[:12]}, not the release commit {sha[:12]}")
    if not have:
        run(["git", "-C", repo, "-c", "tag.gpgSign=false", "tag", tag, sha])
    remote_sha = run(["git", "-C", repo, "ls-remote", "--tags", remote, f"refs/tags/{tag}"]).split()
    if remote_sha and remote_sha[0] != sha:
        raise RuntimeError(f"remote tag {tag} points at {remote_sha[0][:12]}, not {sha[:12]}")
    if not remote_sha:
        run(["git", "-C", repo, "push", "-q", remote, f"refs/tags/{tag}"], capture=False)


class UnavailableAsset(RuntimeError):
    """The website's link works but doesn't deliver the file (empty, truncated or HTML response)."""


def download_asset(url, dest_dir, referer, tries=3):
    """Download one asset, retrying when the response isn't a valid file; raise UnavailableAsset if
    it never is. Network failures still raise RuntimeError (from ffs.fetch) and stop the run."""
    for attempt in range(1, tries + 1):
        try:
            return _download_asset(url, dest_dir, referer)
        except UnavailableAsset:
            if attempt == tries:
                raise
            time.sleep(5 * attempt)


def _download_asset(url, dest_dir, referer):
    name = url.rsplit("/", 1)[1]
    path = os.path.join(dest_dir, name)
    ffs.fetch(url, path, referer=referer)
    ext = os.path.splitext(name)[1].lower()
    with open(path, "rb") as fh:
        head = fh.read(8)
    if head.lstrip().startswith(b"<"):
        # Some downloads (e.g. the Windows installers) redirect to a MediaFire landing page instead of
        # the file; the page carries the direct download URL, which is what its button links to.
        with open(path, encoding="utf-8", errors="replace") as fh:
            m = re.search(r'https://download\d*\.mediafire\.com/[^"\'\s<>]+/' + re.escape(name), fh.read())
        if m:
            ffs.fetch(m.group(0), path, referer=url)
            with open(path, "rb") as fh:
                head = fh.read(8)
    magic = MAGIC.get(ext, b"")
    if os.path.getsize(path) == 0 or head.lstrip().startswith(b"<") or (magic and not head.startswith(magic)):
        raise UnavailableAsset(f"{url} did not return a valid {ext} file (got {head!r})")
    return path


def needs_work(info, existing):
    return (existing is None or existing["draft"]
            or any(u.rsplit("/", 1)[1] not in existing["assets"] for u in info["assets"]))


def publish(args, gh_repo, version, commit, info, existing, is_latest):
    sha, subject, body = commit
    tag = f"v{version}"
    latest = f"--latest={'true' if is_latest else 'false'}"
    ensure_tag(args.repo, args.remote, tag, sha)
    missing = [u for u in info["assets"] if existing is None or u.rsplit("/", 1)[1] not in existing["assets"]]
    with tempfile.TemporaryDirectory(prefix=f"ffs-release-{version}-") as tmp:
        files, unavailable = [], []
        for u in missing:
            try:
                files.append(download_asset(u, tmp, info["referer"]))
            except UnavailableAsset as exc:
                print(f"  warning: skipping {u.rsplit('/', 1)[1]}: {exc}", file=sys.stderr, flush=True)
                unavailable.append(u)
        result = publish_files(gh_repo, tag, subject, body, tmp, files, existing, latest)
    return result + (f", {len(unavailable)} unavailable" if unavailable else ""), unavailable


def publish_files(gh_repo, tag, subject, body, tmp, files, existing, latest):
    if existing is None:
        notes = os.path.join(tmp, "notes.md")
        with open(notes, "w", encoding="utf-8") as fh:
            fh.write(body + "\n")
        run(["gh", "release", "create", tag, "--repo", gh_repo, "--verify-tag", "--title", subject,
             "--notes-file", notes, latest, *files], capture=False)
        return f"created with {len(files)} assets"
    if files:
        run(["gh", "release", "upload", tag, "--repo", gh_repo, "--clobber", *files], capture=False)
    if existing["draft"]:
        run(["gh", "release", "edit", tag, "--repo", gh_repo, "--draft=false", latest], capture=False)
        return f"added {len(files)} missing assets and published the draft"
    return f"added {len(files)} missing assets"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".", help="git repository (default: cwd)")
    ap.add_argument("--branch", default="upstream", help="branch holding the release commits")
    ap.add_argument("--remote", default="origin", help="git remote to push tags to")
    ap.add_argument("--gh-repo", help="GitHub owner/name (default: derived from --remote)")
    ap.add_argument("--versions", nargs="*", help="only these versions (default: every release commit)")
    ap.add_argument("--archive-html", help="use a saved copy of archive.php instead of fetching it")
    ap.add_argument("--download-html", help="use a saved copy of download.php instead of fetching it")
    ap.add_argument("--delay", type=float, default=2.0, help="seconds to wait between releases")
    ap.add_argument("--dry-run", action="store_true", help="only list what would be published")
    args = ap.parse_args()
    args.repo = os.path.realpath(args.repo)
    try:
        sys.exit(run_all(args))
    except RuntimeError as exc:
        sys.exit(f"error: {exc}\n(rerun to resume; finished releases are skipped)")


def run_all(args):
    gh_repo = args.gh_repo or gh_repo_from_remote(args.repo, args.remote)
    commits = release_commits(args.repo, args.branch)
    if not commits:
        sys.exit(f"error: no FreeFileSync release commits on {args.branch!r}")

    archive = ffs.parse_releases(ffs.load_page(args.archive_html, ffs.ARCHIVE_URL), ffs.ARCHIVE_URL)
    current = ffs.parse_releases(ffs.load_page(args.download_html, ffs.DOWNLOAD_URL), ffs.DOWNLOAD_URL,
                                 source_anywhere=True)
    pages = {r["version"]: r for r in archive}
    for r in current:  # the download page lists the current release's files; the archive page doesn't
        if r["assets"] and not pages.get(r["version"], {}).get("assets"):
            pages[r["version"]] = r
    for r in pages.values():  # the source zip is already in git as the release commit
        r["assets"] = [u for u in r["assets"] if not u.endswith("_Source.zip")]

    versions = sorted(args.versions or commits, key=ffs.vkey)
    unknown = [v for v in versions if v not in commits]
    if unknown:
        sys.exit(f"error: no release commit on {args.branch!r} for: {', '.join(unknown)}")
    not_listed = [v for v in versions if v not in pages]
    if not_listed:
        sys.exit(f"error: not listed on the website: {', '.join(not_listed)}")

    newest = max(commits, key=ffs.vkey)
    releases = existing_releases(gh_repo)
    todo = [v for v in versions if needs_work(pages[v], releases.get(f"v{v}"))]
    print(f"{gh_repo}: {len(versions)} release commits considered, {len(releases)} releases on GitHub, "
          f"{len(todo)} to publish or complete")
    if args.dry_run:
        for v in todo:
            print(f"  v{v}: {len(pages[v]['assets'])} files"
                  + ("" if f"v{v}" not in releases else " (incomplete or draft release exists)"))
        return 0
    unavailable = []
    for i, v in enumerate(todo, 1):
        result, skipped = publish(args, gh_repo, v, commits[v], pages[v], releases.get(f"v{v}"), v == newest)
        unavailable += skipped
        print(f"[{i}/{len(todo)}] v{v}: {result}", flush=True)
        if i < len(todo):
            time.sleep(args.delay)
    if unavailable:
        # These releases stay "incomplete", so every later run retries them in case the link gets fixed.
        print(f"\n{len(unavailable)} file(s) the website links to but doesn't deliver; not attached:")
        for u in unavailable:
            print(f"  {u}")
    return 0


if __name__ == "__main__":
    main()
