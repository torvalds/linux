/*
 * KASAN quarantine.
 *
 * Author: Alexander Potapenko <glider@google.com>
 * Copyright (C) 2016 Google, Inc.
 *
 * Based on code by Dmitry Chernenkov.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/gfp.h>
#include <linux/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

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

static void qlist_move(struct qlist_head *from, struct qlist_node *last,
		struct qlist_head *to, size_t size)
{
	if (unlikely(last == from->tail)) {
		qlist_move_all(from, to);
		return;
	}
	if (qlist_empty(to))
		to->head = from->head;
	else
		to->tail->next = from->head;
	to->tail = last;
	from->head = last->next;
	last->next = NULL;
	from->bytes -= size;
	to->bytes += size;
}


/*
 * The object quarantine consists of per-cpu queues and a global queue,
 * guarded by quarantine_lock.
 */
static DEFINE_PER_CPU(struct qlist_head, cpu_quarantine);

static struct qlist_head global_quarantine;
static DEFINE_SPINLOCK(quarantine_lock);

/* Maximum size of the global queue. */
static unsigned long quarantine_size;

/*
 * The fraction of physical memory the quarantine is allowed to occupy.
 * Quarantine doesn't support memory shrinker with SLAB allocator, so we keep
 * the ratio low to avoid OOM.
 */
#define QUARANTINE_FRACTION 32

#define QUARANTINE_LOW_SIZE (READ_ONCE(quarantine_size) * 3 / 4)
#define QUARANTINE_PERCPU_SIZE (1 << 20)

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
	struct kasan_alloc_meta *alloc_info = get_alloc_info(cache, object);
	unsigned long flags;

	if (IS_ENABLED(CONFIG_SLAB))
		local_irq_save(flags);

	alloc_info->state = KASAN_STATE_FREE;
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

void quarantine_put(struct kasan_free_meta *info, struct kmem_cache *cache)
{
	unsigned long flags;
	struct qlist_head *q;
	struct qlist_head temp = QLIST_INIT;

	local_irq_save(flags);

	q = this_cpu_ptr(&cpu_quarantine);
	qlist_put(q, &info->quarantine_link, cache->size);
	if (unlikely(q->bytes > QUARANTINE_PERCPU_SIZE))
		qlist_move_all(q, &temp);

	local_irq_restore(flags);

	if (unlikely(!qlist_empty(&temp))) {
		spin_lock_irqsave(&quarantine_lock, flags);
		qlist_move_all(&temp, &global_quarantine);
		spin_unlock_irqrestore(&quarantine_lock, flags);
	}
}

void quarantine_reduce(void)
{
	size_t new_quarantine_size;
	unsigned long flags;
	struct qlist_head to_free = QLIST_INIT;
	size_t size_to_free = 0;
	struct qlist_node *last;

	if (likely(READ_ONCE(global_quarantine.bytes) <=
		   READ_ONCE(quarantine_size)))
		return;

	spin_lock_irqsave(&quarantine_lock, flags);

	/*
	 * Update quarantine size in case of hotplug. Allocate a fraction of
	 * the installed memory to quarantine minus per-cpu queue limits.
	 */
	new_quarantine_size = (READ_ONCE(totalram_pages) << PAGE_SHIFT) /
		QUARANTINE_FRACTION;
	new_quarantine_size -= QUARANTINE_PERCPU_SIZE * num_online_cpus();
	WRITE_ONCE(quarantine_size, new_quarantine_size);

	last = global_quarantine.head;
	while (last) {
		struct kmem_cache *cache = qlink_to_cache(last);

		size_to_free += cache->size;
		if (!last->next || size_to_free >
		    global_quarantine.bytes - QUARANTINE_LOW_SIZE)
			break;
		last = last->next;
	}
	qlist_move(&global_quarantine, last, &to_free, size_to_free);

	spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, NULL);
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

void quarantine_remove_cache(struct kmem_cache *cache)
{
	unsigned long flags;
	struct qlist_head to_free = QLIST_INIT;

	on_each_cpu(per_cpu_remove_cache, cache, 1);

	spin_lock_irqsave(&quarantine_lock, flags);
	qlist_move_cache(&global_quarantine, &to_free, cache);
	spin_unlock_irqrestore(&quarantine_lock, flags);

	qlist_free_all(&to_free, cache);
}
