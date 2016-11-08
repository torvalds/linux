/*
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

#include <linux/random.h>
#include <linux/sbitmap.h>

int sbitmap_init_node(struct sbitmap *sb, unsigned int depth, int shift,
		      gfp_t flags, int node)
{
	unsigned int bits_per_word;
	unsigned int i;

	if (shift < 0) {
		shift = ilog2(BITS_PER_LONG);
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
	}
	bits_per_word = 1U << shift;
	if (bits_per_word > BITS_PER_LONG)
		return -EINVAL;

	sb->shift = shift;
	sb->depth = depth;
	sb->map_nr = DIV_ROUND_UP(sb->depth, bits_per_word);

	if (depth == 0) {
		sb->map = NULL;
		return 0;
	}

	sb->map = kzalloc_node(sb->map_nr * sizeof(*sb->map), flags, node);
	if (!sb->map)
		return -ENOMEM;

	for (i = 0; i < sb->map_nr; i++) {
		sb->map[i].depth = min(depth, bits_per_word);
		depth -= sb->map[i].depth;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(sbitmap_init_node);

void sbitmap_resize(struct sbitmap *sb, unsigned int depth)
{
	unsigned int bits_per_word = 1U << sb->shift;
	unsigned int i;

	sb->depth = depth;
	sb->map_nr = DIV_ROUND_UP(sb->depth, bits_per_word);

	for (i = 0; i < sb->map_nr; i++) {
		sb->map[i].depth = min(depth, bits_per_word);
		depth -= sb->map[i].depth;
	}
}
EXPORT_SYMBOL_GPL(sbitmap_resize);

static int __sbitmap_get_word(struct sbitmap_word *word, unsigned int hint,
			      bool wrap)
{
	unsigned int orig_hint = hint;
	int nr;

	while (1) {
		nr = find_next_zero_bit(&word->word, word->depth, hint);
		if (unlikely(nr >= word->depth)) {
			/*
			 * We started with an offset, and we didn't reset the
			 * offset to 0 in a failure case, so start from 0 to
			 * exhaust the map.
			 */
			if (orig_hint && hint && wrap) {
				hint = orig_hint = 0;
				continue;
			}
			return -1;
		}

		if (!test_and_set_bit(nr, &word->word))
			break;

		hint = nr + 1;
		if (hint >= word->depth - 1)
			hint = 0;
	}

	return nr;
}

int sbitmap_get(struct sbitmap *sb, unsigned int alloc_hint, bool round_robin)
{
	unsigned int i, index;
	int nr = -1;

	index = SB_NR_TO_INDEX(sb, alloc_hint);

	for (i = 0; i < sb->map_nr; i++) {
		nr = __sbitmap_get_word(&sb->map[index],
					SB_NR_TO_BIT(sb, alloc_hint),
					!round_robin);
		if (nr != -1) {
			nr += index << sb->shift;
			break;
		}

		/* Jump to next index. */
		index++;
		alloc_hint = index << sb->shift;

		if (index >= sb->map_nr) {
			index = 0;
			alloc_hint = 0;
		}
	}

	return nr;
}
EXPORT_SYMBOL_GPL(sbitmap_get);

bool sbitmap_any_bit_set(const struct sbitmap *sb)
{
	unsigned int i;

	for (i = 0; i < sb->map_nr; i++) {
		if (sb->map[i].word)
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(sbitmap_any_bit_set);

bool sbitmap_any_bit_clear(const struct sbitmap *sb)
{
	unsigned int i;

	for (i = 0; i < sb->map_nr; i++) {
		const struct sbitmap_word *word = &sb->map[i];
		unsigned long ret;

		ret = find_first_zero_bit(&word->word, word->depth);
		if (ret < word->depth)
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(sbitmap_any_bit_clear);

unsigned int sbitmap_weight(const struct sbitmap *sb)
{
	unsigned int i, weight = 0;

	for (i = 0; i < sb->map_nr; i++) {
		const struct sbitmap_word *word = &sb->map[i];

		weight += bitmap_weight(&word->word, word->depth);
	}
	return weight;
}
EXPORT_SYMBOL_GPL(sbitmap_weight);

static unsigned int sbq_calc_wake_batch(unsigned int depth)
{
	unsigned int wake_batch;

	/*
	 * For each batch, we wake up one queue. We need to make sure that our
	 * batch size is small enough that the full depth of the bitmap is
	 * enough to wake up all of the queues.
	 */
	wake_batch = SBQ_WAKE_BATCH;
	if (wake_batch > depth / SBQ_WAIT_QUEUES)
		wake_batch = max(1U, depth / SBQ_WAIT_QUEUES);

	return wake_batch;
}

int sbitmap_queue_init_node(struct sbitmap_queue *sbq, unsigned int depth,
			    int shift, bool round_robin, gfp_t flags, int node)
{
	int ret;
	int i;

	ret = sbitmap_init_node(&sbq->sb, depth, shift, flags, node);
	if (ret)
		return ret;

	sbq->alloc_hint = alloc_percpu_gfp(unsigned int, flags);
	if (!sbq->alloc_hint) {
		sbitmap_free(&sbq->sb);
		return -ENOMEM;
	}

	if (depth && !round_robin) {
		for_each_possible_cpu(i)
			*per_cpu_ptr(sbq->alloc_hint, i) = prandom_u32() % depth;
	}

	sbq->wake_batch = sbq_calc_wake_batch(depth);
	atomic_set(&sbq->wake_index, 0);

	sbq->ws = kzalloc_node(SBQ_WAIT_QUEUES * sizeof(*sbq->ws), flags, node);
	if (!sbq->ws) {
		free_percpu(sbq->alloc_hint);
		sbitmap_free(&sbq->sb);
		return -ENOMEM;
	}

	for (i = 0; i < SBQ_WAIT_QUEUES; i++) {
		init_waitqueue_head(&sbq->ws[i].wait);
		atomic_set(&sbq->ws[i].wait_cnt, sbq->wake_batch);
	}

	sbq->round_robin = round_robin;
	return 0;
}
EXPORT_SYMBOL_GPL(sbitmap_queue_init_node);

void sbitmap_queue_resize(struct sbitmap_queue *sbq, unsigned int depth)
{
	sbq->wake_batch = sbq_calc_wake_batch(depth);
	sbitmap_resize(&sbq->sb, depth);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_resize);

int __sbitmap_queue_get(struct sbitmap_queue *sbq)
{
	unsigned int hint, depth;
	int nr;

	hint = this_cpu_read(*sbq->alloc_hint);
	depth = READ_ONCE(sbq->sb.depth);
	if (unlikely(hint >= depth)) {
		hint = depth ? prandom_u32() % depth : 0;
		this_cpu_write(*sbq->alloc_hint, hint);
	}
	nr = sbitmap_get(&sbq->sb, hint, sbq->round_robin);

	if (nr == -1) {
		/* If the map is full, a hint won't do us much good. */
		this_cpu_write(*sbq->alloc_hint, 0);
	} else if (nr == hint || unlikely(sbq->round_robin)) {
		/* Only update the hint if we used it. */
		hint = nr + 1;
		if (hint >= depth - 1)
			hint = 0;
		this_cpu_write(*sbq->alloc_hint, hint);
	}

	return nr;
}
EXPORT_SYMBOL_GPL(__sbitmap_queue_get);

static struct sbq_wait_state *sbq_wake_ptr(struct sbitmap_queue *sbq)
{
	int i, wake_index;

	wake_index = atomic_read(&sbq->wake_index);
	for (i = 0; i < SBQ_WAIT_QUEUES; i++) {
		struct sbq_wait_state *ws = &sbq->ws[wake_index];

		if (waitqueue_active(&ws->wait)) {
			int o = atomic_read(&sbq->wake_index);

			if (wake_index != o)
				atomic_cmpxchg(&sbq->wake_index, o, wake_index);
			return ws;
		}

		wake_index = sbq_index_inc(wake_index);
	}

	return NULL;
}

static void sbq_wake_up(struct sbitmap_queue *sbq)
{
	struct sbq_wait_state *ws;
	int wait_cnt;

	/* Ensure that the wait list checks occur after clear_bit(). */
	smp_mb();

	ws = sbq_wake_ptr(sbq);
	if (!ws)
		return;

	wait_cnt = atomic_dec_return(&ws->wait_cnt);
	if (unlikely(wait_cnt < 0))
		wait_cnt = atomic_inc_return(&ws->wait_cnt);
	if (wait_cnt == 0) {
		atomic_add(sbq->wake_batch, &ws->wait_cnt);
		sbq_index_atomic_inc(&sbq->wake_index);
		wake_up(&ws->wait);
	}
}

void sbitmap_queue_clear(struct sbitmap_queue *sbq, unsigned int nr,
			 unsigned int cpu)
{
	sbitmap_clear_bit(&sbq->sb, nr);
	sbq_wake_up(sbq);
	if (likely(!sbq->round_robin && nr < sbq->sb.depth))
		*per_cpu_ptr(sbq->alloc_hint, cpu) = nr;
}
EXPORT_SYMBOL_GPL(sbitmap_queue_clear);

void sbitmap_queue_wake_all(struct sbitmap_queue *sbq)
{
	int i, wake_index;

	/*
	 * Make sure all changes prior to this are visible from other CPUs.
	 */
	smp_mb();
	wake_index = atomic_read(&sbq->wake_index);
	for (i = 0; i < SBQ_WAIT_QUEUES; i++) {
		struct sbq_wait_state *ws = &sbq->ws[wake_index];

		if (waitqueue_active(&ws->wait))
			wake_up(&ws->wait);

		wake_index = sbq_index_inc(wake_index);
	}
}
EXPORT_SYMBOL_GPL(sbitmap_queue_wake_all);
