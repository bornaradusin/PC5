/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_files.h"
#include "pc5_i18n.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { CHUNK = 1 << 20, DEPTH = 64 };
#define PART ".pc5part"

/* ---- a folder's items ------------------------------------------------------------------ */

static int item_compare(const void *a, const void *b)
{
    const Pc5FileItem *x = a, *y = b;
    if (x->dir != y->dir) return y->dir - x->dir;   /* folders first */
    for (size_t i = 0;; i++) {
        int p = pc5_lower((unsigned char)x->name[i]), q = pc5_lower((unsigned char)y->name[i]);
        if (p != q || !p) return p != q ? p - q : strcmp(x->name, y->name);
    }
}

int pc5_files_list(const char *path, Pc5FileList *list)
{
    DIR *dir;
    struct dirent *entry;
    int capacity = 0;

    memset(list, 0, sizeof(*list));
    if (!(dir = opendir(path))) {
        list->error = errno ? errno : EIO;
        return -1;
    }
    while ((entry = readdir(dir))) {
        char full[PC5_PATH];
        struct stat st;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || strlen(entry->d_name) >= PC5_FILES_NAME ||
            pc5_join(full, sizeof(full), path, entry->d_name))
            continue;
        if (list->count == capacity) {
            int more = capacity ? capacity * 2 : 64;
            Pc5FileItem *grown = pc5_realloc(list->items, sizeof(*grown) * (size_t)more);
            if (!grown) {
                closedir(dir);
                pc5_files_list_free(list);
                list->error = ENOMEM;
                return -1;
            }
            list->items = grown;
            capacity = more;
        }
        Pc5FileItem *item = &list->items[list->count++];
        memset(item, 0, sizeof(*item));
        pc5_copy(item->name, sizeof(item->name), entry->d_name);
        if (!stat(full, &st) || !lstat(full, &st)) {
            item->dir = S_ISDIR(st.st_mode);
            item->size = item->dir ? 0 : (uint64_t)st.st_size;
        }
    }
    closedir(dir);
    if (list->count) qsort(list->items, (size_t)list->count, sizeof(*list->items), item_compare);
    return 0;
}

void pc5_files_list_free(Pc5FileList *list)
{
    pc5_free(list->items);
    memset(list, 0, sizeof(*list));
}

/* ---- names and places ------------------------------------------------------------------- */

static int has_dot_part(const char *path)
{
    for (const char *p = path; *p; p++)
        if ((p == path || p[-1] == '/') && p[0] == '.' && (p[1] == '/' || !p[1] || (p[1] == '.' && (p[2] == '/' || !p[2]))))
            return 1;
    return 0;
}

int pc5_files_within(const char *root, const char *path)
{
    size_t n = strlen(root);
    while (n > 1 && root[n - 1] == '/') n--;
    if (!n || has_dot_part(root) || has_dot_part(path)) return 0;
    if (n == 1 && root[0] == '/') return path[0] == '/';
    return !strncmp(root, path, n) && (path[n] == 0 || path[n] == '/');
}

int pc5_files_name_ok(const char *name, char *why, size_t size)
{
    size_t n = strlen(name);
    if (!n) return pc5_format(why, size, N_("The name is empty.")), -1;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return pc5_format(why, size, N_("\"%s\" can't be a name."), name), -1;
    if (n >= PC5_FILES_NAME) return pc5_format(why, size, N_("The name is too long: 255 bytes at most.")), -1;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        if (*p < 32 || strchr("/\\:*?\"<>|", *p))
            return pc5_format(why, size, N_("A name can't have %s in it."), *p < 32 ? "a control character" :
                              *p == '/' ? "/" : *p == '\\' ? "\\" : *p == ':' ? ":" : *p == '*' ? "*" : *p == '?' ? "?" :
                              *p == '"' ? "\"" : *p == '<' ? "<" : *p == '>' ? ">" : "|"), -1;
    if (name[n - 1] == ' ' || name[n - 1] == '.') return pc5_format(why, size, N_("A name can't end in a space or a dot.")), -1;
    return 0;
}

int pc5_files_free_name(const char *dir, const char *name, char *out, size_t size)
{
    char path[PC5_PATH], stem[PC5_FILES_NAME];
    struct stat st;
    const char *dot = strrchr(name, '.');
    size_t stem_n;

    if (pc5_join(path, sizeof(path), dir, name)) return -1;
    if (lstat(path, &st)) return pc5_format(out, size, "%s", name);
    if (!dot || dot == name) dot = name + strlen(name);
    stem_n = (size_t)(dot - name) < sizeof(stem) ? (size_t)(dot - name) : sizeof(stem) - 1;
    memcpy(stem, name, stem_n);
    stem[stem_n] = 0;
    for (int k = 2; k < 10000; k++) {
        char candidate[PC5_FILES_NAME + 16];
        snprintf(candidate, sizeof(candidate), "%s (%d)%s", stem, k, dot);
        if (strlen(candidate) >= PC5_FILES_NAME || pc5_join(path, sizeof(path), dir, candidate)) return -1;
        if (lstat(path, &st)) return pc5_format(out, size, "%s", candidate);
    }
    return -1;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash && slash[1] ? slash + 1 : path;
}

int pc5_files_conflicts(const char *const *sources, int count, const char *dest, int with_self)
{
    int n = 0;
    for (int i = 0; i < count; i++) {
        char target[PC5_PATH];
        struct stat st, self;
        if (pc5_join(target, sizeof(target), dest, base_name(sources[i])) || lstat(target, &st)) continue;
        if (!with_self && !lstat(sources[i], &self) && self.st_dev == st.st_dev && self.st_ino == st.st_ino) continue;
        n++;
    }
    return n;
}

static void measure(const char *path, Pc5FilesSize *size, uint64_t until, int depth)
{
    struct stat st;
    if (size->partial || lstat(path, &st)) return;
    if (!S_ISDIR(st.st_mode)) {
        size->files++;
        size->bytes += (uint64_t)st.st_size;
        return;
    }
    size->folders++;
    if (depth > DEPTH || pc5_now_ms() >= until) { size->partial = 1; return; }
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) return;
    while ((entry = readdir(dir)) && !size->partial) {
        char child[PC5_PATH];
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || pc5_join(child, sizeof(child), path, entry->d_name))
            continue;
        measure(child, size, until, depth + 1);
    }
    closedir(dir);
}

void pc5_files_measure(const char *const *paths, int count, Pc5FilesSize *size, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);
    memset(size, 0, sizeof(*size));
    for (int i = 0; i < count; i++) measure(paths[i], size, until, 0);
}

/* ---- the work ---------------------------------------------------------------------------- */

typedef enum { T_MKDIR, T_COPY, T_RENAME, T_REMOVE } TaskKind;

struct Pc5FilesTask {
    TaskKind kind;
    uint32_t src, dst;        /* offsets of their paths in job->names */
    uint64_t size;
    uint32_t mode;
};

static const char *name_at(const Pc5FilesJob *job, uint32_t at) { return (const char *)job->names.data + at; }

static int add_name(Pc5FilesJob *job, const char *text, uint32_t *at)
{
    *at = (uint32_t)job->names.len;
    return pc5_buf_put(&job->names, text, strlen(text) + 1);
}

static int add_task(Pc5FilesJob *job, TaskKind kind, const char *src, const char *dst, uint64_t size, uint32_t mode)
{
    static const uint32_t step = 256;
    Pc5FilesTask *t;
    if (job->task_count % step == 0) {
        Pc5FilesTask *grown = pc5_realloc(job->tasks, sizeof(*grown) * (job->task_count + step));
        if (!grown) return -1;
        job->tasks = grown;
    }
    t = &job->tasks[job->task_count];
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    t->size = size;
    t->mode = mode;
    if ((src && add_name(job, src, &t->src)) || (dst && add_name(job, dst, &t->dst))) return -1;
    job->task_count++;
    job->total_items++;
    job->total_bytes += kind == T_COPY ? size : 0;
    return 0;
}

static int fail(Pc5FilesJob *job, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int fail(Pc5FilesJob *job, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(job->error, sizeof(job->error), format, args);
    va_end(args);
    job->state = PC5_FILES_FAILED;
    return -1;
}

/* src (an item) copied to dst, as Replace says where dst has something already. */
static int plan_tree(Pc5FilesJob *job, const char *src, const char *dst, int depth)
{
    struct stat st, there;
    int exists = !lstat(dst, &there);

    if (depth > DEPTH) return fail(job, N_("%s has folders too deep to copy."), base_name(src));
    if (stat(src, &st) && lstat(src, &st)) return fail(job, N_("PC5 can't read %s: %s."), base_name(src), strerror(errno));
    if (!S_ISDIR(st.st_mode)) {
        if (exists && S_ISDIR(there.st_mode) && add_task(job, T_REMOVE, dst, NULL, 0, 0)) goto memory;
        if (add_task(job, T_COPY, src, dst, (uint64_t)st.st_size, (uint32_t)(st.st_mode & 0777))) goto memory;
        return 0;
    }
    if (exists && !S_ISDIR(there.st_mode) && add_task(job, T_REMOVE, dst, NULL, 0, 0)) goto memory;
    if ((!exists || !S_ISDIR(there.st_mode)) && add_task(job, T_MKDIR, NULL, dst, 0, 0777)) goto memory;
    {
        Pc5FileList list;
        if (pc5_files_list(src, &list)) return fail(job, N_("PC5 can't read %s: %s."), base_name(src), strerror(list.error));
        for (int i = 0; i < list.count; i++) {
            char from[PC5_PATH], to[PC5_PATH];
            if (pc5_join(from, sizeof(from), src, list.items[i].name) || pc5_join(to, sizeof(to), dst, list.items[i].name)) {
                pc5_files_list_free(&list);
                return fail(job, N_("A path in %s is too long."), base_name(src));
            }
            if (plan_tree(job, from, to, depth + 1)) {
                pc5_files_list_free(&list);
                return -1;
            }
        }
        pc5_files_list_free(&list);
    }
    return 0;
memory:
    return fail(job, N_("PC5 ran out of memory planning the copy."));
}

int pc5_files_begin(Pc5FilesJob *job, Pc5FilesOp op, const char *const *sources, int count, const char *dest,
                    Pc5FilesConflict conflict)
{
    struct stat dest_st;
    uint32_t *removals = NULL;
    int removal_count = 0;

    memset(job, 0, sizeof(*job));
    job->op = op;
    job->in = job->out = -1;
    job->state = PC5_FILES_WORKING;
    if (count <= 0) return fail(job, N_("Nothing is chosen."));
    if (op == PC5_FILES_DELETE) {
        for (int i = 0; i < count; i++)
            if (add_task(job, T_REMOVE, sources[i], NULL, 0, 0)) return fail(job, N_("PC5 ran out of memory."));
        return 0;
    }
    if (stat(dest, &dest_st) || !S_ISDIR(dest_st.st_mode))
        return fail(job, N_("The folder to paste into isn't there any more."));
    if (!(removals = pc5_alloc(sizeof(*removals) * (size_t)count))) return fail(job, N_("PC5 ran out of memory."));
    for (int i = 0; i < count; i++) {
        const char *src = sources[i];
        char target[PC5_PATH], free_name[PC5_FILES_NAME];
        struct stat st, there;
        if (lstat(src, &st)) {
            pc5_free(removals);
            return fail(job, N_("%s isn't there any more."), base_name(src));
        }
        if (S_ISDIR(st.st_mode) && pc5_files_within(src, dest)) {
            pc5_free(removals);
            return fail(job, N_("%s can't go into itself or a folder inside it."), base_name(src));
        }
        if (pc5_join(target, sizeof(target), dest, base_name(src))) {
            pc5_free(removals);
            return fail(job, N_("The path of %s would be too long."), base_name(src));
        }
        int same = !strcmp(target, src), exists = !lstat(target, &there);
        if (same && (op == PC5_FILES_MOVE || conflict != PC5_FILES_KEEP_BOTH)) {
            job->skipped++;   /* where it is already */
            continue;
        }
        if (exists && conflict == PC5_FILES_SKIP) {
            job->skipped++;
            continue;
        }
        if (exists && conflict == PC5_FILES_KEEP_BOTH) {
            if (pc5_files_free_name(dest, base_name(src), free_name, sizeof(free_name)) ||
                pc5_join(target, sizeof(target), dest, free_name)) {
                pc5_free(removals);
                return fail(job, N_("PC5 can't find a free name for %s."), base_name(src));
            }
            exists = 0;
        }
        /* A move on one drive is a rename, when nothing is merged: none there, or a file over a file. */
        if (op == PC5_FILES_MOVE && st.st_dev == dest_st.st_dev &&
            (!exists || (!S_ISDIR(st.st_mode) && !S_ISDIR(there.st_mode)))) {
            if (add_task(job, T_RENAME, src, target, 0, 0)) goto memory;
            continue;
        }
        if (plan_tree(job, src, target, 0)) {
            pc5_free(removals);
            return -1;
        }
        if (op == PC5_FILES_MOVE) {
            if (add_name(job, src, &removals[removal_count])) goto memory;
            removal_count++;
        }
    }
    /* A move across drives deletes its sources once all of them are copied. */
    for (int i = 0; i < removal_count; i++) {
        char src[PC5_PATH];
        pc5_copy(src, sizeof(src), name_at(job, removals[i]));
        if (add_task(job, T_REMOVE, src, NULL, 0, 0)) goto memory;
    }
    pc5_free(removals);
    {
        uint64_t room = 0;
        if (job->total_bytes && !pc5_free_bytes(dest, &room) && room < job->total_bytes) {
            char need[32], have[32];
            pc5_size_text(job->total_bytes, need, sizeof(need));
            pc5_size_text(room, have, sizeof(have));
            return fail(job, N_("There isn't room: it needs %s and the drive has %s free."), need, have);
        }
    }
    return 0;
memory:
    pc5_free(removals);
    return fail(job, N_("PC5 ran out of memory planning the work."));
}

static void close_copy(Pc5FilesJob *job, int keep)
{
    if (job->in >= 0) close(job->in);
    if (job->out >= 0) close(job->out);
    if (job->out >= 0 && !keep && job->task_at < job->task_count) {
        char part[PC5_PATH + 16];
        snprintf(part, sizeof(part), "%s" PART, name_at(job, job->tasks[job->task_at].dst));
        unlink(part);
    }
    job->in = job->out = -1;
}

/* Copies for a while: 1 while the file isn't whole, 0 when it is in place, -1 on a failure. */
static int copy_some(Pc5FilesJob *job, const Pc5FilesTask *t, uint64_t until)
{
    const char *src = name_at(job, t->src), *dst = name_at(job, t->dst);
    char part[PC5_PATH + 16];

    snprintf(part, sizeof(part), "%s" PART, dst);
    if (job->in < 0) {
        if ((job->in = open(src, O_RDONLY)) < 0)
            return fail(job, N_("PC5 can't read %s: %s."), base_name(src), strerror(errno));
        if ((job->out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
            int saved = errno;
            close_copy(job, 0);
            return fail(job, N_("PC5 can't write %s: %s."), base_name(dst), strerror(saved));
        }
        job->copied = 0;
    }
    if (!job->chunk && !(job->chunk = pc5_alloc(CHUNK))) {
        close_copy(job, 0);
        return fail(job, N_("PC5 ran out of memory copying %s."), base_name(src));
    }
    do {
        ssize_t got = read(job->in, job->chunk, CHUNK);
        if (got < 0) {
            int saved = errno;
            close_copy(job, 0);
            return fail(job, N_("PC5 can't read %s: %s."), base_name(src), strerror(saved));
        }
        if (got == 0) break;
        for (ssize_t put = 0; put < got;) {
            ssize_t n = write(job->out, job->chunk + put, (size_t)(got - put));
            if (n <= 0) {
                int saved = n < 0 ? errno : ENOSPC;
                close_copy(job, 0);
                return fail(job, N_("PC5 can't write %s: %s."), base_name(dst), strerror(saved));
            }
            put += n;
        }
        job->copied += (uint64_t)got;
        job->done_bytes += (uint64_t)got;
        if (pc5_now_ms() >= until) return 1;
    } while (1);
    (void)fchmod(job->out, (mode_t)(t->mode | 0600));
    if (close(job->out)) {
        job->out = -1;
        close_copy(job, 0);
        unlink(part);
        return fail(job, N_("PC5 can't write %s: %s."), base_name(dst), strerror(errno));
    }
    job->out = -1;
    close(job->in);
    job->in = -1;
    if (rename(part, dst)) {
        int saved = errno;
        unlink(part);
        return fail(job, N_("PC5 can't put %s in place: %s."), base_name(dst), strerror(saved));
    }
    /* the bytes counted as read: a file whose size changed since the plan keeps the total right */
    job->total_bytes += job->copied > t->size ? job->copied - t->size : 0;
    return 0;
}

int pc5_files_step(Pc5FilesJob *job, int budget_ms)
{
    uint64_t until = pc5_now_ms() + (uint64_t)(budget_ms > 0 ? budget_ms : 1);

    if (job->state != PC5_FILES_WORKING) return 0;
    while (job->task_at < job->task_count) {
        const Pc5FilesTask *t = &job->tasks[job->task_at];
        if (job->stop) {
            close_copy(job, 0);
            if (job->removing) pc5_remove_end(&job->remove);
            job->removing = 0;
            job->state = PC5_FILES_STOPPED;
            return 0;
        }
        pc5_copy(job->current, sizeof(job->current), base_name(name_at(job, t->kind == T_MKDIR ? t->dst : t->src)));
        switch (t->kind) {
        case T_MKDIR: {
            const char *dst = name_at(job, t->dst);
            if (mkdir(dst, 0777) && !(errno == EEXIST && pc5_is_dir(dst)))
                return fail(job, N_("PC5 can't make the folder %s: %s."), base_name(dst), strerror(errno)), 0;
            (void)chmod(dst, 0777);
            break;
        }
        case T_RENAME:
            if (rename(name_at(job, t->src), name_at(job, t->dst)))
                return fail(job, N_("PC5 can't move %s: %s."), base_name(name_at(job, t->src)), strerror(errno)), 0;
            break;
        case T_REMOVE:
            if (!job->removing) {
                if (pc5_tree_delete_begin(&job->remove, name_at(job, t->src)))
                    return fail(job, N_("PC5 ran out of memory deleting %s."), job->current), 0;
                job->removing = 1;
            }
            {
                int64_t left = (int64_t)until - (int64_t)pc5_now_ms();
                if (pc5_remove_step(&job->remove, left > 1 ? (int)left : 1)) return 1;
            }
            job->removing = 0;
            if (job->remove.failed) {
                char why[sizeof(job->remove.error)];
                pc5_copy(why, sizeof(why), job->remove.error);
                pc5_remove_end(&job->remove);
                return fail(job, "%s", why), 0;
            }
            pc5_remove_end(&job->remove);
            break;
        case T_COPY: {
            int rc = copy_some(job, t, until);
            if (rc < 0) return 0;
            if (rc > 0) return 1;
            break;
        }
        }
        job->task_at++;
        job->done_items++;
        /* out of time with work left: more next step; the last item done: finished below */
        if (job->task_at < job->task_count && pc5_now_ms() >= until) return 1;
    }
    job->current[0] = 0;
    job->state = PC5_FILES_DONE;
    return 0;
}

void pc5_files_stop(Pc5FilesJob *job)
{
    job->stop = 1;
}

void pc5_files_end(Pc5FilesJob *job)
{
    close_copy(job, 0);
    if (job->removing) pc5_remove_end(&job->remove);
    pc5_remove_end(&job->remove);
    pc5_free(job->tasks);
    pc5_free(job->chunk);
    pc5_buf_free(&job->names);
    memset(job, 0, sizeof(*job));
    job->in = job->out = -1;
}
