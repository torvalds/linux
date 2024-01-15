// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Facebook
 * Copyright (C) 2013-2014 Jens Axboe
 */

#include <linux/sched.h>
#include <linux/random.h>
#include <linux/sbitmap.h>
#include <linux/seq_file.h>

static int init_alloc_hint(struct sbitmap *sb, gfp_t flags)
{
	unsigned depth = sb->depth;

	sb->alloc_hint = alloc_percpu_gfp(unsigned int, flags);
	if (!sb->alloc_hint)
		return -ENOMEM;

	if (depth && !sb->round_robin) {
		int i;

		for_each_possible_cpu(i)
			*per_cpu_ptr(sb->alloc_hint, i) = get_random_u32_below(depth);
	}
	return 0;
}

static inline unsigned update_alloc_hint_before_get(struct sbitmap *sb,
						    unsigned int depth)
{
	unsigned hint;

	hint = this_cpu_read(*sb->alloc_hint);
	if (unlikely(hint >= depth)) {
		hint = depth ? get_random_u32_below(depth) : 0;
		this_cpu_write(*sb->alloc_hint, hint);
	}

	return hint;
}

static inline void update_alloc_hint_after_get(struct sbitmap *sb,
					       unsigned int depth,
					       unsigned int hint,
					       unsigned int nr)
{
	if (nr == -1) {
		/* If the map is full, a hint won't do us much good. */
		this_cpu_write(*sb->alloc_hint, 0);
	} else if (nr == hint || unlikely(sb->round_robin)) {
		/* Only update the hint if we used it. */
		hint = nr + 1;
		if (hint >= depth - 1)
			hint = 0;
		this_cpu_write(*sb->alloc_hint, hint);
	}
}

/*
 * See if we have deferred clears that we can batch move
 */
static inline bool sbitmap_deferred_clear(struct sbitmap_word *map)
{
	unsigned long mask;

	if (!READ_ONCE(map->cleared))
		return false;

	/*
	 * First get a stable cleared mask, setting the old mask to 0.
	 */
	mask = xchg(&map->cleared, 0);

	/*
	 * Now clear the masked bits in our free word
	 */
	atomic_long_andnot(mask, (atomic_long_t *)&map->word);
	BUILD_BUG_ON(sizeof(atomic_long_t) != sizeof(map->word));
	return true;
}

int sbitmap_init_node(struct sbitmap *sb, unsigned int depth, int shift,
		      gfp_t flags, int node, bool round_robin,
		      bool alloc_hint)
{
	unsigned int bits_per_word;

	if (shift < 0)
		shift = sbitmap_calculate_shift(depth);

	bits_per_word = 1U << shift;
	if (bits_per_word > BITS_PER_LONG)
		return -EINVAL;

	sb->shift = shift;
	sb->depth = depth;
	sb->map_nr = DIV_ROUND_UP(sb->depth, bits_per_word);
	sb->round_robin = round_robin;

	if (depth == 0) {
		sb->map = NULL;
		return 0;
	}

	if (alloc_hint) {
		if (init_alloc_hint(sb, flags))
			return -ENOMEM;
	} else {
		sb->alloc_hint = NULL;
	}

	sb->map = kvzalloc_node(sb->map_nr * sizeof(*sb->map), flags, node);
	if (!sb->map) {
		free_percpu(sb->alloc_hint);
		return -ENOMEM;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sbitmap_init_node);

void sbitmap_resize(struct sbitmap *sb, unsigned int depth)
{
	unsigned int bits_per_word = 1U << sb->shift;
	unsigned int i;

	for (i = 0; i < sb->map_nr; i++)
		sbitmap_deferred_clear(&sb->map[i]);

	sb->depth = depth;
	sb->map_nr = DIV_ROUND_UP(sb->depth, bits_per_word);
}
EXPORT_SYMBOL_GPL(sbitmap_resize);

static int __sbitmap_get_word(unsigned long *word, unsigned long depth,
			      unsigned int hint, bool wrap)
{
	int nr;

	/* don't wrap if starting from 0 */
	wrap = wrap && hint;

	while (1) {
		nr = find_next_zero_bit(word, depth, hint);
		if (unlikely(nr >= depth)) {
			/*
			 * We started with an offset, and we didn't reset the
			 * offset to 0 in a failure case, so start from 0 to
			 * exhaust the map.
			 */
			if (hint && wrap) {
				hint = 0;
				continue;
			}
			return -1;
		}

		if (!test_and_set_bit_lock(nr, word))
			break;

		hint = nr + 1;
		if (hint >= depth - 1)
			hint = 0;
	}

	return nr;
}

static int sbitmap_find_bit_in_word(struct sbitmap_word *map,
				    unsigned int depth,
				    unsigned int alloc_hint,
				    bool wrap)
{
	int nr;

	do {
		nr = __sbitmap_get_word(&map->word, depth,
					alloc_hint, wrap);
		if (nr != -1)
			break;
		if (!sbitmap_deferred_clear(map))
			break;
	} while (1);

	return nr;
}

static int sbitmap_find_bit(struct sbitmap *sb,
			    unsigned int depth,
			    unsigned int index,
			    unsigned int alloc_hint,
			    bool wrap)
{
	unsigned int i;
	int nr = -1;

	for (i = 0; i < sb->map_nr; i++) {
		nr = sbitmap_find_bit_in_word(&sb->map[index],
					      min_t(unsigned int,
						    __map_depth(sb, index),
						    depth),
					      alloc_hint, wrap);

		if (nr != -1) {
			nr += index << sb->shift;
			break;
		}

		/* Jump to next index. */
		alloc_hint = 0;
		if (++index >= sb->map_nr)
			index = 0;
	}

	return nr;
}

static int __sbitmap_get(struct sbitmap *sb, unsigned int alloc_hint)
{
	unsigned int index;

	index = SB_NR_TO_INDEX(sb, alloc_hint);

	/*
	 * Unless we're doing round robin tag allocation, just use the
	 * alloc_hint to find the right word index. No point in looping
	 * twice in find_next_zero_bit() for that case.
	 */
	if (sb->round_robin)
		alloc_hint = SB_NR_TO_BIT(sb, alloc_hint);
	else
		alloc_hint = 0;

	return sbitmap_find_bit(sb, UINT_MAX, index, alloc_hint,
				!sb->round_robin);
}

int sbitmap_get(struct sbitmap *sb)
{
	int nr;
	unsigned int hint, depth;

	if (WARN_ON_ONCE(unlikely(!sb->alloc_hint)))
		return -1;

	depth = READ_ONCE(sb->depth);
	hint = update_alloc_hint_before_get(sb, depth);
	nr = __sbitmap_get(sb, hint);
	update_alloc_hint_after_get(sb, depth, hint, nr);

	return nr;
}
EXPORT_SYMBOL_GPL(sbitmap_get);

static int __sbitmap_get_shallow(struct sbitmap *sb,
				 unsigned int alloc_hint,
				 unsigned long shallow_depth)
{
	unsigned int index;

	index = SB_NR_TO_INDEX(sb, alloc_hint);
	alloc_hint = SB_NR_TO_BIT(sb, alloc_hint);

	return sbitmap_find_bit(sb, shallow_depth, index, alloc_hint, true);
}

int sbitmap_get_shallow(struct sbitmap *sb, unsigned long shallow_depth)
{
	int nr;
	unsigned int hint, depth;

	if (WARN_ON_ONCE(unlikely(!sb->alloc_hint)))
		return -1;

	depth = READ_ONCE(sb->depth);
	hint = update_alloc_hint_before_get(sb, depth);
	nr = __sbitmap_get_shallow(sb, hint, shallow_depth);
	update_alloc_hint_after_get(sb, depth, hint, nr);

	return nr;
}
EXPORT_SYMBOL_GPL(sbitmap_get_shallow);

bool sbitmap_any_bit_set(const struct sbitmap *sb)
{
	unsigned int i;

	for (i = 0; i < sb->map_nr; i++) {
		if (sb->map[i].word & ~sb->map[i].cleared)
			return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(sbitmap_any_bit_set);

static unsigned int __sbitmap_weight(const struct sbitmap *sb, bool set)
{
	unsigned int i, weight = 0;

	for (i = 0; i < sb->map_nr; i++) {
		const struct sbitmap_word *word = &sb->map[i];
		unsigned int word_depth = __map_depth(sb, i);

		if (set)
			weight += bitmap_weight(&word->word, word_depth);
		else
			weight += bitmap_weight(&word->cleared, word_depth);
	}
	return weight;
}

static unsigned int sbitmap_cleared(const struct sbitmap *sb)
{
	return __sbitmap_weight(sb, false);
}

unsigned int sbitmap_weight(const struct sbitmap *sb)
{
	return __sbitmap_weight(sb, true) - sbitmap_cleared(sb);
}
EXPORT_SYMBOL_GPL(sbitmap_weight);

void sbitmap_show(struct sbitmap *sb, struct seq_file *m)
{
	seq_printf(m, "depth=%u\n", sb->depth);
	seq_printf(m, "busy=%u\n", sbitmap_weight(sb));
	seq_printf(m, "cleared=%u\n", sbitmap_cleared(sb));
	seq_printf(m, "bits_per_word=%u\n", 1U << sb->shift);
	seq_printf(m, "map_nr=%u\n", sb->map_nr);
}
EXPORT_SYMBOL_GPL(sbitmap_show);

static inline void emit_byte(struct seq_file *m, unsigned int offset, u8 byte)
{
	if ((offset & 0xf) == 0) {
		if (offset != 0)
			seq_putc(m, '\n');
		seq_printf(m, "%08x:", offset);
	}
	if ((offset & 0x1) == 0)
		seq_putc(m, ' ');
	seq_printf(m, "%02x", byte);
}

void sbitmap_bitmap_show(struct sbitmap *sb, struct seq_file *m)
{
	u8 byte = 0;
	unsigned int byte_bits = 0;
	unsigned int offset = 0;
	int i;

	for (i = 0; i < sb->map_nr; i++) {
		unsigned long word = READ_ONCE(sb->map[i].word);
		unsigned long cleared = READ_ONCE(sb->map[i].cleared);
		unsigned int word_bits = __map_depth(sb, i);

		word &= ~cleared;

		while (word_bits > 0) {
			unsigned int bits = min(8 - byte_bits, word_bits);

			byte |= (word & (BIT(bits) - 1)) << byte_bits;
			byte_bits += bits;
			if (byte_bits == 8) {
				emit_byte(m, offset, byte);
				byte = 0;
				byte_bits = 0;
				offset++;
			}
			word >>= bits;
			word_bits -= bits;
		}
	}
	if (byte_bits) {
		emit_byte(m, offset, byte);
		offset++;
	}
	if (offset)
		seq_putc(m, '\n');
}
EXPORT_SYMBOL_GPL(sbitmap_bitmap_show);

static unsigned int sbq_calc_wake_batch(struct sbitmap_queue *sbq,
					unsigned int depth)
{
	unsigned int wake_batch;
	unsigned int shallow_depth;

	/*
	 * Each full word of the bitmap has bits_per_word bits, and there might
	 * be a partial word. There are depth / bits_per_word full words and
	 * depth % bits_per_word bits left over. In bitwise arithmetic:
	 *
	 * bits_per_word = 1 << shift
	 * depth / bits_per_word = depth >> shift
	 * depth % bits_per_word = depth & ((1 << shift) - 1)
	 *
	 * Each word can be limited to sbq->min_shallow_depth bits.
	 */
	shallow_depth = min(1U << sbq->sb.shift, sbq->min_shallow_depth);
	depth = ((depth >> sbq->sb.shift) * shallow_depth +
		 min(depth & ((1U << sbq->sb.shift) - 1), shallow_depth));
	wake_batch = clamp_t(unsigned int, depth / SBQ_WAIT_QUEUES, 1,
			     SBQ_WAKE_BATCH);

	return wake_batch;
}

int sbitmap_queue_init_node(struct sbitmap_queue *sbq, unsigned int depth,
			    int shift, bool round_robin, gfp_t flags, int node)
{
	int ret;
	int i;

	ret = sbitmap_init_node(&sbq->sb, depth, shift, flags, node,
				round_robin, true);
	if (ret)
		return ret;

	sbq->min_shallow_depth = UINT_MAX;
	sbq->wake_batch = sbq_calc_wake_batch(sbq, depth);
	atomic_set(&sbq->wake_index, 0);
	atomic_set(&sbq->ws_active, 0);
	atomic_set(&sbq->completion_cnt, 0);
	atomic_set(&sbq->wakeup_cnt, 0);

	sbq->ws = kzalloc_node(SBQ_WAIT_QUEUES * sizeof(*sbq->ws), flags, node);
	if (!sbq->ws) {
		sbitmap_free(&sbq->sb);
		return -ENOMEM;
	}

	for (i = 0; i < SBQ_WAIT_QUEUES; i++)
		init_waitqueue_head(&sbq->ws[i].wait);

	return 0;
}
EXPORT_SYMBOL_GPL(sbitmap_queue_init_node);

static void sbitmap_queue_update_wake_batch(struct sbitmap_queue *sbq,
					    unsigned int depth)
{
	unsigned int wake_batch;

	wake_batch = sbq_calc_wake_batch(sbq, depth);
	if (sbq->wake_batch != wake_batch)
		WRITE_ONCE(sbq->wake_batch, wake_batch);
}

void sbitmap_queue_recalculate_wake_batch(struct sbitmap_queue *sbq,
					    unsigned int users)
{
	unsigned int wake_batch;
	unsigned int depth = (sbq->sb.depth + users - 1) / users;

	wake_batch = clamp_val(depth / SBQ_WAIT_QUEUES,
			1, SBQ_WAKE_BATCH);

	WRITE_ONCE(sbq->wake_batch, wake_batch);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_recalculate_wake_batch);

void sbitmap_queue_resize(struct sbitmap_queue *sbq, unsigned int depth)
{
	sbitmap_queue_update_wake_batch(sbq, depth);
	sbitmap_resize(&sbq->sb, depth);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_resize);

int __sbitmap_queue_get(struct sbitmap_queue *sbq)
{
	return sbitmap_get(&sbq->sb);
}
EXPORT_SYMBOL_GPL(__sbitmap_queue_get);

unsigned long __sbitmap_queue_get_batch(struct sbitmap_queue *sbq, int nr_tags,
					unsigned int *offset)
{
	struct sbitmap *sb = &sbq->sb;
	unsigned int hint, depth;
	unsigned long index, nr;
	int i;

	if (unlikely(sb->round_robin))
		return 0;

	depth = READ_ONCE(sb->depth);
	hint = update_alloc_hint_before_get(sb, depth);

	index = SB_NR_TO_INDEX(sb, hint);

	for (i = 0; i < sb->map_nr; i++) {
		struct sbitmap_word *map = &sb->map[index];
		unsigned long get_mask;
		unsigned int map_depth = __map_depth(sb, index);

		sbitmap_deferred_clear(map);
		if (map->word == (1UL << (map_depth - 1)) - 1)
			goto next;

		nr = find_first_zero_bit(&map->word, map_depth);
		if (nr + nr_tags <= map_depth) {
			atomic_long_t *ptr = (atomic_long_t *) &map->word;
			unsigned long val;

			get_mask = ((1UL << nr_tags) - 1) << nr;
			val = READ_ONCE(map->word);
			while (!atomic_long_try_cmpxchg(ptr, &val,
							  get_mask | val))
				;
			get_mask = (get_mask & ~val) >> nr;
			if (get_mask) {
				*offset = nr + (index << sb->shift);
				update_alloc_hint_after_get(sb, depth, hint,
							*offset + nr_tags - 1);
				return get_mask;
			}
		}
next:
		/* Jump to next index. */
		if (++index >= sb->map_nr)
			index = 0;
	}

	return 0;
}

int sbitmap_queue_get_shallow(struct sbitmap_queue *sbq,
			      unsigned int shallow_depth)
{
	WARN_ON_ONCE(shallow_depth < sbq->min_shallow_depth);

	return sbitmap_get_shallow(&sbq->sb, shallow_depth);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_get_shallow);

void sbitmap_queue_min_shallow_depth(struct sbitmap_queue *sbq,
				     unsigned int min_shallow_depth)
{
	sbq->min_shallow_depth = min_shallow_depth;
	sbitmap_queue_update_wake_batch(sbq, sbq->sb.depth);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_min_shallow_depth);

static void __sbitmap_queue_wake_up(struct sbitmap_queue *sbq, int nr)
{
	int i, wake_index, woken;

	if (!atomic_read(&sbq->ws_active))
		return;

	wake_index = atomic_read(&sbq->wake_index);
	for (i = 0; i < SBQ_WAIT_QUEUES; i++) {
		struct sbq_wait_state *ws = &sbq->ws[wake_index];

		/*
		 * Advance the index before checking the current queue.
		 * It improves fairness, by ensuring the queue doesn't
		 * need to be fully emptied before trying to wake up
		 * from the next one.
		 */
		wake_index = sbq_index_inc(wake_index);

		if (waitqueue_active(&ws->wait)) {
			woken = wake_up_nr(&ws->wait, nr);
			if (woken == nr)
				break;
			nr -= woken;
		}
	}

	if (wake_index != atomic_read(&sbq->wake_index))
		atomic_set(&sbq->wake_index, wake_index);
}

void sbitmap_queue_wake_up(struct sbitmap_queue *sbq, int nr)
{
	unsigned int wake_batch = READ_ONCE(sbq->wake_batch);
	unsigned int wakeups;

	if (!atomic_read(&sbq->ws_active))
		return;

	atomic_add(nr, &sbq->completion_cnt);
	wakeups = atomic_read(&sbq->wakeup_cnt);

	do {
		if (atomic_read(&sbq->completion_cnt) - wakeups < wake_batch)
			return;
	} while (!atomic_try_cmpxchg(&sbq->wakeup_cnt,
				     &wakeups, wakeups + wake_batch));

	__sbitmap_queue_wake_up(sbq, wake_batch);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_wake_up);

static inline void sbitmap_update_cpu_hint(struct sbitmap *sb, int cpu, int tag)
{
	if (likely(!sb->round_robin && tag < sb->depth))
		data_race(*per_cpu_ptr(sb->alloc_hint, cpu) = tag);
}

void sbitmap_queue_clear_batch(struct sbitmap_queue *sbq, int offset,
				int *tags, int nr_tags)
{
	struct sbitmap *sb = &sbq->sb;
	unsigned long *addr = NULL;
	unsigned long mask = 0;
	int i;

	smp_mb__before_atomic();
	for (i = 0; i < nr_tags; i++) {
		const int tag = tags[i] - offset;
		unsigned long *this_addr;

		/* since we're clearing a batch, skip the deferred map */
		this_addr = &sb->map[SB_NR_TO_INDEX(sb, tag)].word;
		if (!addr) {
			addr = this_addr;
		} else if (addr != this_addr) {
			atomic_long_andnot(mask, (atomic_long_t *) addr);
			mask = 0;
			addr = this_addr;
		}
		mask |= (1UL << SB_NR_TO_BIT(sb, tag));
	}

	if (mask)
		atomic_long_andnot(mask, (atomic_long_t *) addr);

	smp_mb__after_atomic();
	sbitmap_queue_wake_up(sbq, nr_tags);
	sbitmap_update_cpu_hint(&sbq->sb, raw_smp_processor_id(),
					tags[nr_tags - 1] - offset);
}

void sbitmap_queue_clear(struct sbitmap_queue *sbq, unsigned int nr,
			 unsigned int cpu)
{
	/*
	 * Once the clear bit is set, the bit may be allocated out.
	 *
	 * Orders READ/WRITE on the associated instance(such as request
	 * of blk_mq) by this bit for avoiding race with re-allocation,
	 * and its pair is the memory barrier implied in __sbitmap_get_word.
	 *
	 * One invariant is that the clear bit has to be zero when the bit
	 * is in use.
	 */
	smp_mb__before_atomic();
	sbitmap_deferred_clear_bit(&sbq->sb, nr);

	/*
	 * Pairs with the memory barrier in set_current_state() to ensure the
	 * proper ordering of clear_bit_unlock()/waitqueue_active() in the waker
	 * and test_and_set_bit_lock()/prepare_to_wait()/finish_wait() in the
	 * waiter. See the comment on waitqueue_active().
	 */
	smp_mb__after_atomic();
	sbitmap_queue_wake_up(sbq, 1);
	sbitmap_update_cpu_hint(&sbq->sb, cpu, nr);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_clear);

void sbitmap_queue_wake_all(struct sbitmap_queue *sbq)
{
	int i, wake_index;

	/*
	 * Pairs with the memory barrier in set_current_state() like in
	 * sbitmap_queue_wake_up().
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

void sbitmap_queue_show(struct sbitmap_queue *sbq, struct seq_file *m)
{
	bool first;
	int i;

	sbitmap_show(&sbq->sb, m);

	seq_puts(m, "alloc_hint={");
	first = true;
	for_each_possible_cpu(i) {
		if (!first)
			seq_puts(m, ", ");
		first = false;
		seq_printf(m, "%u", *per_cpu_ptr(sbq->sb.alloc_hint, i));
	}
	seq_puts(m, "}\n");

	seq_printf(m, "wake_batch=%u\n", sbq->wake_batch);
	seq_printf(m, "wake_index=%d\n", atomic_read(&sbq->wake_index));
	seq_printf(m, "ws_active=%d\n", atomic_read(&sbq->ws_active));

	seq_puts(m, "ws={\n");
	for (i = 0; i < SBQ_WAIT_QUEUES; i++) {
		struct sbq_wait_state *ws = &sbq->ws[i];
		seq_printf(m, "\t{.wait=%s},\n",
			   waitqueue_active(&ws->wait) ? "active" : "inactive");
	}
	seq_puts(m, "}\n");

	seq_printf(m, "round_robin=%d\n", sbq->sb.round_robin);
	seq_printf(m, "min_shallow_depth=%u\n", sbq->min_shallow_depth);
}
EXPORT_SYMBOL_GPL(sbitmap_queue_show);

void sbitmap_add_wait_queue(struct sbitmap_queue *sbq,
			    struct sbq_wait_state *ws,
			    struct sbq_wait *sbq_wait)
{
	if (!sbq_wait->sbq) {
		sbq_wait->sbq = sbq;
		atomic_inc(&sbq->ws_active);
		add_wait_queue(&ws->wait, &sbq_wait->wait);
	}
}
EXPORT_SYMBOL_GPL(sbitmap_add_wait_queue);

void sbitmap_del_wait_queue(struct sbq_wait *sbq_wait)
{
	list_del_init(&sbq_wait->wait.entry);
	if (sbq_wait->sbq) {
		atomic_dec(&sbq_wait->sbq->ws_active);
		sbq_wait->sbq = NULL;
	}
}
EXPORT_SYMBOL_GPL(sbitmap_del_wait_queue);

void sbitmap_prepare_to_wait(struct sbitmap_queue *sbq,
			     struct sbq_wait_state *ws,
			     struct sbq_wait *sbq_wait, int state)
{
	if (!sbq_wait->sbq) {
		atomic_inc(&sbq->ws_active);
		sbq_wait->sbq = sbq;
	}
	prepare_to_wait_exclusive(&ws->wait, &sbq_wait->wait, state);
}
EXPORT_SYMBOL_GPL(sbitmap_prepare_to_wait);

void sbitmap_finish_wait(struct sbitmap_queue *sbq, struct sbq_wait_state *ws,
			 struct sbq_wait *sbq_wait)
{
	finish_wait(&ws->wait, &sbq_wait->wait);
	if (sbq_wait->sbq) {
		atomic_dec(&sbq->ws_active);
		sbq_wait->sbq = NULL;
	}
}
EXPORT_SYMBOL_GPL(sbitmap_finish_wait);
