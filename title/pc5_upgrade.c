/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5 updates itself (pc5_upgrade.h). */
#include "pc5_upgrade.h"
#include "pc5_i18n.h"
#include "pc5_json.h"
#include "pc5_library.h"
#include "pc5_update.h"
#include "pc5_zip.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* stb_image's raw deflate decoder (pc5_stb.c builds it for PNG). */
int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

const char *const pc5_upgrade_drives[] = { "/data", "/mnt/ext0", "/mnt/ext1", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2",
                                           "/mnt/usb3", "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7", NULL };

enum {
    ENTRY_MAX = 512u << 20,          /* the largest file a zip may hold */
    MARGIN = 64u << 20,              /* room left on the drive */
};

#define PREFIX PC5_TITLE_ID "/"
/* In the work folder: what makes it PC5's (nothing else there is deleted), the
 * release's names, and the journal that says files are being put in place. */
#define WORK_NAME "pc5-update"
#define MARKER "pc5-update.txt"
#define LIST "files.txt"
#define JOURNAL "replacing.txt"

static void note(const Pc5UpgradeConfig *config, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void note(const Pc5UpgradeConfig *config, const char *format, ...)
{
    char line[PC5_PATH + 200];
    va_list args;
    if (!config->log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    config->log(config->log_context, line);
}

static const char *const *drives_of(const Pc5UpgradeConfig *config)
{
    return config->drives ? config->drives : pc5_upgrade_drives;
}

/* ---- PC5's folder ------------------------------------------------------------------------- */

/* 1 when the two files hold the same bytes. */
static int same_file(const char *a, const char *b)
{
    struct stat sa, sb;
    int fa = -1, fb = -1, same = 0;
    uint8_t *one = NULL, *two = NULL;
    enum { CHUNK = 1 << 20 };

    if (stat(a, &sa) || stat(b, &sb) || !S_ISREG(sa.st_mode) || !S_ISREG(sb.st_mode) || sa.st_size != sb.st_size)
        return 0;
    if ((fa = open(a, O_RDONLY)) < 0 || (fb = open(b, O_RDONLY)) < 0 || !(one = pc5_alloc(CHUNK)) ||
        !(two = pc5_alloc(CHUNK)))
        goto out;
    for (;;) {
        ssize_t x = read(fa, one, CHUNK), y = x > 0 ? read(fb, two, (size_t)x) : x;
        if (x < 0 || x != y) goto out;
        if (x == 0) break;
        if (memcmp(one, two, (size_t)x)) goto out;
    }
    same = 1;
out:
    if (fa >= 0) close(fa);
    if (fb >= 0) close(fb);
    pc5_free(one);
    pc5_free(two);
    return same;
}

/* The titleId in folder/sce_sys/param.json is PC5's. */
static int is_pc5_title(const char *folder)
{
    char path[PC5_PATH], id[32];
    Pc5Buf text = { 0 };
    Pc5Json json = { 0 };
    int yes = 0;

    if (!pc5_format(path, sizeof(path), "%s/sce_sys/param.json", folder) && !pc5_read_file(path, &text, 256 * 1024) &&
        !pc5_json_parse(&json, (const char *)text.data, text.len, 4096) &&
        !pc5_json_text(&json, pc5_json_get(&json, 0, "titleId"), id, sizeof(id)))
        yes = !strcmp(id, PC5_TITLE_ID);
    pc5_json_free(&json);
    pc5_buf_free(&text);
    return yes;
}

/* Matches so far, the first two kept. */
typedef struct Found {
    int count;
    char *folder, *other;
    size_t size, other_size;
} Found;

/* The folders in parent (one level) that are the running PC5. */
static void find_in(const char *parent, const char *app, int top, Found *found)
{
    char candidate[PC5_PATH], eboot[PC5_PATH], running[PC5_PATH];
    DIR *dir = opendir(parent);
    struct dirent *entry;

    if (!dir || pc5_format(running, sizeof(running), "%s/eboot.bin", app)) {
        if (dir) closedir(dir);
        return;
    }
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        /* At the top of a drive, the folders the scan looks into anyway, and the update's work folder. */
        if (top && (!strcmp(entry->d_name, "homebrew") || !strcmp(entry->d_name, "etaHEN") ||
                    !strcmp(entry->d_name, WORK_NAME)))
            continue;
        if (pc5_join(candidate, sizeof(candidate), parent, entry->d_name) || !pc5_is_dir(candidate) ||
            pc5_format(eboot, sizeof(eboot), "%s/eboot.bin", candidate) || !pc5_is_file(eboot) ||
            !is_pc5_title(candidate) || !same_file(eboot, running))
            continue;
        if (found->count == 0 && pc5_format(found->folder, found->size, "%s", candidate)) continue;
        if (found->count == 1 && found->other) pc5_copy(found->other, found->other_size, candidate);
        found->count++;
    }
    closedir(dir);
}

int pc5_upgrade_find(const Pc5UpgradeConfig *config, char *folder, size_t size, char *other, size_t other_size)
{
    char parent[PC5_PATH];
    Found found = { 0, folder, other, size, other_size };

    for (const char *const *drive = drives_of(config); *drive; drive++) {
        if (!pc5_format(parent, sizeof(parent), "%s/homebrew", *drive)) find_in(parent, config->app, 0, &found);
        if (!pc5_format(parent, sizeof(parent), "%s/etaHEN/games", *drive)) find_in(parent, config->app, 0, &found);
        /* ShadowMountPlus looks at the top of every drive but /data. */
        if (strcmp(*drive, "/data")) find_in(*drive, config->app, 1, &found);
    }
    return found.count == 1 ? 0 : found.count ? -2 : -1;
}

/* A drive's work folder: root/pc5/update on /data, <drive>/pc5-update elsewhere. */
static int drive_work(const Pc5UpgradeConfig *config, const char *drive, char *work, size_t size)
{
    int r = !strcmp(drive, "/data") && config->root ? pc5_format(work, size, "%s/pc5/update", config->root)
                                                    : pc5_format(work, size, "%s/" WORK_NAME, drive);
    if (r) work[0] = 0;   /* never a cut-short path: it is deleted later */
    return r;
}

/* The drive folder is on (one of config's), and the rest of its path. */
static const char *drive_of(const Pc5UpgradeConfig *config, const char *folder, const char **rest)
{
    for (const char *const *drive = drives_of(config); *drive; drive++) {
        size_t n = strlen(*drive);
        if (strncmp(folder, *drive, n) || folder[n] != '/' || !folder[n + 1]) continue;
        *rest = folder + n + 1;
        return *drive;
    }
    return NULL;
}

/* One path inside the other (or the same). */
static int overlap(const char *a, const char *b)
{
    size_t x = strlen(a), y = strlen(b), n = x < y ? x : y;
    return !strncmp(a, b, n) && (x == y || (x < y ? b[n] : a[n]) == '/');
}

/* A work folder PC5 made (its marker in it). */
static int is_work(const char *work)
{
    char marker[PC5_PATH];
    return !pc5_format(marker, sizeof(marker), "%s/" MARKER, work) && pc5_is_file(marker);
}

/* A folder's entries are on the disk (a crash then finds what was renamed into it). */
static void sync_folder(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        (void)fsync(fd);
        close(fd);
    }
}

static int delete_tree(const char *path)
{
    Pc5Remove remove = { 0 };
    int failed;
    if (!pc5_exists(path)) return 0;
    if (pc5_tree_delete_begin(&remove, path)) { pc5_remove_end(&remove); return -1; }
    while (pc5_remove_step(&remove, 1000)) {
    }
    failed = remove.failed;
    pc5_remove_end(&remove);
    return failed || pc5_exists(path) ? -1 : 0;
}

/* ---- the zip -------------------------------------------------------------------------------- */

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int read_at(int fd, void *data, size_t size, uint64_t offset)
{
    for (size_t got = 0; got < size;) {
        ssize_t n = pread(fd, (uint8_t *)data + got, size - got, (off_t)(offset + got));
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static int write_all(int fd, const void *data, size_t size)
{
    for (size_t done = 0; done < size;) {
        ssize_t n = write(fd, (const uint8_t *)data + done, size - done);
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

/* A name inside the app: no empty, "." or ".." parts, no backslashes, not absolute. */
static int safe_name(const char *name)
{
    if (!*name || *name == '/' || strchr(name, '\\')) return 0;
    for (const char *part = name; *part;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if (n == 0 || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (!end) break;
        part = end + 1;
    }
    return 1;
}

/* Folders base/rel's parts, made 0777 as they are made ("" for none). */
static int make_folders(const char *base, const char *rel)
{
    char path[PC5_PATH];
    size_t at;

    if (!rel[0]) return pc5_is_dir(base) ? 0 : -1;
    if (pc5_format(path, sizeof(path), "%s/%s", base, rel)) return -1;
    at = strlen(base) + 1;
    for (char *slash = strchr(path + at, '/');; slash = strchr(slash + 1, '/')) {
        if (slash) *slash = 0;
        if (!pc5_is_dir(path)) {
            if (mkdir(path, 0777) && !(errno == EEXIST && pc5_is_dir(path))) return -1;
            (void)chmod(path, 0777);
        }
        if (!slash) return 0;
        *slash = '/';
    }
}

typedef struct Entry {
    uint32_t crc, packed, size, local;
    uint16_t method, name_length;
    const char *name;            /* in the directory: not 0-terminated */
} Entry;

int pc5_unzip(const char *path, const char *prefix, const char *folder, atomic_uint_fast64_t *done,
              atomic_uint_fast64_t *total, const atomic_int *cancel, uint64_t room, Pc5Buf *names, char *error,
              size_t error_size)
{
    int fd = -1, result = -1;
    struct stat st;
    uint8_t tail[65557], *directory = NULL, *in = NULL, *out = NULL;
    Entry *entries = NULL;
    uint64_t sum = 0, unpacked = 0;
    size_t prefix_length = strlen(prefix);
    uint32_t count = 0;

    error[0] = 0;
    if (done) atomic_store(done, 0);
    if (names) names->len = 0;
    if (!prefix_length || prefix[prefix_length - 1] != '/') {
        snprintf(error, error_size, N_("PC5 can't unpack this zip."));
        return -1;
    }
    if ((fd = open(path, O_RDONLY)) < 0 || fstat(fd, &st) || st.st_size < 22) {
        snprintf(error, error_size, N_("The download can't be read."));
        goto out;
    }
    /* The end of the central directory: within the last 64 KiB and its 22 bytes. */
    {
        uint64_t size = (uint64_t)st.st_size, take = size < sizeof(tail) ? size : sizeof(tail);
        int64_t at = -1;
        if (read_at(fd, tail, (size_t)take, size - take)) goto bad;
        for (int64_t i = (int64_t)take - 22; i >= 0; i--)
            if (le32(tail + i) == 0x06054b50u) { at = i; break; }
        if (at < 0) goto bad;
        const uint8_t *end = tail + at;
        uint64_t directory_size = le32(end + 12), directory_at = le32(end + 16), end_at = size - take + (uint64_t)at;
        count = le16(end + 10);
        if (count == 0xffff || le32(end + 16) == 0xffffffffu) {
            snprintf(error, error_size, N_("The download is a zip PC5 can't read (ZIP64)."));
            goto out;
        }
        if (!count || directory_at + directory_size > end_at || directory_size > (64u << 20)) goto bad;
        if (!(directory = pc5_alloc((size_t)directory_size + 1)) || !(entries = pc5_alloc(sizeof(Entry) * count)))
            goto memory;
        if (read_at(fd, directory, (size_t)directory_size, directory_at)) goto bad;
        /* Every entry first: its name, its method, its sizes. Nothing is written yet. */
        const uint8_t *p = directory, *limit = directory + directory_size;
        for (uint32_t i = 0; i < count; i++) {
            Entry *e = &entries[i];
            if (p + 46 > limit || le32(p) != 0x02014b50u) goto bad;
            uint16_t flags = le16(p + 8), extra = le16(p + 30), comment = le16(p + 32);
            e->method = le16(p + 10);
            e->crc = le32(p + 16);
            e->packed = le32(p + 20);
            e->size = le32(p + 24);
            e->name_length = le16(p + 28);
            e->local = le32(p + 42);
            e->name = (const char *)p + 46;
            if (p + 46 + e->name_length + extra + comment > limit) goto bad;
            char name[PC5_PATH];
            if (e->name_length >= sizeof(name)) goto bad;
            memcpy(name, e->name, e->name_length);
            name[e->name_length] = 0;
            if ((flags & 1) || (e->method != 0 && e->method != 8) || e->size > ENTRY_MAX || e->packed > ENTRY_MAX ||
                (e->method == 0 && e->packed != e->size) || (uint64_t)e->local + 30 > end_at ||
                strncmp(name, prefix, prefix_length) || memchr(name, 0, e->name_length) || strchr(name, '\n') ||
                (name[prefix_length] && !safe_name(name + prefix_length))) {
                snprintf(error, error_size, N_("The download holds a file PC5 won't unpack: %.120s"), name);
                goto out;
            }
            sum += e->size;
            p += 46 + e->name_length + extra + comment;
        }
    }
    if (total) atomic_store(total, sum);
    if (sum > room) {
        char need[32];
        pc5_size_text(sum, need, sizeof(need));
        snprintf(error, error_size, N_("There isn't room on the drive to unpack it: it needs %s."), need);
        goto out;
    }
    if (pc5_mkdirs(folder)) {
        snprintf(error, error_size, N_("The folder to unpack it in can't be made."));
        goto out;
    }
    for (uint32_t i = 0; i < count; i++) {
        const Entry *e = &entries[i];
        char rel[PC5_PATH], target[PC5_PATH];
        uint8_t local[30];

        if (cancel && atomic_load(cancel)) { snprintf(error, error_size, N_("Cancelled.")); goto out; }
        memcpy(rel, e->name + prefix_length, e->name_length - prefix_length);
        rel[e->name_length - prefix_length] = 0;
        if (!rel[0]) continue;                          /* the prefix's own folder */
        if (names && pc5_buf_printf(names, "%s\t%u\n", rel, (unsigned)e->size)) goto memory;
        size_t n = strlen(rel);
        if (rel[n - 1] == '/') {                        /* a folder */
            rel[n - 1] = 0;
            if (make_folders(folder, rel)) goto write_failed;
            continue;
        }
        char *slash = strrchr(rel, '/');
        if (slash) *slash = 0;
        if (make_folders(folder, slash ? rel : "")) goto write_failed;
        if (slash) *slash = '/';
        if (pc5_join(target, sizeof(target), folder, rel)) goto bad;
        /* Its data follows its local header, whose name and extra field may differ in length from the directory's. */
        if (read_at(fd, local, sizeof(local), e->local) || le32(local) != 0x04034b50u) goto bad;
        uint64_t data_at = (uint64_t)e->local + 30 + le16(local + 26) + le16(local + 28);
        if (data_at + e->packed > (uint64_t)st.st_size) goto bad;
        if (!(in = pc5_alloc(e->packed ? e->packed : 1))) goto memory;
        if (read_at(fd, in, e->packed, data_at)) goto bad;
        const uint8_t *data = in;
        if (e->method == 8) {
            if (!(out = pc5_alloc(e->size ? e->size : 1))) goto memory;
            if (e->size && stbi_zlib_decode_noheader_buffer((char *)out, (int)e->size, (const char *)in, (int)e->packed) !=
                               (int)e->size)
                goto damaged;
            data = out;
        }
        if (pc5_crc32(data, e->size) != e->crc) goto damaged;
        int file = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (file < 0) goto write_failed;
        if (write_all(file, data, e->size) || fsync(file)) { close(file); goto write_failed; }
        if (close(file)) goto write_failed;
        (void)chmod(target, 0777);
        pc5_free(in);
        pc5_free(out);
        in = out = NULL;
        unpacked += e->size;
        if (done) atomic_store(done, unpacked);
    }
    result = 0;
    goto out;
damaged:
    snprintf(error, error_size, N_("The download is damaged (a file in it doesn't match its checksum)."));
    goto out;
write_failed:
    snprintf(error, error_size, N_("Files can't be written on the drive (%s)."), strerror(errno));
    goto out;
memory:
    snprintf(error, error_size, N_("PC5 ran out of memory unpacking it."));
    goto out;
bad:
    if (!error[0]) snprintf(error, error_size, N_("The download isn't a zip PC5 can read."));
out:
    if (fd >= 0) close(fd);
    pc5_free(directory);
    pc5_free(entries);
    pc5_free(in);
    pc5_free(out);
    return result;
}

/* ---- putting the files in place ----------------------------------------------------------- */

enum { NAME_MAX_LENGTH = 800 };   /* a name in the release, short enough for every path made from it */

/* The release's names (files.txt, "name<TAB>size" a line each, folders ending in '/'). */
typedef struct Name {
    const char *name;
    uint64_t size;
} Name;

typedef struct Release {
    Pc5Buf text, lower;            /* the list as read, and its names lower-cased (exFAT ignores case) */
    Name *names;                   /* sorted by name */
    size_t count;
    char **lowered;                /* the lower-cased names, sorted */
    char **folders;                /* every folder that holds a name, below the top, each once */
    size_t folder_count;
    Pc5Buf folder_text;
} Release;

static int name_compare(const void *a, const void *b)
{
    return strcmp(((const Name *)a)->name, ((const Name *)b)->name);
}

static int text_compare(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void release_free(Release *r)
{
    pc5_buf_free(&r->text);
    pc5_buf_free(&r->lower);
    pc5_buf_free(&r->folder_text);
    pc5_free(r->names);
    pc5_free(r->lowered);
    pc5_free(r->folders);
    memset(r, 0, sizeof(*r));
}

static int release_load(const char *work, Release *r)
{
    char path[PC5_PATH];
    size_t lines = 0, folders = 0;

    memset(r, 0, sizeof(*r));
    if (pc5_format(path, sizeof(path), "%s/" LIST, work) || pc5_read_file(path, &r->text, 16u << 20) || !r->text.len ||
        r->text.data[r->text.len - 1] != '\n')
        goto fail;
    for (size_t i = 0; i < r->text.len; i++) lines += r->text.data[i] == '\n';
    if (!(r->names = pc5_alloc(sizeof(Name) * lines)) || !(r->lowered = pc5_alloc(sizeof(char *) * lines)))
        goto fail;
    /* Every line a name and its size: anything else and the list isn't used. */
    for (char *line = (char *)r->text.data, *next; *line; line = next) {
        char *tab = strchr(line, '\t'), *end;
        next = strchr(line, '\n');
        *next++ = 0;
        if (!tab || tab > next || tab == line || (size_t)(tab - line) >= NAME_MAX_LENGTH) goto fail;
        *tab = 0;
        unsigned long long size = strtoull(tab + 1, &end, 10);
        if (end == tab + 1 || *end || !safe_name(line) || strchr(line, '\\')) goto fail;
        r->names[r->count++] = (Name){ line, size };
    }
    if (r->count != lines) goto fail;
    qsort(r->names, r->count, sizeof(Name), name_compare);
    /* The same text lower-cased: the names at the same places. */
    if (pc5_buf_put(&r->lower, r->text.data, r->text.len)) goto fail;
    for (size_t i = 0; i < r->lower.len; i++) r->lower.data[i] = (uint8_t)pc5_lower(r->lower.data[i]);
    for (size_t i = 0; i < r->count; i++)
        r->lowered[i] = (char *)r->lower.data + (r->names[i].name - (const char *)r->text.data);
    qsort(r->lowered, r->count, sizeof(char *), text_compare);
    /* Every folder holding a name, each once: "pc5", "pc5/prefix", ... */
    for (size_t i = 0; i < r->count; i++)
        for (const char *slash = strchr(r->names[i].name, '/'); slash && slash[1]; slash = strchr(slash + 1, '/')) {
            if (pc5_buf_put(&r->folder_text, r->names[i].name, (size_t)(slash - r->names[i].name)) ||
                pc5_buf_put(&r->folder_text, "", 1))
                goto fail;
            folders++;
        }
    if (!(r->folders = pc5_alloc(sizeof(char *) * (folders + 1)))) goto fail;
    for (size_t at = 0, n = 0; n < folders; n++) {
        r->folders[n] = (char *)r->folder_text.data + at;
        at += strlen(r->folders[n]) + 1;
    }
    qsort(r->folders, folders, sizeof(char *), text_compare);
    for (size_t i = 0; i < folders; i++)
        if (!r->folder_count || strcmp(r->folders[r->folder_count - 1], r->folders[i]))
            r->folders[r->folder_count++] = r->folders[i];
    return r->count ? 0 : -1;
fail:
    release_free(r);
    return -1;
}

/* name (lower-cased) is one of the release's, or a folder holding one: bytes compared unsigned, as strcmp does. */
static int release_has_lower(const Release *r, const char *lower)
{
    size_t n = strlen(lower), lo = 0, hi = r->count;
    if (bsearch(&lower, r->lowered, r->count, sizeof(char *), text_compare)) return 1;
    /* the first name not before lower + "/" */
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strncmp(r->lowered[mid], lower, n);
        if (c < 0 || (c == 0 && (unsigned char)r->lowered[mid][n] < (unsigned char)'/')) lo = mid + 1;
        else hi = mid;
    }
    return lo < r->count && !strncmp(r->lowered[lo], lower, n) && r->lowered[lo][n] == '/';
}

/* path moved into trash (deleted with the work folder), under a name not used there yet. */
static int to_trash(const char *path, const char *trash, unsigned *counter)
{
    char aside[PC5_PATH];
    const char *base = strrchr(path, '/');
    if (pc5_mkdirs(trash)) return -1;
    do {
        if (pc5_format(aside, sizeof(aside), "%s/%u-%s", trash, (*counter)++, base ? base + 1 : path)) return -1;
    } while (pc5_exists(aside));
    return rename(path, aside);
}

/* new over old: rename does it at once, so old's name always holds one of them. */
static int place(const char *from, const char *to)
{
    if (!rename(from, to)) return 0;
    if (errno != EEXIST) return -1;
    /* A file system that won't rename over a file. */
    if (unlink(to) && errno != ENOENT) return -1;
    return rename(from, to);
}

/* The folders of rel (a folder's path in folder, below its top) made where missing; a file
 * standing where one of them goes is moved to trash. */
static int make_parents(const char *folder, const char *rel, const char *trash, unsigned *counter)
{
    char path[PC5_PATH];
    size_t at = strlen(folder) + 1;
    struct stat st;

    if (!rel[0]) return 0;
    if (pc5_format(path, sizeof(path), "%s/%s", folder, rel)) return -1;
    for (char *slash = strchr(path + at, '/');; slash = strchr(slash + 1, '/')) {
        if (slash) *slash = 0;
        if (!lstat(path, &st) && !S_ISDIR(st.st_mode) && to_trash(path, trash, counter)) return -1;
        if (!pc5_is_dir(path)) {
            if (mkdir(path, 0777) && !(errno == EEXIST && pc5_is_dir(path))) return -1;
            (void)chmod(path, 0777);
        }
        if (!slash) return 0;
        *slash = '/';
    }
}

/* Before anything moves: every path fits, and nothing at the top of PC5's folder (the
 * player's place) stands in the way of a name of another kind. */
static int check_top(const Release *r, const char *fresh, const char *folder, char *error, size_t error_size)
{
    char path[PC5_PATH], top[PC5_PATH];
    for (size_t i = 0; i < r->count; i++) {
        const char *name = r->names[i].name, *slash = strchr(name, '/');
        if (pc5_join(path, sizeof(path), fresh, name) || pc5_join(path, sizeof(path), folder, name)) {
            snprintf(error, error_size, N_("A path in PC5's folder is too long."));
            return -1;
        }
        pc5_copy(top, sizeof(top), name);
        if (slash) top[slash - name] = 0;
        if (pc5_join(path, sizeof(path), folder, top)) return -1;
        int folder_wanted = slash != NULL;
        if ((folder_wanted && pc5_exists(path) && !pc5_is_dir(path)) || (!folder_wanted && pc5_is_dir(path))) {
            snprintf(error, error_size, N_("PC5's folder has %.100s where the new version has a %s of that name. Move it "
                     "out of the folder and try again."), top, folder_wanted ? "folder" : "file");
            return -1;
        }
    }
    return 0;
}

/* dir (a folder of the release, "pc5/prefix") and the folders above it marked in touched:
 * their entries changed, so they are flushed to the disk. */
static void mark_changed(const Release *r, unsigned char *touched, const char *dir)
{
    char at[PC5_PATH];
    pc5_copy(at, sizeof(at), dir);
    while (at[0]) {
        const char *key = at;
        char **found = bsearch(&key, r->folders, r->folder_count, sizeof(char *), text_compare);
        if (found) touched[found - r->folders] = 1;
        char *slash = strrchr(at, '/');
        if (!slash) break;
        *slash = 0;
    }
}

/* The release's files from fresh into folder, each over the old one, eboot.bin last; then
 * what the release's folders (not the top) no longer have goes to trash, and the folders
 * whose entries changed are flushed (from the update zip a handful, not the app's hundreds). Done again after a stop, it goes on where it was: a file already moved is
 * no longer in fresh and has its size in folder. done counts the files. 0, or -1 with
 * error set. eboot_placed: eboot.bin was placed now. */
static int place_files(const Pc5UpgradeConfig *config, const Release *r, const char *fresh, const char *folder,
                       const char *trash, atomic_uint_fast64_t *done, int *eboot_placed, char *error, size_t error_size)
{
    char from[PC5_PATH], to[PC5_PATH], rel[PC5_PATH];
    unsigned counter = 0, stale = 0, flushed = 0;
    uint64_t placed = 0;
    struct stat st;
    unsigned char *touched = pc5_alloc(r->folder_count + 1);

    *eboot_placed = 0;
    if (!touched) {
        snprintf(error, error_size, N_("PC5 ran out of memory."));
        return -1;
    }
    memset(touched, 0, r->folder_count + 1);
    if (check_top(r, fresh, folder, error, error_size)) goto failed;
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < r->count; i++) {
            const char *name = r->names[i].name;
            size_t n = strlen(name);
            pc5_copy(rel, sizeof(rel), name);
            if (name[n - 1] == '/') {                 /* a folder listed on its own */
                rel[n - 1] = 0;
                if (pass == 0 && strchr(rel, '/') && make_parents(folder, rel, trash, &counter)) goto failed_at;
                if (pass == 0 && !strchr(rel, '/') && make_folders(folder, rel)) goto failed_at;
                if (pass == 0) mark_changed(r, touched, rel);
                continue;
            }
            /* eboot.bin on the second pass, after everything else. */
            if ((pass == 0) == !strcmp(name, "eboot.bin")) continue;
            if (pc5_join(from, sizeof(from), fresh, name) || pc5_join(to, sizeof(to), folder, name)) goto too_long;
            if (!pc5_exists(from)) {
                /* Moved before the stop: it is in place, the size the release says. */
                if (lstat(to, &st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != r->names[i].size) goto missing;
            } else {
                char *slash = strrchr(rel, '/');
                if (slash) {
                    *slash = 0;
                    /* the top folder is checked; below it, a file in a folder's place goes */
                    char *first = strchr(rel, '/');
                    if (first ? make_parents(folder, rel, trash, &counter) : make_folders(folder, rel)) goto failed_at;
                }
                if (slash && pc5_is_dir(to) && to_trash(to, trash, &counter)) goto failed_at;
                if (place(from, to)) goto failed_at;
                if (slash) mark_changed(r, touched, rel);
                if (pass == 1) *eboot_placed = 1;
            }
            placed++;
            if (done) atomic_store(done, placed);
        }
    /* What the release's folders had and it doesn't any more, its case aside (exFAT). */
    for (size_t f = 0; f < r->folder_count; f++) {
        DIR *dir;
        struct dirent *entry;
        if (pc5_join(to, sizeof(to), folder, r->folders[f]) || !(dir = opendir(to))) continue;
        while ((entry = readdir(dir))) {
            char inside[PC5_PATH], path[PC5_PATH];
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            if (pc5_format(inside, sizeof(inside), "%s/%s", r->folders[f], entry->d_name) ||
                pc5_join(path, sizeof(path), to, entry->d_name))
                continue;
            for (char *c = inside; *c; c++) *c = (char)pc5_lower((unsigned char)*c);
            if (release_has_lower(r, inside)) continue;
            if (!to_trash(path, trash, &counter)) {
                stale++;
                touched[f] = 1;
            }
        }
        closedir(dir);
    }
    /* The moves on the disk before the journal goes: every folder whose entries changed (a
     * file moved in or out, a folder made), and the top. A file put in a folder's place, or
     * a folder in a file's, may have changed any: then all of them. */
    for (size_t f = 0; f < r->folder_count; f++)
        if ((touched[f] || counter > stale) && !pc5_join(to, sizeof(to), folder, r->folders[f])) {
            sync_folder(to);
            flushed++;
        }
    sync_folder(folder);
    pc5_free(touched);
    note(config, "PC5 update placed=%llu stale=%u flushed=%u of %zu folders in %s", (unsigned long long)placed, stale,
         flushed, r->folder_count, folder);
    return 0;
too_long:
    snprintf(error, error_size, N_("A path in PC5's folder is too long."));
    goto failed;
missing:
    snprintf(error, error_size, N_("A new file is missing: %.200s."), rel);
    goto failed;
failed_at:
    snprintf(error, error_size, N_("PC5 can't put its new files in place (%.150s: %s)."), rel, strerror(errno));
failed:
    pc5_free(touched);
    return -1;
}

/* The work folder deleted, its marker last: a stop part way leaves it recognisably PC5's. */
static int delete_work(const char *work)
{
    char path[PC5_PATH];
    DIR *dir = opendir(work);
    struct dirent *entry;
    int failed = 0;

    if (!dir) return pc5_exists(work) ? -1 : 0;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, MARKER)) continue;
        if (pc5_join(path, sizeof(path), work, entry->d_name) || delete_tree(path)) failed = 1;
    }
    closedir(dir);
    if (failed || pc5_format(path, sizeof(path), "%s/" MARKER, work) || (unlink(path) && errno != ENOENT) || rmdir(work))
        return -1;
    return 0;
}

/* A finished update's work folder is moved aside (work-old-<n>, a rename: no wait) and
 * deleted on a thread of its own: deleting a big one, file by file, took minutes on the
 * console while the update screen waited. What a restart cuts short is deleted the next
 * time PC5 starts, again on a thread. */
static void *delete_aside_main(void *argument)
{
    char *paths = argument;
    for (char *path = paths; *path; path += strlen(path) + 1) (void)delete_work(path);
    free(paths);
    return NULL;
}

/* Deletes the work folders in paths (each NUL-ended, an empty one last) on a detached
 * thread; here when no thread can start. Takes paths over. */
static void delete_aside(char *paths)
{
    pthread_t thread;
    pthread_attr_t attributes;
    int started = 0;
    if (!paths[0]) { free(paths); return; }
    if (!pthread_attr_init(&attributes)) {
        (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
        started = !pthread_create(&thread, &attributes, delete_aside_main, paths);
        pthread_attr_destroy(&attributes);
    }
    if (!started) delete_aside_main(paths);
}

/* work renamed to work-old-<n> (its marker with it). 0, or -1 when it couldn't be. */
static int set_aside(const char *work, char *aside, size_t size)
{
    for (unsigned n = 1; n < 1000; n++) {
        if (pc5_format(aside, size, "%s-old-%u", work, n)) return -1;
        if (pc5_exists(aside)) continue;
        return rename(work, aside) ? -1 : 0;
    }
    return -1;
}

/* The work folders an earlier update set aside beside work (work-old-<n>, PC5's marker in
 * them), as a list for delete_aside; NULL when there are none. */
static char *asides_of(const char *work)
{
    char parent[PC5_PATH], path[PC5_PATH];
    const char *slash = strrchr(work, '/'), *base;
    size_t used = 0, room = 0, prefix;
    char *list = NULL;
    DIR *dir;
    struct dirent *entry;
    if (!slash || slash == work || (size_t)(slash - work) >= sizeof(parent)) return NULL;
    memcpy(parent, work, (size_t)(slash - work));
    parent[slash - work] = 0;
    base = slash + 1;
    prefix = strlen(base);
    if (!(dir = opendir(parent))) return NULL;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, base, prefix) || strncmp(entry->d_name + prefix, "-old-", 5) ||
            pc5_join(path, sizeof(path), parent, entry->d_name) || !is_work(path))
            continue;
        size_t n = strlen(path) + 1;
        if (used + n + 1 > room) {
            size_t want = (room ? room * 2 : 1024) + n;
            char *grown = realloc(list, want);
            if (!grown) break;
            list = grown;
            room = want;
        }
        memcpy(list + used, path, n);
        used += n;
        list[used] = 0;
    }
    closedir(dir);
    return list;
}

/* Deletes work: moved aside and deleted on a thread, or here when it can't be moved. */
static int delete_work_soon(const char *work)
{
    char aside[PC5_PATH];
    if (set_aside(work, aside, sizeof(aside))) return delete_work(work);
    size_t n = strlen(aside) + 1;
    char *paths = malloc(n + 1);
    if (!paths) return 0;   /* deleted the next time PC5 starts */
    memcpy(paths, aside, n);
    paths[n] = 0;
    delete_aside(paths);
    return 0;
}

/* The journal: an earlier update is being put in place there. */
static int has_journal(const char *work)
{
    char journal[PC5_PATH];
    return !pc5_format(journal, sizeof(journal), "%s/" JOURNAL, work) && pc5_is_file(journal);
}

/* A file's size and CRC-32 ("size crc"), for the journal: which eboot.bin is which. */
static int file_mark(const char *path, char *out, size_t size)
{
    Pc5Buf data = { 0 };
    int r = pc5_read_file(path, &data, 64u << 20) ? -1
            : pc5_format(out, size, "%zu %08x", data.len, (unsigned)pc5_crc32(data.data, data.len));
    pc5_buf_free(&data);
    return r;
}

/* The console's copies of the app's sce_sys, which ShadowMountPlus made once, when it
 * first registered PC5: brought up to date as ProsperoStore does. Best effort. */
/* data written to path unless path holds it already (each write is flushed to the disk, and
 * an update changes few of these files). 0, or -1. */
static int write_if_changed(const char *path, const Pc5Buf *data, int *same)
{
    Pc5Buf old = { 0 };
    int equal = !pc5_read_file(path, &old, 32u << 20) && old.len == data->len && !memcmp(old.data, data->data, data->len);
    pc5_buf_free(&old);
    *same = equal;
    return equal ? 0 : pc5_write_file(path, data->data, data->len);
}

static void refresh_registered(const Pc5UpgradeConfig *config, const char *folder)
{
    char source[PC5_PATH], copy[PC5_PATH], from[PC5_PATH], to[PC5_PATH];
    int copied = 0, failed = 0, same = 0, unchanged = 0;
    DIR *dir;
    struct dirent *entry;

    if (!config->user || pc5_format(source, sizeof(source), "%s/sce_sys", folder)) return;
    for (int c = 0; c < 2; c++) {
        if ((c == 0 ? pc5_format(copy, sizeof(copy), "%s/appmeta/%s", config->user, PC5_TITLE_ID)
                    : pc5_format(copy, sizeof(copy), "%s/app/%s/sce_sys", config->user, PC5_TITLE_ID)) ||
            !pc5_is_dir(copy) || !(dir = opendir(source)))
            continue;
        while ((entry = readdir(dir))) {
            Pc5Buf data = { 0 };
            if (entry->d_name[0] == '.' || pc5_join(from, sizeof(from), source, entry->d_name) || !pc5_is_file(from) ||
                pc5_join(to, sizeof(to), copy, entry->d_name))
                continue;
            if (pc5_read_file(from, &data, 32u << 20) || write_if_changed(to, &data, &same)) failed++;
            else if (same) unchanged++;
            else copied++;
            pc5_buf_free(&data);
        }
        closedir(dir);
    }
    if (!pc5_format(to, sizeof(to), "%s/app/%s/icon0.png", config->user, PC5_TITLE_ID) && pc5_is_file(to) &&
        !pc5_format(from, sizeof(from), "%s/icon0.png", source)) {
        Pc5Buf data = { 0 };
        if (pc5_read_file(from, &data, 32u << 20) || write_if_changed(to, &data, &same)) failed++;
        else if (same) unchanged++;
        else copied++;
        pc5_buf_free(&data);
    }
    note(config, "PC5 update registration copies=%d unchanged=%d failed=%d", copied, unchanged, failed);
}

/* ---- the release's list of its files ---------------------------------------------------------- */

/* pc5/manifest.txt (tools/make-app.py): every file of a release but itself, a line each,
 * "name<TAB>size<TAB>sha256" after a first line that is a '#' comment. The installed one
 * says what the console has; the new one, what the update zip's release is. */
#define MANIFEST "pc5/manifest.txt"

typedef struct Listed {
    const char *name;
    uint64_t size;
    const char *sha256;            /* 64 lower-case hex digits */
} Listed;

typedef struct Manifest {
    Pc5Buf text;
    Listed *files;                 /* sorted by name */
    size_t count;
} Manifest;

static int listed_compare(const void *a, const void *b)
{
    return strcmp(((const Listed *)a)->name, ((const Listed *)b)->name);
}

static void manifest_free(Manifest *m)
{
    pc5_buf_free(&m->text);
    pc5_free(m->files);
    memset(m, 0, sizeof(*m));
}

/* The list at path. 0; -1 when it is missing or isn't one: a line that isn't a name, a size
 * and a SHA-256, a name that isn't safe inside the app, a name twice, or the list itself. */
static int manifest_load(const char *path, Manifest *m)
{
    size_t lines = 0;
    char *line;

    memset(m, 0, sizeof(*m));
    if (pc5_read_file(path, &m->text, 16u << 20) || !m->text.len || m->text.data[m->text.len - 1] != '\n') goto fail;
    for (size_t i = 0; i < m->text.len; i++) lines += m->text.data[i] == '\n';
    if (memchr(m->text.data, 0, m->text.len) || !(m->files = pc5_alloc(sizeof(Listed) * lines))) goto fail;
    line = (char *)m->text.data;
    if (*line == '#') line = strchr(line, '\n') + 1;   /* what the file is */
    for (char *next; *line; line = next) {
        char *size = strchr(line, '\t'), *sha = size ? strchr(size + 1, '\t') : NULL, *end;
        next = strchr(line, '\n');
        *next++ = 0;
        if (!size || !sha || size == line || (size_t)(size - line) >= NAME_MAX_LENGTH || strlen(sha + 1) != 64 ||
            strspn(sha + 1, "0123456789abcdef") != 64)
            goto fail;
        *size = *sha = 0;
        unsigned long long bytes = strtoull(size + 1, &end, 10);
        if (end == size + 1 || *end || size[1] == '-' || !safe_name(line) || size[-1] == '/' || !strcmp(line, MANIFEST))
            goto fail;
        m->files[m->count++] = (Listed){ line, bytes, sha + 1 };
    }
    if (!m->count) goto fail;
    qsort(m->files, m->count, sizeof(Listed), listed_compare);
    for (size_t i = 1; i < m->count; i++)
        if (!strcmp(m->files[i - 1].name, m->files[i].name)) goto fail;
    return 0;
fail:
    manifest_free(m);
    return -1;
}

static const Listed *manifest_find(const Manifest *m, const char *name)
{
    Listed key = { name, 0, NULL };
    return bsearch(&key, m->files, m->count, sizeof(Listed), listed_compare);
}

/* A file's SHA-256 as lower-case hex. 0, or -1 when it can't be read. */
static int file_sha256(const char *path, char hex[65])
{
    enum { CHUNK = 1 << 20 };
    uint8_t *chunk = pc5_alloc(CHUNK), digest[32];
    Pc5Sha256 sha;
    int fd = open(path, O_RDONLY), result = -1;
    ssize_t n;

    if (fd < 0 || !chunk) goto out;
    pc5_sha256_init(&sha);
    while ((n = read(fd, chunk, CHUNK)) > 0) pc5_sha256_update(&sha, chunk, (size_t)n);
    if (n < 0) goto out;
    pc5_sha256_final(&sha, digest);
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    result = 0;
out:
    if (fd >= 0) close(fd);
    pc5_free(chunk);
    return result;
}

/* ---- the update ------------------------------------------------------------------------------- */

typedef struct Saving {
    Pc5Upgrade *upgrade;
    int fd;
    Pc5Sha256 sha;
    uint64_t got, expected;
} Saving;

static int saving_begin(void *context, uint64_t length)
{
    Saving *s = context;
    if (length && length != s->expected) {
        note(&s->upgrade->config, "PC5 update the server's length %llu isn't the release's %llu",
             (unsigned long long)length, (unsigned long long)s->expected);
        return -1;
    }
    return 0;
}

static int saving_write(void *context, const void *data, size_t size)
{
    Saving *s = context;
    if (s->got + size > s->expected || write_all(s->fd, data, size)) return -1;
    pc5_sha256_update(&s->sha, data, size);
    s->got += size;
    atomic_store(&s->upgrade->done, s->got);
    return 0;
}

static int saving_stop(void *context)
{
    Saving *s = context;
    return atomic_load(&s->upgrade->cancel);
}

static int fail(Pc5Upgrade *upgrade, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int fail(Pc5Upgrade *upgrade, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(upgrade->error, sizeof(upgrade->error), format, args);
    va_end(args);
    return -1;
}

/* The new version has PC5's eboot, fonts and param.json (and is PC5's title). With the
 * update zip, installed (PC5's folder) gives each the zip doesn't have: it is the same
 * file (the lists say so). NULL for the whole app's zip: all of them in fresh. */
static int check_new(const char *fresh, const char *installed)
{
    static const char *const needed[] = { "eboot.bin", "pc5/fonts.bin", "sce_sys/param.json" };
    char path[PC5_PATH];
    const char *title = fresh;

    for (size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); i++) {
        if (pc5_format(path, sizeof(path), "%s/%s", fresh, needed[i])) return -1;
        if (pc5_is_file(path)) continue;
        if (!installed || pc5_format(path, sizeof(path), "%s/%s", installed, needed[i]) || !pc5_is_file(path))
            return -1;
        if (i == 2) title = installed;   /* param.json: whichever folder has it */
    }
    return is_pc5_title(title) ? 0 : -1;
}

/* The work folder made empty, with its marker. */
static int make_work(const char *work)
{
    char marker[PC5_PATH];
    static const char text[] = "PC5's update work folder. PC5 deletes it when the update is done.\n";
    if (pc5_exists(work) && (!is_work(work) || delete_work(work))) return -1;
    if (pc5_mkdirs(work) || pc5_format(marker, sizeof(marker), "%s/" MARKER, work) ||
        pc5_write_file(marker, text, sizeof(text) - 1))
        return -1;
    return 0;
}

/* url (size bytes, sha256 "" when GitHub lists none) downloaded to part, checked, and renamed
 * to zip; the screens follow it (DOWNLOADING). 0, or -1 with error set ("Cancelled." when
 * asked to stop). */
static int download(Pc5Upgrade *upgrade, const char *url, uint64_t size, const char *sha256, const char *part,
                    const char *zip)
{
    const Pc5UpgradeConfig *config = &upgrade->config;
    Saving saving = { 0 };
    int status = 0, rc;

    atomic_store(&upgrade->total, size);
    atomic_store(&upgrade->done, 0);
    atomic_store(&upgrade->step, PC5_UPGRADE_DOWNLOADING);
    saving.upgrade = upgrade;
    saving.expected = size;
    pc5_sha256_init(&saving.sha);
    if ((saving.fd = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        return fail(upgrade, N_("The download can't be saved (%s)."), strerror(errno));
    {
        Pc5Sink sink = { &saving, saving_begin, saving_write, saving_stop };
        rc = config->http.download(config->http.context, url, &sink, &status);
    }
    if (fsync(saving.fd) && !rc) rc = -1;
    if (close(saving.fd) && !rc) rc = -1;
    note(config, "PC5 update download rc=%d status=%d bytes=%llu", rc, status, (unsigned long long)saving.got);
    if (atomic_load(&upgrade->cancel)) return fail(upgrade, N_("Cancelled."));
    if (rc || status != 200)
        return fail(upgrade, status && status != 200 ? N_("GitHub answered the download with an error (HTTP %d).")
                                                     : N_("The download stopped (%d). Is the console still online?"), status);
    if (saving.got != size) return fail(upgrade, N_("The download was cut short."));
    if (sha256[0]) {
        uint8_t digest[32];
        char hex[65];
        pc5_sha256_final(&saving.sha, digest);
        for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (strcmp(hex, sha256)) return fail(upgrade, N_("The download doesn't match the checksum GitHub lists for it."));
    }
    if (rename(part, zip)) return fail(upgrade, N_("The download can't be saved (%s)."), strerror(errno));
    return 0;
}

/* The update zip (PC5_TITLE_ID "-update.zip"): downloaded and unpacked into fresh when the
 * release has one and PC5's folder has its list of files (MANIFEST), and used when the two
 * make the whole new release: each file in the zip is the one the new list names (its size
 * and SHA-256), and each file the zip doesn't have is in PC5's folder as the new list has it
 * (the installed list's SHA-256, its size on the drive). Then list (files.txt) names every
 * file of the new list, and the list itself, and the update goes on as with the whole app:
 * a file not in fresh is one already in place (place_files). 1 when it is used; 0 when the
 * whole app is to be downloaded instead (why is logged, and what it left is deleted); -1
 * when asked to stop (error set). */
static int use_update_zip(Pc5Upgrade *upgrade, const Pc5Update *update, const char *work, const char *fresh,
                          const char *list)
{
    const Pc5UpgradeConfig *config = &upgrade->config;
    char installed_list[PC5_PATH], zip[PC5_PATH] = "", part[PC5_PATH] = "", new_list[PC5_PATH], unpacked[PC5_PATH] = "",
        path[PC5_PATH], hex[65], why[PC5_ERROR] = "";
    Manifest installed = { 0 }, release = { 0 };
    Pc5Buf names = { 0 }, files = { 0 };
    char **in_zip = NULL;
    size_t zipped = 0;
    unsigned wrong = 0, missing = 0;
    uint64_t room;
    struct stat st;
    int result = 0;

    if (!update->delta_url[0]) return 0;
    /* Only the changes since the release before: from an older PC5 the whole app, without
     * downloading the update zip first. */
    if (!pc5_update_small(update, PC5_VERSION)) {
        note(config, "PC5 update: the update zip holds the changes since %s and this is %s, so the whole app is "
                     "downloaded", update->delta_from[0] ? update->delta_from : "an unnamed release", PC5_VERSION);
        return 0;
    }
    if (pc5_format(installed_list, sizeof(installed_list), "%s/" MANIFEST, upgrade->folder) ||
        manifest_load(installed_list, &installed)) {
        note(config, "PC5 update: PC5's folder has no list of its files (%s), so the whole app is downloaded", MANIFEST);
        return 0;
    }
    if (pc5_format(zip, sizeof(zip), "%s/%s-update.zip", work, PC5_TITLE_ID) ||
        pc5_format(part, sizeof(part), "%s/%s-update.zip.part", work, PC5_TITLE_ID) ||
        pc5_format(new_list, sizeof(new_list), "%s/" MANIFEST, fresh) ||
        pc5_format(unpacked, sizeof(unpacked), "%s/new", work)) {
        /* never a cut-short path: fall_back deletes them */
        note(config, "PC5 update can't use the update zip (a path is too long); downloading the whole app");
        manifest_free(&installed);
        return 0;
    }
    /* The zip, and its files unpacked: about four times the zip. */
    if (!pc5_free_bytes(work, &room) && room < update->delta_size * 4 + MARGIN) {
        snprintf(why, sizeof(why), "there isn't room for it");
        goto fall_back;
    }
    if (atomic_load(&upgrade->cancel)) goto cancelled;
    note(config, "PC5 update using %s-update.zip size=%llu", PC5_TITLE_ID, (unsigned long long)update->delta_size);
    if (download(upgrade, update->delta_url, update->delta_size, update->delta_sha256, part, zip)) {
        if (atomic_load(&upgrade->cancel)) goto cancelled;
        pc5_copy(why, sizeof(why), upgrade->error);
        goto fall_back;
    }
    atomic_store(&upgrade->done, 0);
    atomic_store(&upgrade->total, 0);
    atomic_store(&upgrade->step, PC5_UPGRADE_UNPACKING);
    if (pc5_free_bytes(work, &room)) room = UINT64_MAX;
    else room = room > MARGIN ? room - MARGIN : 0;
    if (pc5_unzip(zip, PREFIX, fresh, &upgrade->done, &upgrade->total, &upgrade->cancel, room, &names, upgrade->error,
                  sizeof(upgrade->error))) {
        if (atomic_load(&upgrade->cancel)) goto cancelled;
        pc5_copy(why, sizeof(why), upgrade->error);
        goto fall_back;
    }
    unlink(zip);
    if (manifest_load(new_list, &release)) {
        snprintf(why, sizeof(why), "it has no list of the new release's files PC5 can read");
        goto fall_back;
    }
    if (!manifest_find(&release, "eboot.bin") || !manifest_find(&release, "pc5/fonts.bin") ||
        !manifest_find(&release, "sce_sys/param.json")) {
        snprintf(why, sizeof(why), "its list of files doesn't have PC5's own");
        goto fall_back;
    }
    /* Each file in the zip is the new release's. */
    if (pc5_buf_terminate(&names) || !(in_zip = pc5_alloc(sizeof(char *) * (names.len / 2 + 1)))) {
        snprintf(why, sizeof(why), "PC5 ran out of memory");
        goto fall_back;
    }
    for (char *line = (char *)names.data, *next; line && *line; line = next) {
        char *tab = strchr(line, '\t');
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!tab) continue;
        *tab = 0;
        if (!line[0] || line[strlen(line) - 1] == '/') continue;   /* a folder */
        if (atomic_load(&upgrade->cancel)) goto cancelled;
        in_zip[zipped++] = line;
        if (!strcmp(line, MANIFEST)) continue;
        const Listed *listed = manifest_find(&release, line);
        if (!listed || strtoull(tab + 1, NULL, 10) != listed->size || pc5_join(path, sizeof(path), fresh, line) ||
            file_sha256(path, hex) || strcmp(hex, listed->sha256))
            wrong++;
    }
    if (wrong) {
        snprintf(why, sizeof(why), "%u files in it aren't the ones its list names", wrong);
        goto fall_back;
    }
    /* Each file it doesn't have is on the console as the new release has it. */
    qsort(in_zip, zipped, sizeof(char *), text_compare);
    for (size_t i = 0; i < release.count; i++) {
        const Listed *file = &release.files[i], *had;
        if (bsearch(&file->name, in_zip, zipped, sizeof(char *), text_compare)) continue;
        if (atomic_load(&upgrade->cancel)) goto cancelled;
        had = manifest_find(&installed, file->name);
        if (!had || had->size != file->size || strcmp(had->sha256, file->sha256) ||
            pc5_join(path, sizeof(path), upgrade->folder, file->name) || lstat(path, &st) || !S_ISREG(st.st_mode) ||
            (uint64_t)st.st_size != file->size)
            missing++;
    }
    if (missing) {
        snprintf(why, sizeof(why), "%u files aren't in the update zip or on the console as it needs them", missing);
        goto fall_back;
    }
    /* The release's names: the new list's, and the list itself. */
    for (size_t i = 0; i < release.count; i++)
        if (pc5_buf_printf(&files, "%s\t%llu\n", release.files[i].name, (unsigned long long)release.files[i].size)) {
            snprintf(why, sizeof(why), "PC5 ran out of memory");
            goto fall_back;
        }
    if (stat(new_list, &st) || pc5_buf_printf(&files, MANIFEST "\t%llu\n", (unsigned long long)st.st_size) ||
        pc5_write_file(list, files.data, files.len)) {
        snprintf(why, sizeof(why), "the work folder can't be written");
        goto fall_back;
    }
    note(config, "PC5 update the update zip holds %zu of the release's %zu files; the rest are in place", zipped,
         release.count + 1);
    result = 1;
    goto out;
cancelled:
    result = fail(upgrade, N_("Cancelled."));
    goto out;
fall_back:
    note(config, "PC5 update can't use the update zip (%s); downloading the whole app", why);
    upgrade->error[0] = 0;
    if (unpacked[0] && delete_tree(unpacked)) note(config, "PC5 update can't delete %s", unpacked);
    if (zip[0]) unlink(zip);
    if (part[0]) unlink(part);
    unlink(list);
    result = 0;
out:
    manifest_free(&installed);
    manifest_free(&release);
    pc5_buf_free(&names);
    pc5_buf_free(&files);
    pc5_free(in_zip);
    return result;
}

static int run(Pc5Upgrade *upgrade, char *work, size_t work_size)
{
    const Pc5UpgradeConfig *config = &upgrade->config;
    Pc5Update update;
    char zip[PC5_PATH], part[PC5_PATH], fresh[PC5_PATH], probe[PC5_PATH], other[PC5_PATH],
        list[PC5_PATH], journal[PC5_PATH], trash[PC5_PATH], text[PC5_PATH + 64];
    const char *drive, *rest = NULL;
    uint64_t room = 0;
    Pc5Buf names = { 0 };
    Release release;
    int rc, eboot_placed, used_update;

    pc5_update_check(&config->http, PC5_VERSION, &update);
    if (update.state == PC5_UPDATE_FAILED || update.state == PC5_UPDATE_UNKNOWN)
        return fail(upgrade, N_("GitHub didn't answer. Is the console online?"));
    if (update.state != PC5_UPDATE_AVAILABLE)
        return fail(upgrade, N_("You already have the newest PC5 (%s)."), PC5_VERSION);
    pc5_copy(upgrade->version, sizeof(upgrade->version), update.version);
    if (!update.asset_url[0] || !config->http.download)
        return fail(upgrade, N_("PC5 %s's release on GitHub has no %s.zip to install. Download it from GitHub instead."),
                    update.version, PC5_TITLE_ID);
    rc = pc5_upgrade_find(config, upgrade->folder, sizeof(upgrade->folder), other, sizeof(other));
    if (rc == -2)
        return fail(upgrade, N_("PC5 is on the console more than once (%.200s and %.200s), so it can't tell which to "
                             "update. Delete the copy you don't use, or install PC5 %s from GitHub as before."),
                    upgrade->folder, other, update.version);
    if (rc)
        return fail(upgrade, N_("PC5's folder wasn't found on the console's drives (in homebrew, etaHEN/games or at the "
                             "top of a drive). Install PC5 %s from GitHub as before."), update.version);
    note(config, "PC5 update version=%s folder=%s size=%llu sha256=%s update_zip=%llu", update.version, upgrade->folder,
         (unsigned long long)update.asset_size, update.asset_sha256[0] ? update.asset_sha256 : "-",
         (unsigned long long)(update.delta_url[0] ? update.delta_size : 0));
    if (!(drive = drive_of(config, upgrade->folder, &rest)) || drive_work(config, drive, work, work_size) ||
        overlap(work, upgrade->folder))
        return fail(upgrade, N_("PC5 can't update itself where it is (%s)."), upgrade->folder);
    if (pc5_format(probe, sizeof(probe), "%s/.pc5-update-test", upgrade->folder) || pc5_write_file(probe, "x", 1))
        return fail(upgrade, N_("PC5 can't write to its folder (%s)."), upgrade->folder);
    unlink(probe);
    if (pc5_is_dir(work) && is_work(work) && has_journal(work)) {
        work[0] = 0;   /* kept: the next start finishes it */
        return fail(upgrade, N_("An earlier update isn't finished. Close PC5 and open it again: it finishes it then."));
    }
    if (make_work(work)) {
        char failed[PC5_PATH];
        pc5_copy(failed, sizeof(failed), work);
        work[0] = 0;   /* not PC5's, or not made: left as it is */
        return fail(upgrade, N_("PC5 can't make its work folder (%s)."), failed);
    }
    if (pc5_format(zip, sizeof(zip), "%s/%s.zip", work, PC5_TITLE_ID) ||
        pc5_format(part, sizeof(part), "%s/%s.zip.part", work, PC5_TITLE_ID) ||
        pc5_format(fresh, sizeof(fresh), "%s/new/%s", work, PC5_TITLE_ID) ||
        pc5_format(list, sizeof(list), "%s/" LIST, work) || pc5_format(journal, sizeof(journal), "%s/" JOURNAL, work) ||
        pc5_format(trash, sizeof(trash), "%s/old", work))
        return fail(upgrade, N_("A path in PC5's folder is too long."));
    /* Only the files that changed, when the release has them in its update zip and the
     * console has the rest; otherwise the whole app. */
    rc = use_update_zip(upgrade, &update, work, fresh, list);
    if (rc < 0) return -1;
    used_update = rc;
    if (!used_update) {
        /* The zip, and its files unpacked: about four times the zip. */
        if (!pc5_free_bytes(work, &room) && room < update.asset_size * 4 + MARGIN) {
            pc5_size_text(update.asset_size * 4 + MARGIN, text, sizeof(text));
            return fail(upgrade, N_("There isn't room on PC5's drive for the update: it needs about %s free."), text);
        }
        if (atomic_load(&upgrade->cancel)) return fail(upgrade, N_("Cancelled."));
        note(config, "PC5 update using %s.zip size=%llu", PC5_TITLE_ID, (unsigned long long)update.asset_size);
        if (download(upgrade, update.asset_url, update.asset_size, update.asset_sha256, part, zip)) return -1;

        atomic_store(&upgrade->done, 0);
        atomic_store(&upgrade->total, 0);
        atomic_store(&upgrade->step, PC5_UPGRADE_UNPACKING);
        if (pc5_free_bytes(work, &room)) room = UINT64_MAX;
        else room = room > MARGIN ? room - MARGIN : 0;
        rc = pc5_unzip(zip, PREFIX, fresh, &upgrade->done, &upgrade->total, &upgrade->cancel, room, &names,
                       upgrade->error, sizeof(upgrade->error));
        if (!rc && pc5_write_file(list, names.data, names.len)) rc = fail(upgrade, N_("The work folder can't be written."));
        pc5_buf_free(&names);
        if (rc) return -1;
        unlink(zip);
    }
    if (check_new(fresh, used_update ? upgrade->folder : NULL))
        return fail(upgrade, N_("The download isn't PC5 (%s is missing something)."), PC5_TITLE_ID);
    if (release_load(work, &release)) return fail(upgrade, N_("The work folder can't be read."));
    /* The unpacked files' folders on the disk before the journal says they are being moved. */
    for (size_t f = 0; f < release.folder_count; f++) {
        char path[PC5_PATH];
        if (!pc5_join(path, sizeof(path), fresh, release.folders[f])) sync_folder(path);
    }
    sync_folder(fresh);
    if (atomic_load(&upgrade->cancel)) {
        release_free(&release);
        return fail(upgrade, N_("Cancelled."));
    }

    /* From here the job is finished, now or by the next start: the journal says where. */
    atomic_store(&upgrade->done, 0);
    atomic_store(&upgrade->total, release.count);
    atomic_store(&upgrade->step, PC5_UPGRADE_REPLACING);
    if (check_top(&release, fresh, upgrade->folder, upgrade->error, sizeof(upgrade->error))) {
        release_free(&release);
        return -1;
    }
    /* The journal: PC5's folder from its drive, the version, and the old and new eboot.bin. */
    {
        char old_eboot[PC5_PATH], new_eboot[PC5_PATH], old_mark[48], new_mark[48];
        if (pc5_format(old_eboot, sizeof(old_eboot), "%s/eboot.bin", upgrade->folder) ||
            pc5_format(new_eboot, sizeof(new_eboot), "%s/eboot.bin", fresh) ||
            /* the update zip without it: the installed one is the new one */
            (!pc5_is_file(new_eboot) && used_update &&
             pc5_format(new_eboot, sizeof(new_eboot), "%s/eboot.bin", upgrade->folder)) ||
            file_mark(old_eboot, old_mark, sizeof(old_mark)) || file_mark(new_eboot, new_mark, sizeof(new_mark)) ||
            pc5_format(text, sizeof(text), "%s\n%s\n%s\n%s\n", rest, update.version, old_mark, new_mark) ||
            pc5_write_file(journal, text, strlen(text))) {
            release_free(&release);
            return fail(upgrade, N_("The work folder can't be written."));
        }
    }
    sync_folder(work);
    rc = place_files(config, &release, fresh, upgrade->folder, trash, &upgrade->done, &eboot_placed, upgrade->error,
                     sizeof(upgrade->error));
    release_free(&release);
    if (rc) {
        char why[PC5_ERROR];
        pc5_copy(why, sizeof(why), upgrade->error);
        upgrade->partial = 1;
        note(config, "PC5 update not finished: %s", why);
        return fail(upgrade, N_("%.120s Close PC5 and open it again: it finishes the update then. If it still can't, "
                             "install PC5 %s from GitHub."), why, update.version);
    }
    refresh_registered(config, upgrade->folder);
    unlink(journal);
    sync_folder(work);
    return 0;
}

void pc5_upgrade_run(Pc5Upgrade *upgrade)
{
    char work[PC5_PATH] = "";

    upgrade->error[0] = 0;
    upgrade->version[0] = 0;
    upgrade->folder[0] = 0;
    upgrade->partial = 0;
    atomic_store(&upgrade->done, 0);
    atomic_store(&upgrade->total, 0);
    atomic_store(&upgrade->step, PC5_UPGRADE_FINDING);
    int result = run(upgrade, work, sizeof(work));
    if (result) note(&upgrade->config, "PC5 update stopped: %s", upgrade->error);
    else atomic_store(&upgrade->step, PC5_UPGRADE_TIDYING);
    /* What the update left (the old files, or everything when it stopped); kept when the
     * next start must finish putting files in place. */
    if (work[0] && is_work(work) && !has_journal(work) && delete_work_soon(work))
        note(&upgrade->config, "PC5 update can't delete %s", work);
    if (!result) note(&upgrade->config, "PC5 update done version=%s", upgrade->version);
    atomic_store(&upgrade->step, !result ? PC5_UPGRADE_DONE
                                 : atomic_load(&upgrade->cancel) && !upgrade->partial ? PC5_UPGRADE_CANCELLED
                                                                                      : PC5_UPGRADE_FAILED);
}

static void *worker(void *argument)
{
    pc5_upgrade_run(argument);
    return NULL;
}

int pc5_upgrade_start(Pc5Upgrade *upgrade, const Pc5UpgradeConfig *config)
{
    pthread_t thread;
    pthread_attr_t attributes;
    int rc;

    upgrade->config = *config;
    upgrade->error[0] = 0;
    upgrade->partial = 0;
    atomic_store(&upgrade->cancel, 0);
    atomic_store(&upgrade->done, 0);
    atomic_store(&upgrade->total, 0);
    atomic_store(&upgrade->step, PC5_UPGRADE_FINDING);
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    /* Room for the zip's 64 KiB end and the paths on the thread's stack. */
    pthread_attr_setstacksize(&attributes, 1u << 20);
    rc = pthread_create(&thread, &attributes, worker, upgrade);
    pthread_attr_destroy(&attributes);
    if (rc) {
        fail(upgrade, N_("PC5 can't start the update (%d)."), rc);
        atomic_store(&upgrade->step, PC5_UPGRADE_FAILED);
        return -1;
    }
    return 0;
}

void pc5_upgrade_cancel(Pc5Upgrade *upgrade)
{
    int step = atomic_load(&upgrade->step);
    if (step >= PC5_UPGRADE_FINDING && step <= PC5_UPGRADE_UNPACKING) atomic_store(&upgrade->cancel, 1);
}

int pc5_upgrade_busy(const Pc5Upgrade *upgrade)
{
    int step = atomic_load(&upgrade->step);
    return step >= PC5_UPGRADE_FINDING && step <= PC5_UPGRADE_TIDYING;
}

/* ---- when PC5 opens -------------------------------------------------------------------------- */

/* The console stopped while files were being put in place: the rest of them now. 0 when
 * the work folder is done with (eboot_placed: eboot.bin was placed now), -1 when it is
 * to be kept for another try. */
static int finish_replacing(const Pc5UpgradeConfig *config, const char *drive, const char *work, int *eboot_placed,
                            char *version, size_t version_size)
{
    char journal[PC5_PATH], fresh[PC5_PATH], trash[PC5_PATH], folder[PC5_PATH], eboot[PC5_PATH], error[PC5_ERROR],
        mark[48];
    char *line[4] = { NULL };
    Pc5Buf text = { 0 };
    Release release;
    int result = -1, n = 0;

    *eboot_placed = 0;
    if (pc5_format(journal, sizeof(journal), "%s/" JOURNAL, work) || pc5_read_file(journal, &text, PC5_PATH + 256) ||
        pc5_format(fresh, sizeof(fresh), "%s/new/%s", work, PC5_TITLE_ID) ||
        pc5_format(trash, sizeof(trash), "%s/old", work)) {
        note(config, "PC5 update can't read %s", journal);
        goto out;
    }
    for (char *at = (char *)text.data, *next; n < 4 && at && *at; at = next) {
        next = strchr(at, '\n');
        if (next) *next++ = 0;
        line[n++] = at;
    }
    /* The folder's path from its drive: the drive may have another number now (usb0 then, usb1 now). */
    if (n < 4 || !safe_name(line[0]) || pc5_format(folder, sizeof(folder), "%s/%s", drive, line[0]) ||
        !pc5_is_dir(folder) || !is_pc5_title(folder) || pc5_format(eboot, sizeof(eboot), "%s/eboot.bin", folder)) {
        note(config, "PC5 update: %s's folder %s isn't there; its files are kept", work, n ? line[0] : "-");
        goto out;
    }
    pc5_copy(version, version_size, line[1]);
    /* Its eboot.bin is the old or the new one; otherwise PC5 was installed again meanwhile. */
    if (file_mark(eboot, mark, sizeof(mark)) || (strcmp(mark, line[2]) && strcmp(mark, line[3]))) {
        note(config, "PC5 update: %s has another PC5 now; %s's update is dropped", folder, work);
        version[0] = 0;
        result = 0;
        goto out;
    }
    if (release_load(work, &release)) {
        note(config, "PC5 update can't read %s's list; its files are kept", work);
        goto out;
    }
    result = place_files(config, &release, fresh, folder, trash, NULL, eboot_placed, error, sizeof(error));
    release_free(&release);
    if (result) {
        note(config, "PC5 update can't finish in %s: %s; its files are kept", folder, error);
        goto out;
    }
    refresh_registered(config, folder);
    unlink(journal);
    sync_folder(work);
    note(config, "PC5 update finished in %s version=%s", folder, version);
out:
    pc5_buf_free(&text);
    return result;
}

int pc5_upgrade_tidy(const Pc5UpgradeConfig *config, char *finished, size_t size)
{
    char work[PC5_PATH], journal[PC5_PATH], version[32] = "";
    int restart = 0;

    if (finished && size) finished[0] = 0;
    /* What earlier updates set aside and a restart cut short (before this start sets any aside), deleted on a thread. */
    for (const char *const *drive = drives_of(config); *drive; drive++) {
        char *asides;
        if (drive_work(config, *drive, work, sizeof(work)) || !(asides = asides_of(work))) continue;
        note(config, "PC5 update deleting what an earlier one set aside beside %s", work);
        delete_aside(asides);
    }
    for (const char *const *drive = drives_of(config); *drive; drive++) {
        if (drive_work(config, *drive, work, sizeof(work)) || !pc5_is_dir(work)) continue;
        if (!is_work(work)) {
            note(config, "PC5 update: %s isn't PC5's work folder; left as it is", work);
            continue;
        }
        if (!pc5_format(journal, sizeof(journal), "%s/" JOURNAL, work) && pc5_is_file(journal)) {
            int eboot_placed = 0;
            if (finish_replacing(config, *drive, work, &eboot_placed, version, sizeof(version))) continue;
            if (eboot_placed && !version[0]) eboot_placed = 0;
            if (eboot_placed) {
                restart = 1;
                if (finished) pc5_copy(finished, size, version);
            }
        }
        note(config, "PC5 update leftover %s deleted=%d", work, !delete_work_soon(work));
    }
    return restart;
}
