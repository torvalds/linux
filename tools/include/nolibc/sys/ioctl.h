/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Ioctl definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_IOCTL_H
#define _NOLIBC_SYS_IOCTL_H

#include "../sys.h"

#include <linux/ioctl.h>

/*
 * int ioctl(int fd, unsigned long cmd, ... arg);
 */

static __attribute__((unused))
long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return my_syscall3(__NR_ioctl, fd, cmd, arg);
}

#define ioctl(fd, cmd, arg) __sysret(sys_ioctl(fd, cmd, (unsigned long)(arg)))

#endif /* _NOLIBC_SYS_IOCTL_H */
