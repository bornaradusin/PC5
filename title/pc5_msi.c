/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: Windows Installer packages read and installed (pc5_msi.h). A package is a compound file
 * ([MS-CFB]); its tables are streams whose names Windows Installer encodes, their columns one
 * after another, strings by their index in _StringPool/_StringData, as Wine's msi.dll reads
 * them. Every offset, size and index read from the package is checked before it is used. */
#include "pc5_msi.h"
#include "pc5_cab.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the compound file ------------------------------------------------------------------- */
#define CFB_END 0xfffffffeu
enum { CFB_ENTRIES_MAX = 1 << 20 };

typedef struct CfbEntry {
    char name[96];          /* as Windows Installer names it, decoded ("!File", "media1.cab") */
    uint8_t type;           /* 1 storage, 2 stream, 5 the root */
    uint32_t start;
    uint64_t size;
} CfbEntry;

typedef struct Cfb {
    int fd;
    uint64_t base, file_size;
    uint32_t sector, mini_cutoff;
    uint32_t *fat, fat_count;
    uint32_t *minifat, minifat_count;
    uint32_t *ministream, ministream_count;   /* the root's sectors, where the small streams are */
    CfbEntry *entries;
    uint32_t entry_count;
} Cfb;

typedef struct Stream {
    const Cfb *cfb;
    uint32_t *chain, count;
    int mini;
    uint64_t size;
} Stream;

static int raw_read(const Cfb *cfb, uint64_t offset, void *out, size_t size)
{
    uint8_t *at = out;
    if (offset > cfb->file_size || size > cfb->file_size - offset) return -1;
    offset += cfb->base;
    while (size) {
        ssize_t got = pread(cfb->fd, at, size, (off_t)offset);
        if (got <= 0) return -1;
        at += got;
        offset += (uint64_t)got;
        size -= (size_t)got;
    }
    return 0;
}

static int sector_read(const Cfb *cfb, uint32_t sector, uint32_t into, void *out, size_t size)
{
    return raw_read(cfb, ((uint64_t)sector + 1) * cfb->sector + into, out, size);
}

/* The sectors of a chain from start in table (count entries), at most max of them. */
static int chain(const uint32_t *table, uint32_t count, uint32_t start, uint32_t max, uint32_t **out, uint32_t *n)
{
    uint32_t have = 0, cap = 64;
    uint32_t *list = pc5_alloc(cap * sizeof(*list));
    if (!list) return -1;
    for (uint32_t at = start; at != CFB_END; at = table[at]) {
        if (at >= count || have >= max) {
            pc5_free(list);
            return -1;
        }
        if (have == cap) {
            uint32_t *more = pc5_realloc(list, cap * 2 * sizeof(*list));
            if (!more) {
                pc5_free(list);
                return -1;
            }
            list = more;
            cap *= 2;
        }
        list[have++] = at;
    }
    *out = list;
    *n = have;
    return 0;
}

static int stream_read(const Stream *s, uint64_t offset, void *out, size_t size)
{
    uint8_t *at = out;
    uint32_t unit = s->mini ? 64 : s->cfb->sector;
    if (offset > s->size || size > s->size - offset) return -1;
    while (size) {
        uint64_t index = offset / unit;
        uint32_t into = (uint32_t)(offset % unit), take = unit - into;
        if (take > size) take = (uint32_t)size;
        if (index >= s->count) return -1;
        if (s->mini) {
            uint64_t in_ministream = (uint64_t)s->chain[index] * 64 + into;
            uint64_t big = in_ministream / s->cfb->sector;
            if (big >= s->cfb->ministream_count ||
                sector_read(s->cfb, s->cfb->ministream[big], (uint32_t)(in_ministream % s->cfb->sector), at, take))
                return -1;
        } else if (sector_read(s->cfb, s->chain[index], into, at, take)) {
            return -1;
        }
        at += take;
        offset += take;
        size -= take;
    }
    return 0;
}

static int stream_reader(void *context, uint64_t offset, void *out, size_t size)
{
    return stream_read(context, offset, out, size);
}

/* Windows Installer's stream names: two characters in each unit of 0x3800-0x47ff, one in
 * 0x4800-0x483f, and 0x4840 before a table's name. */
static void decode_name(const uint8_t *utf16, int units, char *out, size_t size)
{
    static const char mime[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
    size_t n = 0;
    for (int i = 0; i < units && n + 3 < size; i++) {
        unsigned c = u16(utf16 + 2 * i);
        if (!c) break;
        if (c >= 0x3800 && c < 0x4800) {
            c -= 0x3800;
            out[n++] = mime[c & 0x3f];
            out[n++] = mime[(c >> 6) & 0x3f];
        } else if (c >= 0x4800 && c < 0x4840) {
            out[n++] = mime[c - 0x4800];
        } else if (c == 0x4840) {
            out[n++] = '!';
        } else {
            out[n++] = c < 0x80 ? (char)c : '?';
        }
    }
    out[n] = 0;
}

static void cfb_close(Cfb *cfb)
{
    if (cfb->fd >= 0) close(cfb->fd);
    pc5_free(cfb->fat);
    pc5_free(cfb->minifat);
    pc5_free(cfb->ministream);
    pc5_free(cfb->entries);
    memset(cfb, 0, sizeof(*cfb));
    cfb->fd = -1;
}

static int cfb_open(Cfb *cfb, const char *path, uint64_t base)
{
    static const uint8_t signature[8] = { 0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1 };
    uint8_t head[512];
    struct stat st;
    uint32_t *dir_chain = NULL, dir_count = 0;

    memset(cfb, 0, sizeof(*cfb));
    cfb->fd = open(path, O_RDONLY);
    if (cfb->fd < 0 || fstat(cfb->fd, &st) || (uint64_t)st.st_size <= base) goto fail;
    cfb->base = base;
    cfb->file_size = (uint64_t)st.st_size - base;
    if (raw_read(cfb, 0, head, sizeof(head)) || memcmp(head, signature, 8) || u16(head + 0x1c) != 0xfffe) goto fail;
    unsigned shift = u16(head + 0x1e);
    if (shift != 9 && shift != 12) goto fail;
    cfb->sector = 1u << shift;
    cfb->mini_cutoff = u32(head + 0x38);
    uint32_t fat_sectors = u32(head + 0x2c), dir_start = u32(head + 0x30), minifat_start = u32(head + 0x3c);
    uint32_t minifat_sectors = u32(head + 0x40), difat_at = u32(head + 0x44), difat_sectors = u32(head + 0x48);
    uint32_t per = cfb->sector / 4, sectors_in_file = (uint32_t)(cfb->file_size / cfb->sector);
    if (!fat_sectors || fat_sectors > sectors_in_file || difat_sectors > sectors_in_file) goto fail;
    /* The FAT: its sectors from the header's 109 and the DIFAT sectors after them. */
    cfb->fat_count = fat_sectors * per;
    if (!(cfb->fat = pc5_alloc((size_t)cfb->fat_count * 4))) goto fail;
    uint8_t *sector = pc5_alloc(cfb->sector);
    if (!sector) goto fail;
    uint32_t loaded = 0, difat = difat_at;
    for (uint32_t i = 0; i < 109 && loaded < fat_sectors; i++, loaded++) {
        if (sector_read(cfb, u32(head + 0x4c + 4 * i), 0, sector, cfb->sector)) {
            pc5_free(sector);
            goto fail;
        }
        for (uint32_t j = 0; j < per; j++) cfb->fat[loaded * per + j] = u32(sector + 4 * j);
    }
    uint8_t *list = pc5_alloc(cfb->sector);
    for (uint32_t d = 0; list && d < difat_sectors && loaded < fat_sectors; d++) {
        if (sector_read(cfb, difat, 0, list, cfb->sector)) break;
        for (uint32_t i = 0; i + 1 < per && loaded < fat_sectors; i++, loaded++) {
            if (sector_read(cfb, u32(list + 4 * i), 0, sector, cfb->sector)) break;
            for (uint32_t j = 0; j < per; j++) cfb->fat[loaded * per + j] = u32(sector + 4 * j);
        }
        difat = u32(list + cfb->sector - 4);
    }
    pc5_free(list);
    if (loaded < fat_sectors) {
        pc5_free(sector);
        goto fail;
    }
    /* The directory. */
    if (chain(cfb->fat, cfb->fat_count, dir_start, sectors_in_file, &dir_chain, &dir_count) ||
        (uint64_t)dir_count * (cfb->sector / 128) > CFB_ENTRIES_MAX) {
        pc5_free(sector);
        goto fail;
    }
    cfb->entry_count = dir_count * (cfb->sector / 128);
    if (!(cfb->entries = pc5_alloc((size_t)cfb->entry_count * sizeof(*cfb->entries)))) {
        pc5_free(sector);
        goto fail;
    }
    for (uint32_t s = 0; s < dir_count; s++) {
        if (sector_read(cfb, dir_chain[s], 0, sector, cfb->sector)) {
            pc5_free(sector);
            goto fail;
        }
        for (uint32_t e = 0; e < cfb->sector / 128; e++) {
            const uint8_t *raw = sector + 128 * e;
            CfbEntry *entry = &cfb->entries[s * (cfb->sector / 128) + e];
            int units = u16(raw + 64) / 2;
            decode_name(raw, units > 32 ? 32 : units, entry->name, sizeof(entry->name));
            entry->type = raw[66];
            entry->start = u32(raw + 116);
            entry->size = u32(raw + 120) | (cfb->sector == 4096 ? (uint64_t)u32(raw + 124) << 32 : 0);
        }
    }
    pc5_free(sector);
    pc5_free(dir_chain);
    dir_chain = NULL;
    if (!cfb->entry_count || cfb->entries[0].type != 5) goto fail;
    /* The small streams: the mini FAT, and the root's stream they are in. */
    if (minifat_sectors && minifat_start != CFB_END) {
        uint32_t *mchain, mcount;
        if (chain(cfb->fat, cfb->fat_count, minifat_start, sectors_in_file, &mchain, &mcount)) goto fail;
        cfb->minifat_count = mcount * per;
        cfb->minifat = pc5_alloc((size_t)cfb->minifat_count * 4 + 4);
        uint8_t *buffer = pc5_alloc(cfb->sector);
        int bad = !cfb->minifat || !buffer;
        for (uint32_t s = 0; !bad && s < mcount; s++) {
            bad = sector_read(cfb, mchain[s], 0, buffer, cfb->sector);
            for (uint32_t j = 0; !bad && j < per; j++) cfb->minifat[s * per + j] = u32(buffer + 4 * j);
        }
        pc5_free(buffer);
        pc5_free(mchain);
        if (bad) goto fail;
        if (cfb->entries[0].start != CFB_END &&
            chain(cfb->fat, cfb->fat_count, cfb->entries[0].start, sectors_in_file, &cfb->ministream,
                  &cfb->ministream_count))
            goto fail;
    }
    return 0;
fail:
    pc5_free(dir_chain);
    cfb_close(cfb);
    return -1;
}

static const CfbEntry *cfb_find(const Cfb *cfb, const char *name)
{
    for (uint32_t i = 1; i < cfb->entry_count; i++)
        if (cfb->entries[i].type == 2 && !strcmp(cfb->entries[i].name, name)) return &cfb->entries[i];
    return NULL;
}

static int stream_open(const Cfb *cfb, const char *name, Stream *s)
{
    const CfbEntry *entry = cfb_find(cfb, name);
    memset(s, 0, sizeof(*s));
    if (!entry) return 1;
    s->cfb = cfb;
    s->size = entry->size;
    if (!entry->size) return 0;
    s->mini = entry->size < cfb->mini_cutoff;
    uint64_t unit = s->mini ? 64 : cfb->sector, need = (entry->size + unit - 1) / unit;
    if (s->mini ? !cfb->minifat : 0) return -1;
    if (chain(s->mini ? cfb->minifat : cfb->fat, s->mini ? cfb->minifat_count : cfb->fat_count, entry->start,
              (uint32_t)(need + 1), &s->chain, &s->count))
        return -1;
    if (s->count < need) {
        pc5_free(s->chain);
        s->chain = NULL;
        return -1;
    }
    return 0;
}

static void stream_close(Stream *s)
{
    pc5_free(s->chain);
    s->chain = NULL;
}

/* A whole stream, when it is at most max bytes: 0, 1 when there is none, -1. */
static int stream_load(const Cfb *cfb, const char *name, Pc5Buf *out, size_t max)
{
    Stream s;
    int status = stream_open(cfb, name, &s);
    memset(out, 0, sizeof(*out));
    if (status) return status;
    if (s.size > max || pc5_buf_reserve(out, (size_t)s.size + 1) || stream_read(&s, 0, out->data, (size_t)s.size)) {
        stream_close(&s);
        pc5_buf_free(out);
        return -1;
    }
    out->len = (size_t)s.size;
    stream_close(&s);
    return 0;
}

/* ---- the database ------------------------------------------------------------------------- */
enum { TYPE_VALID = 0x100, TYPE_STRING = 0x800, TYPE_NULLABLE = 0x1000, MAX_COLUMNS = 32 };

typedef struct Msi {
    Cfb cfb;
    char *data;              /* every string, each with its 0, in UTF-8 */
    uint32_t *string_at;     /* by index: its offset in data, or UINT32_MAX for none */
    uint32_t strings;
    int strref;              /* bytes a string's index takes in a table: 2, or 3 */
    Pc5Buf columns;          /* _Columns, as it is */
    uint32_t column_rows;
} Msi;

typedef struct Table {
    int count;
    struct { char name[64]; uint16_t type; int bytes; } col[MAX_COLUMNS];
    uint32_t rows;
    uint32_t *cells;         /* row by row: a string's index, or an integer as stored (0 for none) */
} Table;

static const char *str(const Msi *msi, uint32_t index)
{
    if (!index || index >= msi->strings || msi->string_at[index] == UINT32_MAX) return NULL;
    return msi->data + msi->string_at[index];
}

static int msi_strings(Msi *msi)
{
    Pc5Buf pool, data;
    if (stream_load(&msi->cfb, "!_StringPool", &pool, 64u << 20)) return -1;
    if (stream_load(&msi->cfb, "!_StringData", &data, 256u << 20)) {
        pc5_buf_free(&pool);
        return -1;
    }
    int status = -1;
    uint32_t count = (uint32_t)(pool.len / 4);
    if (count < 1) goto done;
    uint32_t codepage = u16(pool.data) | (uint32_t)(u16(pool.data + 2) & 0x7fff) << 16;
    msi->strref = (u16(pool.data + 2) & 0x8000) ? 3 : 2;
    /* Each byte of a single-byte code page up to three of UTF-8 (Latin-1's are two). */
    if (!(msi->string_at = pc5_alloc(sizeof(uint32_t) * (count + 1))) || !(msi->data = pc5_alloc(data.len * 3 + count + 1)))
        goto done;
    size_t offset = 0, out = 0;
    uint32_t i = 1, n = 1;
    msi->string_at[0] = UINT32_MAX;
    while (i < count) {
        uint32_t len = u16(pool.data + 4 * i), refs = u16(pool.data + 4 * i + 2);
        if (!len && !refs) {
            msi->string_at[n++] = UINT32_MAX;
            i++;
            continue;
        }
        if (!len) {   /* over 64 KiB: the next entry holds its length */
            if (i + 1 >= count) goto done;
            len = (uint32_t)u16(pool.data + 4 * i + 6) << 16 | u16(pool.data + 4 * i + 4);
            i += 2;
        } else {
            i++;
        }
        if (len > data.len - offset) goto done;
        msi->string_at[n++] = (uint32_t)out;
        for (uint32_t k = 0; k < len; k++) {
            uint8_t c = data.data[offset + k];
            if (c < 0x80 || codepage == 65001) {
                msi->data[out++] = (char)c;
            } else {
                msi->data[out++] = (char)(0xc0 | c >> 6);
                msi->data[out++] = (char)(0x80 | (c & 0x3f));
            }
        }
        msi->data[out++] = 0;
        offset += len;
    }
    msi->strings = n;
    status = 0;
done:
    pc5_buf_free(&pool);
    pc5_buf_free(&data);
    return status;
}

static void msi_close(Msi *msi)
{
    pc5_free(msi->data);
    pc5_free(msi->string_at);
    pc5_buf_free(&msi->columns);
    cfb_close(&msi->cfb);
    memset(msi, 0, sizeof(*msi));
}

static int msi_open(Msi *msi, const char *path, uint64_t offset)
{
    memset(msi, 0, sizeof(*msi));
    if (cfb_open(&msi->cfb, path, offset)) return -1;
    if (msi_strings(msi) || stream_load(&msi->cfb, "!_Columns", &msi->columns, 16u << 20)) {
        msi_close(msi);
        return -1;
    }
    msi->column_rows = (uint32_t)(msi->columns.len / (2u * msi->strref + 4));
    return 0;
}

/* A stored value of bytes at p. */
static uint32_t cell_at(const uint8_t *p, int bytes)
{
    return bytes == 2 ? u16(p) : bytes == 3 ? (uint32_t)(p[0] | p[1] << 8 | p[2] << 16) : u32(p);
}

static void table_free(Table *t)
{
    pc5_free(t->cells);
    memset(t, 0, sizeof(*t));
}

/* The table name: 0, 1 when the package has none, -1. */
static int table_load(const Msi *msi, const char *name, Table *t)
{
    memset(t, 0, sizeof(*t));
    /* Its columns, from _Columns: Table, Number, Name and Type, each column's values together. */
    uint32_t rows = msi->column_rows;
    const uint8_t *c = msi->columns.data;
    int r = msi->strref;
    for (uint32_t i = 0; i < rows; i++) {
        const char *table = str(msi, cell_at(c + (size_t)r * i, r));
        if (!table || strcmp(table, name)) continue;
        uint32_t number = cell_at(c + (size_t)r * rows + 2u * i, 2) ^ 0x8000;
        const char *column = str(msi, cell_at(c + (size_t)(r + 2) * rows + (size_t)r * i, r));
        uint16_t type = (uint16_t)(cell_at(c + (size_t)(2 * r + 2) * rows + 2u * i, 2) ^ 0x8000);
        if (number < 1 || number > MAX_COLUMNS || !column) return -1;
        pc5_copy(t->col[number - 1].name, sizeof(t->col[0].name), column);
        t->col[number - 1].type = type;
        if ((int)number > t->count) t->count = (int)number;
    }
    if (!t->count) return 1;
    size_t row_size = 0;
    for (int i = 0; i < t->count; i++) {
        uint16_t type = t->col[i].type;
        if (!t->col[i].name[0]) return -1;
        t->col[i].bytes = (type & ~TYPE_NULLABLE) == (TYPE_STRING | TYPE_VALID) ? 2
                          : (type & TYPE_STRING)                              ? r
                          : (type & 0xff) <= 2                                ? 2
                                                                              : 4;
        row_size += (size_t)t->col[i].bytes;
    }
    char stream[80];
    Pc5Buf data;
    snprintf(stream, sizeof(stream), "!%s", name);
    int status = stream_load(&msi->cfb, stream, &data, 128u << 20);
    if (status < 0) return -1;
    if (status) return 0;   /* the table without rows */
    t->rows = (uint32_t)(data.len / row_size);
    if (t->rows && !(t->cells = pc5_alloc(sizeof(uint32_t) * t->rows * (size_t)t->count))) {
        pc5_buf_free(&data);
        return -1;
    }
    size_t column_at = 0;
    for (int col = 0; col < t->count; col++) {
        for (uint32_t row = 0; row < t->rows; row++)
            t->cells[(size_t)row * t->count + col] =
                cell_at(data.data + column_at + (size_t)t->col[col].bytes * row, t->col[col].bytes);
        column_at += (size_t)t->col[col].bytes * t->rows;
    }
    pc5_buf_free(&data);
    return 0;
}

static int column(const Table *t, const char *name)
{
    for (int i = 0; i < t->count; i++)
        if (!strcmp(t->col[i].name, name)) return i;
    return -1;
}

/* A string cell, NULL for none. */
static const char *cell_str(const Msi *msi, const Table *t, uint32_t row, int col)
{
    if (col < 0 || row >= t->rows || !(t->col[col].type & TYPE_STRING)) return NULL;
    return str(msi, t->cells[(size_t)row * t->count + col]);
}

/* An integer cell; fallback for none. */
static long cell_int(const Table *t, uint32_t row, int col, long fallback)
{
    if (col < 0 || row >= t->rows || (t->col[col].type & TYPE_STRING)) return fallback;
    uint32_t v = t->cells[(size_t)row * t->count + col];
    if (!v) return fallback;
    return t->col[col].bytes == 2 ? (long)(int16_t)(uint16_t)(v ^ 0x8000) : (long)(int32_t)(v ^ 0x80000000u);
}

/* ---- properties and conditions ------------------------------------------------------------ */
typedef struct Props {
    const Msi *msi;
    const Table *table;      /* Property */
    const char *extra;       /* "NAME=value" lines */
} Props;

static const char *const builtin[][2] = {
    { "VersionNT", "601" }, { "VersionNT64", "601" }, { "WindowsBuild", "7601" }, { "ServicePackLevel", "1" },
    { "Intel", "6" },       { "Msix64", "6" },        { "Privileged", "1" },      { "AdminUser", "1" },
    { "ALLUSERS", "1" },    { "INSTALLLEVEL", "1" },  { "ProductLanguage", "1033" }, { "UserLanguageID", "1033" },
    { "SystemLanguageID", "1033" },
};

/* A property's value, or NULL when it has none. */
static const char *prop(const Props *p, const char *name, char *buffer, size_t size)
{
    if (p->extra) {
        size_t n = strlen(name);
        for (const char *line = p->extra; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL)
            if (!strncmp(line, name, n) && line[n] == '=') {
                const char *end = strchr(line + n + 1, '\n');
                size_t len = end ? (size_t)(end - line - n - 1) : strlen(line + n + 1);
                if (len >= size) len = size - 1;
                memcpy(buffer, line + n + 1, len);
                buffer[len] = 0;
                return buffer;
            }
    }
    if (p->table) {
        int key = column(p->table, "Property"), value = column(p->table, "Value");
        for (uint32_t row = 0; row < p->table->rows; row++) {
            const char *k = cell_str(p->msi, p->table, row, key);
            if (k && !strcmp(k, name)) return cell_str(p->msi, p->table, row, value);
        }
    }
    for (size_t i = 0; i < sizeof(builtin) / sizeof(builtin[0]); i++)
        if (!strcmp(builtin[i][0], name)) return builtin[i][1];
    return NULL;
}

typedef struct Cond {
    const char *at;
    const Props *props;
    int bad;
} Cond;

static void skip_space(Cond *c)
{
    while (*c->at == ' ' || *c->at == '\t' || *c->at == '\r' || *c->at == '\n') c->at++;
}

static int word_is(Cond *c, const char *word)
{
    size_t n = strlen(word);
    skip_space(c);
    if (strncasecmp(c->at, word, n)) return 0;
    char next = c->at[n];
    if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z') || (next >= '0' && next <= '9') || next == '_' ||
        next == '.')
        return 0;
    c->at += n;
    return 1;
}

/* A value: a string, an integer or a property (an environment variable, a component's or a
 * feature's state count as none). is_int when it is an integer. */
static void cond_value(Cond *c, char *out, size_t size, int *present)
{
    char buffer[256];
    skip_space(c);
    *present = 1;
    out[0] = 0;
    if (*c->at == '"') {
        const char *end = strchr(c->at + 1, '"');
        if (!end) {
            c->bad = 1;
            return;
        }
        size_t len = (size_t)(end - c->at - 1);
        if (len >= size) len = size - 1;
        memcpy(out, c->at + 1, len);
        out[len] = 0;
        c->at = end + 1;
        return;
    }
    if ((*c->at >= '0' && *c->at <= '9') || *c->at == '-') {
        size_t n = 0;
        if (*c->at == '-') out[n++] = *c->at++;
        while (*c->at >= '0' && *c->at <= '9' && n + 1 < size) out[n++] = *c->at++;
        out[n] = 0;
        return;
    }
    int special = *c->at == '%' || *c->at == '$' || *c->at == '?' || *c->at == '&' || *c->at == '!';
    if (special) c->at++;
    size_t n = 0;
    while (((*c->at >= 'A' && *c->at <= 'Z') || (*c->at >= 'a' && *c->at <= 'z') || (*c->at >= '0' && *c->at <= '9') ||
            *c->at == '_' || *c->at == '.') && n + 1 < sizeof(buffer))
        buffer[n++] = *c->at++;
    buffer[n] = 0;
    if (!n) {
        c->bad = 1;
        return;
    }
    if (special) {
        *present = 0;
        return;
    }
    const char *value = prop(c->props, buffer, out, size);
    if (!value || !value[0]) *present = 0;
    else if (value != out) pc5_copy(out, size, value);
}

static int is_number(const char *s, long *v)
{
    char *end;
    if (!s[0]) return 0;
    *v = strtol(s, &end, 10);
    return !*end;
}

static int cond_expr(Cond *c);

static int cond_factor(Cond *c)
{
    char a[256], b[256], op[3] = "";
    int has_a, has_b;
    skip_space(c);
    if (word_is(c, "NOT")) return !cond_factor(c);
    if (*c->at == '(') {
        c->at++;
        int v = cond_expr(c);
        skip_space(c);
        if (*c->at == ')') c->at++;
        else c->bad = 1;
        return v;
    }
    cond_value(c, a, sizeof(a), &has_a);
    if (c->bad) return 1;
    skip_space(c);
    int ci = 0;
    if (*c->at == '~') {
        ci = 1;
        c->at++;
    }
    static const char *const ops[] = { "<>", "><", "<<", ">>", ">=", "<=", "=", "<", ">" };
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
        if (!strncmp(c->at, ops[i], strlen(ops[i]))) {
            pc5_copy(op, sizeof(op), ops[i]);
            c->at += strlen(ops[i]);
            break;
        }
    if (!op[0]) {
        long v;
        return has_a && !(is_number(a, &v) && v == 0);
    }
    cond_value(c, b, sizeof(b), &has_b);
    if (c->bad) return 1;
    long x, y;
    if (is_number(a, &x) && is_number(b, &y)) {
        if (!strcmp(op, "=")) return x == y;
        if (!strcmp(op, "<>")) return x != y;
        if (!strcmp(op, "<")) return x < y;
        if (!strcmp(op, ">")) return x > y;
        if (!strcmp(op, "<=")) return x <= y;
        if (!strcmp(op, ">=")) return x >= y;
        if (!strcmp(op, "><")) return (x & y) != 0;
        return 0;
    }
    int order = ci ? strcasecmp(a, b) : strcmp(a, b);
    size_t la = strlen(a), lb = strlen(b);
    if (!strcmp(op, "=")) return order == 0;
    if (!strcmp(op, "<>")) return order != 0;
    if (!strcmp(op, "<")) return order < 0;
    if (!strcmp(op, ">")) return order > 0;
    if (!strcmp(op, "<=")) return order <= 0;
    if (!strcmp(op, ">=")) return order >= 0;
    if (!strcmp(op, "><")) return strstr(a, b) != NULL;
    if (!strcmp(op, "<<")) return lb <= la && !(ci ? strncasecmp(a, b, lb) : strncmp(a, b, lb));
    if (!strcmp(op, ">>")) return lb <= la && !(ci ? strcasecmp(a + la - lb, b) : strcmp(a + la - lb, b));
    return 0;
}

static int cond_and(Cond *c)
{
    int v = cond_factor(c);
    while (!c->bad && word_is(c, "AND")) {
        int w = cond_factor(c);
        v = v && w;
    }
    return v;
}

static int cond_expr(Cond *c)
{
    int v = cond_and(c);
    for (;;) {
        if (c->bad) return v;
        if (word_is(c, "OR")) {
            int w = cond_and(c);
            v = v || w;
        } else if (word_is(c, "XOR")) {
            int w = cond_and(c);
            v = v != w;
        } else if (word_is(c, "EQV")) {
            int w = cond_and(c);
            v = v == w;
        } else if (word_is(c, "IMP")) {
            int w = cond_and(c);
            v = !v || w;
        } else {
            return v;
        }
    }
}

static int condition(const Props *props, const char *text)
{
    if (!text || !text[0]) return 1;
    Cond c = { text, props, 0 };
    int v = cond_expr(&c);
    skip_space(&c);
    if (c.bad || *c.at) return 1;   /* not understood: as if true */
    return v;
}

int pc5_msi_condition(const char *text, const char *properties)
{
    Props props = { NULL, NULL, properties };
    return condition(&props, text);
}

/* ---- where things go ----------------------------------------------------------------------- */
/* Windows' folders as 64-bit Windows has them, by their Directory key: under drive_c, or NULL
 * for a folder of the user's (left out). */
static const struct { const char *key, *path; } folders[] = {
    { "TARGETDIR", "" },
    { "ROOTDRIVE", "" },
    { "WindowsVolume", "" },
    { "ProgramFilesFolder", "Program Files (x86)" },
    { "ProgramFiles64Folder", "Program Files" },
    { "CommonFilesFolder", "Program Files (x86)/Common Files" },
    { "CommonFiles64Folder", "Program Files/Common Files" },
    { "SystemFolder", "windows/syswow64" },
    { "System64Folder", "windows/system32" },
    { "System16Folder", "windows/system" },
    { "WindowsFolder", "windows" },
    { "CommonAppDataFolder", "ProgramData" },
    { "FontsFolder", "windows/Fonts" },
    { "AppDataFolder", NULL },        { "LocalAppDataFolder", NULL }, { "PersonalFolder", NULL },
    { "DesktopFolder", NULL },        { "ProgramMenuFolder", NULL },  { "StartMenuFolder", NULL },
    { "StartupFolder", NULL },        { "SendToFolder", NULL },       { "TemplateFolder", NULL },
    { "FavoritesFolder", NULL },      { "NetHoodFolder", NULL },      { "PrintHoodFolder", NULL },
    { "RecentFolder", NULL },         { "MyPicturesFolder", NULL },   { "AdminToolsFolder", NULL },
    { "TempFolder", NULL },
};

typedef struct Install {
    Msi *msi;
    Props props;
    Table dirs, comps, files, media, registry, env, property;
    char (*dir_path)[PC5_PATH];   /* by Directory row: its path under drive_c, "" C:\ itself */
    int8_t *dir_state;            /* 0 not yet, 1 resolved, -1 left out (the user's), -2 being resolved */
    int8_t *comp_on;              /* by Component row */
    const char *prefix_dir;
    Pc5MsiReport *report;
    /* While a cabinet is read: the File rows sorted by key, the one being written. */
    uint32_t *file_order;
    int *file_cab;                /* by File row: its Media row, -1 none */
    int writing_fd;
    char writing[PC5_PATH], writing_part[PC5_PATH];
    int cab;
    int failed;
    char error[PC5_ERROR];
} Install;

static int dir_row(const Install *in, const char *key)
{
    int col = column(&in->dirs, "Directory");
    for (uint32_t r = 0; r < in->dirs.rows; r++) {
        const char *k = cell_str(in->msi, &in->dirs, r, col);
        if (k && !strcmp(k, key)) return (int)r;
    }
    return -1;
}

/* The target long name of a DefaultDir ("target:source", each "short|long"). */
static void default_dir(const char *text, char *out, size_t size)
{
    char target[PC5_PATH];
    pc5_copy(target, sizeof(target), text ? text : ".");
    char *colon = strchr(target, ':');
    if (colon) *colon = 0;
    char *bar = strchr(target, '|');
    pc5_copy(out, size, bar ? bar + 1 : target);
}

/* The path under drive_c of a Directory key: 0, 1 when it is a folder of the user's, -1. */
static int dir_resolve(Install *in, const char *key, char *out, size_t size, int depth)
{
    for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
        if (!strcmp(folders[i].key, key)) {
            if (!folders[i].path) return 1;
            pc5_copy(out, size, folders[i].path);
            return 0;
        }
    int row = dir_row(in, key);
    if (row < 0 || depth > 64) return -1;
    if (in->dir_state[row] == 1) {
        pc5_copy(out, size, in->dir_path[row]);
        return 0;
    }
    if (in->dir_state[row] == -1) return 1;
    const char *parent = cell_str(in->msi, &in->dirs, (uint32_t)row, column(&in->dirs, "Directory_Parent"));
    char name[PC5_PATH], base[PC5_PATH];
    default_dir(cell_str(in->msi, &in->dirs, (uint32_t)row, column(&in->dirs, "DefaultDir")), name, sizeof(name));
    if (!parent || !strcmp(parent, key)) {
        base[0] = 0;   /* the root: C:\ */
        pc5_copy(name, sizeof(name), ".");
    } else {
        int status = dir_resolve(in, parent, base, sizeof(base), depth + 1);
        if (status) {
            if (status > 0) in->dir_state[row] = -1;
            return status;
        }
    }
    if (strchr(name, '/') || strchr(name, '\\') || !strcmp(name, "..")) return -1;
    if (!strcmp(name, ".") || !name[0]) pc5_copy(out, size, base);
    else if (pc5_format(out, size, "%s%s%s", base, base[0] ? "/" : "", name)) return -1;
    pc5_copy(in->dir_path[row], PC5_PATH, out);
    in->dir_state[row] = 1;
    return 0;
}

/* A path under drive_c as Windows writes it: "C:\Program Files\X" (with a backslash at the
 * end when slash). */
static void windows_path(const char *path, int slash, char *out, size_t size)
{
    size_t n = 0;
    if (size < 8) return;
    memcpy(out, "C:\\", 3);
    n = 3;
    for (const char *p = path; *p && n + 2 < size; p++) out[n++] = *p == '/' ? '\\' : *p;
    if (slash && n > 3 && n + 2 < size) out[n++] = '\\';
    out[n] = 0;
}

static int comp_row(const Install *in, const char *key)
{
    int col = column(&in->comps, "Component");
    for (uint32_t r = 0; r < in->comps.rows; r++) {
        const char *k = cell_str(in->msi, &in->comps, r, col);
        if (k && !strcmp(k, key)) return (int)r;
    }
    return -1;
}

/* A File row's target under drive_c: 0, 1 left out, -1. */
static int file_target(Install *in, uint32_t row, char *out, size_t size)
{
    const char *comp = cell_str(in->msi, &in->files, row, column(&in->files, "Component_"));
    int c = comp ? comp_row(in, comp) : -1;
    if (c < 0 || !in->comp_on[c]) return 1;
    const char *dir = cell_str(in->msi, &in->comps, (uint32_t)c, column(&in->comps, "Directory_"));
    char base[PC5_PATH], name[PC5_PATH];
    int status = dir ? dir_resolve(in, dir, base, sizeof(base), 0) : -1;
    if (status) return status;
    default_dir(cell_str(in->msi, &in->files, row, column(&in->files, "FileName")), name, sizeof(name));
    if (!name[0] || strchr(name, '/') || strchr(name, '\\') || !strcmp(name, ".") || !strcmp(name, "..")) return -1;
    return pc5_format(out, size, "%s%s%s", base, base[0] ? "/" : "", name) ? -1 : 0;
}

/* Formatted text ([Property], [#File], [$Component], [DirectoryKey], [\x]) into out: 0, or
 * -1 for a multi-string ([~]) or one that doesn't fit. */
static int format_text(Install *in, const char *text, char *out, size_t size)
{
    size_t n = 0;
    out[0] = 0;
    if (!text) return 0;
    for (const char *p = text; *p;) {
        const char *end = *p == '[' ? strchr(p, ']') : NULL;
        if (!end || end == p + 1) {
            if (n + 1 >= size) return -1;
            out[n++] = *p++;
            continue;
        }
        char name[256], value[PC5_PATH], buffer[PC5_PATH];
        size_t len = (size_t)(end - p - 1);
        if (len >= sizeof(name)) return -1;
        memcpy(name, p + 1, len);
        name[len] = 0;
        p = end + 1;
        value[0] = 0;
        if (!strcmp(name, "~")) return -1;
        if (name[0] == '\\') {
            pc5_copy(value, sizeof(value), name + 1);
        } else if (name[0] == '#' || name[0] == '!') {
            int key = column(&in->files, "File");
            for (uint32_t r = 0; r < in->files.rows; r++) {
                const char *k = cell_str(in->msi, &in->files, r, key);
                if (k && !strcmp(k, name + 1)) {
                    if (!file_target(in, r, buffer, sizeof(buffer))) windows_path(buffer, 0, value, sizeof(value));
                    break;
                }
            }
        } else if (name[0] == '$') {
            int c = comp_row(in, name + 1);
            const char *dir = c >= 0 ? cell_str(in->msi, &in->comps, (uint32_t)c, column(&in->comps, "Directory_")) : NULL;
            if (dir && !dir_resolve(in, dir, buffer, sizeof(buffer), 0)) windows_path(buffer, 1, value, sizeof(value));
        } else if (name[0] != '%') {
            int known = 0;
            for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) known |= !strcmp(folders[i].key, name);
            if ((known || dir_row(in, name) >= 0) && !dir_resolve(in, name, buffer, sizeof(buffer), 0)) {
                windows_path(buffer, 1, value, sizeof(value));
            } else {
                const char *v = prop(&in->props, name, buffer, sizeof(buffer));
                if (v) pc5_copy(value, sizeof(value), v);
            }
        }
        size_t vlen = strlen(value);
        if (n + vlen >= size) return -1;
        memcpy(out + n, value, vlen);
        n += vlen;
        out[n] = 0;
    }
    out[n] = 0;
    return 0;
}

/* ---- writing the files ------------------------------------------------------------------- */
static Install *sort_in;
static int cmp_file_key(const void *a, const void *b)
{
    int key = column(&sort_in->files, "File");
    const char *x = cell_str(sort_in->msi, &sort_in->files, *(const uint32_t *)a, key);
    const char *y = cell_str(sort_in->msi, &sort_in->files, *(const uint32_t *)b, key);
    return strcmp(x ? x : "", y ? y : "");
}

static int file_by_key(Install *in, const char *name)
{
    int key = column(&in->files, "File");
    uint32_t lo = 0, hi = in->files.rows;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const char *k = cell_str(in->msi, &in->files, in->file_order[mid], key);
        int order = strcmp(k ? k : "", name);
        if (!order) return (int)in->file_order[mid];
        if (order < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

static int set_error(Install *in, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int set_error(Install *in, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(in->error, sizeof(in->error), format, args);
    va_end(args);
    in->failed = 1;
    return -1;
}

static int out_begin(void *context, const char *name, uint32_t size)
{
    Install *in = context;
    char target[PC5_PATH], folder[PC5_PATH];
    (void)size;
    int row = file_by_key(in, name);
    if (row < 0 || in->file_cab[row] != in->cab) return 0;   /* not one this cabinet is used for */
    int status = file_target(in, (uint32_t)row, target, sizeof(target));
    if (status) {
        if (status > 0) in->report->skipped++;
        return 0;
    }
    if (pc5_format(in->writing, sizeof(in->writing), "%s/drive_c/%s", in->prefix_dir, target) ||
        pc5_format(in->writing_part, sizeof(in->writing_part), "%s.part", in->writing))
        return set_error(in, "a path is too long: %s", target);
    /* What is in C:\windows already stays: Wine's own DLLs. */
    if (!strncmp(target, "windows/", 8) && pc5_exists(in->writing)) {
        in->report->kept++;
        return 0;
    }
    pc5_copy(folder, sizeof(folder), in->writing);
    char *slash = strrchr(folder, '/');
    if (slash) *slash = 0;
    if (pc5_mkdirs(folder)) return set_error(in, "can't make %s", folder);
    in->writing_fd = open(in->writing_part, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (in->writing_fd < 0) return set_error(in, "can't write %s: %s", in->writing, strerror(errno));
    return 1;
}

static int out_write(void *context, const void *data, size_t size)
{
    Install *in = context;
    const uint8_t *at = data;
    while (size) {
        ssize_t put = write(in->writing_fd, at, size);
        if (put <= 0) return set_error(in, "can't write %s: %s", in->writing, strerror(put < 0 ? errno : ENOSPC));
        at += put;
        size -= (size_t)put;
    }
    return 0;
}

static int out_end(void *context)
{
    Install *in = context;
    int failed = close(in->writing_fd);
    in->writing_fd = -1;
    if (failed || rename(in->writing_part, in->writing)) {
        unlink(in->writing_part);
        return set_error(in, "can't write %s", in->writing);
    }
    in->report->files++;
    return 0;
}

typedef struct FileIn {
    int fd;
    uint64_t size;
} FileIn;

static int file_reader(void *context, uint64_t offset, void *out, size_t size)
{
    FileIn *f = context;
    uint8_t *at = out;
    if (offset > f->size || size > f->size - offset) return -1;
    while (size) {
        ssize_t got = pread(f->fd, at, size, (off_t)offset);
        if (got <= 0) return -1;
        at += got;
        offset += (uint64_t)got;
        size -= (size_t)got;
    }
    return 0;
}

/* The Media row's cabinet: in the package ("#name") or a file beside it. */
static int extract_cabinet(Install *in, const char *package, uint32_t media_row)
{
    const char *cabinet = cell_str(in->msi, &in->media, media_row, column(&in->media, "Cabinet"));
    Pc5CabOut out = { in, out_begin, out_write, out_end };
    char why[PC5_ERROR] = "";
    int taken;

    if (!cabinet || !cabinet[0]) return 0;
    in->cab = (int)media_row;
    if (cabinet[0] == '#') {
        Stream s;
        if (stream_open(&in->msi->cfb, cabinet + 1, &s)) return set_error(in, "its cabinet %s is missing", cabinet + 1);
        Pc5CabIn cab = { &s, s.size, stream_reader };
        taken = pc5_cab_extract(&cab, &out, why, sizeof(why));
        stream_close(&s);
    } else {
        char path[PC5_PATH], dir[PC5_PATH];
        pc5_copy(dir, sizeof(dir), package);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = 0;
        else pc5_copy(dir, sizeof(dir), ".");
        if (pc5_join(path, sizeof(path), dir, cabinet)) return set_error(in, "its cabinet's path is too long");
        FileIn f = { open(path, O_RDONLY), 0 };
        struct stat st;
        if (f.fd < 0 || fstat(f.fd, &st)) {
            if (f.fd >= 0) close(f.fd);
            return set_error(in, "its cabinet %s isn't beside it", cabinet);
        }
        f.size = (uint64_t)st.st_size;
        Pc5CabIn cab = { &f, f.size, file_reader };
        taken = pc5_cab_extract(&cab, &out, why, sizeof(why));
        close(f.fd);
    }
    if (in->writing_fd >= 0) {
        close(in->writing_fd);
        in->writing_fd = -1;
        unlink(in->writing_part);
    }
    if (taken < 0 && !in->failed) set_error(in, "%s", why);
    return taken < 0 ? -1 : 0;
}

/* ---- the registry and PATH ----------------------------------------------------------------- */
/* text as a .reg file's quoted string: \ and " escaped, other than ASCII as \xHHHH. */
static int reg_quote(Pc5Buf *buf, const char *text)
{
    uint16_t units[PC5_PATH];
    size_t count = pc5_utf16_from_utf8(units, sizeof(units) / sizeof(units[0]), text);
    for (size_t i = 0; i < count; i++) {
        uint16_t c = units[i];
        int status;
        if (c == '\\' || c == '"') status = pc5_buf_printf(buf, "\\%c", (char)c);
        else if (c == '\n') status = pc5_buf_puts(buf, "\\n");
        else if (c >= 0x20 && c < 0x7f) status = pc5_buf_printf(buf, "%c", (char)c);
        else status = pc5_buf_printf(buf, "\\x%04x", c);
        if (status) return -1;
    }
    return 0;
}

/* One Registry row into the text for system.reg (machine) or user.reg (user). */
static int registry_row(Install *in, uint32_t row, Pc5Buf *machine, Pc5Buf *user)
{
    const Table *t = &in->registry;
    const char *comp = cell_str(in->msi, t, row, column(t, "Component_"));
    int c = comp ? comp_row(in, comp) : -1;
    if (c < 0 || !in->comp_on[c]) return 0;
    long root = cell_int(t, row, column(t, "Root"), -2);
    char key[1024], name[512], value[2048], path[1200];
    if (format_text(in, cell_str(in->msi, t, row, column(t, "Key")), key, sizeof(key)) ||
        format_text(in, cell_str(in->msi, t, row, column(t, "Name")), name, sizeof(name)) ||
        format_text(in, cell_str(in->msi, t, row, column(t, "Value")), value, sizeof(value))) {
        in->report->skipped++;
        return 0;
    }
    const char *raw_value = cell_str(in->msi, t, row, column(t, "Value"));
    int bits64 = (cell_int(&in->comps, (uint32_t)c, column(&in->comps, "Attributes"), 0) & 0x100) != 0;
    Pc5Buf *to;
    if (root == -1 || root == 2) {   /* -1: the machine's, for all users (ALLUSERS=1) */
        to = machine;
        if (!strncasecmp(key, "Software\\", 9) && !bits64 && strncasecmp(key, "Software\\Classes\\", 17))
            snprintf(path, sizeof(path), "Software\\Wow6432Node\\%s", key + 9);
        else pc5_copy(path, sizeof(path), key);
    } else if (root == 0) {
        to = machine;
        snprintf(path, sizeof(path), "Software\\Classes\\%s", key);
    } else if (root == 1) {
        to = user;
        pc5_copy(path, sizeof(path), key);
    } else {
        in->report->skipped++;
        return 0;
    }
    if (!path[0]) return 0;
    /* "+", "-", "*" without a value make or delete the key: only made here. */
    int key_only = !raw_value && (!name[0] || !strcmp(name, "+") || !strcmp(name, "-") || !strcmp(name, "*"));
    if (!raw_value && !key_only) return 0;
    if (!strcmp(name, "-")) return 0;
    if (pc5_buf_puts(to, "\n[") || reg_quote(to, path) || pc5_buf_puts(to, "]\n")) return -1;
    if (key_only) return 0;
    if (!name[0] ? pc5_buf_puts(to, "@=") : (pc5_buf_puts(to, "\"") || reg_quote(to, name) || pc5_buf_puts(to, "\"=")))
        return -1;
    int status;
    if (raw_value[0] == '#' && (raw_value[1] == 'x' || raw_value[1] == 'X')) {
        status = pc5_buf_puts(to, "hex:");
        for (size_t i = 2; !status && value[i] && value[i + 1]; i += 2)
            status = pc5_buf_printf(to, "%s%c%c", i > 2 ? "," : "", pc5_lower((unsigned char)value[i]),
                                    pc5_lower((unsigned char)value[i + 1]));
        status = status || pc5_buf_puts(to, "\n");
    } else if (raw_value[0] == '#' && raw_value[1] == '%') {
        status = pc5_buf_puts(to, "str(2):\"") || reg_quote(to, value + 2) || pc5_buf_puts(to, "\"\n");
    } else if (raw_value[0] == '#' && raw_value[1] == '#') {
        status = pc5_buf_puts(to, "\"") || reg_quote(to, value + 1) || pc5_buf_puts(to, "\"\n");
    } else if (raw_value[0] == '#') {
        status = pc5_buf_printf(to, "dword:%08x\n", (uint32_t)strtol(value + 1, NULL, 10));
    } else {
        status = pc5_buf_puts(to, "\"") || reg_quote(to, value) || pc5_buf_puts(to, "\"\n");
    }
    if (!status) in->report->registry++;
    return status;
}

static int append_text(const char *path, const Pc5Buf *text)
{
    if (!text->len) return 0;
    FILE *file = fopen(path, "ab");
    if (!file) return -1;
    int ok = fwrite(text->data, 1, text->len, file) == text->len;
    return fclose(file) || !ok ? -1 : 0;
}

/* The folders an Environment row puts in PATH, one a line into lines. */
static void environment_row(Install *in, uint32_t row, Pc5Buf *lines)
{
    const Table *t = &in->env;
    const char *comp = cell_str(in->msi, t, row, column(t, "Component_"));
    int c = comp ? comp_row(in, comp) : -1;
    const char *name = cell_str(in->msi, t, row, column(t, "Name"));
    const char *value = cell_str(in->msi, t, row, column(t, "Value"));
    if (c < 0 || !in->comp_on[c] || !name || !value) return;
    while (*name == '=' || *name == '+' || *name == '-' || *name == '!' || *name == '*') name++;
    if (strcasecmp(name, "PATH")) return;
    /* "[~];folder", "folder;[~]": the folders around [~], each formatted. */
    char text[2048];
    pc5_copy(text, sizeof(text), value);
    for (char *part = strtok(text, ";"); part; part = strtok(NULL, ";")) {
        char folder[PC5_PATH];
        if (!strcmp(part, "[~]") || format_text(in, part, folder, sizeof(folder)) || !folder[0]) continue;
        size_t n = strlen(folder);
        if (n > 3 && folder[n - 1] == '\\') folder[n - 1] = 0;
        if (!pc5_buf_printf(lines, "%s\n", folder)) in->report->path++;
    }
}

/* ---- a package ------------------------------------------------------------------------------ */
static void install_free(Install *in)
{
    table_free(&in->dirs);
    table_free(&in->comps);
    table_free(&in->files);
    table_free(&in->media);
    table_free(&in->registry);
    table_free(&in->env);
    table_free(&in->property);
    pc5_free(in->dir_path);
    pc5_free(in->dir_state);
    pc5_free(in->comp_on);
    pc5_free(in->file_order);
    pc5_free(in->file_cab);
}

int pc5_msi_install(const char *package, uint64_t offset, const char *prefix_dir, Pc5MsiReport *report, char *error,
                    size_t size)
{
    Msi msi;
    Install in;
    char path[PC5_PATH];
    int status = -1;

    memset(report, 0, sizeof(*report));
    memset(&in, 0, sizeof(in));
    in.writing_fd = -1;
    if (msi_open(&msi, package, offset)) {
        pc5_copy(error, size, "it isn't a Windows Installer package PC5 can read");
        return -1;
    }
    in.msi = &msi;
    in.prefix_dir = prefix_dir;
    in.report = report;
    if (table_load(&msi, "Property", &in.property) < 0 || table_load(&msi, "Directory", &in.dirs) ||
        table_load(&msi, "Component", &in.comps) || table_load(&msi, "File", &in.files) ||
        table_load(&msi, "Media", &in.media) < 0 || table_load(&msi, "Registry", &in.registry) < 0 ||
        table_load(&msi, "Environment", &in.env) < 0) {
        pc5_copy(error, size, "its tables can't be read");
        goto done;
    }
    in.props = (Props){ &msi, &in.property, NULL };
    {
        char buffer[PC5_NAME];
        const char *name = prop(&in.props, "ProductName", buffer, sizeof(buffer));
        pc5_copy(report->product, sizeof(report->product), name ? name : "");
    }
    in.dir_path = pc5_alloc((in.dirs.rows + 1) * sizeof(*in.dir_path));
    in.dir_state = pc5_alloc(in.dirs.rows + 1);
    in.comp_on = pc5_alloc(in.comps.rows + 1);
    in.file_order = pc5_alloc((in.files.rows + 1) * sizeof(*in.file_order));
    in.file_cab = pc5_alloc((in.files.rows + 1) * sizeof(*in.file_cab));
    if (!in.dir_path || !in.dir_state || !in.comp_on || !in.file_order || !in.file_cab) {
        pc5_copy(error, size, "not enough memory");
        goto done;
    }
    /* The components whose condition holds. */
    int cond_col = column(&in.comps, "Condition");
    for (uint32_t r = 0; r < in.comps.rows; r++) {
        in.comp_on[r] = (int8_t)condition(&in.props, cell_str(&msi, &in.comps, r, cond_col));
        if (!in.comp_on[r]) report->skipped++;
    }
    /* Each file's cabinet: the Media row with the smallest LastSequence at or after its own. */
    int seq_col = column(&in.files, "Sequence"), last_col = column(&in.media, "LastSequence");
    for (uint32_t r = 0; r < in.files.rows; r++) {
        long seq = cell_int(&in.files, r, seq_col, 0), best = -1;
        in.file_cab[r] = -1;
        for (uint32_t m = 0; m < in.media.rows; m++) {
            long last = cell_int(&in.media, m, last_col, 0);
            if (last >= seq && (best < 0 || last < best)) {
                best = last;
                in.file_cab[r] = (int)m;
            }
        }
        in.file_order[r] = r;
    }
    sort_in = &in;
    qsort(in.file_order, in.files.rows, sizeof(*in.file_order), cmp_file_key);
    for (uint32_t m = 0; m < in.media.rows; m++)
        if (extract_cabinet(&in, package, m)) {
            pc5_copy(error, size, in.error);
            goto done;
        }
    /* Files the cabinets didn't hold (beside the package, uncompressed). */
    {
        int written = report->files + report->kept + report->skipped;
        if ((uint32_t)written < in.files.rows) report->skipped += (int)in.files.rows - written;
    }
    /* The registry, then PATH. */
    Pc5Buf machine = { 0 }, user = { 0 }, lines = { 0 };
    for (uint32_t r = 0; r < in.registry.rows; r++)
        if (registry_row(&in, r, &machine, &user)) {
            pc5_copy(error, size, "its registry values don't fit");
            pc5_buf_free(&machine);
            pc5_buf_free(&user);
            goto done;
        }
    for (uint32_t r = 0; r < in.env.rows; r++) environment_row(&in, r, &lines);
    int failed = 0;
    if (pc5_format(path, sizeof(path), "%s/system.reg", prefix_dir) || append_text(path, &machine)) failed = 1;
    if (pc5_format(path, sizeof(path), "%s/user.reg", prefix_dir) || append_text(path, &user)) failed = 1;
    if (lines.len) {
        char folder[PC5_PATH];
        if (pc5_format(folder, sizeof(folder), "%s/pc5", prefix_dir) || pc5_mkdirs(folder) ||
            pc5_format(path, sizeof(path), "%s/pc5/path.txt", prefix_dir) || append_text(path, &lines))
            failed = 1;
    }
    pc5_buf_free(&machine);
    pc5_buf_free(&user);
    pc5_buf_free(&lines);
    if (failed) {
        pc5_copy(error, size, "its registry values or PATH couldn't be written");
        goto done;
    }
    status = 0;
done:
    install_free(&in);
    msi_close(&msi);
    return status;
}

int64_t pc5_msi_embedded(const char *program)
{
    static const uint8_t signature[8] = { 0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1 };
    enum { CHUNK = 1 << 20 };
    uint8_t *buffer = pc5_alloc(CHUNK + 8);
    int fd = open(program, O_RDONLY);
    int64_t found = -1;
    uint64_t at = 0;
    size_t kept = 0;

    if (!buffer || fd < 0) {
        if (fd >= 0) close(fd);
        pc5_free(buffer);
        return -1;
    }
    while (found < 0 && at < (512u << 20)) {
        ssize_t got = read(fd, buffer + kept, CHUNK);
        if (got <= 0) break;
        size_t have = kept + (size_t)got;
        for (size_t i = 0; found < 0 && i + 8 <= have; i++) {
            if (buffer[i] != 0xd0 || memcmp(buffer + i, signature, 8)) continue;
            uint64_t offset = at - kept + i;
            Msi msi;
            if (!msi_open(&msi, program, offset)) {
                Table files;
                if (!table_load(&msi, "File", &files) && files.rows) found = (int64_t)offset;
                table_free(&files);
                msi_close(&msi);
            }
        }
        kept = have < 7 ? have : 7;
        memmove(buffer, buffer + have - kept, kept);
        at += (uint64_t)got;
    }
    close(fd);
    pc5_free(buffer);
    return found;
}

/* ---- a game's redistributables ------------------------------------------------------------- */
enum { REDIST_MAX = 32, REDIST_DEPTH = 5 };

typedef struct Found {
    char path[PC5_PATH];      /* on the disk */
    char relative[PC5_PATH];  /* in the game's folder */
    uint64_t size;
    int64_t mtime;
    int program;              /* a setup program that may carry a package */
} Found;

static int contains_ci(const char *text, const char *part)
{
    size_t n = strlen(part);
    for (; *text; text++)
        if (!strncasecmp(text, part, n)) return 1;
    return 0;
}

/* The folders redistributables are kept in. */
static int redist_folder(const char *name)
{
    static const char *const names[] = { "_CommonRedist", "CommonRedist", "Redist", "_Redist", "Redistributable",
                                         "Redistributables", "Support", "__Installer", "Installer", "Installers",
                                         "PhysX", "Prerequisites", "_Prerequisites", "Prereqs", "ISSetupPrerequisites",
                                         "Setup", "Extras" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcasecmp(name, names[i])) return 1;
    return 0;
}

/* Packages that put back what Wine already has, or that need what PC5 doesn't do. */
static int left_out(const char *name)
{
    static const char *const parts[] = { "vcredist", "vc_red", "vc_redist", "directx", "dxsetup", "dotnet", "ndp",
                                         "netfx", "xna", "oalinst", "uplay", "steam", "easyanticheat", "battleye",
                                         "eossdk" };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++)
        if (contains_ci(name, parts[i])) return 1;
    return 0;
}

static void find_in(const char *dir, const char *relative, int depth, int inside, Found *found, int *count)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while (*count < REDIST_MAX && (e = readdir(d))) {
        char path[PC5_PATH], rel[PC5_PATH];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        if (pc5_join(path, sizeof(path), dir, e->d_name) ||
            pc5_format(rel, sizeof(rel), "%s%s%s", relative, relative[0] ? "/" : "", e->d_name) || lstat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < REDIST_DEPTH && (inside || redist_folder(e->d_name))) find_in(path, rel, depth + 1, 1, found, count);
            continue;
        }
        if (!S_ISREG(st.st_mode) || left_out(e->d_name)) continue;
        int msi = pc5_ends_with_ci(e->d_name, ".msi");
        int program = pc5_ends_with_ci(e->d_name, ".exe") && contains_ci(e->d_name, "physx");
        if (!msi && !program) continue;
        if (!inside && !msi && !program) continue;
        Found *f = &found[(*count)++];
        pc5_copy(f->path, sizeof(f->path), path);
        pc5_copy(f->relative, sizeof(f->relative), rel);
        f->size = (uint64_t)st.st_size;
        f->mtime = (int64_t)st.st_mtime;
        f->program = program;
    }
    closedir(d);
}

static int redist_list_path(const char *root, const char *prefix, char *out, size_t size)
{
    return pc5_format(out, size, "%s/prefixes/%s/pc5/redist.txt", root, prefix);
}

/* 1 when redist.txt lists f with its size and time. */
static int listed(const Pc5Buf *list, const Found *f)
{
    char line[PC5_PATH + 64];
    if (!list->data) return 0;
    snprintf(line, sizeof(line), "%llu %lld ", (unsigned long long)f->size, (long long)f->mtime);
    size_t n = strlen(line), rlen = strlen(f->relative);
    for (const char *at = (const char *)list->data; at && *at; at = strchr(at, '\n') ? strchr(at, '\n') + 1 : NULL) {
        if (strncmp(at, line, n)) continue;
        const char *result = at + n, *space = strchr(result, ' ');
        if (space && !strncmp(space + 1, f->relative, rlen) && (space[1 + rlen] == '\n' || !space[1 + rlen])) return 1;
    }
    return 0;
}

static int found_new(const char *root, const char *prefix, const char *game_dir, Found *found)
{
    char path[PC5_PATH];
    Pc5Buf list = { 0 };
    int count = 0, kept = 0;

    if (!prefix || !prefix[0] || !strcmp(prefix, "default") || !game_dir || !pc5_is_dir(game_dir)) return 0;
    find_in(game_dir, "", 0, 0, found, &count);
    if (!redist_list_path(root, prefix, path, sizeof(path)) && pc5_is_file(path) && !pc5_read_file(path, &list, 1 << 20))
        pc5_buf_terminate(&list);
    for (int i = 0; i < count; i++)
        if (!listed(&list, &found[i])) found[kept++] = found[i];
    pc5_buf_free(&list);
    return kept;
}

int pc5_redist_pending(const char *root, const char *prefix, const char *game_dir, char *name, size_t size)
{
    Found *found = pc5_alloc(sizeof(*found) * REDIST_MAX);
    int count = found ? found_new(root, prefix, game_dir, found) : 0;
    if (count && name) {
        const char *base = strrchr(found[0].relative, '/');
        pc5_copy(name, size, contains_ci(found[0].relative, "physx") ? "PhysX" : base ? base + 1 : found[0].relative);
    }
    pc5_free(found);
    return count > 0;
}

static void say_line(void (*log)(void *, const char *), void *context, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
static void say_line(void (*log)(void *, const char *), void *context, const char *format, ...)
{
    char line[PC5_PATH + 400];
    va_list args;
    if (!log) return;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log(context, line);
}

int pc5_redist_install(const char *root, const char *prefix, const char *game_dir,
                       void (*log)(void *context, const char *line), void *context)
{
    char prefix_dir[PC5_PATH], list_path[PC5_PATH], folder[PC5_PATH];
    Found *found = pc5_alloc(sizeof(*found) * REDIST_MAX);
    int count = found ? found_new(root, prefix, game_dir, found) : 0, installed = 0, failed = 0;
    Pc5Buf lines = { 0 };

    if (!count || pc5_format(prefix_dir, sizeof(prefix_dir), "%s/prefixes/%s", root, prefix) ||
        redist_list_path(root, prefix, list_path, sizeof(list_path)) ||
        pc5_format(folder, sizeof(folder), "%s/pc5", prefix_dir) || pc5_mkdirs(folder)) {
        pc5_free(found);
        return count ? -1 : 0;
    }
    for (int i = 0; i < count; i++) {
        const Found *f = &found[i];
        const char *result;
        int64_t offset = f->program ? pc5_msi_embedded(f->path) : 0;
        if (offset < 0) {
            result = "none";
            say_line(log, context, "PC5 redist none in %s", f->relative);
        } else {
            Pc5MsiReport report;
            char error[PC5_ERROR];
            if (pc5_msi_install(f->path, (uint64_t)offset, prefix_dir, &report, error, sizeof(error))) {
                result = "failed";
                failed = 1;
                say_line(log, context, "PC5 problem redist %s: %s", f->relative, error);
            } else {
                result = "installed";
                installed++;
                say_line(log, context, "PC5 redist installed %s (%s) files=%d kept=%d skipped=%d registry=%d path=%d",
                         f->relative, report.product[0] ? report.product : "?", report.files, report.kept,
                         report.skipped, report.registry, report.path);
            }
        }
        pc5_buf_printf(&lines, "%llu %lld %s %s\n", (unsigned long long)f->size, (long long)f->mtime, result,
                       f->relative);
    }
    if (append_text(list_path, &lines)) failed = 1;
    pc5_buf_free(&lines);
    pc5_free(found);
    return failed ? -1 : installed;
}

/* ---- the PhysX runtime's registration ------------------------------------------------------- */

static int file_has(const char *path, const char *text)
{
    Pc5Buf data = { 0 };
    int has = !pc5_read_file(path, &data, 64u << 20) && !pc5_buf_terminate(&data) &&
              contains_ci((const char *)data.data, text);
    pc5_buf_free(&data);
    return has;
}

int pc5_physx_register(const char *root, const char *prefix, void (*log)(void *context, const char *line),
                       void *context)
{
    static const char engine_win[] = "C:\\Program Files (x86)\\NVIDIA Corporation\\PhysX\\Engine";
    static const char common_win[] = "C:\\Program Files (x86)\\NVIDIA Corporation\\PhysX\\Common";
    char prefix_dir[PC5_PATH], engine[PC5_PATH], common[PC5_PATH], path[PC5_PATH];
    int changed = 0;

    if (pc5_format(prefix_dir, sizeof(prefix_dir), "%s/prefixes/%s", root, prefix) ||
        pc5_format(engine, sizeof(engine), "%s/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine", prefix_dir) ||
        pc5_format(common, sizeof(common), "%s/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Common", prefix_dir) ||
        !pc5_is_dir(engine))
        return 0;
    /* PhysXLoader finds the engine by HKLM\Software\Ageia Technologies' "PhysXCore Path" (a
     * 32-bit game: Wow6432Node's), then its version's folder: a setup program's custom action
     * writes it, which PC5 doesn't run, and a runtime copied in by hand has none. */
    if (pc5_format(path, sizeof(path), "%s/system.reg", prefix_dir) == 0 && !file_has(path, "\"PhysXCore Path\"")) {
        Pc5Buf text = { 0 };
        int failed = 0;
        for (int view = 0; view < 2 && !failed; view++)
            failed = pc5_buf_puts(&text, view ? "\n[Software\\\\Wow6432Node\\\\Ageia Technologies]\n"
                                              : "\n[Software\\\\Ageia Technologies]\n") ||
                     pc5_buf_puts(&text, "\"PhysXCore Path\"=\"") || reg_quote(&text, engine_win) ||
                     pc5_buf_puts(&text, "\"\n");
        if (!failed && !append_text(path, &text)) changed |= 1;
        pc5_buf_free(&text);
    }
    /* PhysX 2.8.1's engine is also listed under PhysX_A32_Engines, by its setup program: a
     * game's PhysXLoader that finds no entry for it reports "Error initializing PhysX". */
    if (pc5_format(path, sizeof(path), "%s/v2.8.1", engine) == 0 && pc5_is_dir(path) &&
        pc5_format(path, sizeof(path), "%s/system.reg", prefix_dir) == 0 && !file_has(path, "PhysX_A32_Engines")) {
        Pc5Buf text = { 0 };
        int failed = 0;
        for (int view = 0; view < 2 && !failed; view++)
            failed = pc5_buf_puts(&text, view ? "\n[Software\\\\Wow6432Node\\\\Ageia Technologies\\\\PhysX_A32_Engines]\n"
                                              : "\n[Software\\\\Ageia Technologies\\\\PhysX_A32_Engines]\n") ||
                     pc5_buf_puts(&text, "\"2.8.1\"=dword:00000036\n");
        if (!failed && !append_text(path, &text)) changed |= 4;
        pc5_buf_free(&text);
    }
    /* The engine's PhysXCore.dll imports the CUDA runtime (cudart32_65.dll), which the setup
     * program puts in PhysX\Common or beside the engine and in PATH. */
    if (pc5_format(path, sizeof(path), "%s/pc5/path.txt", prefix_dir) == 0) {
        Pc5Buf lines = { 0 };
        char folder[PC5_PATH];
        int added = 0, failed = 0;
        if (pc5_is_dir(common) && !file_has(path, common_win)) {
            failed |= pc5_buf_printf(&lines, "%s\n", common_win);
            added |= 2;
        }
        DIR *d = opendir(engine);
        struct dirent *e;
        while (d && !failed && (e = readdir(d))) {
            char version[PC5_PATH], win[PC5_PATH];
            if ((e->d_name[0] != 'v' && e->d_name[0] != 'V') || !strchr(e->d_name, '.') ||
                pc5_join(version, sizeof(version), engine, e->d_name) || !pc5_is_dir(version) ||
                pc5_format(win, sizeof(win), "%s\\%s", engine_win, e->d_name) || file_has(path, win))
                continue;
            failed |= pc5_buf_printf(&lines, "%s\n", win);
            added |= 8;
        }
        if (d) closedir(d);
        if (added && !failed && !pc5_format(folder, sizeof(folder), "%s/pc5", prefix_dir) && !pc5_mkdirs(folder) &&
            !append_text(path, &lines))
            changed |= added;
        pc5_buf_free(&lines);
    }
    if (changed)
        say_line(log, context, "PC5 physx registered%s%s%s%s", changed & 1 ? " PhysXCore Path" : "",
                 changed & 4 ? " PhysX_A32_Engines 2.8.1" : "", changed & 2 ? " Common in PATH" : "",
                 changed & 8 ? " engine folders in PATH" : "");
    return changed;
}

/* ---- an Unreal Development Kit game's EULA ------------------------------------------------- */

/* A file in dir by its name in any case: 0 and its path in out, -1 for none. */
static int file_any_case(const char *dir, const char *name, char *out, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = -1;
    while (d && found && (e = readdir(d)))
        if (!strcasecmp(e->d_name, name) && !pc5_join(out, size, dir, e->d_name) && pc5_is_file(out)) found = 0;
    if (d) closedir(d);
    return found;
}

/* The install GUID in an InstallInfo.xml (<InstallGuidString>), checked: 0, or -1 for none. */
static int udk_guid(const char *path, char *out, size_t size)
{
    static const char open_tag[] = "<InstallGuidString>";
    Pc5Buf data = { 0 };
    const char *text, *start, *end;
    size_t n = 0;
    int bad = 0;
    if (pc5_read_file(path, &data, 1u << 20) || pc5_buf_terminate(&data)) {
        pc5_buf_free(&data);
        return -1;
    }
    text = (const char *)data.data;
    start = strstr(text, open_tag);
    end = start ? strstr(start, "</InstallGuidString>") : NULL;
    if (start && end) {
        start += sizeof(open_tag) - 1;
        while (start < end && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n' || *start == '{'))
            start++;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n' || end[-1] == '}'))
            end--;
        n = (size_t)(end - start);
        for (const char *c = start; c < end; c++)
            if (!isxdigit((unsigned char)*c) && *c != '-') bad = 1;
    }
    if (!start || !end || bad || n < 32 || n > 40 || n >= size) {
        pc5_buf_free(&data);
        return -1;
    }
    memcpy(out, start, n);
    out[n] = 0;
    pc5_buf_free(&data);
    return 0;
}

int pc5_udk_eula_register(const char *root, const char *prefix, const char *program,
                          void (*log)(void *context, const char *line), void *context)
{
    char dir[PC5_PATH], info[PC5_PATH], guid[48], key[64], path[PC5_PATH];
    char *slash;
    int found = -1;

    if (!program || !program[0]) return 0;
    /* InstallInfo.xml is in the game's Binaries, beside or above its program
     * (Binaries/Win64/UDK.exe, Binaries/Win32/UDK.exe). */
    pc5_copy(dir, sizeof(dir), program);
    for (int up = 0; up < 3 && found; up++) {
        if (!(slash = strrchr(dir, '/'))) break;
        *slash = 0;
        found = file_any_case(dir, "InstallInfo.xml", info, sizeof(info));
    }
    if (found || udk_guid(info, guid, sizeof(guid)) || pc5_format(key, sizeof(key), "UDK-%s", guid) ||
        pc5_format(path, sizeof(path), "%s/prefixes/%s/system.reg", root, prefix) || !pc5_is_file(path) ||
        file_has(path, key))
        return 0;
    /* An Unreal Development Kit game asks its UnSetup.exe to show Epic's EULA until the
     * install it was made from says it was accepted: Uninstall\UDK-<its GUID>'s EULAAccepted,
     * which Steam's install writes. The console can't start UnSetup.exe, and the game stops
     * ("Failed to launch UnSetup.exe. Please reinstall."). */
    Pc5Buf text = { 0 };
    int failed = 0;
    for (int view = 0; view < 2 && !failed; view++)
        failed = pc5_buf_printf(&text, "\n[Software\\\\%sMicrosoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\%s]\n",
                                view ? "Wow6432Node\\\\" : "", key) ||
                 pc5_buf_puts(&text, "\"EULAAccepted\"=dword:00000001\n");
    if (!failed) failed = append_text(path, &text);
    pc5_buf_free(&text);
    if (failed) return -1;
    say_line(log, context, "PC5 udk eula accepted %s", key);
    return 1;
}

