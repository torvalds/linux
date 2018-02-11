/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERCPU_IDA_H__
#define __PERCPU_IDA_H__

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock_types.h>
#include <linux/wait.h>
#include <linux/cpumask.h>

struct percpu_ida_cpu;

struct percpu_ida {
	/*
	 * number of tags available to be allocated, as passed to
	 * percpu_ida_init()
	 */
	unsigned			nr_tags;
	unsigned			percpu_max_size;
	unsigned			percpu_batch_size;

	struct percpu_ida_cpu __percpu	*tag_cpu;

	/*
	 * Bitmap of cpus that (may) have tags on their percpu freelists:
	 * steal_tags() uses this to decide when to steal tags, and which cpus
	 * to try stealing from.
	 *
	 * It's ok for a freelist to be empty when its bit is set - steal_tags()
	 * will just keep looking - but the bitmap _must_ be set whenever a
	 * percpu freelist does have tags.
	 */
	cpumask_t			cpus_have_tags;

	struct {
		spinlock_t		lock;
		/*
		 * When we go to steal tags from another cpu (see steal_tags()),
		 * we want to pick a cpu at random. Cycling through them every
		 * time we steal is a bit easier and more or less equivalent:
		 */
		unsigned		cpu_last_stolen;

		/* For sleeping on allocation failure */
		wait_queue_head_t	wait;

		/*
		 * Global freelist - it's a stack where nr_free points to the
		 * top
		 */
		unsigned		nr_free;
		unsigned		*freelist;
	} ____cacheline_aligned_in_smp;
};

/*
 * Number of tags we move between the percpu freelist and the global freelist at
 * a time
 */
#define IDA_DEFAULT_PCPU_BATCH_MOVE	32U
/* Max size of percpu freelist, */
#define IDA_DEFAULT_PCPU_SIZE	((IDA_DEFAULT_PCPU_BATCH_MOVE * 3) / 2)

int percpu_ida_alloc(struct percpu_ida *pool, int state);
void percpu_ida_free(struct percpu_ida *pool, unsigned tag);

void percpu_ida_destroy(struct percpu_ida *pool);
int __percpu_ida_init(struct percpu_ida *pool, unsigned long nr_tags,
	unsigned long max_size, unsigned long batch_size);
static inline int percpu_ida_init(struct percpu_ida *pool, unsigned long nr_tags)
{
	return __percpu_ida_init(pool, nr_tags, IDA_DEFAULT_PCPU_SIZE,
		IDA_DEFAULT_PCPU_BATCH_MOVE);
}

typedef int (*percpu_ida_cb)(unsigned, void *);
int percpu_ida_for_each_free(struct percpu_ida *pool, percpu_ida_cb fn,
	void *data);

unsigned percpu_ida_free_tags(struct percpu_ida *pool, int cpu);
#endif /* __PERCPU_IDA_H__ */
