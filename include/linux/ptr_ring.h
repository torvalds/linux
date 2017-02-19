/*
 *	Definitions for the 'struct ptr_ring' datastructure.
 *
 *	Author:
 *		Michael S. Tsirkin <mst@redhat.com>
 *
 *	Copyright (C) 2016 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	This is a limited-size FIFO maintaining pointers in FIFO order, with
 *	one CPU producing entries and another consuming entries from a FIFO.
 *
 *	This implementation tries to minimize cache-contention when there is a
 *	single producer and a single consumer CPU.
 */

#ifndef _LINUX_PTR_RING_H
#define _LINUX_PTR_RING_H 1

#ifdef __KERNEL__
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <asm/errno.h>
#endif

struct ptr_ring {
	int producer ____cacheline_aligned_in_smp;
	spinlock_t producer_lock;
	int consumer ____cacheline_aligned_in_smp;
	spinlock_t consumer_lock;
	/* Shared consumer/producer data */
	/* Read-only by both the producer and the consumer */
	int size ____cacheline_aligned_in_smp; /* max entries in queue */
	void **queue;
};

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().  If ring is ever resized, callers must hold
 * producer_lock - see e.g. ptr_ring_full.  Otherwise, if callers don't hold
 * producer_lock, the next call to __ptr_ring_produce may fail.
 */
static inline bool __ptr_ring_full(struct ptr_ring *r)
{
	return r->queue[r->producer];
}

static inline bool ptr_ring_full(struct ptr_ring *r)
{
	bool ret;

	spin_lock(&r->producer_lock);
	ret = __ptr_ring_full(r);
	spin_unlock(&r->producer_lock);

	return ret;
}

static inline bool ptr_ring_full_irq(struct ptr_ring *r)
{
	bool ret;

	spin_lock_irq(&r->producer_lock);
	ret = __ptr_ring_full(r);
	spin_unlock_irq(&r->producer_lock);

	return ret;
}

static inline bool ptr_ring_full_any(struct ptr_ring *r)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&r->producer_lock, flags);
	ret = __ptr_ring_full(r);
	spin_unlock_irqrestore(&r->producer_lock, flags);

	return ret;
}

static inline bool ptr_ring_full_bh(struct ptr_ring *r)
{
	bool ret;

	spin_lock_bh(&r->producer_lock);
	ret = __ptr_ring_full(r);
	spin_unlock_bh(&r->producer_lock);

	return ret;
}

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax(). Callers must hold producer_lock.
 */
static inline int __ptr_ring_produce(struct ptr_ring *r, void *ptr)
{
	if (unlikely(!r->size) || r->queue[r->producer])
		return -ENOSPC;

	r->queue[r->producer++] = ptr;
	if (unlikely(r->producer >= r->size))
		r->producer = 0;
	return 0;
}

/*
 * Note: resize (below) nests producer lock within consumer lock, so if you
 * consume in interrupt or BH context, you must disable interrupts/BH when
 * calling this.
 */
static inline int ptr_ring_produce(struct ptr_ring *r, void *ptr)
{
	int ret;

	spin_lock(&r->producer_lock);
	ret = __ptr_ring_produce(r, ptr);
	spin_unlock(&r->producer_lock);

	return ret;
}

static inline int ptr_ring_produce_irq(struct ptr_ring *r, void *ptr)
{
	int ret;

	spin_lock_irq(&r->producer_lock);
	ret = __ptr_ring_produce(r, ptr);
	spin_unlock_irq(&r->producer_lock);

	return ret;
}

static inline int ptr_ring_produce_any(struct ptr_ring *r, void *ptr)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&r->producer_lock, flags);
	ret = __ptr_ring_produce(r, ptr);
	spin_unlock_irqrestore(&r->producer_lock, flags);

	return ret;
}

static inline int ptr_ring_produce_bh(struct ptr_ring *r, void *ptr)
{
	int ret;

	spin_lock_bh(&r->producer_lock);
	ret = __ptr_ring_produce(r, ptr);
	spin_unlock_bh(&r->producer_lock);

	return ret;
}

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax(). Callers must take consumer_lock
 * if they dereference the pointer - see e.g. PTR_RING_PEEK_CALL.
 * If ring is never resized, and if the pointer is merely
 * tested, there's no need to take the lock - see e.g.  __ptr_ring_empty.
 */
static inline void *__ptr_ring_peek(struct ptr_ring *r)
{
	if (likely(r->size))
		return r->queue[r->consumer];
	return NULL;
}

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax(). Callers must take consumer_lock
 * if the ring is ever resized - see e.g. ptr_ring_empty.
 */
static inline bool __ptr_ring_empty(struct ptr_ring *r)
{
	return !__ptr_ring_peek(r);
}

static inline bool ptr_ring_empty(struct ptr_ring *r)
{
	bool ret;

	spin_lock(&r->consumer_lock);
	ret = __ptr_ring_empty(r);
	spin_unlock(&r->consumer_lock);

	return ret;
}

static inline bool ptr_ring_empty_irq(struct ptr_ring *r)
{
	bool ret;

	spin_lock_irq(&r->consumer_lock);
	ret = __ptr_ring_empty(r);
	spin_unlock_irq(&r->consumer_lock);

	return ret;
}

static inline bool ptr_ring_empty_any(struct ptr_ring *r)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&r->consumer_lock, flags);
	ret = __ptr_ring_empty(r);
	spin_unlock_irqrestore(&r->consumer_lock, flags);

	return ret;
}

static inline bool ptr_ring_empty_bh(struct ptr_ring *r)
{
	bool ret;

	spin_lock_bh(&r->consumer_lock);
	ret = __ptr_ring_empty(r);
	spin_unlock_bh(&r->consumer_lock);

	return ret;
}

/* Must only be called after __ptr_ring_peek returned !NULL */
static inline void __ptr_ring_discard_one(struct ptr_ring *r)
{
	r->queue[r->consumer++] = NULL;
	if (unlikely(r->consumer >= r->size))
		r->consumer = 0;
}

static inline void *__ptr_ring_consume(struct ptr_ring *r)
{
	void *ptr;

	ptr = __ptr_ring_peek(r);
	if (ptr)
		__ptr_ring_discard_one(r);

	return ptr;
}

/*
 * Note: resize (below) nests producer lock within consumer lock, so if you
 * call this in interrupt or BH context, you must disable interrupts/BH when
 * producing.
 */
static inline void *ptr_ring_consume(struct ptr_ring *r)
{
	void *ptr;

	spin_lock(&r->consumer_lock);
	ptr = __ptr_ring_consume(r);
	spin_unlock(&r->consumer_lock);

	return ptr;
}

static inline void *ptr_ring_consume_irq(struct ptr_ring *r)
{
	void *ptr;

	spin_lock_irq(&r->consumer_lock);
	ptr = __ptr_ring_consume(r);
	spin_unlock_irq(&r->consumer_lock);

	return ptr;
}

static inline void *ptr_ring_consume_any(struct ptr_ring *r)
{
	unsigned long flags;
	void *ptr;

	spin_lock_irqsave(&r->consumer_lock, flags);
	ptr = __ptr_ring_consume(r);
	spin_unlock_irqrestore(&r->consumer_lock, flags);

	return ptr;
}

static inline void *ptr_ring_consume_bh(struct ptr_ring *r)
{
	void *ptr;

	spin_lock_bh(&r->consumer_lock);
	ptr = __ptr_ring_consume(r);
	spin_unlock_bh(&r->consumer_lock);

	return ptr;
}

/* Cast to structure type and call a function without discarding from FIFO.
 * Function must return a value.
 * Callers must take consumer_lock.
 */
#define __PTR_RING_PEEK_CALL(r, f) ((f)(__ptr_ring_peek(r)))

#define PTR_RING_PEEK_CALL(r, f) ({ \
	typeof((f)(NULL)) __PTR_RING_PEEK_CALL_v; \
	\
	spin_lock(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v = __PTR_RING_PEEK_CALL(r, f); \
	spin_unlock(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v; \
})

#define PTR_RING_PEEK_CALL_IRQ(r, f) ({ \
	typeof((f)(NULL)) __PTR_RING_PEEK_CALL_v; \
	\
	spin_lock_irq(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v = __PTR_RING_PEEK_CALL(r, f); \
	spin_unlock_irq(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v; \
})

#define PTR_RING_PEEK_CALL_BH(r, f) ({ \
	typeof((f)(NULL)) __PTR_RING_PEEK_CALL_v; \
	\
	spin_lock_bh(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v = __PTR_RING_PEEK_CALL(r, f); \
	spin_unlock_bh(&(r)->consumer_lock); \
	__PTR_RING_PEEK_CALL_v; \
})

#define PTR_RING_PEEK_CALL_ANY(r, f) ({ \
	typeof((f)(NULL)) __PTR_RING_PEEK_CALL_v; \
	unsigned long __PTR_RING_PEEK_CALL_f;\
	\
	spin_lock_irqsave(&(r)->consumer_lock, __PTR_RING_PEEK_CALL_f); \
	__PTR_RING_PEEK_CALL_v = __PTR_RING_PEEK_CALL(r, f); \
	spin_unlock_irqrestore(&(r)->consumer_lock, __PTR_RING_PEEK_CALL_f); \
	__PTR_RING_PEEK_CALL_v; \
})

static inline void **__ptr_ring_init_queue_alloc(int size, gfp_t gfp)
{
	return kzalloc(ALIGN(size * sizeof(void *), SMP_CACHE_BYTES), gfp);
}

static inline int ptr_ring_init(struct ptr_ring *r, int size, gfp_t gfp)
{
	r->queue = __ptr_ring_init_queue_alloc(size, gfp);
	if (!r->queue)
		return -ENOMEM;

	r->size = size;
	r->producer = r->consumer = 0;
	spin_lock_init(&r->producer_lock);
	spin_lock_init(&r->consumer_lock);

	return 0;
}

static inline void **__ptr_ring_swap_queue(struct ptr_ring *r, void **queue,
					   int size, gfp_t gfp,
					   void (*destroy)(void *))
{
	int producer = 0;
	void **old;
	void *ptr;

	while ((ptr = __ptr_ring_consume(r)))
		if (producer < size)
			queue[producer++] = ptr;
		else if (destroy)
			destroy(ptr);

	r->size = size;
	r->producer = producer;
	r->consumer = 0;
	old = r->queue;
	r->queue = queue;

	return old;
}

/*
 * Note: producer lock is nested within consumer lock, so if you
 * resize you must make sure all uses nest correctly.
 * In particular if you consume ring in interrupt or BH context, you must
 * disable interrupts/BH when doing so.
 */
static inline int ptr_ring_resize(struct ptr_ring *r, int size, gfp_t gfp,
				  void (*destroy)(void *))
{
	unsigned long flags;
	void **queue = __ptr_ring_init_queue_alloc(size, gfp);
	void **old;

	if (!queue)
		return -ENOMEM;

	spin_lock_irqsave(&(r)->consumer_lock, flags);
	spin_lock(&(r)->producer_lock);

	old = __ptr_ring_swap_queue(r, queue, size, gfp, destroy);

	spin_unlock(&(r)->producer_lock);
	spin_unlock_irqrestore(&(r)->consumer_lock, flags);

	kfree(old);

	return 0;
}

/*
 * Note: producer lock is nested within consumer lock, so if you
 * resize you must make sure all uses nest correctly.
 * In particular if you consume ring in interrupt or BH context, you must
 * disable interrupts/BH when doing so.
 */
static inline int ptr_ring_resize_multiple(struct ptr_ring **rings, int nrings,
					   int size,
					   gfp_t gfp, void (*destroy)(void *))
{
	unsigned long flags;
	void ***queues;
	int i;

	queues = kmalloc(nrings * sizeof *queues, gfp);
	if (!queues)
		goto noqueues;

	for (i = 0; i < nrings; ++i) {
		queues[i] = __ptr_ring_init_queue_alloc(size, gfp);
		if (!queues[i])
			goto nomem;
	}

	for (i = 0; i < nrings; ++i) {
		spin_lock_irqsave(&(rings[i])->consumer_lock, flags);
		spin_lock(&(rings[i])->producer_lock);
		queues[i] = __ptr_ring_swap_queue(rings[i], queues[i],
						  size, gfp, destroy);
		spin_unlock(&(rings[i])->producer_lock);
		spin_unlock_irqrestore(&(rings[i])->consumer_lock, flags);
	}

	for (i = 0; i < nrings; ++i)
		kfree(queues[i]);

	kfree(queues);

	return 0;

nomem:
	while (--i >= 0)
		kfree(queues[i]);

	kfree(queues);

noqueues:
	return -ENOMEM;
}

static inline void ptr_ring_cleanup(struct ptr_ring *r, void (*destroy)(void *))
{
	void *ptr;

	if (destroy)
		while ((ptr = ptr_ring_consume(r)))
			destroy(ptr);
	kfree(r->queue);
}

#endif /* _LINUX_PTR_RING_H  */
