/*
 * Percpu IDA library
 *
 * Copyright (C) 2013 Datera, Inc. Kent Overstreet
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/hardirq.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/percpu_ida.h>

/*
 * Number of tags we move between the percpu freelist and the global freelist at
 * a time
 */
#define IDA_PCPU_BATCH_MOVE	32U

/* Max size of percpu freelist, */
#define IDA_PCPU_SIZE		((IDA_PCPU_BATCH_MOVE * 3) / 2)

struct percpu_ida_cpu {
	/*
	 * Even though this is percpu, we need a lock for tag stealing by remote
	 * CPUs:
	 */
	spinlock_t			lock;

	/* nr_free/freelist form a stack of free IDs */
	unsigned			nr_free;
	unsigned			freelist[];
};

static inline void move_tags(unsigned *dst, unsigned *dst_nr,
			     unsigned *src, unsigned *src_nr,
			     unsigned nr)
{
	*src_nr -= nr;
	memcpy(dst + *dst_nr, src + *src_nr, sizeof(unsigned) * nr);
	*dst_nr += nr;
}

/*
 * Try to steal tags from a remote cpu's percpu freelist.
 *
 * We first check how many percpu freelists have tags - we don't steal tags
 * unless enough percpu freelists have tags on them that it's possible more than
 * half the total tags could be stuck on remote percpu freelists.
 *
 * Then we iterate through the cpus until we find some tags - we don't attempt
 * to find the "best" cpu to steal from, to keep cacheline bouncing to a
 * minimum.
 */
static inline void steal_tags(struct percpu_ida *pool,
			      struct percpu_ida_cpu *tags)
{
	unsigned cpus_have_tags, cpu = pool->cpu_last_stolen;
	struct percpu_ida_cpu *remote;

	for (cpus_have_tags = cpumask_weight(&pool->cpus_have_tags);
	     cpus_have_tags * IDA_PCPU_SIZE > pool->nr_tags / 2;
	     cpus_have_tags--) {
		cpu = cpumask_next(cpu, &pool->cpus_have_tags);

		if (cpu >= nr_cpu_ids) {
			cpu = cpumask_first(&pool->cpus_have_tags);
			if (cpu >= nr_cpu_ids)
				BUG();
		}

		pool->cpu_last_stolen = cpu;
		remote = per_cpu_ptr(pool->tag_cpu, cpu);

		cpumask_clear_cpu(cpu, &pool->cpus_have_tags);

		if (remote == tags)
			continue;

		spin_lock(&remote->lock);

		if (remote->nr_free) {
			memcpy(tags->freelist,
			       remote->freelist,
			       sizeof(unsigned) * remote->nr_free);

			tags->nr_free = remote->nr_free;
			remote->nr_free = 0;
		}

		spin_unlock(&remote->lock);

		if (tags->nr_free)
			break;
	}
}

/*
 * Pop up to IDA_PCPU_BATCH_MOVE IDs off the global freelist, and push them onto
 * our percpu freelist:
 */
static inline void alloc_global_tags(struct percpu_ida *pool,
				     struct percpu_ida_cpu *tags)
{
	move_tags(tags->freelist, &tags->nr_free,
		  pool->freelist, &pool->nr_free,
		  min(pool->nr_free, IDA_PCPU_BATCH_MOVE));
}

static inline unsigned alloc_local_tag(struct percpu_ida *pool,
				       struct percpu_ida_cpu *tags)
{
	int tag = -ENOSPC;

	spin_lock(&tags->lock);
	if (tags->nr_free)
		tag = tags->freelist[--tags->nr_free];
	spin_unlock(&tags->lock);

	return tag;
}

/**
 * percpu_ida_alloc - allocate a tag
 * @pool: pool to allocate from
 * @gfp: gfp flags
 *
 * Returns a tag - an integer in the range [0..nr_tags) (passed to
 * tag_pool_init()), or otherwise -ENOSPC on allocation failure.
 *
 * Safe to be called from interrupt context (assuming it isn't passed
 * __GFP_WAIT, of course).
 *
 * @gfp indicates whether or not to wait until a free id is available (it's not
 * used for internal memory allocations); thus if passed __GFP_WAIT we may sleep
 * however long it takes until another thread frees an id (same semantics as a
 * mempool).
 *
 * Will not fail if passed __GFP_WAIT.
 */
int percpu_ida_alloc(struct percpu_ida *pool, gfp_t gfp)
{
	DEFINE_WAIT(wait);
	struct percpu_ida_cpu *tags;
	unsigned long flags;
	int tag;

	local_irq_save(flags);
	tags = this_cpu_ptr(pool->tag_cpu);

	/* Fastpath */
	tag = alloc_local_tag(pool, tags);
	if (likely(tag >= 0)) {
		local_irq_restore(flags);
		return tag;
	}

	while (1) {
		spin_lock(&pool->lock);

		/*
		 * prepare_to_wait() must come before steal_tags(), in case
		 * percpu_ida_free() on another cpu flips a bit in
		 * cpus_have_tags
		 *
		 * global lock held and irqs disabled, don't need percpu lock
		 */
		prepare_to_wait(&pool->wait, &wait, TASK_UNINTERRUPTIBLE);

		if (!tags->nr_free)
			alloc_global_tags(pool, tags);
		if (!tags->nr_free)
			steal_tags(pool, tags);

		if (tags->nr_free) {
			tag = tags->freelist[--tags->nr_free];
			if (tags->nr_free)
				cpumask_set_cpu(smp_processor_id(),
						&pool->cpus_have_tags);
		}

		spin_unlock(&pool->lock);
		local_irq_restore(flags);

		if (tag >= 0 || !(gfp & __GFP_WAIT))
			break;

		schedule();

		local_irq_save(flags);
		tags = this_cpu_ptr(pool->tag_cpu);
	}

	finish_wait(&pool->wait, &wait);
	return tag;
}
EXPORT_SYMBOL_GPL(percpu_ida_alloc);

/**
 * percpu_ida_free - free a tag
 * @pool: pool @tag was allocated from
 * @tag: a tag previously allocated with percpu_ida_alloc()
 *
 * Safe to be called from interrupt context.
 */
void percpu_ida_free(struct percpu_ida *pool, unsigned tag)
{
	struct percpu_ida_cpu *tags;
	unsigned long flags;
	unsigned nr_free;

	BUG_ON(tag >= pool->nr_tags);

	local_irq_save(flags);
	tags = this_cpu_ptr(pool->tag_cpu);

	spin_lock(&tags->lock);
	tags->freelist[tags->nr_free++] = tag;

	nr_free = tags->nr_free;
	spin_unlock(&tags->lock);

	if (nr_free == 1) {
		cpumask_set_cpu(smp_processor_id(),
				&pool->cpus_have_tags);
		wake_up(&pool->wait);
	}

	if (nr_free == IDA_PCPU_SIZE) {
		spin_lock(&pool->lock);

		/*
		 * Global lock held and irqs disabled, don't need percpu
		 * lock
		 */
		if (tags->nr_free == IDA_PCPU_SIZE) {
			move_tags(pool->freelist, &pool->nr_free,
				  tags->freelist, &tags->nr_free,
				  IDA_PCPU_BATCH_MOVE);

			wake_up(&pool->wait);
		}
		spin_unlock(&pool->lock);
	}

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(percpu_ida_free);

/**
 * percpu_ida_destroy - release a tag pool's resources
 * @pool: pool to free
 *
 * Frees the resources allocated by percpu_ida_init().
 */
void percpu_ida_destroy(struct percpu_ida *pool)
{
	free_percpu(pool->tag_cpu);
	free_pages((unsigned long) pool->freelist,
		   get_order(pool->nr_tags * sizeof(unsigned)));
}
EXPORT_SYMBOL_GPL(percpu_ida_destroy);

/**
 * percpu_ida_init - initialize a percpu tag pool
 * @pool: pool to initialize
 * @nr_tags: number of tags that will be available for allocation
 *
 * Initializes @pool so that it can be used to allocate tags - integers in the
 * range [0, nr_tags). Typically, they'll be used by driver code to refer to a
 * preallocated array of tag structures.
 *
 * Allocation is percpu, but sharding is limited by nr_tags - for best
 * performance, the workload should not span more cpus than nr_tags / 128.
 */
int percpu_ida_init(struct percpu_ida *pool, unsigned long nr_tags)
{
	unsigned i, cpu, order;

	memset(pool, 0, sizeof(*pool));

	init_waitqueue_head(&pool->wait);
	spin_lock_init(&pool->lock);
	pool->nr_tags = nr_tags;

	/* Guard against overflow */
	if (nr_tags > (unsigned) INT_MAX + 1) {
		pr_err("percpu_ida_init(): nr_tags too large\n");
		return -EINVAL;
	}

	order = get_order(nr_tags * sizeof(unsigned));
	pool->freelist = (void *) __get_free_pages(GFP_KERNEL, order);
	if (!pool->freelist)
		return -ENOMEM;

	for (i = 0; i < nr_tags; i++)
		pool->freelist[i] = i;

	pool->nr_free = nr_tags;

	pool->tag_cpu = __alloc_percpu(sizeof(struct percpu_ida_cpu) +
				       IDA_PCPU_SIZE * sizeof(unsigned),
				       sizeof(unsigned));
	if (!pool->tag_cpu)
		goto err;

	for_each_possible_cpu(cpu)
		spin_lock_init(&per_cpu_ptr(pool->tag_cpu, cpu)->lock);

	return 0;
err:
	percpu_ida_destroy(pool);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(percpu_ida_init);
