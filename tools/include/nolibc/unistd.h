/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * unistd function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_UNISTD_H
#define _NOLIBC_UNISTD_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"


#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2


static __attribute__((unused))
int msleep(unsigned int msecs)
{
	struct timeval my_timeval = { msecs / 1000, (msecs % 1000) * 1000 };

	if (sys_select(0, 0, 0, 0, &my_timeval) < 0)
		return (my_timeval.tv_sec * 1000) +
			(my_timeval.tv_usec / 1000) +
			!!(my_timeval.tv_usec % 1000);
	else
		return 0;
}

static __attribute__((unused))
unsigned int sleep(unsigned int seconds)
{
	struct timeval my_timeval = { seconds, 0 };

	if (sys_select(0, 0, 0, 0, &my_timeval) < 0)
		return my_timeval.tv_sec + !!my_timeval.tv_usec;
	else
		return 0;
}

static __attribute__((unused))
int usleep(unsigned int usecs)
{
	struct timeval my_timeval = { usecs / 1000000, usecs % 1000000 };

	return sys_select(0, 0, 0, 0, &my_timeval);
}

static __attribute__((unused))
int tcsetpgrp(int fd, pid_t pid)
{
	return ioctl(fd, TIOCSPGRP, &pid);
}

#define __syscall_narg(_0, _1, _2, _3, _4, _5, _6, N, ...) N
#define _syscall_narg(...) __syscall_narg(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define _syscall(N, ...) __sysret(my_syscall##N(__VA_ARGS__))
#define _syscall_n(N, ...) _syscall(N, __VA_ARGS__)
#define syscall(...) _syscall_n(_syscall_narg(__VA_ARGS__), ##__VA_ARGS__)

/* make sure to include all global symbols */
#include "nolibc.h"

#endif /* _NOLIBC_UNISTD_H */
