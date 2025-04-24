/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * time definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_TIME_H
#define _NOLIBC_SYS_TIME_H

#include "../arch.h"
#include "../sys.h"

/*
 * int gettimeofday(struct timeval *tv, struct timezone *tz);
 */

static __attribute__((unused))
int sys_gettimeofday(struct timeval *tv, struct timezone *tz)
{
#ifdef __NR_gettimeofday
	return my_syscall2(__NR_gettimeofday, tv, tz);
#else
	return __nolibc_enosys(__func__, tv, tz);
#endif
}

static __attribute__((unused))
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return __sysret(sys_gettimeofday(tv, tz));
}

#endif /* _NOLIBC_SYS_TIME_H */
