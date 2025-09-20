/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Reboot definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_REBOOT_H
#define _NOLIBC_SYS_REBOOT_H

#include "../sys.h"

#include <linux/reboot.h>

/*
 * int reboot(int cmd);
 * <cmd> is among LINUX_REBOOT_CMD_*
 */

static __attribute__((unused))
ssize_t sys_reboot(int magic1, int magic2, int cmd, void *arg)
{
	return my_syscall4(__NR_reboot, magic1, magic2, cmd, arg);
}

static __attribute__((unused))
int reboot(int cmd)
{
	return __sysret(sys_reboot(LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, 0));
}

#endif /* _NOLIBC_SYS_REBOOT_H */
