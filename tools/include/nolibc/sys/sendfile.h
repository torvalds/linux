/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * sendfile for NOLIBC
 * Copyright (C) 2026 Daniel Palmer <daniel@thingy.jp>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_SENDFILE_H
#define _NOLIBC_SYS_SENDFILE_H

#include "../sys.h"
#include <linux/unistd.h>

/*
 * ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
 */

static __attribute__((unused))
ssize_t _sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	__nolibc_static_assert(sizeof(*offset) == sizeof(__kernel_loff_t));

#ifdef __NR_sendfile64
	/* 32-bit applications use sendfile64 so offset is treated as a __kernel_loff_t */
	return __nolibc_syscall4(__NR_sendfile64, out_fd, in_fd, offset, count);
#else
	__nolibc_static_assert(sizeof(*offset) == sizeof(__kernel_off_t));

	return __nolibc_syscall4(__NR_sendfile, out_fd, in_fd, offset, count);
#endif
}

static __attribute__((unused))
ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	return __sysret(_sys_sendfile(out_fd, in_fd, offset, count));
}

#endif /* _NOLIBC_SYS_SENDFILE_H */
