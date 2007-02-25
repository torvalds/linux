#ifndef _ASM_POWERPC_CURRENT_H
#define _ASM_POWERPC_CURRENT_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct task_struct;

#ifdef __powerpc64__
#include <linux/stddef.h>
#include <asm/paca.h>

static inline struct task_struct *get_current(void)
{
	struct task_struct *task;

	__asm__ __volatile__("ld %0,%1(13)"
	: "=r" (task)
	: "i" (offsetof(struct paca_struct, __current)));

	return task;
}
#define current	get_current()

#else

/*
 * We keep `current' in r2 for speed.
 */
register struct task_struct *current asm ("r2");

#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_CURRENT_H */
