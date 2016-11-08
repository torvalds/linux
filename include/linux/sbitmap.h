/*
 * Fast and scalable bitmaps.
 *
 * Copyright (C) 2016 Facebook
 * Copyright (C) 2013-2014 Jens Axboe
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LINUX_SCALE_BITMAP_H
#define __LINUX_SCALE_BITMAP_H

#include <linux/kernel.h>
#include <linux/slab.h>

/**
 * struct sbitmap_word - Word in a &struct sbitmap.
 */
struct sbitmap_word {
	/**
	 * @word: The bitmap word itself.
	 */
	unsigned long word;

	/**
	 * @depth: Number of bits being used in @word.
	 */
	unsigned long depth;
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
	 * @map: Allocated bitmap.
	 */
	struct sbitmap_word *map;
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

	/*
	 * @alloc_hint: Cache of last successfully allocated or freed bit.
	 *
	 * This is per-cpu, which allows multiple users to stick to different
	 * cachelines until the map is exhausted.
	 */
	unsigned int __percpu *alloc_hint;

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

	/**
	 * @round_robin: Allocate bits in strict round-robin order.
	 */
	bool round_robin;
};

/**
 * sbitmap_init_node() - Initialize a &struct sbitmap on a specific memory node.
 * @sb: Bitmap to initialize.
 * @depth: Number of bits to allocate.
 * @shift: Use 2^@shift bits per word in the bitmap; if a negative number if
 *         given, a good default is chosen.
 * @flags: Allocation flags.
 * @node: Memory node to allocate on.
 *
 * Return: Zero on success or negative errno on failure.
 */
int sbitmap_init_node(struct sbitmap *sb, unsigned int depth, int shift,
		      gfp_t flags, int node);

/**
 * sbitmap_free() - Free memory used by a &struct sbitmap.
 * @sb: Bitmap to free.
 */
static inline void sbitmap_free(struct sbitmap *sb)
{
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
 * @alloc_hint: Hint for where to start searching for a free bit.
 * @round_robin: If true, be stricter about allocation order; always allocate
 *               starting from the last allocated bit. This is less efficient
 *               than the default behavior (false).
 *
 * Return: Non-negative allocated bit number if successful, -1 otherwise.
 */
int sbitmap_get(struct sbitmap *sb, unsigned int alloc_hint, bool round_robin);

/**
 * sbitmap_any_bit_set() - Check for a set bit in a &struct sbitmap.
 * @sb: Bitmap to check.
 *
 * Return: true if any bit in the bitmap is set, false otherwise.
 */
bool sbitmap_any_bit_set(const struct sbitmap *sb);

/**
 * sbitmap_any_bit_clear() - Check for an unset bit in a &struct
 * sbitmap.
 * @sb: Bitmap to check.
 *
 * Return: true if any bit in the bitmap is clear, false otherwise.
 */
bool sbitmap_any_bit_clear(const struct sbitmap *sb);

typedef bool (*sb_for_each_fn)(struct sbitmap *, unsigned int, void *);

/**
 * sbitmap_for_each_set() - Iterate over each set bit in a &struct sbitmap.
 * @sb: Bitmap to iterate over.
 * @fn: Callback. Should return true to continue or false to break early.
 * @data: Pointer to pass to callback.
 *
 * This is inline even though it's non-trivial so that the function calls to the
 * callback will hopefully get optimized away.
 */
static inline void sbitmap_for_each_set(struct sbitmap *sb, sb_for_each_fn fn,
					void *data)
{
	unsigned int i;

	for (i = 0; i < sb->map_nr; i++) {
		struct sbitmap_word *word = &sb->map[i];
		unsigned int off, nr;

		if (!word->word)
			continue;

		nr = 0;
		off = i << sb->shift;
		while (1) {
			nr = find_next_bit(&word->word, word->depth, nr);
			if (nr >= word->depth)
				break;

			if (!fn(sb, off + nr, data))
				return;

			nr++;
		}
	}
}

#define SB_NR_TO_INDEX(sb, bitnr) ((bitnr) >> (sb)->shift)
#define SB_NR_TO_BIT(sb, bitnr) ((bitnr) & ((1U << (sb)->shift) - 1U))

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

static inline int sbitmap_test_bit(struct sbitmap *sb, unsigned int bitnr)
{
	return test_bit(SB_NR_TO_BIT(sb, bitnr), __sbitmap_word(sb, bitnr));
}

unsigned int sbitmap_weight(const struct sbitmap *sb);

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
	free_percpu(sbq->alloc_hint);
	sbitmap_free(&sbq->sb);
}

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
 * sbitmap_queue_clear() - Free an allocated bit and wake up waiters on a
 * &struct sbitmap_queue.
 * @sbq: Bitmap to free from.
 * @nr: Bit number to free.
 * @cpu: CPU the bit was allocated on.
 */
void sbitmap_queue_clear(struct sbitmap_queue *sbq, unsigned int nr,
			 unsigned int cpu);

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

#endif /* __LINUX_SCALE_BITMAP_H */
