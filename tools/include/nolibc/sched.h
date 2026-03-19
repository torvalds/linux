/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * sched function definitions for NOLIBC
 * Copyright (C) 2025 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_SCHED_H
#define _NOLIBC_SCHED_H

#include "sys.h"

#include <linux/sched.h>

/*
 * int setns(int fd, int nstype);
 */

static __attribute__((unused))
int _sys_setns(int fd, int nstype)
{
	return __nolibc_syscall2(__NR_setns, fd, nstype);
}

static __attribute__((unused))
int setns(int fd, int nstype)
{
	return __sysret(_sys_setns(fd, nstype));
}


/*
 * int unshare(int flags);
 */

static __attribute__((unused))
int _sys_unshare(int flags)
{
	return __nolibc_syscall1(__NR_unshare, flags);
}

static __attribute__((unused))
int unshare(int flags)
{
	return __sysret(_sys_unshare(flags));
}

#endif /* _NOLIBC_SCHED_H */
