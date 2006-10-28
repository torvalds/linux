/* taskstats_kern.h - kernel header for per-task statistics interface
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
 */

#ifndef _LINUX_TASKSTATS_KERN_H
#define _LINUX_TASKSTATS_KERN_H

#include <linux/taskstats.h>
#include <linux/sched.h>
#include <net/genetlink.h>

#ifdef CONFIG_TASKSTATS
extern kmem_cache_t *taskstats_cache;
extern struct mutex taskstats_exit_mutex;

static inline void taskstats_exit_free(struct taskstats *tidstats)
{
	if (tidstats)
		kmem_cache_free(taskstats_cache, tidstats);
}

static inline void taskstats_tgid_init(struct signal_struct *sig)
{
	sig->stats = NULL;
}

static inline void taskstats_tgid_alloc(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	struct taskstats *stats;

	if (sig->stats != NULL)
		return;

	/* No problem if kmem_cache_zalloc() fails */
	stats = kmem_cache_zalloc(taskstats_cache, SLAB_KERNEL);

	spin_lock_irq(&tsk->sighand->siglock);
	if (!sig->stats) {
		sig->stats = stats;
		stats = NULL;
	}
	spin_unlock_irq(&tsk->sighand->siglock);

	if (stats)
		kmem_cache_free(taskstats_cache, stats);
}

static inline void taskstats_tgid_free(struct signal_struct *sig)
{
	if (sig->stats)
		kmem_cache_free(taskstats_cache, sig->stats);
}

extern void taskstats_exit_alloc(struct taskstats **, unsigned int *);
extern void taskstats_exit_send(struct task_struct *, struct taskstats *, int, unsigned int);
extern void taskstats_init_early(void);
#else
static inline void taskstats_exit_alloc(struct taskstats **ptidstats, unsigned int *mycpu)
{}
static inline void taskstats_exit_free(struct taskstats *ptidstats)
{}
static inline void taskstats_exit_send(struct task_struct *tsk,
				       struct taskstats *tidstats,
				       int group_dead, unsigned int cpu)
{}
static inline void taskstats_tgid_init(struct signal_struct *sig)
{}
static inline void taskstats_tgid_alloc(struct task_struct *tsk)
{}
static inline void taskstats_tgid_free(struct signal_struct *sig)
{}
static inline void taskstats_init_early(void)
{}
#endif /* CONFIG_TASKSTATS */

#endif

