/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5's host check of Wine patches 0911 and 0979: the windows that take the controller's
 * dialog keys (Cross as Enter, the D-pad as the arrows) in a game played with it as its
 * XInput controller or DualSense. tests/test.sh takes is_dialog from Wine's
 * dlls/win32u/ps5drv.c with prospero-win's patches applied into dialog_func.c, and builds
 * this around it, with windows of its own. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t WCHAR;
typedef int BOOL, INT;
typedef void *HWND;
typedef struct { uint16_t Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING;
typedef struct { int left, top, right, bottom; } RECT;
struct ratio { unsigned int numerator, denominator; };
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
enum { MDT_DEFAULT };

/* The window in front: its class and size, on a display of 1920x1080. */
static const char *window_class;
static int window_w, window_h;

static INT NtUserGetClassName(HWND hwnd, BOOL real, UNICODE_STRING *name)
{
    INT n = 0;
    (void)hwnd; (void)real;
    while (window_class[n] && (size_t)(n + 1) * sizeof(WCHAR) <= name->MaximumLength) {
        name->Buffer[n] = (WCHAR)window_class[n];
        n++;
    }
    return n;
}

static RECT get_virtual_screen_rect(struct ratio dpi, int type)
{
    (void)dpi; (void)type;
    return (RECT){ 0, 0, 1920, 1080 };
}

static BOOL get_window_rect(HWND hwnd, RECT *rect, struct ratio dpi)
{
    (void)hwnd; (void)dpi;
    *rect = (RECT){ 564, 316, 564 + window_w, 316 + window_h };
    return TRUE;
}

#include "dialog_func.c"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "test_wine_dialog: failed: %s (line %d)\n", #c, __LINE__); failures++; } } while (0)

static BOOL in_front(const char *name, int w, int h)
{
    window_class = name;
    window_w = w;
    window_h = h;
    return is_dialog((HWND)1);
}

int main(void)
{
    /* 0911: a dialog box, as MessageBox and DialogBox make (Spider-Man's "driver below the
     * requirements"), whatever its size. */
    CHECK(in_front("#32770", 520, 180) && in_front("#32770", 1920, 1080));
    CHECK(!is_dialog(NULL));
    /* 0979: Marvel's Spider-Man Remastered's launch window (Play, Settings, Quit), a
     * GameNxApp pop-up of 792x447 that reads only the keyboard: the controller's ✕ and D-pad
     * did nothing there without a USB keyboard. */
    CHECK(in_front("GameNxApp", 792, 447));
    /* Its game's window, which may share the class, covers the display one way or both, and
     * never gets the keys beside the controller. */
    CHECK(!in_front("GameNxApp", 1920, 1080) && !in_front("GameNxApp", 1920, 800) && !in_front("GameNxApp", 1440, 1080));
    /* Any other game's window, however small, nor a class that only starts like one. */
    CHECK(!in_front("UnityWndClass", 792, 447) && !in_front("GameNxAppX", 792, 447) && !in_front("GameNx", 792, 447));
    if (failures) return 1;
    printf("test_wine_dialog: the windows that take the controller's dialog keys passed\n");
    return 0;
}
