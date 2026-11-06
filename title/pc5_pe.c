/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: reading a Windows program's PE file (pc5_pe.h). The layout is the one
 * Microsoft documents ("PE Format"); every offset and size read from the file
 * is checked against the file before it is used. */
#include "pc5_pe.h"
#include "pc5_image.h"

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct Section {
    uint32_t va, vsize, raw, rawsize;
    char name[9];
} Section;

typedef struct Pe {
    int fd;
    uint64_t file_size;
    uint64_t image_base;
    uint16_t machine, characteristics, subsystem;
    int bits;
    uint32_t dir_rva[16], dir_size[16];
    uint32_t entry;   /* AddressOfEntryPoint */
    Section sections[96];
    int section_count;
} Pe;

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }

static int read_at(const Pe *pe, uint64_t offset, void *out, size_t size)
{
    uint8_t *at = out;

    if (offset > pe->file_size || size > pe->file_size - offset) return -1;
    while (size) {
        ssize_t got = pread(pe->fd, at, size, (off_t)offset);
        if (got <= 0) return -1;
        at += got;
        offset += (uint64_t)got;
        size -= (size_t)got;
    }
    return 0;
}

/* The file offset of rva, with how many bytes of its section follow it. */
static int rva_offset(const Pe *pe, uint32_t rva, uint64_t *offset, uint32_t *left)
{
    for (int i = 0; i < pe->section_count; i++) {
        const Section *s = &pe->sections[i];
        uint32_t span = s->vsize > s->rawsize ? s->vsize : s->rawsize;
        if (rva >= s->va && rva - s->va < span) {
            uint32_t into = rva - s->va;
            if (into >= s->rawsize) return -1;   /* in the zero-filled tail */
            *offset = (uint64_t)s->raw + into;
            *left = s->rawsize - into;
            return 0;
        }
    }
    return -1;
}

static int read_rva(const Pe *pe, uint32_t rva, void *out, size_t size)
{
    uint64_t offset;
    uint32_t left;

    if (rva_offset(pe, rva, &offset, &left) || size > left) return -1;
    return read_at(pe, offset, out, size);
}

/* A 0-terminated ASCII name at rva, lower-cased. */
static int read_name(const Pe *pe, uint32_t rva, char *out, size_t size)
{
    uint64_t offset;
    uint32_t left;
    size_t n;

    if (rva_offset(pe, rva, &offset, &left) || !size) return -1;
    n = left < size ? left : size;
    if (read_at(pe, offset, out, n)) return -1;
    for (size_t i = 0; i < n; i++) {
        if (!out[i]) return 0;
        out[i] = (char)pc5_lower((unsigned char)out[i]);
    }
    return -1;   /* longer than a library name can be */
}

static int pe_open(const char *path, Pe *pe)
{
    uint8_t dos[64], head[24 + 240];
    struct stat st;
    uint32_t lfanew;
    uint16_t sections, optional_size, magic;
    const uint8_t *opt;
    uint32_t dirs_at, dir_count;

    memset(pe, 0, sizeof(*pe));
    pe->fd = open(path, O_RDONLY);
    if (pe->fd < 0) return -1;
    if (fstat(pe->fd, &st) || !S_ISREG(st.st_mode)) goto fail;
    pe->file_size = (uint64_t)st.st_size;
    if (read_at(pe, 0, dos, sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z') goto fail;
    lfanew = u32(dos + 0x3c);
    if (lfanew < 64 || read_at(pe, lfanew, head, 24)) goto fail;
    if (memcmp(head, "PE\0\0", 4)) goto fail;
    pe->machine = u16(head + 4);
    sections = u16(head + 6);
    optional_size = u16(head + 20);
    pe->characteristics = u16(head + 22);
    if (pe->machine != 0x14c && pe->machine != 0x8664) goto fail;
    if (optional_size < 96 || optional_size > 240 || read_at(pe, (uint64_t)lfanew + 24, head + 24, optional_size))
        goto fail;
    opt = head + 24;
    magic = u16(opt);
    if (magic == 0x10b) {
        pe->bits = 32;
        pe->image_base = u32(opt + 28);
        dirs_at = 96;
    } else if (magic == 0x20b) {
        pe->bits = 64;
        if (optional_size < 112) goto fail;
        pe->image_base = u64(opt + 24);
        dirs_at = 112;
    } else {
        goto fail;
    }
    pe->subsystem = u16(opt + 68);
    pe->entry = u32(opt + 16);
    dir_count = u32(opt + dirs_at - 4);
    if (dir_count > 16) dir_count = 16;
    for (uint32_t i = 0; i < dir_count && dirs_at + 8 * i + 8 <= optional_size; i++) {
        pe->dir_rva[i] = u32(opt + dirs_at + 8 * i);
        pe->dir_size[i] = u32(opt + dirs_at + 8 * i + 4);
    }
    if (sections > 96) goto fail;
    for (int i = 0; i < sections; i++) {
        uint8_t s[40];
        if (read_at(pe, (uint64_t)lfanew + 24 + optional_size + 40u * (unsigned)i, s, sizeof(s))) goto fail;
        pe->sections[i] = (Section){ u32(s + 12), u32(s + 8), u32(s + 20), u32(s + 16), { 0 } };
        memcpy(pe->sections[i].name, s, 8);
    }
    pe->section_count = sections;
    return 0;
fail:
    close(pe->fd);
    pe->fd = -1;
    return -1;
}

static void pe_close(Pe *pe)
{
    if (pe->fd >= 0) close(pe->fd);
    pe->fd = -1;
}

static uint32_t import_bit(const char *name)
{
    static const struct { const char *name; uint32_t bit; } known[] = {
        { "d3d8.dll", PC5_IMPORT_D3D8 },       { "d3d9.dll", PC5_IMPORT_D3D9 },
        { "d3d10.dll", PC5_IMPORT_D3D10 },     { "d3d10_1.dll", PC5_IMPORT_D3D10 },
        { "d3d10core.dll", PC5_IMPORT_D3D10 }, { "d3d11.dll", PC5_IMPORT_D3D11 },
        { "dxgi.dll", PC5_IMPORT_DXGI },       { "d3d12.dll", PC5_IMPORT_D3D12 },
        { "opengl32.dll", PC5_IMPORT_OPENGL }, { "vulkan-1.dll", PC5_IMPORT_VULKAN },
        { "ddraw.dll", PC5_IMPORT_DDRAW },     { "mscoree.dll", PC5_IMPORT_DOTNET },
        { "steam_api.dll", PC5_IMPORT_STEAM }, { "steam_api64.dll", PC5_IMPORT_STEAM },
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (!strcmp(name, known[i].name)) return known[i].bit;
    return 0;
}

static uint32_t read_imports(const Pe *pe)
{
    uint32_t bits = 0;
    char name[64];

    /* Import descriptors: 20 bytes each, the name's RVA at 12, a zeroed one last. */
    if (pe->dir_rva[1]) {
        for (uint32_t i = 0; i < 4096; i++) {
            uint8_t d[20];
            if (read_rva(pe, pe->dir_rva[1] + 20 * i, d, sizeof(d))) break;
            if (!u32(d + 12) && !u32(d)) break;
            if (!read_name(pe, u32(d + 12), name, sizeof(name))) bits |= import_bit(name);
        }
    }
    /* Delay-load descriptors: 32 bytes, attributes at 0 (bit 0: RVAs, else
     * addresses), the name at 4. */
    if (pe->dir_rva[13]) {
        for (uint32_t i = 0; i < 1024; i++) {
            uint8_t d[32];
            uint32_t at;
            if (read_rva(pe, pe->dir_rva[13] + 32 * i, d, sizeof(d))) break;
            if (!u32(d + 4)) break;
            at = u32(d + 4);
            if (!(u32(d) & 1)) at = (uint32_t)((uint64_t)at - pe->image_base);
            /* Steam's library delay-loaded is only loaded when the game asks for it, and a game
             * that does copes without it (an Unreal Engine game from another store): not counted. */
            if (!read_name(pe, at, name, sizeof(name))) bits |= import_bit(name) & ~(uint32_t)PC5_IMPORT_STEAM;
        }
    }
    /* A CLR header (directory 14) is a .NET program, whatever it imports. */
    if (pe->dir_rva[14] && pe->dir_size[14]) bits |= PC5_IMPORT_DOTNET;
    return bits;
}

/* ---- resources ----------------------------------------------------------------- */

/* A resource directory's entries: the id (or -1 for a named one) and where it
 * leads (bit 31: another directory). */
typedef struct ResEntry { int32_t id; uint32_t target; } ResEntry;

static int res_dir(const Pe *pe, uint32_t at, ResEntry *out, int max)
{
    uint8_t head[16];
    int count = 0, total;

    if (read_rva(pe, pe->dir_rva[2] + at, head, sizeof(head))) return -1;
    total = u16(head + 12) + u16(head + 14);
    for (int i = 0; i < total && count < max; i++) {
        uint8_t e[8];
        if (read_rva(pe, pe->dir_rva[2] + at + 16 + 8u * (unsigned)i, e, sizeof(e))) break;
        uint32_t name = u32(e);
        out[count++] = (ResEntry){ (name & 0x80000000u) ? -1 : (int32_t)(name & 0xffff), u32(e + 4) };
    }
    return count;
}

/* The data of a resource: type, then the first (or the given) name, then the
 * first language. */
static int res_find(const Pe *pe, int type, int name, uint32_t *rva, uint32_t *size)
{
    ResEntry level[256];
    uint32_t at = 0;
    int count;

    if (!pe->dir_rva[2]) return -1;
    for (int depth = 0; depth < 3; depth++) {
        int want = depth == 0 ? type : depth == 1 ? name : -2, found = -1;
        if ((count = res_dir(pe, at, level, 256)) <= 0) return -1;
        for (int i = 0; i < count && found < 0; i++)
            if (want == -2 || level[i].id == want) found = i;
        if (found < 0) return -1;
        if (depth < 2 && !(level[found].target & 0x80000000u)) return -1;
        at = level[found].target & 0x7fffffffu;
    }
    {
        uint8_t data[16];
        if (read_rva(pe, pe->dir_rva[2] + at, data, sizeof(data))) return -1;
        *rva = u32(data);
        *size = u32(data + 4);
    }
    return *size ? 0 : -1;
}

/* UTF-16LE to UTF-8, stopping at a 0 or the end. */
static void utf16_to_utf8(const uint8_t *p, size_t units, char *out, size_t size)
{
    size_t n = 0;

    for (size_t i = 0; i < units; i++) {
        uint32_t c = u16(p + 2 * i);
        if (!c) break;
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < units) {
            uint32_t low = u16(p + 2 * i + 2);
            if (low >= 0xdc00 && low < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i++;
            }
        }
        if (c < 0x20) continue;
        if (c < 0x80) { if (n + 1 >= size) break; out[n++] = (char)c; }
        else if (c < 0x800) { if (n + 2 >= size) break; out[n++] = (char)(0xc0 | c >> 6); out[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            if (n + 3 >= size) break;
            out[n++] = (char)(0xe0 | c >> 12); out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63));
        } else {
            if (n + 4 >= size) break;
            out[n++] = (char)(0xf0 | c >> 18); out[n++] = (char)(0x80 | ((c >> 12) & 63));
            out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63));
        }
    }
    out[n] = 0;
    /* Trim spaces at both ends. */
    while (n && out[n - 1] == ' ') out[--n] = 0;
    if (out[0] == ' ') {
        size_t skip = 0;
        while (out[skip] == ' ') skip++;
        memmove(out, out + skip, n - skip + 1);
    }
}

static int key_is(const uint8_t *key, size_t units, const char *wanted)
{
    size_t i = 0;
    for (; i < units && wanted[i]; i++)
        if (u16(key + 2 * i) != (uint8_t)wanted[i]) return 0;
    return !wanted[i] && i < units && u16(key + 2 * i) == 0;
}

/* Walks a version-information block (wLength, wValueLength, wType, key, value,
 * children, each 32-bit aligned) and keeps the two strings PC5 looks for. */
static void version_walk(const uint8_t *data, size_t size, int depth, Pc5PeInfo *info)
{
    size_t at = 0;

    while (depth < 4 && at + 6 <= size) {
        size_t length = u16(data + at), value_length = u16(data + at + 2), type = u16(data + at + 4);
        size_t key = at + 6, key_units = 0, value;
        if (length < 6 || length > size - at) break;
        while (key + 2 * key_units + 1 < at + length && u16(data + key + 2 * key_units)) key_units++;
        value = (key + 2 * key_units + 2 + 3) & ~(size_t)3;
        if (depth == 3 && value < at + length) {
            size_t units = (at + length - value) / 2;
            if (value_length && value_length < units) units = value_length;
            if (key_is(data + key, key_units + 1, "ProductName") && !info->product[0])
                utf16_to_utf8(data + value, units, info->product, sizeof(info->product));
            if (key_is(data + key, key_units + 1, "FileDescription") && !info->description[0])
                utf16_to_utf8(data + value, units, info->description, sizeof(info->description));
        } else {
            size_t children = value + (depth == 0 ? value_length : (type == 1 ? value_length * 2 : value_length));
            children = (children + 3) & ~(size_t)3;
            /* Only StringFileInfo is gone into, not VarFileInfo. */
            int into = depth != 1 || key_is(data + key, key_units + 1, "StringFileInfo");
            if (into && children < at + length)
                version_walk(data + children, at + length - children, depth + 1, info);
        }
        at = (at + length + 3) & ~(size_t)3;
        if (depth == 0) break;   /* one VS_VERSIONINFO */
    }
}

static void read_version(const Pe *pe, Pc5PeInfo *info)
{
    uint32_t rva, size;
    uint8_t *data;

    if (res_find(pe, 16, -2, &rva, &size) || size > 256 * 1024) return;
    if (!(data = pc5_alloc(size))) return;
    if (!read_rva(pe, rva, data, size)) version_walk(data, size, 0, info);
    pc5_free(data);
}

int pc5_pe_read(const char *path, Pc5PeInfo *info)
{
    Pe pe;

    memset(info, 0, sizeof(*info));
    if (pe_open(path, &pe)) return -1;
    info->bits = pe.bits;
    info->dll = (pe.characteristics & 0x2000) != 0;
    info->gui = pe.subsystem == 2;
    info->imports = read_imports(&pe);
    read_version(&pe, info);
    pe_close(&pe);
    return 0;
}

/* name (lower case) at p, in ASCII, or in UTF-16 (wide), any case. */
static int names_at(const uint8_t *p, size_t left, const char *name, int wide)
{
    size_t n = strlen(name), step = wide ? 2 : 1;
    if (left < n * step) return 0;
    for (size_t i = 0; i < n; i++)
        if (pc5_lower(p[i * step]) != (unsigned char)name[i] || (wide && p[i * step + 1])) return 0;
    return 1;
}

uint32_t pc5_pe_named_libraries(const char *path)
{
    static const struct { const char *name; uint32_t bit; } named[] = {
        { "vulkan-1", PC5_IMPORT_VULKAN }, { "d3d12.dll", PC5_IMPORT_D3D12 }, { "d3d11.dll", PC5_IMPORT_D3D11 },
        { "dxgi.dll", PC5_IMPORT_DXGI },   { "d3d10", PC5_IMPORT_D3D10 },     { "d3d9.dll", PC5_IMPORT_D3D9 },
        { "d3d8.dll", PC5_IMPORT_D3D8 },   { "opengl32", PC5_IMPORT_OPENGL }, { "ddraw.dll", PC5_IMPORT_DDRAW },
    };
    enum { CHUNK = 1 << 20, KEEP = 32 };
    const uint64_t most = (uint64_t)1 << 30;
    uint8_t *buf;
    uint64_t total = 0;
    uint32_t bits = 0;
    size_t kept = 0;
    ssize_t got;
    int fd;

    if ((fd = open(path, O_RDONLY)) < 0) return 0;
    if (!(buf = pc5_alloc(CHUNK + KEEP))) {
        close(fd);
        return 0;
    }
    while (total < most && (got = read(fd, buf + kept, CHUNK)) > 0) {
        size_t have = kept + (size_t)got;
        total += (uint64_t)got;
        for (size_t at = 0; at < have; at++) {
            uint8_t c = (uint8_t)pc5_lower(buf[at]);
            if (c != 'v' && c != 'd' && c != 'o') continue;
            for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
                if (!(bits & named[i].bit) && (names_at(buf + at, have - at, named[i].name, 0) ||
                                               names_at(buf + at, have - at, named[i].name, 1)))
                    bits |= named[i].bit;
        }
        /* The tail again with the next chunk: a name may run across the two. */
        kept = have < KEEP ? have : KEEP;
        memmove(buf, buf + have - kept, kept);
    }
    pc5_free(buf);
    close(fd);
    return bits;
}

Pc5Api pc5_api_from_imports(uint32_t imports)
{
    if (imports & PC5_IMPORT_D3D12) return PC5_API_D3D12;
    if (imports & PC5_IMPORT_D3D11) return PC5_API_D3D11;
    if (imports & PC5_IMPORT_D3D10) return PC5_API_D3D10;
    if (imports & PC5_IMPORT_D3D9) return PC5_API_D3D9;
    if (imports & PC5_IMPORT_D3D8) return PC5_API_D3D8;
    if (imports & PC5_IMPORT_VULKAN) return PC5_API_VULKAN;
    if (imports & PC5_IMPORT_DXGI) return PC5_API_D3D11;   /* dxgi alone: 10/11 loaded at run time */
    if (imports & PC5_IMPORT_OPENGL) return PC5_API_OPENGL;
    if (imports & PC5_IMPORT_DDRAW) return PC5_API_DDRAW;
    return PC5_API_GDI;
}

/* 1 when the program exports name (lower case), as the Direct3D 12 Agility SDK has a program
 * export D3D12SDKVersion: the runtime looks it up by that name, so no packer hides it. */
static int exports(const Pe *pe, const char *name)
{
    uint8_t d[40];
    char got[64];
    uint32_t count, names;

    if (!pe->dir_rva[0] || read_rva(pe, pe->dir_rva[0], d, sizeof(d))) return 0;
    count = u32(d + 24);
    names = u32(d + 32);
    for (uint32_t i = 0; i < count && i < 65536; i++) {
        uint8_t at[4];
        if (read_rva(pe, names + 4 * i, at, sizeof(at))) return 0;
        if (!read_name(pe, u32(at), got, sizeof(got)) && !strcmp(got, name)) return 1;
    }
    return 0;
}

/* name in dir, its case aside, into out: 1 when there is one. */
static int entry_ci(const char *dir, const char *name, char *out, size_t size)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int found = 0;
    if (!d) return 0;
    while (!found && (e = readdir(d)))
        if (!pc5_casecmp(e->d_name, name)) found = !pc5_join(out, size, dir, e->d_name);
    closedir(d);
    return found;
}

/* 1 when the program ships the Direct3D 12 Agility SDK: D3D12\D3D12Core.dll beside it (any
 * case), or the SDK's export in it. Only a program that draws with Direct3D 12 has either. */
static int agility_sdk(const char *path)
{
    char dir[PC5_PATH], sub[PC5_PATH], core[PC5_PATH], x64[PC5_PATH];
    char *slash;
    Pe pe;
    int found = 0;

    pc5_copy(dir, sizeof(dir), path);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        /* D3D12\D3D12Core.dll, or D3D12\x64\D3D12Core.dll as some ship it (Gears of War: E-Day). */
        if (entry_ci(dir, "d3d12", sub, sizeof(sub)) &&
            (entry_ci(sub, "d3d12core.dll", core, sizeof(core)) ||
             (entry_ci(sub, "x64", x64, sizeof(x64)) && entry_ci(x64, "d3d12core.dll", core, sizeof(core)))))
            return 1;
    }
    if (!pe_open(path, &pe)) {
        found = exports(&pe, "d3d12sdkversion");
        pe_close(&pe);
    }
    return found;
}

int pc5_pe_unreal_d3d12(const char *path, uint32_t imports)
{
    char lower[PC5_PATH];
    size_t n = 0;

    if (imports & PC5_IMPORT_D3D12) return 0;
    for (const char *p = path; *p && n + 1 < sizeof(lower); p++) lower[n++] = *p == '\\' ? '/' : (char)pc5_lower((unsigned char)*p);
    lower[n] = 0;
    /* Games that draw only with Direct3D 12 and load it themselves, outside Unreal: given
     * Wine's own d3d12 beside DXVK's dxgi, they find no Direct3D 12 and say so. Their own
     * imports may name no Direct3D at all (Spider-Man.exe imports only vulkan-1.dll), so
     * the name is enough. */
    static const char *const d3d12_only[] = { "spider-man.exe", "milesmorales.exe", "riftapart.exe" };
    const char *name = strrchr(lower, '/') ? strrchr(lower, '/') + 1 : lower;
    for (size_t i = 0; i < sizeof(d3d12_only) / sizeof(d3d12_only[0]) && !(imports & (PC5_IMPORT_D3D9 | PC5_IMPORT_D3D8)); i++)
        if (!strcmp(name, d3d12_only[i])) return 1;
    if (!(imports & (PC5_IMPORT_D3D11 | PC5_IMPORT_DXGI | PC5_IMPORT_D3D10))) return 0;
    /* An Unreal game's program is in a folder of its Binaries: Win64, or the store's build's
     * (Gears Tactics' Binaries\Steam), but not a 32-bit one. */
    const char *folder = NULL;
    for (const char *at = lower; (at = strstr(at, "binaries/")); at++)
        if (at == lower || at[-1] == '/') folder = at + 9;
    const char *slash = folder ? strchr(folder, '/') : NULL;
    if (!slash || slash == folder || strchr(slash + 1, '/') || !strncmp(folder, "win32/", 6)) return 0;
    /* The Agility SDK first: some programs (a large or packed one) don't show d3d12.dll's
     * name where pc5_pe_named_libraries looks. */
    return agility_sdk(path) || (pc5_pe_named_libraries(path) & PC5_IMPORT_D3D12) != 0;
}

/* 1 when the file at path holds text (ASCII, as it is), in its first GB. */
static int file_has_text(const char *path, const char *text)
{
    enum { CHUNK = 1 << 20, KEEP = 64 };
    const size_t n = strlen(text);
    const uint64_t most = (uint64_t)1 << 30;
    uint64_t total = 0;
    size_t kept = 0;
    ssize_t got;
    uint8_t *buf;
    int fd, found = 0;

    if (!n || n > KEEP || (fd = open(path, O_RDONLY)) < 0) return 0;
    if (!(buf = pc5_alloc(CHUNK + KEEP))) {
        close(fd);
        return 0;
    }
    while (!found && total < most && (got = read(fd, buf + kept, CHUNK)) > 0) {
        size_t have = kept + (size_t)got;
        total += (uint64_t)got;
        for (const uint8_t *at = buf; !found && (at = memchr(at, (unsigned char)text[0], have - (size_t)(at - buf)));
             at++)
            found = (size_t)(buf + have - at) >= n && !memcmp(at, text, n);
        /* The tail again with the next chunk: the text may run across the two. */
        kept = have < KEEP ? have : KEEP;
        memmove(buf, buf + have - kept, kept);
    }
    pc5_free(buf);
    close(fd);
    return found;
}

int pc5_pe_godot(const char *path)
{
    char pck[PC5_PATH];
    size_t n = strlen(path);
    struct stat st;

    if (n > 4 && n < sizeof(pck) && !pc5_casecmp(path + n - 4, ".exe")) {
        memcpy(pck, path, n - 4);
        memcpy(pck + n - 4, ".pck", 5);
        if (!stat(pck, &st) && S_ISREG(st.st_mode)) return 1;
    }
    return file_has_text(path, "Godot Engine");
}

uint32_t pc5_pe_unity_direct3d(const char *path, uint32_t imports)
{
    const uint32_t direct3d = PC5_IMPORT_D3D8 | PC5_IMPORT_D3D9 | PC5_IMPORT_D3D10 | PC5_IMPORT_D3D11 | PC5_IMPORT_DXGI;
    char data[PC5_PATH];
    size_t n = strlen(path);
    struct stat st;

    uint32_t drawing = imports & PC5_IMPORT_DRAWING;
    if (drawing && drawing != PC5_IMPORT_OPENGL) return 0;
    if (n <= 4 || n + 1 >= sizeof(data) || pc5_casecmp(path + n - 4, ".exe")) return 0;
    memcpy(data, path, n - 4);
    memcpy(data + n - 4, "_Data", 6);
    if (stat(data, &st) || !S_ISDIR(st.st_mode)) return 0;
    /* Unity 5.5 and before: the player is the program itself. */
    if (drawing) return pc5_pe_named_libraries(path) & direct3d;
    /* Unity 2017 and after: the program only starts UnityPlayer.dll beside it, which imports
     * OpenGL and loads Direct3D 11 at run time (Direct3D 12 or Vulkan only when the game asks
     * for them); its libraries beside it say OpenGL, or Direct3D 12 with the D3D12 folder. */
    char dir[PC5_PATH], player[PC5_PATH];
    DIR *d;
    struct dirent *e;
    int found = 0;
    memcpy(dir, path, n + 1);
    char *slash = strrchr(dir, '/');
    if (!slash) return 0;
    *slash = 0;
    if (!(d = opendir(dir))) return 0;
    while (!found && (e = readdir(d)))
        found = !pc5_casecmp(e->d_name, "UnityPlayer.dll") && !pc5_join(player, sizeof(player), dir, e->d_name);
    closedir(d);
    return found && (pc5_pe_named_libraries(player) & PC5_IMPORT_D3D11) ? PC5_IMPORT_D3D11 : 0;
}

int pc5_pe_section(const char *path, const char *name)
{
    Pe pe;
    int found = 0;
    if (pe_open(path, &pe)) return 0;
    for (int i = 0; i < pe.section_count && !found; i++) found = !strcmp(pe.sections[i].name, name);
    pe_close(&pe);
    return found;
}

int pc5_pe_entry_in(const char *path, const char *name)
{
    Pe pe;
    int found = 0;
    if (pe_open(path, &pe)) return 0;
    for (int i = 0; i < pe.section_count && !found; i++) {
        const Section *s = &pe.sections[i];
        uint32_t size = s->vsize > s->rawsize ? s->vsize : s->rawsize;
        found = !strcmp(s->name, name) && pe.entry >= s->va && pe.entry - s->va < size;
    }
    pe_close(&pe);
    return found;
}

int pc5_pe_signed_by(const char *path, const char *who)
{
    Pe pe;
    int found = 0;
    if (pe_open(path, &pe)) return 0;
    /* The certificate table (directory 4) is where the file is, not where it is loaded. */
    uint32_t at = pe.dir_rva[4], size = pe.dir_size[4];
    size_t n = strlen(who);
    if (at && size >= n && size <= (1u << 20)) {
        uint8_t *table = pc5_alloc(size);
        if (table && !read_at(&pe, at, table, size))
            for (uint32_t i = 0; !found && i + n <= size; i++) found = table[i] == (uint8_t)who[0] && !memcmp(table + i, who, n);
        pc5_free(table);
    }
    pe_close(&pe);
    return found;
}

const char *pc5_api_name(Pc5Api api)
{
    switch (api) {
    case PC5_API_D3D12: return "DirectX 12";
    case PC5_API_D3D11: return "DirectX 11";
    case PC5_API_D3D10: return "DirectX 10";
    case PC5_API_D3D9: return "DirectX 9";
    case PC5_API_D3D8: return "DirectX 8";
    case PC5_API_VULKAN: return "Vulkan";
    case PC5_API_OPENGL: return "OpenGL";
    case PC5_API_DDRAW: return "DirectDraw";
    default: return "Windows";
    }
}

/* ---- icons ---------------------------------------------------------------------- */

/* A DIB icon image: BITMAPINFOHEADER, palette, colour rows, AND-mask rows,
 * both bottom-up and 4-byte aligned; the height counts both halves. */
static int decode_dib(const uint8_t *p, size_t size, Pc5Image *out)
{
    uint32_t header, colors;
    int32_t w, h;
    int bpp, any_alpha = 0;
    size_t stride, mask_stride, palette_at, pixels_at, mask_at;

    if (size < 40 || (header = u32(p)) < 40 || header > size) return -1;
    w = (int32_t)u32(p + 4);
    h = (int32_t)u32(p + 8) / 2;
    bpp = u16(p + 14);
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return -1;
    if (bpp != 32 && bpp != 24 && bpp != 8 && bpp != 4 && bpp != 1) return -1;
    colors = bpp <= 8 ? (u32(p + 32) ? u32(p + 32) : 1u << bpp) : 0;
    if (colors > 256) return -1;
    palette_at = header;
    pixels_at = palette_at + 4u * colors;
    stride = (((size_t)w * (size_t)bpp + 31) / 32) * 4;
    mask_stride = (((size_t)w + 31) / 32) * 4;
    mask_at = pixels_at + stride * (size_t)h;
    if (mask_at > size) return -1;
    if (pc5_image_new(out, w, h)) return -1;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = p + pixels_at + stride * (size_t)(h - 1 - y);
        for (int x = 0; x < w; x++) {
            uint32_t c;
            if (bpp == 32) {
                c = u32(row + 4 * x);
                any_alpha |= (c >> 24) != 0;
            } else if (bpp == 24) {
                c = 0xff000000u | (uint32_t)row[3 * x + 2] << 16 | (uint32_t)row[3 * x + 1] << 8 | row[3 * x];
            } else {
                int index = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15
                                                         : (row[x / 8] >> (7 - (x & 7))) & 1;
                const uint8_t *pal = p + palette_at + 4 * (size_t)(index < (int)colors ? index : 0);
                c = 0xff000000u | (uint32_t)pal[2] << 16 | (uint32_t)pal[1] << 8 | pal[0];
            }
            out->px[(size_t)y * (size_t)w + (size_t)x] = c;
        }
    }
    /* Without alpha in the colours, the AND mask says what is transparent. */
    if (bpp != 32 || !any_alpha) {
        int has_mask = mask_at + mask_stride * (size_t)h <= size;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint32_t *c = &out->px[(size_t)y * (size_t)w + (size_t)x];
                int clear = has_mask && ((p[mask_at + mask_stride * (size_t)(h - 1 - y) + (size_t)x / 8] >> (7 - (x & 7))) & 1);
                *c = clear ? 0 : (*c | 0xff000000u);
            }
    }
    return 0;
}

int pc5_pe_icon(const char *path, Pc5Image *icon)
{
    Pe pe;
    uint32_t rva, size;
    uint8_t group[6 + 14 * 64];
    int best = -1, best_score = -1, status = -1;

    *icon = (Pc5Image){ 0 };
    if (pe_open(path, &pe)) return -1;
    if (res_find(&pe, 14, -2, &rva, &size) || size < 6 || read_rva(&pe, rva, group, size < sizeof(group) ? size : sizeof(group)))
        goto out;
    {
        int count = u16(group + 4);
        if (count > 64 || 6 + 14u * (unsigned)count > size) goto out;
        for (int i = 0; i < count; i++) {
            const uint8_t *e = group + 6 + 14 * i;
            int w = e[0] ? e[0] : 256, bpp = u16(e + 6);
            int score = w * 64 + (bpp ? bpp : 8);
            if (score > best_score) { best_score = score; best = u16(e + 12); }
        }
    }
    if (best < 0 || res_find(&pe, 3, best, &rva, &size) || size > 1024 * 1024) goto out;
    {
        uint8_t *data = pc5_alloc(size);
        if (!data) goto out;
        if (!read_rva(&pe, rva, data, size)) {
            if (size > 8 && !memcmp(data, "\x89PNG", 4)) status = pc5_image_decode(data, size, icon);
            else status = decode_dib(data, size, icon);
        }
        pc5_free(data);
    }
out:
    pe_close(&pe);
    return status;
}
