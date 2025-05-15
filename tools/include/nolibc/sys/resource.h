/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Resource definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_RESOURCE_H
#define _NOLIBC_SYS_RESOURCE_H

#include "../sys.h"

#include <linux/resource.h>

/*
 * int getrlimit(int resource, struct rlimit *rlim);
 * int setrlimit(int resource, const struct rlimit *rlim);
 */

static __attribute__((unused))
int sys_prlimit64(pid_t pid, int resource,
		  const struct rlimit64 *new_limit, struct rlimit64 *old_limit)
{
	return my_syscall4(__NR_prlimit64, pid, resource, new_limit, old_limit);
}

static __attribute__((unused))
int getrlimit(int resource, struct rlimit *rlim)
{
	struct rlimit64 rlim64;
	int ret;

	ret = __sysret(sys_prlimit64(0, resource, NULL, &rlim64));
	rlim->rlim_cur = rlim64.rlim_cur;
	rlim->rlim_max = rlim64.rlim_max;

	return ret;
}

static __attribute__((unused))
int setrlimit(int resource, const struct rlimit *rlim)
{
	struct rlimit64 rlim64 = {
		.rlim_cur = rlim->rlim_cur,
		.rlim_max = rlim->rlim_max,
	};

	return __sysret(sys_prlimit64(0, resource, &rlim64, NULL));
}

#endif /* _NOLIBC_SYS_RESOURCE_H */
