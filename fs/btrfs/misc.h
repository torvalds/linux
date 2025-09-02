/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_MISC_H
#define BTRFS_MISC_H

#include <linux/types.h>
#include <linux/bitmap.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/math64.h>
#include <linux/rbtree.h>
#include <linux/bio.h>

/*
 * Enumerate bits using enum autoincrement. Define the @name as the n-th bit.
 */
#define ENUM_BIT(name)                                  \
	__ ## name ## _BIT,                             \
	name = (1U << __ ## name ## _BIT),              \
	__ ## name ## _SEQ = __ ## name ## _BIT

static inline phys_addr_t bio_iter_phys(struct bio *bio, struct bvec_iter *iter)
{
	struct bio_vec bv = bio_iter_iovec(bio, *iter);

	return bvec_phys(&bv);
}

/*
 * Iterate bio using btrfs block size.
 *
 * This will handle large folio and highmem.
 *
 * @paddr:	Physical memory address of each iteration
 * @bio:	The bio to iterate
 * @iter:	The bvec_iter (pointer) to use.
 * @blocksize:	The blocksize to iterate.
 *
 * This requires all folios in the bio to cover at least one block.
 */
#define btrfs_bio_for_each_block(paddr, bio, iter, blocksize)		\
	for (; (iter)->bi_size &&					\
	     (paddr = bio_iter_phys((bio), (iter)), 1);			\
	     bio_advance_iter_single((bio), (iter), (blocksize)))

/* Initialize a bvec_iter to the size of the specified bio. */
static inline struct bvec_iter init_bvec_iter_for_bio(struct bio *bio)
{
	struct bio_vec *bvec;
	u32 bio_size = 0;
	int i;

	bio_for_each_bvec_all(bvec, bio, i)
		bio_size += bvec->bv_len;

	return (struct bvec_iter) {
		.bi_sector = 0,
		.bi_size = bio_size,
		.bi_idx = 0,
		.bi_bvec_done = 0,
	};
}

#define btrfs_bio_for_each_block_all(paddr, bio, blocksize)		\
	for (struct bvec_iter iter = init_bvec_iter_for_bio(bio);	\
	     (iter).bi_size &&						\
	     (paddr = bio_iter_phys((bio), &(iter)), 1);		\
	     bio_advance_iter_single((bio), &(iter), (blocksize)))

static inline void cond_wake_up(struct wait_queue_head *wq)
{
	/*
	 * This implies a full smp_mb barrier, see comments for
	 * waitqueue_active why.
	 */
	if (wq_has_sleeper(wq))
		wake_up(wq);
}

static inline void cond_wake_up_nomb(struct wait_queue_head *wq)
{
	/*
	 * Special case for conditional wakeup where the barrier required for
	 * waitqueue_active is implied by some of the preceding code. Eg. one
	 * of such atomic operations (atomic_dec_and_return, ...), or a
	 * unlock/lock sequence, etc.
	 */
	if (waitqueue_active(wq))
		wake_up(wq);
}

static inline u64 mult_perc(u64 num, u32 percent)
{
	return div_u64(num * percent, 100);
}
/* Copy of is_power_of_two that is 64bit safe */
static inline bool is_power_of_two_u64(u64 n)
{
	return n != 0 && (n & (n - 1)) == 0;
}

static inline bool has_single_bit_set(u64 n)
{
	return is_power_of_two_u64(n);
}

/*
 * Simple bytenr based rb_tree relate structures
 *
 * Any structure wants to use bytenr as single search index should have their
 * structure start with these members.
 */
struct rb_simple_node {
	struct rb_node rb_node;
	u64 bytenr;
};

static inline struct rb_node *rb_simple_search(const struct rb_root *root, u64 bytenr)
{
	struct rb_node *node = root->rb_node;
	struct rb_simple_node *entry;

	while (node) {
		entry = rb_entry(node, struct rb_simple_node, rb_node);

		if (bytenr < entry->bytenr)
			node = node->rb_left;
		else if (bytenr > entry->bytenr)
			node = node->rb_right;
		else
			return node;
	}
	return NULL;
}

/*
 * Search @root from an entry that starts or comes after @bytenr.
 *
 * @root:	the root to search.
 * @bytenr:	bytenr to search from.
 *
 * Return the rb_node that start at or after @bytenr.  If there is no entry at
 * or after @bytner return NULL.
 */
static inline struct rb_node *rb_simple_search_first(const struct rb_root *root,
						     u64 bytenr)
{
	struct rb_node *node = root->rb_node, *ret = NULL;
	struct rb_simple_node *entry, *ret_entry = NULL;

	while (node) {
		entry = rb_entry(node, struct rb_simple_node, rb_node);

		if (bytenr < entry->bytenr) {
			if (!ret || entry->bytenr < ret_entry->bytenr) {
				ret = node;
				ret_entry = entry;
			}

			node = node->rb_left;
		} else if (bytenr > entry->bytenr) {
			node = node->rb_right;
		} else {
			return node;
		}
	}

	return ret;
}

static int rb_simple_node_bytenr_cmp(struct rb_node *new, const struct rb_node *existing)
{
	struct rb_simple_node *new_entry = rb_entry(new, struct rb_simple_node, rb_node);
	struct rb_simple_node *existing_entry = rb_entry(existing, struct rb_simple_node, rb_node);

	if (new_entry->bytenr < existing_entry->bytenr)
		return -1;
	else if (new_entry->bytenr > existing_entry->bytenr)
		return 1;

	return 0;
}

static inline struct rb_node *rb_simple_insert(struct rb_root *root,
					       struct rb_simple_node *simple_node)
{
	return rb_find_add(&simple_node->rb_node, root, rb_simple_node_bytenr_cmp);
}

static inline bool bitmap_test_range_all_set(const unsigned long *addr,
					     unsigned long start,
					     unsigned long nbits)
{
	unsigned long found_zero;

	found_zero = find_next_zero_bit(addr, start + nbits, start);
	return (found_zero == start + nbits);
}

static inline bool bitmap_test_range_all_zero(const unsigned long *addr,
					      unsigned long start,
					      unsigned long nbits)
{
	unsigned long found_set;

	found_set = find_next_bit(addr, start + nbits, start);
	return (found_set == start + nbits);
}

static inline u64 folio_end(struct folio *folio)
{
	return folio_pos(folio) + folio_size(folio);
}

#endif
