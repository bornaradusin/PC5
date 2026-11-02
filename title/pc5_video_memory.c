/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Only FFmpeg's private mem.o uses these functions. Large codec allocations
 * must use PC5's mapped pages, rather than the title's small libc heap. */
#include "pc5.h"
#include <errno.h>
#include <string.h>

typedef struct VideoBlock { void *base; size_t size; } VideoBlock;

int pc5_video_memalign(void **out, size_t alignment, size_t size)
{
    void *base;
    uintptr_t address;
    VideoBlock *block;
    if (!out || alignment < sizeof(void *) || (alignment & (alignment - 1))) return EINVAL;
    if (size > SIZE_MAX - sizeof(VideoBlock) - alignment) return ENOMEM;
    base = pc5_alloc(size + sizeof(VideoBlock) + alignment);
    if (!base) return ENOMEM;
    address = ((uintptr_t)base + sizeof(VideoBlock) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    block = (VideoBlock *)address - 1;
    block->base = base;
    block->size = size;
    *out = (void *)address;
    return 0;
}

void *pc5_video_malloc(size_t size)
{
    void *out = NULL;
    if (pc5_video_memalign(&out, 64, size)) return NULL;
    return out;
}

void pc5_video_free(void *pointer)
{
    if (pointer) pc5_free(((VideoBlock *)pointer - 1)->base);
}

void *pc5_video_realloc(void *pointer, size_t size)
{
    void *out;
    if (!size) { pc5_video_free(pointer); return NULL; }
    out = pc5_video_malloc(size);
    if (!out) return NULL;
    if (pointer) {
        size_t old = ((VideoBlock *)pointer - 1)->size;
        memcpy(out, pointer, old < size ? old : size);
        pc5_video_free(pointer);
    }
    return out;
}

void *pc5_video_aligned_alloc(size_t alignment, size_t size)
{
    void *out = NULL;
    if (pc5_video_memalign(&out, alignment, size)) return NULL;
    return out;
}
