/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Utsname definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_UTSNAME_H
#define _NOLIBC_SYS_UTSNAME_H

#include "../sys.h"

#include <linux/utsname.h>

/*
 * int uname(struct utsname *buf);
 */

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

static __attribute__((unused))
int sys_uname(struct utsname *buf)
{
	return my_syscall1(__NR_uname, buf);
}

static __attribute__((unused))
int uname(struct utsname *buf)
{
	return __sysret(sys_uname(buf));
}

#endif /* _NOLIBC_SYS_UTSNAME_H */
