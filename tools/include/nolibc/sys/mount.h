/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Mount definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_MOUNT_H
#define _NOLIBC_SYS_MOUNT_H

#include "../sys.h"

#include <linux/mount.h>

/*
 * int mount(const char *source, const char *target,
 *           const char *fstype, unsigned long flags,
 *           const void *data);
 */
static __attribute__((unused))
int sys_mount(const char *src, const char *tgt, const char *fst,
	      unsigned long flags, const void *data)
{
	return my_syscall5(__NR_mount, src, tgt, fst, flags, data);
}

static __attribute__((unused))
int mount(const char *src, const char *tgt,
	  const char *fst, unsigned long flags,
	  const void *data)
{
	return __sysret(sys_mount(src, tgt, fst, flags, data));
}

#endif /* _NOLIBC_SYS_MOUNT_H */
