/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host check of Wine patch 0985: a name missing from a PS5 game's system32 or
 * syswow64 is Wine's own file of that name. tests/test.sh takes ps5_wine_system_file from
 * Wine's dlls/ntdll/unix/file.c with prospero-win's patches applied into system_func.c,
 * and builds this around it, with a prefix and a Wine folder of its own. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef uint16_t WCHAR;
typedef long NTSTATUS;
#define STATUS_SUCCESS 0
#define STATUS_OBJECT_NAME_NOT_FOUND 0xc0000034L
#define STATUS_NO_MEMORY 0xc0000017L
#define MAX_DIR_ENTRY_LEN 255
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TRACE(...) ((void)0)
#define debugstr_wn(name, length) ((void)(name), (void)(length), "")
#define STATUS_DISK_FULL 0xc000007fL
typedef int BOOL;
typedef unsigned int ULONG, ACCESS_MASK;
typedef int LONG;
#define TRUE 1
#define FALSE 0
#define GENERIC_WRITE 0x40000000u
#define GENERIC_ALL 0x10000000u
#define MAXIMUM_ALLOWED 0x02000000u
#define GENERIC_READ 0x80000000u
#define FILE_READ_DATA 0x0001u
#define FILE_WRITE_DATA 0x0002u
#define FILE_APPEND_DATA 0x0004u
#define FILE_WRITE_EA 0x0010u
#define FILE_READ_ATTRIBUTES 0x0080u
#define FILE_WRITE_ATTRIBUTES 0x0100u
#define DELETE 0x00010000u
#define WRITE_DAC 0x00040000u
#define WRITE_OWNER 0x00080000u
#define SYNCHRONIZE 0x00100000u
#define FILE_SUPERSEDE 0
#define FILE_OPEN 1
#define FILE_CREATE 2
#define FILE_OPEN_IF 3
#define FILE_OVERWRITE 4
#define FILE_OVERWRITE_IF 5
#define FILE_DELETE_ON_CLOSE 0x1000u
#define MESSAGE(format, ...) ((void)sizeof(printf(format, __VA_ARGS__)))
static LONG InterlockedExchange(LONG *target, LONG value) { LONG old = *target; *target = value; return old; }
static NTSTATUS errno_to_status(int error) { return error ? 0xc0000001L : 0; }
static const char *config_dir;
static const char **dll_paths;
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "system_func.c"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "test_wine_system: failed: %s (line %d)\n", #c, __LINE__); failures++; } } while (0)

static void touch(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
}

/* What the lookup gives for name in folder (under the prefix): the path, or NULL. */
static char *serve(const char *folder, const char *name)
{
    WCHAR wide[300];
    int length = (int)strlen(name), unix_len;
    char *buffer;
    /* As lookup_unix_name holds it: the folder's path up to pos, in a buffer too small for more. */
    if (asprintf(&buffer, "%s%s", config_dir, folder) < 0) exit(1);
    unix_len = (int)strlen(buffer) + 1;
    for (int i = 0; i < length; i++) wide[i] = (WCHAR)(unsigned char)name[i];
    if (!strcmp(name, "\xc3\xa9.dll")) wide[0] = 0xe9, wide[1] = '.', wide[2] = 'd', wide[3] = 'l', wide[4] = 'l', length = 5;
    NTSTATUS status = ps5_wine_system_file(&buffer, &unix_len, unix_len - 1, wide, length);
    if (status != STATUS_SUCCESS) {
        CHECK(status == STATUS_OBJECT_NAME_NOT_FOUND);
        free(buffer);
        return NULL;
    }
    CHECK((size_t)unix_len >= strlen(buffer) + 1);
    return buffer;
}

static int served_from(const char *folder, const char *name, const char *expect)
{
    char *got = serve(folder, name);
    int ok = expect ? got && !strcmp(got, expect) : !got;
    if (!ok) fprintf(stderr, "test_wine_system: %s in %s gave %s, not %s\n", name, folder, got ? got : "nothing",
                     expect ? expect : "nothing");
    free(got);
    return ok;
}

int main(void)
{
    char root[] = "/tmp/pc5-wine-system-XXXXXX", path[2048], prefix[128], wine[128], dll64[256], dll32[256];
    if (!mkdtemp(root)) return 1;
    snprintf(prefix, sizeof(prefix), "%s/prefixes/game", root);
    snprintf(wine, sizeof(wine), "%s/app0/win/wine/lib/wine", root);
    snprintf(path, sizeof(path), "mkdir -p '%s' '%s/x86_64-windows' '%s/i386-windows'", prefix, wine, wine);
    if (system(path)) return 1;
    snprintf(dll64, sizeof(dll64), "%s/x86_64-windows/kernel32.dll", wine);
    snprintf(dll32, sizeof(dll32), "%s/i386-windows/kernel32.dll", wine);
    touch(dll64);
    touch(dll32);
    snprintf(path, sizeof(path), "%s/x86_64-windows/libkernel32.a", wine);
    touch(path);
    snprintf(path, sizeof(path), "%s/i386-windows/drivers", wine);
    mkdir(path, 0755);
    const char *paths[] = { "/nowhere/lib/wine", wine, NULL };
    config_dir = prefix;
    dll_paths = paths;

    /* system32 is the 64-bit folder, syswow64 the 32-bit one, by either way to them. */
    CHECK(served_from("/dosdevices/c:/windows/system32", "kernel32.dll", dll64));
    CHECK(served_from("/drive_c/windows/system32", "kernel32.dll", dll64));
    CHECK(served_from("/dosdevices/c:/windows/syswow64", "kernel32.dll", dll32));
    CHECK(served_from("/drive_c/windows/syswow64", "kernel32.dll", dll32));
    /* Asked for in any case: Wine's files are lower case. */
    CHECK(served_from("/dosdevices/c:/windows/system32", "KERNEL32.DLL", dll64));
    CHECK(served_from("/dosdevices/c:/windows/syswow64", "Kernel32.Dll", dll32));
    /* Only Wine's own names. */
    CHECK(served_from("/dosdevices/c:/windows/system32", "physxcore.dll", NULL));
    CHECK(served_from("/dosdevices/c:/windows/system32", "libkernel32.a", NULL));
    CHECK(served_from("/dosdevices/c:/windows/syswow64", "drivers", NULL));   /* a folder isn't */
    CHECK(served_from("/dosdevices/c:/windows/system32", "..", NULL));
    CHECK(served_from("/dosdevices/c:/windows/system32", "\xc3\xa9.dll", NULL));
    /* Only those two folders of this prefix. */
    CHECK(served_from("/dosdevices/c:/windows", "kernel32.dll", NULL));
    CHECK(served_from("/dosdevices/c:/windows/system32/drivers", "kernel32.dll", NULL));
    CHECK(served_from("/dosdevices/c:/Games/system32", "kernel32.dll", NULL));
    CHECK(served_from("/dosdevices/c:/windows/system3", "kernel32.dll", NULL));
    /* 0986: which paths are Wine's own, which opens change a file, and the copy made first. */
    CHECK(ps5_is_wine_own(dll64) && ps5_is_wine_own(dll32));
    snprintf(path, sizeof(path), "%s/drive_c/windows/system32/kernel32.dll", prefix);
    CHECK(!ps5_is_wine_own(path));
    snprintf(path, sizeof(path), "%s/x86_64-windowsX/kernel32.dll", wine);
    CHECK(!ps5_is_wine_own(path));
    CHECK(!ps5_open_changes(GENERIC_READ | SYNCHRONIZE, FILE_OPEN, 0));
    CHECK(!ps5_open_changes(FILE_READ_DATA | FILE_READ_ATTRIBUTES, FILE_OPEN, 0));
    CHECK(ps5_open_changes(GENERIC_READ | GENERIC_WRITE, FILE_OPEN, 0));
    CHECK(ps5_open_changes(DELETE, FILE_OPEN, 0) && ps5_open_changes(FILE_READ_DATA, FILE_OPEN, FILE_DELETE_ON_CLOSE));
    CHECK(ps5_open_changes(FILE_WRITE_ATTRIBUTES, FILE_OPEN, 0) && ps5_open_changes(GENERIC_READ, FILE_OVERWRITE, 0));
    {
        FILE *f = fopen(dll64, "w");
        struct stat a, b;
        char copy[300], text[16] = "";
        fputs("MZ wine", f);
        fclose(f);
        snprintf(path, sizeof(path), "mkdir -p '%s/drive_c/windows/system32' && touch -d 2020-01-02 '%s'", prefix, dll64);
        CHECK(!system(path));
        snprintf(copy, sizeof(copy), "%s/drive_c/windows/system32/kernel32.dll", prefix);
        CHECK(ps5_copy_wine_own(dll64, copy) == STATUS_SUCCESS);
        f = fopen(copy, "r");
        CHECK(f && fgets(text, sizeof(text), f) && !strcmp(text, "MZ wine"));
        if (f) fclose(f);
        CHECK(!stat(dll64, &a) && !stat(copy, &b) && a.st_mtime == b.st_mtime);
        CHECK(ps5_copy_wine_own(dll64, copy) != STATUS_SUCCESS);   /* never over the game's own */
    }
    config_dir = "/elsewhere";
    {
        char *got;
        int unix_len = (int)strlen(prefix) + 40;
        char *buffer = malloc((size_t)unix_len);
        WCHAR name[] = { 'k', 'e', 'r', 'n', 'e', 'l', '3', '2', '.', 'd', 'l', 'l' };
        snprintf(buffer, (size_t)unix_len, "%s/dosdevices/c:/windows/system32", prefix);
        CHECK(ps5_wine_system_file(&buffer, &unix_len, (int)strlen(buffer), name, 12) == STATUS_OBJECT_NAME_NOT_FOUND);
        got = buffer;
        free(got);
    }
    snprintf(path, sizeof(path), "rm -rf '%s'", root);
    if (system(path)) return 1;
    if (failures) return 1;
    printf("test_wine_system: Wine's own DLLs in a game's system32 and syswow64 passed\n");
    return 0;
}
