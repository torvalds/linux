/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * unistd function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_UNISTD_H
#define _NOLIBC_UNISTD_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"


#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/*
 * int access(const char *path, int amode);
 * int faccessat(int fd, const char *path, int amode, int flag);
 */

static __attribute__((unused))
int sys_faccessat(int fd, const char *path, int amode, int flag)
{
	return my_syscall4(__NR_faccessat, fd, path, amode, flag);
}

static __attribute__((unused))
int faccessat(int fd, const char *path, int amode, int flag)
{
	return __sysret(sys_faccessat(fd, path, amode, flag));
}

static __attribute__((unused))
int access(const char *path, int amode)
{
	return faccessat(AT_FDCWD, path, amode, 0);
}


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

#endif /* _NOLIBC_UNISTD_H */
