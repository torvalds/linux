/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Prctl definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_PRCTL_H
#define _NOLIBC_SYS_PRCTL_H

#include "../sys.h"

#include <linux/prctl.h>

/*
 * int prctl(int option, unsigned long arg2, unsigned long arg3,
 *                       unsigned long arg4, unsigned long arg5);
 */

static __attribute__((unused))
int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
			  unsigned long arg4, unsigned long arg5)
{
	return my_syscall5(__NR_prctl, option, arg2, arg3, arg4, arg5);
}

static __attribute__((unused))
int prctl(int option, unsigned long arg2, unsigned long arg3,
		      unsigned long arg4, unsigned long arg5)
{
	return __sysret(sys_prctl(option, arg2, arg3, arg4, arg5));
}

#endif /* _NOLIBC_SYS_PRCTL_H */
