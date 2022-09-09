/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Definitions for the 'struct ptr_ring' datastructure.
 *
 *	Author:
 *		Michael S. Tsirkin <mst@redhat.com>
 *
 *	Copyright (C) 2016 Red Hat, Inc.
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
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/errno.h>
#endif

struct ptr_ring {
	int producer ____cacheline_aligned_in_smp;
	spinlock_t producer_lock;
	int consumer_head ____cacheline_aligned_in_smp; /* next valid entry */
	int consumer_tail; /* next entry to invalidate */
	spinlock_t consumer_lock;
	/* Shared consumer/producer data */
	/* Read-only by both the producer and the consumer */
	int size ____cacheline_aligned_in_smp; /* max entries in queue */
	int batch; /* number of entries to consume in a batch */
	void **queue;
};

/* Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 *
 * NB: this is unlike __ptr_ring_empty in that callers must hold producer_lock:
 * see e.g. ptr_ring_full.
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
 * Callers are responsible for making sure pointer that is being queued
 * points to a valid data.
 */
static inline int __ptr_ring_produce(struct ptr_ring *r, void *ptr)
{
	if (unlikely(!r->size) || r->queue[r->producer])
		return -ENOSPC;

	/* Make sure the pointer we are storing points to a valid data. */
	/* Pairs with the dependency ordering in __ptr_ring_consume. */
	smp_wmb();

	WRITE_ONCE(r->queue[r->producer++], ptr);
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

static inline void *__ptr_ring_peek(struct ptr_ring *r)
{
	if (likely(r->size))
		return READ_ONCE(r->queue[r->consumer_head]);
	return NULL;
}

/*
 * Test ring empty status without taking any locks.
 *
 * NB: This is only safe to call if ring is never resized.
 *
 * However, if some other CPU consumes ring entries at the same time, the value
 * returned is not guaranteed to be correct.
 *
 * In this case - to avoid incorrectly detecting the ring
 * as empty - the CPU consuming the ring entries is responsible
 * for either consuming all ring entries until the ring is empty,
 * or synchronizing with some other CPU and causing it to
 * re-test __ptr_ring_empty and/or consume the ring enteries
 * after the synchronization point.
 *
 * Note: callers invoking this in a loop must use a compiler barrier,
 * for example cpu_relax().
 */
static inline bool __ptr_ring_empty(struct ptr_ring *r)
{
	if (likely(r->size))
		return !r->queue[READ_ONCE(r->consumer_head)];
	return true;
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
	/* Fundamentally, what we want to do is update consumer
	 * index and zero out the entry so producer can reuse it.
	 * Doing it naively at each consume would be as simple as:
	 *       consumer = r->consumer;
	 *       r->queue[consumer++] = NULL;
	 *       if (unlikely(consumer >= r->size))
	 *               consumer = 0;
	 *       r->consumer = consumer;
	 * but that is suboptimal when the ring is full as producer is writing
	 * out new entries in the same cache line.  Defer these updates until a
	 * batch of entries has been consumed.
	 */
	/* Note: we must keep consumer_head valid at all times for __ptr_ring_empty
	 * to work correctly.
	 */
	int consumer_head = r->consumer_head;
	int head = consumer_head++;

	/* Once we have processed enough entries invalidate them in
	 * the ring all at once so producer can reuse their space in the ring.
	 * We also do this when we reach end of the ring - not mandatory
	 * but helps keep the implementation simple.
	 */
	if (unlikely(consumer_head - r->consumer_tail >= r->batch ||
		     consumer_head >= r->size)) {
		/* Zero out entries in the reverse order: this way we touch the
		 * cache line that producer might currently be reading the last;
		 * producer won't make progress and touch other cache lines
		 * besides the first one until we write out all entries.
		 */
		while (likely(head >= r->consumer_tail))
			r->queue[head--] = NULL;
		r->consumer_tail = consumer_head;
	}
	if (unlikely(consumer_head >= r->size)) {
		consumer_head = 0;
		r->consumer_tail = 0;
	}
	/* matching READ_ONCE in __ptr_ring_empty for lockless tests */
	WRITE_ONCE(r->consumer_head, consumer_head);
}

static inline void *__ptr_ring_consume(struct ptr_ring *r)
{
	void *ptr;

	/* The READ_ONCE in __ptr_ring_peek guarantees that anyone
	 * accessing data through the pointer is up to date. Pairs
	 * with smp_wmb in __ptr_ring_produce.
	 */
	ptr = __ptr_ring_peek(r);
	if (ptr)
		__ptr_ring_discard_one(r);

	return ptr;
}

static inline int __ptr_ring_consume_batched(struct ptr_ring *r,
					     void **array, int n)
{
	void *ptr;
	int i;

	for (i = 0; i < n; i++) {
		ptr = __ptr_ring_consume(r);
		if (!ptr)
			break;
		array[i] = ptr;
	}

	return i;
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

static inline int ptr_ring_consume_batched(struct ptr_ring *r,
					   void **array, int n)
{
	int ret;

	spin_lock(&r->consumer_lock);
	ret = __ptr_ring_consume_batched(r, array, n);
	spin_unlock(&r->consumer_lock);

	return ret;
}

static inline int ptr_ring_consume_batched_irq(struct ptr_ring *r,
					       void **array, int n)
{
	int ret;

	spin_lock_irq(&r->consumer_lock);
	ret = __ptr_ring_consume_batched(r, array, n);
	spin_unlock_irq(&r->consumer_lock);

	return ret;
}

static inline int ptr_ring_consume_batched_any(struct ptr_ring *r,
					       void **array, int n)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&r->consumer_lock, flags);
	ret = __ptr_ring_consume_batched(r, array, n);
	spin_unlock_irqrestore(&r->consumer_lock, flags);

	return ret;
}

static inline int ptr_ring_consume_batched_bh(struct ptr_ring *r,
					      void **array, int n)
{
	int ret;

	spin_lock_bh(&r->consumer_lock);
	ret = __ptr_ring_consume_batched(r, array, n);
	spin_unlock_bh(&r->consumer_lock);

	return ret;
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

/* Not all gfp_t flags (besides GFP_KERNEL) are allowed. See
 * documentation for vmalloc for which of them are legal.
 */
static inline void **__ptr_ring_init_queue_alloc(unsigned int size, gfp_t gfp)
{
	if (size > KMALLOC_MAX_SIZE / sizeof(void *))
		return NULL;
	return kvmalloc_array(size, sizeof(void *), gfp | __GFP_ZERO);
}

static inline void __ptr_ring_set_size(struct ptr_ring *r, int size)
{
	r->size = size;
	r->batch = SMP_CACHE_BYTES * 2 / sizeof(*(r->queue));
	/* We need to set batch at least to 1 to make logic
	 * in __ptr_ring_discard_one work correctly.
	 * Batching too much (because ring is small) would cause a lot of
	 * burstiness. Needs tuning, for now disable batching.
	 */
	if (r->batch > r->size / 2 || !r->batch)
		r->batch = 1;
}

static inline int ptr_ring_init(struct ptr_ring *r, int size, gfp_t gfp)
{
	r->queue = __ptr_ring_init_queue_alloc(size, gfp);
	if (!r->queue)
		return -ENOMEM;

	__ptr_ring_set_size(r, size);
	r->producer = r->consumer_head = r->consumer_tail = 0;
	spin_lock_init(&r->producer_lock);
	spin_lock_init(&r->consumer_lock);

	return 0;
}

/*
 * Return entries into ring. Destroy entries that don't fit.
 *
 * Note: this is expected to be a rare slow path operation.
 *
 * Note: producer lock is nested within consumer lock, so if you
 * resize you must make sure all uses nest correctly.
 * In particular if you consume ring in interrupt or BH context, you must
 * disable interrupts/BH when doing so.
 */
static inline void ptr_ring_unconsume(struct ptr_ring *r, void **batch, int n,
				      void (*destroy)(void *))
{
	unsigned long flags;
	int head;

	spin_lock_irqsave(&r->consumer_lock, flags);
	spin_lock(&r->producer_lock);

	if (!r->size)
		goto done;

	/*
	 * Clean out buffered entries (for simplicity). This way following code
	 * can test entries for NULL and if not assume they are valid.
	 */
	head = r->consumer_head - 1;
	while (likely(head >= r->consumer_tail))
		r->queue[head--] = NULL;
	r->consumer_tail = r->consumer_head;

	/*
	 * Go over entries in batch, start moving head back and copy entries.
	 * Stop when we run into previously unconsumed entries.
	 */
	while (n) {
		head = r->consumer_head - 1;
		if (head < 0)
			head = r->size - 1;
		if (r->queue[head]) {
			/* This batch entry will have to be destroyed. */
			goto done;
		}
		r->queue[head] = batch[--n];
		r->consumer_tail = head;
		/* matching READ_ONCE in __ptr_ring_empty for lockless tests */
		WRITE_ONCE(r->consumer_head, head);
	}

done:
	/* Destroy all entries left in the batch. */
	while (n)
		destroy(batch[--n]);
	spin_unlock(&r->producer_lock);
	spin_unlock_irqrestore(&r->consumer_lock, flags);
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

	if (producer >= size)
		producer = 0;
	__ptr_ring_set_size(r, size);
	r->producer = producer;
	r->consumer_head = 0;
	r->consumer_tail = 0;
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

	kvfree(old);

	return 0;
}

/*
 * Note: producer lock is nested within consumer lock, so if you
 * resize you must make sure all uses nest correctly.
 * In particular if you consume ring in interrupt or BH context, you must
 * disable interrupts/BH when doing so.
 */
static inline int ptr_ring_resize_multiple(struct ptr_ring **rings,
					   unsigned int nrings,
					   int size,
					   gfp_t gfp, void (*destroy)(void *))
{
	unsigned long flags;
	void ***queues;
	int i;

	queues = kmalloc_array(nrings, sizeof(*queues), gfp);
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
		kvfree(queues[i]);

	kfree(queues);

	return 0;

nomem:
	while (--i >= 0)
		kvfree(queues[i]);

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
	kvfree(r->queue);
}

#endif /* _LINUX_PTR_RING_H  */
