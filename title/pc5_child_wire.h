/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: what the title and a program it starts as a process of its own say to each other
 * (pc5_children.c in the title, child/pc5child.c in the program's process).
 *
 * A Windows program a game starts can't be a second process of the title: a PS5 title can't
 * create one. So the title sends its own small loader (pc5child.elf) to elfldr, which runs it
 * as a process of its own with the connection as its standard input and output, and then the
 * start: the bootstrap, below, over that connection. The loader connects back to the title
 * through a local socket that the bootstrap names, says hello with the bootstrap's token, and
 * receives its connection to Wine's server and a control socket (the hand-over). Only once the
 * loader says it holds them does the title close its own copies: the console's kernel can
 * drop a socket that is in flight when no process holds it any more. The loader then starts
 * Wine, which runs the program; its output comes back over the first connection, line by
 * line, for the title's log.
 *
 * Everything here is pure: no system calls, so the host tests check it. */
#ifndef PC5_CHILD_WIRE_H
#define PC5_CHILD_WIRE_H

#include <stddef.h>
#include <stdint.h>

#define PC5_CHILD_BOOT_MAGIC UINT32_C(0x42354350)    /* "PC5B" */
#define PC5_CHILD_FRAME_MAGIC UINT32_C(0x46354350)   /* "PC5F" */
#define PC5_CHILD_VERSION 1u
#define PC5_CHILD_TOKEN 16u
#define PC5_CHILD_MAX_ARGS 64u
#define PC5_CHILD_MAX_ENV 512u
#define PC5_CHILD_MAX_BOOT (256u * 1024u)
#define PC5_CHILD_MAX_FRAME (128u * 1024u)

/* The bootstrap's fixed part, then its strings, each ending in a NUL: the hand-over socket's
 * path, the title's app0 as other processes see it, ntdll.prx's path, the program's argv and
 * its environment. Little-endian, as the console is. */
typedef struct Pc5ChildBootHeader {
    uint32_t magic;
    uint16_t version, header_size;
    uint32_t payload_size, flags;
    uint8_t token[PC5_CHILD_TOKEN];
    uint32_t argc, envc;
} Pc5ChildBootHeader;
_Static_assert(sizeof(Pc5ChildBootHeader) == 40, "the bootstrap's header is part of the wire");

typedef struct Pc5ChildBoot {
    uint8_t token[PC5_CHILD_TOKEN];
    uint32_t flags;
    const char *socket_path, *app0, *ntdll;
    uint32_t argc, envc;
    const char *argv[PC5_CHILD_MAX_ARGS + 1];   /* NULL after the last */
    const char *envp[PC5_CHILD_MAX_ENV + 1];
} Pc5ChildBoot;

/* The bootstrap in out: its size, or 0 when it doesn't fit (or more than the maximum of
 * arguments or environment, or a NULL among the strings). */
size_t pc5_child_boot_encode(uint8_t *out, size_t capacity, const uint8_t token[PC5_CHILD_TOKEN], uint32_t flags,
                             const char *socket_path, const char *app0, const char *ntdll,
                             const char *const *argv, const char *const *envp);
/* The header's payload size, or 0 for a header that isn't a bootstrap of this version. */
uint32_t pc5_child_boot_payload_size(const Pc5ChildBootHeader *header);
/* Reads a bootstrap (its header, then the payload_size bytes of strings): 0, its strings
 * pointing into payload, or -1. */
int pc5_child_boot_decode(const Pc5ChildBootHeader *header, const char *payload, size_t size, Pc5ChildBoot *out);

/* Frames on the control socket, both ways. */
enum Pc5ChildFrameKind {
    PC5_CHILD_HELLO = 1,     /* loader -> title: value = its pid, the payload its token */
    PC5_CHILD_HANDOVER = 2,  /* title -> loader: the server's socket and the control socket as rights */
    PC5_CHILD_HELD = 3,      /* loader -> title: value = its pid; the title may close its copies */
    PC5_CHILD_READY = 4,     /* loader -> title: Wine is loaded and starts the program */
    PC5_CHILD_SIGNAL = 5,    /* title -> loader: value = the signal, extra = the thread's kernel id */
    PC5_CHILD_STOP = 6,      /* title -> loader: end now */
    PC5_CHILD_SPAWN = 7,     /* loader -> title: start a program for this one; the new process's
                              * server socket as a right, the payload its argv and environment */
    PC5_CHILD_SPAWNED = 8,   /* title -> loader: value = 0 or an errno, extra = the new pid */
    PC5_CHILD_FAILED = 9,    /* loader -> title: value = the step that failed, extra = its status */
};

typedef struct Pc5ChildFrame {
    uint32_t magic, kind, size;
    int32_t value;
    int64_t extra;
} Pc5ChildFrame;
_Static_assert(sizeof(Pc5ChildFrame) == 24, "a control frame is part of the wire");

void pc5_child_frame(Pc5ChildFrame *frame, uint32_t kind, int32_t value, int64_t extra, uint32_t size);
/* 1 for a frame of this protocol whose payload isn't larger than the maximum. */
int pc5_child_frame_valid(const Pc5ChildFrame *frame);

/* A SPAWN frame's payload: argc, envc, then the strings. Sizes and NULLs as above. */
size_t pc5_child_args_encode(uint8_t *out, size_t capacity, const char *const *argv, const char *const *envp);
int pc5_child_args_decode(const uint8_t *payload, size_t size, const char **argv, uint32_t max_args,
                          const char **envp, uint32_t max_env);

/* Steps the loader names when it fails (PC5_CHILD_FAILED, and its log). */
enum Pc5ChildStep {
    PC5_CHILD_STEP_BOOT = 1,      /* the bootstrap didn't come or wasn't one */
    PC5_CHILD_STEP_CONNECT = 2,   /* the hand-over socket */
    PC5_CHILD_STEP_HANDOVER = 3,
    PC5_CHILD_STEP_NTDLL = 4,     /* ntdll.prx didn't load */
    PC5_CHILD_STEP_WINE = 5,      /* Wine's environment or its thread */
};
const char *pc5_child_step_name(int step);

#endif
