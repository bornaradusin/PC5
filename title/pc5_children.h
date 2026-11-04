/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* PC5: Windows programs a game starts, each in a process of its own (pc5_child_wire.h says
 * how). The program runs hidden: nothing of it is on the screen, and it has no controller or
 * sound. That is what installers the game runs need (a Visual C++ or DirectX setup), and the
 * programs those start in turn.
 *
 * The title sends its loader to elfldr on port 9021, which PC5 already uses for its /data
 * access; without elfldr nothing starts, and Wine's start of the program fails as before. */
#ifndef PC5_CHILDREN_H
#define PC5_CHILDREN_H

#include <stddef.h>
#include <sys/types.h>

typedef struct Pc5ChildrenConfig {
    const char *loader;     /* pc5child.elf, as the title reads it */
    const char *app0;       /* the title's app0, as other processes see it */
    const char *ntdll;      /* ntdll.prx, as other processes see it */
    const char *run_dir;    /* a folder both see, for the hand-over socket */
    void (*log)(const char *line);
} Pc5ChildrenConfig;

/* 0 once the loader is read and the folder is there, or -1 (nothing starts then). */
int pc5_children_init(const Pc5ChildrenConfig *config);
/* Starts a program as a process of its own: argv and envp are Wine's (argv[0] is "wine"),
 * server_fd the new process's end of its connection to Wine's server, which the caller closes
 * once this returns. The new pid, or -1 with errno. */
int pc5_children_spawn(const char *const *argv, const char *const *envp, int server_fd);
/* Wine's server signalling a thread of a started program (thr_kill2): 0, or -1 with errno
 * (ESRCH when no started program has that pid). */
int pc5_children_signal(pid_t pid, long tid, int signal);
/* Before the title ends: every started program ends, killed after a moment if it won't. */
void pc5_children_stop(const char *reason);
/* The started programs still running. */
unsigned pc5_children_running(void);

#endif
