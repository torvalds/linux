// SPDX-License-Identifier: GPL-2.0
/*
 * KASAN quarantine.
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on code by Dmitry Chernenkov.
 */

#include <linux/gfp.h>
#include <linux/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/cpuhotplug.h>

#include "../slab.h"
#include "kasan.h"

/* Data structure and operations for quarantine queues. */

/*
 * Each queue is a signle-linked list, which also stores the total size of
 * objects inside of it.
 */
struct qlist_head {
	struct qlist_node *head;
	struct qlist_node *tail;
	size_t bytes;
	bool offline;
};

#define QLIST_INIT { NULL, NULL, 0 }

static bool qlist_empty(struct qlist_head *q)
{
	return !q->head;
}

static void qlist_init(struct qlist_head *q)
{
	q->head = q->tail = NULL;
	q->bytes = 0;
}

static void qlist_put(struct qlist_head *q, struct qlist_node *qlink,
		size_t size)
{
	if (unlikely(qlist_empty(q)))
		q->head = qlink;
	else
		q->tail->next = qlink;
	q->tail = qlink;
	qlink->next = NULL;
	q->bytes += size;
}

static void qlist_move_all(struct qlist_head *from, struct qlist_head *to)
{
	if (unlikely(qlist_empty(from)))
		return;

	if (qlist_empty(to)) {
		*to = *from;
		qlist_init(from);
		return;
	}

	to->tail->next = from->head;
	to->tail = from->tail;
	to->bytes += from->bytes;

	qlist_init(from);
}

#define QUARANTINE_PERCPU_SIZE (1 << 20)
#define QUARANTINE_BATCHES \
	(1024 > 4 * CONFIG_NR_CPUS ? 1024 : 4 * CONFIG_NR_CPUS)

/*
 * The object quarantine consists of per-cpu queues and a global queue,
 * guarded by quarantine_lock.
 */
static DEFINE_PER_CPU(struct qlist_head, cpu_quarantine);

/* Round-robin FIFO array of batches. */
static struct qlist_head global_quarantine[QUARANTINE_BATCHES];
static int quarantine_head;
static int quarantine_tail;
/* Total size of all objects in global_quarantine across all batches. */
static unsigned long quarantine_size;
static DEFINE_RAW_SPINLOCK(quarantine_lock);
DEFINE_STATIC_SRCU(remove_cache_srcu);

/* Maximum size of the global queue. */
static unsigned long quarantine_max_size;

/*
 * Target size of a batch in global_quarantine.
 * Usually equal to QUARANTINE_PERCPU_SIZE unless we have too much RAM.
 */
static unsigned long quarantine_batch_size;

/*
 * The fraction of physical memory the quarantine is allowed to occupy.
 * Quarantine doesn't support memory shrinker with SLAB allocator, so we keep
 * the ratio low to avoid OOM.
 */
#define QUARANTINE_FRACTION 32

static struct kmem_cache *qlink_to_cache(struct qlist_node *qlink)
{
	return virt_to_head_page(qlink)->slab_cache;
}

static void *qlink_to_object(struct qlist_node *qlink, struct kmem_cache *cache)
{
	struct kasan_free_meta *free_info =
		container_of(qlink, struct kasan_free_meta,
			     quarantine_link);

	return ((void *)free_info) - cache->kasan_info.free_meta_offset;
}

static void qlink_free(struct qlist_node *qlink, struct kmem_cache *cache)
{
	void *object = qlink_to_object(qlink, cache);
	unsigned long flags;

	if (IS_ENABLED(CONFIG_SLAB))
		local_irq_save(flags);

	/*
	 * As the object now gets freed from the quaratine, assume that its
	 * free track is no longer valid.
	 */
	*(u8 *)kasan_mem_to_shadow(object) = KASAN_KMALLOC_FREE;

	___cache_free(cache, object, _THIS_IP_);

	if (IS_ENABLED(CONFIG_SLAB))
		local_irq_restore(flags);
}

static void qlist_free_all(struct qlist_head *q, struct kmem_cache *cache)
{
	struct qlist_node *qlink;

	if (unlikely(qlist_empty(q)))
		return;

	qlink = q->head;
	while (qlink) {
		struct kmem_cache *obj_cache =
			cache ? cache :	qlink_to_cache(qlink);
		struct qlist_node *next = qlink->next;

		qlink_free(qlink, obj_cache);
		qlink = next;
	}
	qlist_init(q);
}

bool quarantine_put(struct kmem_cache *cache, void *object)
{
	unsigned long flags;
	struct qlist_head *q;
	struct qlist_head temp = QLIST_INIT;
	struct kasan_free_meta *meta = kasan_get_free_meta(cache, object);

	/*
	 * If there's no metadata for this object, don't put it into
	 * quarantine.
	 */
	if (!meta)
		return false;

	/*
	 * Note: irq must be disabled until after we move the batch to the
	 * global quarantine. Otherwise quarantine_remove_cache() can miss
	 * some objects belonging to the cache if they are in our local temp
	 * list. quarantine_remove_cache() executes on_each_cpu() at the
	 * beginning which ensures that it either sees the objects in per-cpu
	 * lists or in the global quarantine.
	 */
	local_irq_save(flags);

	q = this_cpu_ptr(&cpu_quarantine);
	if (q->offline) {
		local_irq_restore(flags);
		return false;
	}
	qlist_put(q, &meta->quarantine_link, cache->size);
	if (unlikely(q->bytes > QUARANTINE_PERCPU_SIZE)) {
		qlist_move_all(q, &temp);

		raw_spin_lock(&quarantine_lock);
		WRITE_ONCE(quarantine_size, quarantine_size + temp.bytes);
		qlist_move_all(&temp, &global_quarantine[quarantine_tail]);
		if (global_quarantine[quarantine_tail].bytes >=
				READ_ONCE(quarantine_batch_size)) {
			int new_tail;

			new_tail = quarantine_tail + 1;
			if (new_tail == QUARANTINE_BATCHES)
				new_tail = 0;
			if (new_tail != quarantine_head)
				quarantine_tail = new_tail;
		}
		raw_spin_unlock(&quarantine_lock);
	}

	local_irq_restore(flags);

	return true;
}

void quarantine_reduce(void)
{
	size_t total_size, new_quarantine_size, percpu_quarantines;
	unsigned long flags;
	int srcu_idx;
	struct qlist_head to_free = QLIST_INIT;

	if (likely(READ_ONCE(quarantine_size) <=
		   READ_ONCE(quarantine_max_size)))
		return;

	/*
	 * srcu critical section ensures that quarantine_remove_cache()
	 * will not miss objects belonging to the cache while they are in our
	 * local to_free list. srcu is chosen because (1) it gives us private
	 * grace period domain that does not interfere with anything else,
	 * and (2) it allows synchronize_srcu() to return without waiting
	 * if there are no pending read critical sections (which is the
	 * expected case).
	 */
	srcu_idx = srcu_read_lock(&remove_cache_srcu);
	raw_spin_lock_irqsave(&quarantine_lock, flags);

	/*
	 * Update quarantine size in case of hotplug. Allocate a fraction of
	 * the installed memory to quarantine minus per-cpu queue limits.
	 */
	total_size = (totalram_pages() << PAGE_SHIFT) /
		QUARANTINE_FRACTION;
	percpu_quarantines = QUARANTINE_PERCPU_SIZE * num_online_cpus();
	new_quarantine_size = (total_size < percpu_quarantines) ?
		0 : total_size - percpu_quarantines;
	WRITE_ONCE(quarantine_max_size, new_quarantine_size);
	/* Aim at consuming at most 1/2 of slots in quarantine. */
	WRITE_ONCE(quarantine_batch_size, max((size_t)QUARANTINE_PERCPU_SIZE,
		2 * total_size / QUARANTINE_BATCHES));

	if (likely(quarantine_size > quarantine_max_size)) {
		qlist_move_all(&global_quarantine[quarantine_head], &to_free);
		WRITE_ONCE(quarantine_size, quarantine_size - to_free.bytes);
		quarantine_head++;
		if (quarantine_head == QUARANTINE_BATCHES)
			quarantine_head = 0;
	}

	raw_spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, NULL);
	srcu_read_unlock(&remove_cache_srcu, srcu_idx);
}

static void qlist_move_cache(struct qlist_head *from,
				   struct qlist_head *to,
				   struct kmem_cache *cache)
{
	struct qlist_node *curr;

	if (unlikely(qlist_empty(from)))
		return;

	curr = from->head;
	qlist_init(from);
	while (curr) {
		struct qlist_node *next = curr->next;
		struct kmem_cache *obj_cache = qlink_to_cache(curr);

		if (obj_cache == cache)
			qlist_put(to, curr, obj_cache->size);
		else
			qlist_put(from, curr, obj_cache->size);

		curr = next;
	}
}

static void per_cpu_remove_cache(void *arg)
{
	struct kmem_cache *cache = arg;
	struct qlist_head to_free = QLIST_INIT;
	struct qlist_head *q;

	q = this_cpu_ptr(&cpu_quarantine);
	qlist_move_cache(q, &to_free, cache);
	qlist_free_all(&to_free, cache);
}

/* Free all quarantined objects belonging to cache. */
void quarantine_remove_cache(struct kmem_cache *cache)
{
	unsigned long flags, i;
	struct qlist_head to_free = QLIST_INIT;

	/*
	 * Must be careful to not miss any objects that are being moved from
	 * per-cpu list to the global quarantine in quarantine_put(),
	 * nor objects being freed in quarantine_reduce(). on_each_cpu()
	 * achieves the first goal, while synchronize_srcu() achieves the
	 * second.
	 */
	on_each_cpu(per_cpu_remove_cache, cache, 1);

	raw_spin_lock_irqsave(&quarantine_lock, flags);
	for (i = 0; i < QUARANTINE_BATCHES; i++) {
		if (qlist_empty(&global_quarantine[i]))
			continue;
		qlist_move_cache(&global_quarantine[i], &to_free, cache);
		/* Scanning whole quarantine can take a while. */
		raw_spin_unlock_irqrestore(&quarantine_lock, flags);
		cond_resched();
		raw_spin_lock_irqsave(&quarantine_lock, flags);
	}
	raw_spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, cache);

	synchronize_srcu(&remove_cache_srcu);
}

static int kasan_cpu_online(unsigned int cpu)
{
	this_cpu_ptr(&cpu_quarantine)->offline = false;
	return 0;
}

static int kasan_cpu_offline(unsigned int cpu)
{
	struct qlist_head *q;

	q = this_cpu_ptr(&cpu_quarantine);
	/* Ensure the ordering between the writing to q->offline and
	 * qlist_free_all. Otherwise, cpu_quarantine may be corrupted
	 * by interrupt.
	 */
	WRITE_ONCE(q->offline, true);
	barrier();
	qlist_free_all(q, NULL);
	return 0;
}

static int __init kasan_cpu_quarantine_init(void)
{
	int ret = 0;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mm/kasan:online",
				kasan_cpu_online, kasan_cpu_offline);
	if (ret < 0)
		pr_err("kasan cpu quarantine register failed [%d]\n", ret);
	return ret;
}
late_initcall(kasan_cpu_quarantine_init);
