/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  syscore_ops.h - System core operations.
 *
 *  Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Novell Inc.
 */

#ifndef _LINUX_SYSCORE_OPS_H
#define _LINUX_SYSCORE_OPS_H

#include <linux/list.h>

struct syscore_ops {
	int (*suspend)(void *data);
	void (*resume)(void *data);
	void (*shutdown)(void *data);
};

struct syscore {
	struct list_head node;
	const struct syscore_ops *ops;
	void *data;
};

extern void register_syscore(struct syscore *syscore);
extern void unregister_syscore(struct syscore *syscore);
#ifdef CONFIG_PM_SLEEP
extern int syscore_suspend(void);
extern void syscore_resume(void);
#endif
extern void syscore_shutdown(void);

#endif
