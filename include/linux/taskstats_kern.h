/* taskstats_kern.h - kernel header for per-task statistics interface
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
 */

#ifndef _LINUX_TASKSTATS_KERN_H
#define _LINUX_TASKSTATS_KERN_H

#include <linux/taskstats.h>
#include <linux/sched.h>

enum {
	TASKSTATS_MSG_UNICAST,		/* send data only to requester */
	TASKSTATS_MSG_MULTICAST,	/* send data to a group */
};

#ifdef CONFIG_TASKSTATS
extern kmem_cache_t *taskstats_cache;

static inline void taskstats_exit_alloc(struct taskstats **ptidstats,
					struct taskstats **ptgidstats)
{
	*ptidstats = kmem_cache_zalloc(taskstats_cache, SLAB_KERNEL);
	*ptgidstats = kmem_cache_zalloc(taskstats_cache, SLAB_KERNEL);
}

static inline void taskstats_exit_free(struct taskstats *tidstats,
					struct taskstats *tgidstats)
{
	if (tidstats)
		kmem_cache_free(taskstats_cache, tidstats);
	if (tgidstats)
		kmem_cache_free(taskstats_cache, tgidstats);
}

extern void taskstats_exit_send(struct task_struct *, struct taskstats *,
				struct taskstats *);
extern void taskstats_init_early(void);

#else
static inline void taskstats_exit_alloc(struct taskstats **ptidstats,
					struct taskstats **ptgidstats)
{}
static inline void taskstats_exit_free(struct taskstats *ptidstats,
					struct taskstats *ptgidstats)
{}
static inline void taskstats_exit_send(struct task_struct *tsk,
					struct taskstats *tidstats,
					struct taskstats *tgidstats)
{}
static inline void taskstats_init_early(void)
{}
#endif /* CONFIG_TASKSTATS */

#endif

