/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shadow Call Stack support.
 *
 * Copyright (C) 2019 Google LLC
 */

#ifndef _LINUX_SCS_H
#define _LINUX_SCS_H

#include <linux/gfp.h>
#include <linux/poison.h>
#include <linux/sched.h>
#include <linux/sizes.h>

#ifdef CONFIG_SHADOW_CALL_STACK

/*
 * In testing, 1 KiB shadow stack size (i.e. 128 stack frames on a 64-bit
 * architecture) provided ~40% safety margin on stack usage while keeping
 * memory allocation overhead reasonable.
 */
#define SCS_SIZE		SZ_1K
#define GFP_SCS			(GFP_KERNEL | __GFP_ZERO)

/* An illegal pointer value to mark the end of the shadow stack. */
#define SCS_END_MAGIC		(0x5f6UL + POISON_POINTER_DELTA)

#define task_scs(tsk)		(task_thread_info(tsk)->scs_base)
#define task_scs_offset(tsk)	(task_thread_info(tsk)->scs_offset)

void scs_init(void);
int scs_prepare(struct task_struct *tsk, int node);
void scs_release(struct task_struct *tsk);

static inline void scs_task_reset(struct task_struct *tsk)
{
	/*
	 * Reset the shadow stack to the base address in case the task
	 * is reused.
	 */
	task_scs_offset(tsk) = 0;
}

static inline unsigned long *__scs_magic(void *s)
{
	return (unsigned long *)(s + SCS_SIZE) - 1;
}

static inline bool scs_corrupted(struct task_struct *tsk)
{
	unsigned long *magic = __scs_magic(task_scs(tsk));

	return (task_scs_offset(tsk) >= SCS_SIZE - 1 ||
		READ_ONCE_NOCHECK(*magic) != SCS_END_MAGIC);
}

#else /* CONFIG_SHADOW_CALL_STACK */

static inline void scs_init(void) {}
static inline void scs_task_reset(struct task_struct *tsk) {}
static inline int scs_prepare(struct task_struct *tsk, int node) { return 0; }
static inline bool scs_corrupted(struct task_struct *tsk) { return false; }
static inline void scs_release(struct task_struct *tsk) {}

#endif /* CONFIG_SHADOW_CALL_STACK */

#endif /* _LINUX_SCS_H */
