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
int _sys_faccessat(int fd, const char *path, int amode, int flag)
{
	return __nolibc_syscall4(__NR_faccessat, fd, path, amode, flag);
}

static __attribute__((unused))
int faccessat(int fd, const char *path, int amode, int flag)
{
	return __sysret(_sys_faccessat(fd, path, amode, flag));
}

static __attribute__((unused))
int access(const char *path, int amode)
{
	return faccessat(AT_FDCWD, path, amode, 0);
}

#if !defined(_sys_ftruncate64) && defined(__NR_ftruncate64)
static __attribute__((unused))
int _sys_ftruncate64(int fd, uint32_t length0, uint32_t length1)
{
	return __nolibc_syscall3(__NR_ftruncate64, fd, length0, length1);
}
#define _sys_ftruncate64 _sys_ftruncate64
#endif

static __attribute__((unused))
int _sys_ftruncate(int fd, off_t length)
{
#if defined(_sys_ftruncate64)
	return _sys_ftruncate64(fd, __NOLIBC_LLARGPART(length, 0), __NOLIBC_LLARGPART(length, 1));
#else
	return __nolibc_syscall2(__NR_ftruncate, fd, length);
#endif
}

static __attribute__((unused))
int ftruncate(int fd, off_t length)
{
	return __sysret(_sys_ftruncate(fd, length));
}

static __attribute__((unused))
int msleep(unsigned int msecs)
{
	struct timeval my_timeval = { msecs / 1000, (msecs % 1000) * 1000 };

	if (_sys_select(0, NULL, NULL, NULL, &my_timeval) < 0)
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

	if (_sys_select(0, NULL, NULL, NULL, &my_timeval) < 0)
		return my_timeval.tv_sec + !!my_timeval.tv_usec;
	else
		return 0;
}

static __attribute__((unused))
int usleep(unsigned int usecs)
{
	struct timeval my_timeval = { usecs / 1000000, usecs % 1000000 };

	return _sys_select(0, NULL, NULL, NULL, &my_timeval);
}

static __attribute__((unused))
int tcsetpgrp(int fd, pid_t pid)
{
	return ioctl(fd, TIOCSPGRP, &pid);
}

#endif /* _NOLIBC_UNISTD_H */
