// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "debug.h"
#include "extents.h"
#include "extent_update.h"

/*
 * This counts the number of iterators to the alloc & ec btrees we'll need
 * inserting/removing this extent:
 */
static unsigned bch2_bkey_nr_alloc_ptrs(struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	unsigned ret = 0, lru = 0;

	bkey_extent_entry_for_each(ptrs, entry) {
		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
			/* Might also be updating LRU btree */
			if (entry->ptr.cached)
				lru++;

			fallthrough;
		case BCH_EXTENT_ENTRY_stripe_ptr:
			ret++;
		}
	}

	/*
	 * Updating keys in the alloc btree may also update keys in the
	 * freespace or discard btrees:
	 */
	return lru + ret * 2;
}

#define EXTENT_ITERS_MAX	64

static int count_iters_for_insert(struct btree_trans *trans,
				  struct bkey_s_c k,
				  unsigned offset,
				  struct bpos *end,
				  unsigned *nr_iters)
{
	int ret = 0, ret2 = 0;

	if (*nr_iters >= EXTENT_ITERS_MAX) {
		*end = bpos_min(*end, k.k->p);
		ret = 1;
	}

	switch (k.k->type) {
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		*nr_iters += bch2_bkey_nr_alloc_ptrs(k);

		if (*nr_iters >= EXTENT_ITERS_MAX) {
			*end = bpos_min(*end, k.k->p);
			ret = 1;
		}

		break;
	case KEY_TYPE_reflink_p: {
		struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
		u64 idx = REFLINK_P_IDX(p.v);
		unsigned sectors = bpos_min(*end, p.k->p).offset -
			bkey_start_offset(p.k);
		struct btree_iter iter;
		struct bkey_s_c r_k;

		for_each_btree_key_norestart(trans, iter,
				   BTREE_ID_reflink, POS(0, idx + offset),
				   BTREE_ITER_slots, r_k, ret2) {
			if (bkey_ge(bkey_start_pos(r_k.k), POS(0, idx + sectors)))
				break;

			/* extent_update_to_keys(), for the reflink_v update */
			*nr_iters += 1;

			*nr_iters += 1 + bch2_bkey_nr_alloc_ptrs(r_k);

			if (*nr_iters >= EXTENT_ITERS_MAX) {
				struct bpos pos = bkey_start_pos(k.k);
				pos.offset += min_t(u64, k.k->size,
						    r_k.k->p.offset - idx);

				*end = bpos_min(*end, pos);
				ret = 1;
				break;
			}
		}
		bch2_trans_iter_exit(trans, &iter);

		break;
	}
	}

	return ret2 ?: ret;
}

int bch2_extent_atomic_end(struct btree_trans *trans,
			   struct btree_iter *iter,
			   struct bpos *end)
{
	unsigned nr_iters = 0;

	struct btree_iter copy;
	bch2_trans_copy_iter(trans, &copy, iter);

	int ret = bch2_btree_iter_traverse(trans, &copy);
	if (ret)
		goto err;

	struct bkey_s_c k;
	for_each_btree_key_max_continue_norestart(trans, copy, *end, 0, k, ret) {
		unsigned offset = 0;

		if (bkey_gt(iter->pos, bkey_start_pos(k.k)))
			offset = iter->pos.offset - bkey_start_offset(k.k);

		ret = count_iters_for_insert(trans, k, offset, end, &nr_iters);
		if (ret)
			break;
	}
err:
	bch2_trans_iter_exit(trans, &copy);
	return ret < 0 ? ret : 0;
}

int bch2_extent_trim_atomic(struct btree_trans *trans,
			    struct btree_iter *iter,
			    struct bkey_i *k)
{
	struct bpos end = k->k.p;
	int ret = bch2_extent_atomic_end(trans, iter, &end);
	if (ret)
		return ret;

	/* tracepoint */

	if (bpos_lt(end, k->k.p)) {
		if (trace_extent_trim_atomic_enabled()) {
			CLASS(printbuf, buf)();
			bch2_bpos_to_text(&buf, end);
			prt_newline(&buf);
			bch2_bkey_val_to_text(&buf, trans->c, bkey_i_to_s_c(k));
			trace_extent_trim_atomic(trans->c, buf.buf);
		}
		bch2_cut_back(end, k);
	}
	return 0;
}
