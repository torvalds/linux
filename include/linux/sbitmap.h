/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fast and scalable bitmaps.
 *
 * Copyright (C) 2016 Facebook
 * Copyright (C) 2013-2014 Jens Axboe
 */

#ifndef __LINUX_SCALE_BITMAP_H
#define __LINUX_SCALE_BITMAP_H

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/minmax.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/wait.h>

struct seq_file;

/**
 * struct sbitmap_word - Word in a &struct sbitmap.
 */
struct sbitmap_word {
	/**
	 * @depth: Number of bits being used in @word/@cleared
	 */
	unsigned long depth;

	/**
	 * @word: word holding free bits
	 */
	unsigned long word ____cacheline_aligned_in_smp;

	/**
	 * @cleared: word holding cleared bits
	 */
	unsigned long cleared ____cacheline_aligned_in_smp;
} ____cacheline_aligned_in_smp;

/**
 * struct sbitmap - Scalable bitmap.
 *
 * A &struct sbitmap is spread over multiple cachelines to avoid ping-pong. This
 * trades off higher memory usage for better scalability.
 */
struct sbitmap {
	/**
	 * @depth: Number of bits used in the whole bitmap.
	 */
	unsigned int depth;

	/**
	 * @shift: log2(number of bits used per word)
	 */
	unsigned int shift;

	/**
	 * @map_nr: Number of words (cachelines) being used for the bitmap.
	 */
	unsigned int map_nr;

	/**
	 * @round_robin: Allocate bits in strict round-robin order.
	 */
	bool round_robin;

	/**
	 * @map: Allocated bitmap.
	 */
	struct sbitmap_word *map;

	/*
	 * @alloc_hint: Cache of last successfully allocated or freed bit.
	 *
	 * This is per-cpu, which allows multiple users to stick to different
	 * cachelines until the map is exhausted.
	 */
	unsigned int __percpu *alloc_hint;
};

#define SBQ_WAIT_QUEUES 8
#define SBQ_WAKE_BATCH 8

/**
 * struct sbq_wait_state - Wait queue in a &struct sbitmap_queue.
 */
struct sbq_wait_state {
	/**
	 * @wait_cnt: Number of frees remaining before we wake up.
	 */
	atomic_t wait_cnt;

	/**
	 * @wait: Wait queue.
	 */
	wait_queue_head_t wait;
} ____cacheline_aligned_in_smp;

/**
 * struct sbitmap_queue - Scalable bitmap with the added ability to wait on free
 * bits.
 *
 * A &struct sbitmap_queue uses multiple wait queues and rolling wakeups to
 * avoid contention on the wait queue spinlock. This ensures that we don't hit a
 * scalability wall when we run out of free bits and have to start putting tasks
 * to sleep.
 */
struct sbitmap_queue {
	/**
	 * @sb: Scalable bitmap.
	 */
	struct sbitmap sb;

	/**
	 * @wake_batch: Number of bits which must be freed before we wake up any
	 * waiters.
	 */
	unsigned int wake_batch;

	/**
	 * @wake_index: Next wait queue in @ws to wake up.
	 */
	atomic_t wake_index;

	/**
	 * @ws: Wait queues.
	 */
	struct sbq_wait_state *ws;

	/*
	 * @ws_active: count of currently active ws waitqueues
	 */
	atomic_t ws_active;

	/**
	 * @min_shallow_depth: The minimum shallow depth which may be passed to
	 * sbitmap_queue_get_shallow() or __sbitmap_queue_get_shallow().
	 */
	unsigned int min_shallow_depth;
};

/**
 * sbitmap_init_node() - Initialize a &struct sbitmap on a specific memory node.
 * @sb: Bitmap to initialize.
 * @depth: Number of bits to allocate.
 * @shift: Use 2^@shift bits per word in the bitmap; if a negative number if
 *         given, a good default is chosen.
 * @flags: Allocation flags.
 * @node: Memory node to allocate on.
 * @round_robin: If true, be stricter about allocation order; always allocate
 *               starting from the last allocated bit. This is less efficient
 *               than the default behavior (false).
 * @alloc_hint: If true, apply percpu hint for where to start searching for
 *              a free bit.
 *
 * Return: Zero on success or negative errno on failure.
 */
int sbitmap_init_node(struct sbitmap *sb, unsigned int depth, int shift,
		      gfp_t flags, int node, bool round_robin, bool alloc_hint);

/**
 * sbitmap_free() - Free memory used by a &struct sbitmap.
 * @sb: Bitmap to free.
 */
static inline void sbitmap_free(struct sbitmap *sb)
{
	free_percpu(sb->alloc_hint);
	kfree(sb->map);
	sb->map = NULL;
}

/**
 * sbitmap_resize() - Resize a &struct sbitmap.
 * @sb: Bitmap to resize.
 * @depth: New number of bits to resize to.
 *
 * Doesn't reallocate anything. It's up to the caller to ensure that the new
 * depth doesn't exceed the depth that the sb was initialized with.
 */
void sbitmap_resize(struct sbitmap *sb, unsigned int depth);

/**
 * sbitmap_get() - Try to allocate a free bit from a &struct sbitmap.
 * @sb: Bitmap to allocate from.
 *
 * This operation provides acquire barrier semantics if it succeeds.
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
int sbitmap_get(struct sbitmap *sb);

/**
 * sbitmap_get_shallow() - Try to allocate a free bit from a &struct sbitmap,
 * limiting the depth used from each word.
 * @sb: Bitmap to allocate from.
 * @shallow_depth: The maximum number of bits to allocate from a single word.
 *
 * This rather specific operation allows for having multiple users with
 * different allocation limits. E.g., there can be a high-priority class that
 * uses sbitmap_get() and a low-priority class that uses sbitmap_get_shallow()
 * with a @shallow_depth of (1 << (@sb->shift - 1)). Then, the low-priority
 * class can only allocate half of the total bits in the bitmap, preventing it
 * from starving out the high-priority class.
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
int sbitmap_get_shallow(struct sbitmap *sb, unsigned long shallow_depth);

/**
 * sbitmap_any_bit_set() - Check for a set bit in a &struct sbitmap.
 * @sb: Bitmap to check.
 *
 * Return: true if any bit in the bitmap is set, false otherwise.
 */
bool sbitmap_any_bit_set(const struct sbitmap *sb);

#define SB_NR_TO_INDEX(sb, bitnr) ((bitnr) >> (sb)->shift)
#define SB_NR_TO_BIT(sb, bitnr) ((bitnr) & ((1U << (sb)->shift) - 1U))

typedef bool (*sb_for_each_fn)(struct sbitmap *, unsigned int, void *);

/**
 * __sbitmap_for_each_set() - Iterate over each set bit in a &struct sbitmap.
 * @start: Where to start the iteration.
 * @sb: Bitmap to iterate over.
 * @fn: Callback. Should return true to continue or false to break early.
 * @data: Pointer to pass to callback.
 *
 * This is inline even though it's non-trivial so that the function calls to the
 * callback will hopefully get optimized away.
 */
static inline void __sbitmap_for_each_set(struct sbitmap *sb,
					  unsigned int start,
					  sb_for_each_fn fn, void *data)
{
	unsigned int index;
	unsigned int nr;
	unsigned int scanned = 0;

	if (start >= sb->depth)
		start = 0;
	index = SB_NR_TO_INDEX(sb, start);
	nr = SB_NR_TO_BIT(sb, start);

	while (scanned < sb->depth) {
		unsigned long word;
		unsigned int depth = min_t(unsigned int,
					   sb->map[index].depth - nr,
					   sb->depth - scanned);

		scanned += depth;
		word = sb->map[index].word & ~sb->map[index].cleared;
		if (!word)
			goto next;

		/*
		 * On the first iteration of the outer loop, we need to add the
		 * bit offset back to the size of the word for find_next_bit().
		 * On all other iterations, nr is zero, so this is a noop.
		 */
		depth += nr;
		while (1) {
			nr = find_next_bit(&word, depth, nr);
			if (nr >= depth)
				break;
			if (!fn(sb, (index << sb->shift) + nr, data))
				return;

			nr++;
		}
next:
		nr = 0;
		if (++index >= sb->map_nr)
			index = 0;
	}
}

/**
 * sbitmap_for_each_set() - Iterate over each set bit in a &struct sbitmap.
 * @sb: Bitmap to iterate over.
 * @fn: Callback. Should return true to continue or false to break early.
 * @data: Pointer to pass to callback.
 */
static inline void sbitmap_for_each_set(struct sbitmap *sb, sb_for_each_fn fn,
					void *data)
{
	__sbitmap_for_each_set(sb, 0, fn, data);
}

static inline unsigned long *__sbitmap_word(struct sbitmap *sb,
					    unsigned int bitnr)
{
	return &sb->map[SB_NR_TO_INDEX(sb, bitnr)].word;
}

/* Helpers equivalent to the operations in asm/bitops.h and linux/bitmap.h */

static inline void sbitmap_set_bit(struct sbitmap *sb, unsigned int bitnr)
{
	set_bit(SB_NR_TO_BIT(sb, bitnr), __sbitmap_word(sb, bitnr));
}

static inline void sbitmap_clear_bit(struct sbitmap *sb, unsigned int bitnr)
{
	clear_bit(SB_NR_TO_BIT(sb, bitnr), __sbitmap_word(sb, bitnr));
}

/*
 * This one is special, since it doesn't actually clear the bit, rather it
 * sets the corresponding bit in the ->cleared mask instead. Paired with
 * the caller doing sbitmap_deferred_clear() if a given index is full, which
 * will clear the previously freed entries in the corresponding ->word.
 */
static inline void sbitmap_deferred_clear_bit(struct sbitmap *sb, unsigned int bitnr)
{
	unsigned long *addr = &sb->map[SB_NR_TO_INDEX(sb, bitnr)].cleared;

	set_bit(SB_NR_TO_BIT(sb, bitnr), addr);
}

/*
 * Pair of sbitmap_get, and this one applies both cleared bit and
 * allocation hint.
 */
static inline void sbitmap_put(struct sbitmap *sb, unsigned int bitnr)
{
	sbitmap_deferred_clear_bit(sb, bitnr);

	if (likely(sb->alloc_hint && !sb->round_robin && bitnr < sb->depth))
		*raw_cpu_ptr(sb->alloc_hint) = bitnr;
}

static inline int sbitmap_test_bit(struct sbitmap *sb, unsigned int bitnr)
{
	return test_bit(SB_NR_TO_BIT(sb, bitnr), __sbitmap_word(sb, bitnr));
}

static inline int sbitmap_calculate_shift(unsigned int depth)
{
	int	shift = ilog2(BITS_PER_LONG);

	/*
	 * If the bitmap is small, shrink the number of bits per word so
	 * we spread over a few cachelines, at least. If less than 4
	 * bits, just forget about it, it's not going to work optimally
	 * anyway.
	 */
	if (depth >= 4) {
		while ((4U << shift) > depth)
			shift--;
	}

	return shift;
}

/**
 * sbitmap_show() - Dump &struct sbitmap information to a &struct seq_file.
 * @sb: Bitmap to show.
 * @m: struct seq_file to write to.
 *
 * This is intended for debugging. The format may change at any time.
 */
void sbitmap_show(struct sbitmap *sb, struct seq_file *m);


/**
 * sbitmap_weight() - Return how many set and not cleared bits in a &struct
 * sbitmap.
 * @sb: Bitmap to check.
 *
 * Return: How many set and not cleared bits set
 */
unsigned int sbitmap_weight(const struct sbitmap *sb);

/**
 * sbitmap_bitmap_show() - Write a hex dump of a &struct sbitmap to a &struct
 * seq_file.
 * @sb: Bitmap to show.
 * @m: struct seq_file to write to.
 *
 * This is intended for debugging. The output isn't guaranteed to be internally
 * consistent.
 */
void sbitmap_bitmap_show(struct sbitmap *sb, struct seq_file *m);

/**
 * sbitmap_queue_init_node() - Initialize a &struct sbitmap_queue on a specific
 * memory node.
 * @sbq: Bitmap queue to initialize.
 * @depth: See sbitmap_init_node().
 * @shift: See sbitmap_init_node().
 * @round_robin: See sbitmap_get().
 * @flags: Allocation flags.
 * @node: Memory node to allocate on.
 *
 * Return: Zero on success or negative errno on failure.
 */
int sbitmap_queue_init_node(struct sbitmap_queue *sbq, unsigned int depth,
			    int shift, bool round_robin, gfp_t flags, int node);

/**
 * sbitmap_queue_free() - Free memory used by a &struct sbitmap_queue.
 *
 * @sbq: Bitmap queue to free.
 */
static inline void sbitmap_queue_free(struct sbitmap_queue *sbq)
{
	kfree(sbq->ws);
	sbitmap_free(&sbq->sb);
}

/**
 * sbitmap_queue_recalculate_wake_batch() - Recalculate wake batch
 * @sbq: Bitmap queue to recalculate wake batch.
 * @users: Number of shares.
 *
 * Like sbitmap_queue_update_wake_batch(), this will calculate wake batch
 * by depth. This interface is for HCTX shared tags or queue shared tags.
 */
void sbitmap_queue_recalculate_wake_batch(struct sbitmap_queue *sbq,
					    unsigned int users);

/**
 * sbitmap_queue_resize() - Resize a &struct sbitmap_queue.
 * @sbq: Bitmap queue to resize.
 * @depth: New number of bits to resize to.
 *
 * Like sbitmap_resize(), this doesn't reallocate anything. It has to do
 * some extra work on the &struct sbitmap_queue, so it's not safe to just
 * resize the underlying &struct sbitmap.
 */
void sbitmap_queue_resize(struct sbitmap_queue *sbq, unsigned int depth);

/**
 * __sbitmap_queue_get() - Try to allocate a free bit from a &struct
 * sbitmap_queue with preemption already disabled.
 * @sbq: Bitmap queue to allocate from.
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
int __sbitmap_queue_get(struct sbitmap_queue *sbq);

/**
 * __sbitmap_queue_get_batch() - Try to allocate a batch of free bits
 * @sbq: Bitmap queue to allocate from.
 * @nr_tags: number of tags requested
 * @offset: offset to add to returned bits
 *
 * Return: Mask of allocated tags, 0 if none are found. Each tag allocated is
 * a bit in the mask returned, and the caller must add @offset to the value to
 * get the absolute tag value.
 */
unsigned long __sbitmap_queue_get_batch(struct sbitmap_queue *sbq, int nr_tags,
					unsigned int *offset);

/**
 * __sbitmap_queue_get_shallow() - Try to allocate a free bit from a &struct
 * sbitmap_queue, limiting the depth used from each word, with preemption
 * already disabled.
 * @sbq: Bitmap queue to allocate from.
 * @shallow_depth: The maximum number of bits to allocate from a single word.
 * See sbitmap_get_shallow().
 *
 * If you call this, make sure to call sbitmap_queue_min_shallow_depth() after
 * initializing @sbq.
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
int __sbitmap_queue_get_shallow(struct sbitmap_queue *sbq,
				unsigned int shallow_depth);

/**
 * sbitmap_queue_get() - Try to allocate a free bit from a &struct
 * sbitmap_queue.
 * @sbq: Bitmap queue to allocate from.
 * @cpu: Output parameter; will contain the CPU we ran on (e.g., to be passed to
 *       sbitmap_queue_clear()).
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
static inline int sbitmap_queue_get(struct sbitmap_queue *sbq,
				    unsigned int *cpu)
{
	int nr;

	*cpu = get_cpu();
	nr = __sbitmap_queue_get(sbq);
	put_cpu();
	return nr;
}

/**
 * sbitmap_queue_get_shallow() - Try to allocate a free bit from a &struct
 * sbitmap_queue, limiting the depth used from each word.
 * @sbq: Bitmap queue to allocate from.
 * @cpu: Output parameter; will contain the CPU we ran on (e.g., to be passed to
 *       sbitmap_queue_clear()).
 * @shallow_depth: The maximum number of bits to allocate from a single word.
 * See sbitmap_get_shallow().
 *
 * If you call this, make sure to call sbitmap_queue_min_shallow_depth() after
 * initializing @sbq.
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
static inline int sbitmap_queue_get_shallow(struct sbitmap_queue *sbq,
					    unsigned int *cpu,
					    unsigned int shallow_depth)
{
	int nr;

	*cpu = get_cpu();
	nr = __sbitmap_queue_get_shallow(sbq, shallow_depth);
	put_cpu();
	return nr;
}

/**
 * sbitmap_queue_min_shallow_depth() - Inform a &struct sbitmap_queue of the
 * minimum shallow depth that will be used.
 * @sbq: Bitmap queue in question.
 * @min_shallow_depth: The minimum shallow depth that will be passed to
 * sbitmap_queue_get_shallow() or __sbitmap_queue_get_shallow().
 *
 * sbitmap_queue_clear() batches wakeups as an optimization. The batch size
 * depends on the depth of the bitmap. Since the shallow allocation functions
 * effectively operate with a different depth, the shallow depth must be taken
 * into account when calculating the batch size. This function must be called
 * with the minimum shallow depth that will be used. Failure to do so can result
 * in missed wakeups.
 */
void sbitmap_queue_min_shallow_depth(struct sbitmap_queue *sbq,
				     unsigned int min_shallow_depth);

/**
 * sbitmap_queue_clear() - Free an allocated bit and wake up waiters on a
 * &struct sbitmap_queue.
 * @sbq: Bitmap to free from.
 * @nr: Bit number to free.
 * @cpu: CPU the bit was allocated on.
 */
void sbitmap_queue_clear(struct sbitmap_queue *sbq, unsigned int nr,
			 unsigned int cpu);

/**
 * sbitmap_queue_clear_batch() - Free a batch of allocated bits
 * &struct sbitmap_queue.
 * @sbq: Bitmap to free from.
 * @offset: offset for each tag in array
 * @tags: array of tags
 * @nr_tags: number of tags in array
 */
void sbitmap_queue_clear_batch(struct sbitmap_queue *sbq, int offset,
				int *tags, int nr_tags);

static inline int sbq_index_inc(int index)
{
	return (index + 1) & (SBQ_WAIT_QUEUES - 1);
}

static inline void sbq_index_atomic_inc(atomic_t *index)
{
	int old = atomic_read(index);
	int new = sbq_index_inc(old);
	atomic_cmpxchg(index, old, new);
}

/**
 * sbq_wait_ptr() - Get the next wait queue to use for a &struct
 * sbitmap_queue.
 * @sbq: Bitmap queue to wait on.
 * @wait_index: A counter per "user" of @sbq.
 */
static inline struct sbq_wait_state *sbq_wait_ptr(struct sbitmap_queue *sbq,
						  atomic_t *wait_index)
{
	struct sbq_wait_state *ws;

	ws = &sbq->ws[atomic_read(wait_index)];
	sbq_index_atomic_inc(wait_index);
	return ws;
}

/**
 * sbitmap_queue_wake_all() - Wake up everything waiting on a &struct
 * sbitmap_queue.
 * @sbq: Bitmap queue to wake up.
 */
void sbitmap_queue_wake_all(struct sbitmap_queue *sbq);

/**
 * sbitmap_queue_wake_up() - Wake up some of waiters in one waitqueue
 * on a &struct sbitmap_queue.
 * @sbq: Bitmap queue to wake up.
 */
void sbitmap_queue_wake_up(struct sbitmap_queue *sbq);

/**
 * sbitmap_queue_show() - Dump &struct sbitmap_queue information to a &struct
 * seq_file.
 * @sbq: Bitmap queue to show.
 * @m: struct seq_file to write to.
 *
 * This is intended for debugging. The format may change at any time.
 */
void sbitmap_queue_show(struct sbitmap_queue *sbq, struct seq_file *m);

struct sbq_wait {
	struct sbitmap_queue *sbq;	/* if set, sbq_wait is accounted */
	struct wait_queue_entry wait;
};

#define DEFINE_SBQ_WAIT(name)							\
	struct sbq_wait name = {						\
		.sbq = NULL,							\
		.wait = {							\
			.private	= current,				\
			.func		= autoremove_wake_function,		\
			.entry		= LIST_HEAD_INIT((name).wait.entry),	\
		}								\
	}

/*
 * Wrapper around prepare_to_wait_exclusive(), which maintains some extra
 * internal state.
 */
void sbitmap_prepare_to_wait(struct sbitmap_queue *sbq,
				struct sbq_wait_state *ws,
				struct sbq_wait *sbq_wait, int state);

/*
 * Must be paired with sbitmap_prepare_to_wait().
 */
void sbitmap_finish_wait(struct sbitmap_queue *sbq, struct sbq_wait_state *ws,
				struct sbq_wait *sbq_wait);

/*
 * Wrapper around add_wait_queue(), which maintains some extra internal state
 */
void sbitmap_add_wait_queue(struct sbitmap_queue *sbq,
			    struct sbq_wait_state *ws,
			    struct sbq_wait *sbq_wait);

/*
 * Must be paired with sbitmap_add_wait_queue()
 */
void sbitmap_del_wait_queue(struct sbq_wait *sbq_wait);

#endif /* __LINUX_SCALE_BITMAP_H */
