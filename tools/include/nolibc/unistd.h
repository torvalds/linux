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

/*
 * char *getcwd(char *buf, size_t size);
 */

static __attribute__((unused))
int _sys_getcwd(char *buf, size_t size)
{
	return __nolibc_syscall2(__NR_getcwd, buf, size);
}

static __attribute__((unused))
char *getcwd(char *buf, size_t size)
{
	int ret;

	/* Unlike other libc's we don't handle passing NULL for buf */
	if (!buf || !size) {
		SET_ERRNO(EINVAL);
		return NULL;
	}

	ret = __sysret(_sys_getcwd(buf, size));

	/* On error return NULL, __sysret() above will have set errno */
	if (ret < 0)
		return NULL;

	/* Handle no path being written or the kernel putting
	 * "(unreachable)" into the buffer instead of a path.
	 * This matches what musl is doing.
	 */
	if (ret == 0 || buf[0] != '/') {
		SET_ERRNO(ENOENT);
		return NULL;
	}

	/* ret must be the number of bytes written at this point,
	 * so return the pointer to buf.
	 */
	return buf;
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

/*
 * ssize_t readlink(const char *path, char *buf, size_t bufsiz);
 */

static __attribute__((unused))
ssize_t _sys_readlink(const char *path, char *buf, size_t bufsiz)
{
	return __nolibc_syscall4(__NR_readlinkat, AT_FDCWD, path, buf, bufsiz);
}

static __attribute__((unused))
ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
	return __sysret(_sys_readlink(path, buf, bufsiz));
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
