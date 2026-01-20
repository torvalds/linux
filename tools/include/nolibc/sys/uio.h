/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * uio for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2025 Intel Corporation
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_UIO_H
#define _NOLIBC_SYS_UIO_H

#include "../sys.h"
#include <linux/uio.h>


/*
 * ssize_t readv(int fd, const struct iovec *iovec, int count);
 */
static __attribute__((unused))
ssize_t sys_readv(int fd, const struct iovec *iovec, int count)
{
	return my_syscall3(__NR_readv, fd, iovec, count);
}

static __attribute__((unused))
ssize_t readv(int fd, const struct iovec *iovec, int count)
{
	return __sysret(sys_readv(fd, iovec, count));
}

/*
 * ssize_t writev(int fd, const struct iovec *iovec, int count);
 */
static __attribute__((unused))
ssize_t sys_writev(int fd, const struct iovec *iovec, int count)
{
	return my_syscall3(__NR_writev, fd, iovec, count);
}

static __attribute__((unused))
ssize_t writev(int fd, const struct iovec *iovec, int count)
{
	return __sysret(sys_writev(fd, iovec, count));
}


#endif /* _NOLIBC_SYS_UIO_H */
