/* delayacct.h - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 */

#ifndef _LINUX_DELAYACCT_H
#define _LINUX_DELAYACCT_H

#include <uapi/linux/taskstats.h>

/*
 * Per-task flags relevant to delay accounting
 * maintained privately to avoid exhausting similar flags in sched.h:PF_*
 * Used to set current->delays->flags
 */
#define DELAYACCT_PF_SWAPIN	0x00000001	/* I am doing a swapin */
#define DELAYACCT_PF_BLKIO	0x00000002	/* I am waiting on IO */

#ifdef CONFIG_TASK_DELAY_ACCT
struct task_delay_info {
	raw_spinlock_t	lock;
	unsigned int	flags;	/* Private per-task flags */

	/* For each stat XXX, add following, aligned appropriately
	 *
	 * struct timespec XXX_start, XXX_end;
	 * u64 XXX_delay;
	 * u32 XXX_count;
	 *
	 * Atomicity of updates to XXX_delay, XXX_count protected by
	 * single lock above (split into XXX_lock if contention is an issue).
	 */

	/*
	 * XXX_count is incremented on every XXX operation, the delay
	 * associated with the operation is added to XXX_delay.
	 * XXX_delay contains the accumulated delay time in nanoseconds.
	 */
	u64 blkio_start;	/* Shared by blkio, swapin */
	u64 blkio_delay;	/* wait for sync block io completion */
	u64 swapin_delay;	/* wait for swapin block io completion */
	u32 blkio_count;	/* total count of the number of sync block */
				/* io operations performed */
	u32 swapin_count;	/* total count of the number of swapin block */
				/* io operations performed */

	u64 freepages_start;
	u64 freepages_delay;	/* wait for memory reclaim */
	u32 freepages_count;	/* total count of memory reclaim */
};
#endif

#include <linux/sched.h>
#include <linux/slab.h>

#ifdef CONFIG_TASK_DELAY_ACCT
extern int delayacct_on;	/* Delay accounting turned on/off */
extern struct kmem_cache *delayacct_cache;
extern void delayacct_init(void);
extern void __delayacct_tsk_init(struct task_struct *);
extern void __delayacct_tsk_exit(struct task_struct *);
extern void __delayacct_blkio_start(void);
extern void __delayacct_blkio_end(struct task_struct *);
extern int __delayacct_add_tsk(struct taskstats *, struct task_struct *);
extern __u64 __delayacct_blkio_ticks(struct task_struct *);
extern void __delayacct_freepages_start(void);
extern void __delayacct_freepages_end(void);

static inline int delayacct_is_task_waiting_on_io(struct task_struct *p)
{
	if (p->delays)
		return (p->delays->flags & DELAYACCT_PF_BLKIO);
	else
		return 0;
}

static inline void delayacct_set_flag(int flag)
{
	if (current->delays)
		current->delays->flags |= flag;
}

static inline void delayacct_clear_flag(int flag)
{
	if (current->delays)
		current->delays->flags &= ~flag;
}

static inline void delayacct_tsk_init(struct task_struct *tsk)
{
	/* reinitialize in case parent's non-null pointer was dup'ed*/
	tsk->delays = NULL;
	if (delayacct_on)
		__delayacct_tsk_init(tsk);
}

/* Free tsk->delays. Called from bad fork and __put_task_struct
 * where there's no risk of tsk->delays being accessed elsewhere
 */
static inline void delayacct_tsk_free(struct task_struct *tsk)
{
	if (tsk->delays)
		kmem_cache_free(delayacct_cache, tsk->delays);
	tsk->delays = NULL;
}

static inline void delayacct_blkio_start(void)
{
	delayacct_set_flag(DELAYACCT_PF_BLKIO);
	if (current->delays)
		__delayacct_blkio_start();
}

static inline void delayacct_blkio_end(struct task_struct *p)
{
	if (current->delays)
		__delayacct_blkio_end(p);
	delayacct_clear_flag(DELAYACCT_PF_BLKIO);
}

static inline int delayacct_add_tsk(struct taskstats *d,
					struct task_struct *tsk)
{
	if (!delayacct_on || !tsk->delays)
		return 0;
	return __delayacct_add_tsk(d, tsk);
}

static inline __u64 delayacct_blkio_ticks(struct task_struct *tsk)
{
	if (tsk->delays)
		return __delayacct_blkio_ticks(tsk);
	return 0;
}

static inline void delayacct_freepages_start(void)
{
	if (current->delays)
		__delayacct_freepages_start();
}

static inline void delayacct_freepages_end(void)
{
	if (current->delays)
		__delayacct_freepages_end();
}

#else
static inline void delayacct_set_flag(int flag)
{}
static inline void delayacct_clear_flag(int flag)
{}
static inline void delayacct_init(void)
{}
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_free(struct task_struct *tsk)
{}
static inline void delayacct_blkio_start(void)
{}
static inline void delayacct_blkio_end(struct task_struct *p)
{}
static inline int delayacct_add_tsk(struct taskstats *d,
					struct task_struct *tsk)
{ return 0; }
static inline __u64 delayacct_blkio_ticks(struct task_struct *tsk)
{ return 0; }
static inline int delayacct_is_task_waiting_on_io(struct task_struct *p)
{ return 0; }
static inline void delayacct_freepages_start(void)
{}
static inline void delayacct_freepages_end(void)
{}

#endif /* CONFIG_TASK_DELAY_ACCT */

#endif
