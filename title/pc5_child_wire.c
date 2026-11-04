/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: the title's and a started program's wire (pc5_child_wire.h). */
#include "pc5_child_wire.h"

#include <string.h>

/* Appends text and its NUL at *at: 0, or -1 when it doesn't fit. */
static int put(uint8_t *out, size_t capacity, size_t *at, const char *text)
{
    size_t n;
    if (!text) return -1;
    n = strlen(text) + 1;
    if (n > capacity - *at) return -1;
    memcpy(out + *at, text, n);
    *at += n;
    return 0;
}

static int count(const char *const *list, uint32_t max, uint32_t *out)
{
    uint32_t n = 0;
    while (list && list[n]) {
        if (++n > max) return -1;
    }
    *out = n;
    return 0;
}

size_t pc5_child_boot_encode(uint8_t *out, size_t capacity, const uint8_t token[PC5_CHILD_TOKEN], uint32_t flags,
                             const char *socket_path, const char *app0, const char *ntdll,
                             const char *const *argv, const char *const *envp)
{
    Pc5ChildBootHeader header;
    size_t at = sizeof(header);
    uint32_t argc, envc;

    if (!out || !token || capacity < sizeof(header) || capacity > PC5_CHILD_MAX_BOOT + sizeof(header) ||
        count(argv, PC5_CHILD_MAX_ARGS, &argc) || !argc || count(envp, PC5_CHILD_MAX_ENV, &envc))
        return 0;
    if (put(out, capacity, &at, socket_path) || put(out, capacity, &at, app0) || put(out, capacity, &at, ntdll))
        return 0;
    for (uint32_t i = 0; i < argc; i++)
        if (put(out, capacity, &at, argv[i])) return 0;
    for (uint32_t i = 0; i < envc; i++)
        if (put(out, capacity, &at, envp[i])) return 0;
    memset(&header, 0, sizeof(header));
    header.magic = PC5_CHILD_BOOT_MAGIC;
    header.version = PC5_CHILD_VERSION;
    header.header_size = sizeof(header);
    header.payload_size = (uint32_t)(at - sizeof(header));
    header.flags = flags;
    memcpy(header.token, token, PC5_CHILD_TOKEN);
    header.argc = argc;
    header.envc = envc;
    memcpy(out, &header, sizeof(header));
    return at;
}

uint32_t pc5_child_boot_payload_size(const Pc5ChildBootHeader *header)
{
    if (!header || header->magic != PC5_CHILD_BOOT_MAGIC || header->version != PC5_CHILD_VERSION ||
        header->header_size != sizeof(*header) || !header->payload_size || header->payload_size > PC5_CHILD_MAX_BOOT ||
        !header->argc || header->argc > PC5_CHILD_MAX_ARGS || header->envc > PC5_CHILD_MAX_ENV)
        return 0;
    return header->payload_size;
}

/* The next string at *at in [payload, payload + size): it, or NULL when its NUL isn't there. */
static const char *take(const char *payload, size_t size, size_t *at)
{
    const char *text = payload + *at, *end;
    if (*at >= size || !(end = memchr(text, 0, size - *at))) return NULL;
    *at += (size_t)(end - text) + 1;
    return text;
}

int pc5_child_boot_decode(const Pc5ChildBootHeader *header, const char *payload, size_t size, Pc5ChildBoot *out)
{
    size_t at = 0;

    if (!out || !payload || !pc5_child_boot_payload_size(header) || size != header->payload_size) return -1;
    memset(out, 0, sizeof(*out));
    memcpy(out->token, header->token, PC5_CHILD_TOKEN);
    out->flags = header->flags;
    if (!(out->socket_path = take(payload, size, &at)) || !(out->app0 = take(payload, size, &at)) ||
        !(out->ntdll = take(payload, size, &at)) || !out->socket_path[0] || !out->ntdll[0])
        return -1;
    out->argc = header->argc;
    out->envc = header->envc;
    for (uint32_t i = 0; i < out->argc; i++)
        if (!(out->argv[i] = take(payload, size, &at))) return -1;
    for (uint32_t i = 0; i < out->envc; i++)
        if (!(out->envp[i] = take(payload, size, &at))) return -1;
    out->argv[out->argc] = NULL;
    out->envp[out->envc] = NULL;
    return at == size ? 0 : -1;
}

void pc5_child_frame(Pc5ChildFrame *frame, uint32_t kind, int32_t value, int64_t extra, uint32_t size)
{
    memset(frame, 0, sizeof(*frame));
    frame->magic = PC5_CHILD_FRAME_MAGIC;
    frame->kind = kind;
    frame->size = size;
    frame->value = value;
    frame->extra = extra;
}

int pc5_child_frame_valid(const Pc5ChildFrame *frame)
{
    return frame && frame->magic == PC5_CHILD_FRAME_MAGIC && frame->kind >= PC5_CHILD_HELLO &&
           frame->kind <= PC5_CHILD_FAILED && frame->size <= PC5_CHILD_MAX_FRAME;
}

size_t pc5_child_args_encode(uint8_t *out, size_t capacity, const char *const *argv, const char *const *envp)
{
    uint32_t argc, envc;
    size_t at = 8;

    if (!out || capacity < 8 || capacity > PC5_CHILD_MAX_FRAME || count(argv, PC5_CHILD_MAX_ARGS, &argc) || !argc ||
        count(envp, PC5_CHILD_MAX_ENV, &envc))
        return 0;
    memcpy(out, &argc, 4);
    memcpy(out + 4, &envc, 4);
    for (uint32_t i = 0; i < argc; i++)
        if (put(out, capacity, &at, argv[i])) return 0;
    for (uint32_t i = 0; i < envc; i++)
        if (put(out, capacity, &at, envp[i])) return 0;
    return at;
}

int pc5_child_args_decode(const uint8_t *payload, size_t size, const char **argv, uint32_t max_args,
                          const char **envp, uint32_t max_env)
{
    uint32_t argc, envc;
    size_t at = 8;

    if (!payload || size < 8 || !argv || !envp) return -1;
    memcpy(&argc, payload, 4);
    memcpy(&envc, payload + 4, 4);
    if (!argc || argc > max_args || envc > max_env) return -1;
    for (uint32_t i = 0; i < argc; i++)
        if (!(argv[i] = take((const char *)payload, size, &at))) return -1;
    for (uint32_t i = 0; i < envc; i++)
        if (!(envp[i] = take((const char *)payload, size, &at))) return -1;
    argv[argc] = NULL;
    envp[envc] = NULL;
    return at == size ? 0 : -1;
}

const char *pc5_child_step_name(int step)
{
    switch (step) {
    case PC5_CHILD_STEP_BOOT: return "bootstrap";
    case PC5_CHILD_STEP_CONNECT: return "connect";
    case PC5_CHILD_STEP_HANDOVER: return "hand-over";
    case PC5_CHILD_STEP_NTDLL: return "ntdll";
    case PC5_CHILD_STEP_WINE: return "wine";
    default: return "unknown";
    }
}
