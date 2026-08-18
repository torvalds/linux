/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * fcntl definition for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_FCNTL_H
#define _NOLIBC_FCNTL_H

#include "arch.h"
#include "types.h"
#include "sys.h"

#define __nolibc_open_flags(_flags) ((_flags) | O_LARGEFILE)

#define __nolibc_open_mode(_flags)							\
({											\
	mode_t _mode;									\
	va_list args;									\
											\
	va_start(args, (_flags));							\
	_mode = va_arg(args, mode_t);							\
	va_end(args);									\
											\
	_mode;										\
})

/*
 * int openat(int dirfd, const char *path, int flags[, mode_t mode]);
 */

static __attribute__((unused))
int _sys_openat(int dirfd, const char *path, int flags, mode_t mode)
{
	return __nolibc_syscall4(__NR_openat, dirfd, path, flags, mode);
}

static __attribute__((unused))
int openat(int dirfd, const char *path, int flags, ...)
{
	return __sysret(_sys_openat(dirfd, path, __nolibc_open_flags(flags),
						 __nolibc_open_mode(flags)));
}

/*
 * int open(const char *path, int flags[, mode_t mode]);
 */

static __attribute__((unused))
int _sys_open(const char *path, int flags, mode_t mode)
{
	return __nolibc_syscall4(__NR_openat, AT_FDCWD, path, flags, mode);
}

static __attribute__((unused))
int open(const char *path, int flags, ...)
{
	return __sysret(_sys_open(path, __nolibc_open_flags(flags), __nolibc_open_mode(flags)));
}

/*
 * int creat(const char *path, mode_t mode);
 */

static __attribute__((unused))
int creat(const char *path, mode_t mode)
{
	return open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

#endif /* _NOLIBC_FCNTL_H */
