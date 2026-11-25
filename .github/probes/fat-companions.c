// Probe for macos-runner-diagnostics.yml: run by a provenance-tracked process (started by Homebrew's bash) on a volume without
// native extended attributes, where macOS keeps an item's attributes in an AppleDouble companion "._<name>". Does the kernel
// rename, move and remove the companion together with the item (file, folder, symlink)? Prints, after each call, which "._"
// entries exist in the two test folders.
//   fat-companions <folder on the volume>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char a[2048], b[2048];

static void list(const char* dir, char* out, size_t n)
{
    DIR* d = opendir(dir);
    for (struct dirent* e; d && (e = readdir(d));)
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") && strcmp(e->d_name, ".fseventsd")) { strlcat(out, e->d_name, n); strlcat(out, " ", n); }
    if (d) closedir(d);
}

static void show(const char* step, int rv)
{
    char la[4096] = "", lb[4096] = "";
    list(a, la, sizeof(la));
    list(b, lb, sizeof(lb));
    printf("  %-44s %s  a: [%s]  b: [%s]\n", step, rv == 0 ? "ok    " : strerror(errno), la, lb);
}

static int mk(const char* path) { int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644); if (fd < 0) return -1; write(fd, "x", 1); return close(fd); }

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    char p[2048], q[2048];
    snprintf(a, sizeof(a), "%s/a", argv[1]); snprintf(b, sizeof(b), "%s/b", argv[1]);
    mkdir(a, 0777); mkdir(b, 0777);
#define P(x) (snprintf(p, sizeof(p), "%s/%s", a, x), p)
#define Q(d, x) (snprintf(q, sizeof(q), "%s/%s", d, x), q)
    show("start (folders a, b)", 0);
    show("create file f", mk(P("f")));
    show("rename f -> g (same folder)", rename(P("f"), Q(a, "g")));
    show("rename a/g -> b/g (other folder)", rename(P("g"), Q(b, "g")));
    show("unlink b/g", unlink(Q(b, "g")));
    show("create folder d", mkdir(P("d"), 0777));
    show("create file d/x", mk(Q(P("d"), "x")) );
    snprintf(q, sizeof(q), "%s/d/x", a);
    show("unlink d/x", unlink(q));
    show("rmdir d (empty but for companions?)", rmdir(P("d")));
    show("create folder e", mkdir(P("e"), 0777));
    show("rename a/e -> b/e2 (folder, other folder)", rename(P("e"), Q(b, "e2")));
    show("rmdir b/e2", rmdir(Q(b, "e2")));
    show("create symlink l -> f", symlink("f", P("l")));
    show("rename symlink l -> m", rename(P("l"), Q(a, "m")));
    show("unlink symlink m", unlink(P("m")));
    show("create file h, then a plain file ._h", mk(P("h")) | mk(P("._h")));
    show("unlink h (the ._h made as a file)", unlink(P("h")));
    show("unlink ._h", unlink(P("._h")));
    show("rmdir a", rmdir(a));
    show("rmdir b", rmdir(b));
    return 0;
}
