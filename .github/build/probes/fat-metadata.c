// Probe for macos-runner-diagnostics.yml: replays, one call at a time, what zen's copyNewFile() and copyDirectoryAttributes()
// do to a target on a volume (zen/file_access.cpp, zen/file_io.cpp), and prints after each call whether an AppleDouble "._"
// file appeared and which extended attributes the target has; also prints zen's volume capability answer (volumeSupports()).
//   fat-metadata <source file> <source folder> <folder on the volume>
// Changes nothing outside <folder on the volume>.
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <copyfile.h>
#include <time.h>
#include <unistd.h>

static const char* g_dir;

static void show(const char* step, const char* item)
{
    char names[4096] = "", list[4096] = "";
    DIR* d = opendir(g_dir);
    for (struct dirent* e; d && (e = readdir(d));)
        if (strncmp(e->d_name, "._", 2) == 0) { strlcat(names, e->d_name, sizeof(names)); strlcat(names, " ", sizeof(names)); }
    if (d) closedir(d);
    ssize_t n = item ? listxattr(item, list, sizeof(list) - 1, XATTR_NOFOLLOW) : 0;
    for (ssize_t i = 0; i < n; ++i) if (list[i] == '\0') list[i] = ' ';
    printf("  after %-40s AppleDouble: [%s] attributes: [%s]\n", step, names, n > 0 ? list : "");
}

static void gate(const char* what, const struct statfs* st)
{
    struct { uint32_t length; vol_capabilities_attr_t caps; } __attribute__((aligned(4), packed)) buf = {0};
    struct attrlist al = { .bitmapcount = ATTR_BIT_MAP_COUNT, .volattr = ATTR_VOL_INFO | ATTR_VOL_CAPABILITIES };
    int rv = getattrlist(st->f_mntonname, &al, &buf, sizeof(buf), 0);
    const uint32_t x = VOL_CAP_INT_EXTENDED_ATTR, s = VOL_CAP_INT_EXTENDED_SECURITY;
    printf("  gate (%s): fs %s, fsid %08x:%08x, getattrlist %d, EXTENDED_ATTR valid %d set %d, EXTENDED_SECURITY valid %d set %d\n",
           what, st->f_fstypename, (unsigned)st->f_fsid.val[0], (unsigned)st->f_fsid.val[1], rv,
           !!(buf.caps.valid[VOL_CAPABILITIES_INTERFACES] & x), !!(buf.caps.capabilities[VOL_CAPABILITIES_INTERFACES] & x),
           !!(buf.caps.valid[VOL_CAPABILITIES_INTERFACES] & s), !!(buf.caps.capabilities[VOL_CAPABILITIES_INTERFACES] & s));
}

int main(int argc, char** argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s <source file> <source folder> <folder on the volume>\n", argv[0]); return 2; }
    const char* src = argv[1]; g_dir = argv[3];
    char dst[2048], dir[2048], forced[2048];
    snprintf(dst, sizeof(dst), "%s/copy.txt", g_dir);
    snprintf(dir, sizeof(dir), "%s/dir", g_dir);
    snprintf(forced, sizeof(forced), "%s/forced.txt", g_dir);
    show("start", NULL);

    // copyNewFile(): source opened read-only with F_RDAHEAD, target created with the source's mode | S_IWUSR
    int in = open(src, O_RDONLY | O_CLOEXEC);
    struct stat si; fstat(in, &si);
    fcntl(in, F_RDAHEAD, 1);
    int out = open(dst, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, (si.st_mode & 0777) | S_IWUSR);
    show("open(O_CREAT|O_EXCL)", dst);
    char buf[65536]; ssize_t r;
    while ((r = read(in, buf, sizeof(buf))) > 0) write(out, buf, (size_t)r);
    show("write", dst);
    struct statfs st; fstatfs(out, &st);
    gate("fstatfs of the target descriptor", &st);
    show("fstatfs + getattrlist", dst);
    struct stat so; fstat(out, &so);
    show("fstat", dst);
    close(out); close(in);
    show("close", dst);
    struct timespec t[2] = { { .tv_sec = time(NULL) }, si.st_mtimespec };
    utimensat(AT_FDCWD, dst, t, 0);
    show("utimensat", dst);

    // createDirectory() + copyDirectoryAttributes()
    mkdir(dir, 0777); // zen::createDirectory(): 0777, umask applies
    show("mkdir", dir);
    struct statfs sd; statfs(dir, &sd);
    gate("statfs of the target folder", &sd);
    int fs = open(argv[2], O_RDONLY | O_DIRECTORY | O_CLOEXEC), fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    show("open folders", dir);
    close(fs); close(fd);

    // what fcopyfile(COPYFILE_XATTR) does there, which the gate is meant to prevent (on a separate file)
    int a = open(src, O_RDONLY), b = open(forced, O_CREAT | O_EXCL | O_WRONLY, 0644);
    int cr = fcopyfile(a, b, NULL, COPYFILE_XATTR);
    close(a); close(b);
    show(cr == 0 ? "fcopyfile(COPYFILE_XATTR) on forced.txt" : "fcopyfile(COPYFILE_XATTR) failed", forced);
    return 0;
}
