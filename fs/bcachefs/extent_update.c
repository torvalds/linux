// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_on_stack.h"
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
	unsigned ret = 0;

	bkey_extent_entry_for_each(ptrs, entry) {
		switch (__extent_entry_type(entry)) {
		case BCH_EXTENT_ENTRY_ptr:
		case BCH_EXTENT_ENTRY_stripe_ptr:
			ret++;
		}
	}

	return ret;
}

static int count_iters_for_insert(struct btree_trans *trans,
				  struct bkey_s_c k,
				  unsigned offset,
				  struct bpos *end,
				  unsigned *nr_iters,
				  unsigned max_iters,
				  bool overwrite)
{
	int ret = 0;

	switch (k.k->type) {
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		*nr_iters += bch2_bkey_nr_alloc_ptrs(k);

		if (*nr_iters >= max_iters) {
			*end = bpos_min(*end, k.k->p);
			ret = 1;
		}

		break;
	case KEY_TYPE_reflink_p: {
		struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
		u64 idx = le64_to_cpu(p.v->idx);
		unsigned sectors = bpos_min(*end, p.k->p).offset -
			bkey_start_offset(p.k);
		struct btree_iter *iter;
		struct bkey_s_c r_k;

		for_each_btree_key(trans, iter,
				   BTREE_ID_REFLINK, POS(0, idx + offset),
				   BTREE_ITER_SLOTS, r_k, ret) {
			if (bkey_cmp(bkey_start_pos(r_k.k),
				     POS(0, idx + sectors)) >= 0)
				break;

			*nr_iters += 1 + bch2_bkey_nr_alloc_ptrs(r_k);

			if (*nr_iters >= max_iters) {
				struct bpos pos = bkey_start_pos(k.k);
				pos.offset += r_k.k->p.offset - idx;

				*end = bpos_min(*end, pos);
				ret = 1;
				break;
			}
		}

		bch2_trans_iter_put(trans, iter);
		break;
	}
	}

	return ret;
}

#define EXTENT_ITERS_MAX	(BTREE_ITER_MAX / 3)

int bch2_extent_atomic_end(struct btree_iter *iter,
			   struct bkey_i *insert,
			   struct bpos *end)
{
	struct btree_trans *trans = iter->trans;
	struct btree *b;
	struct btree_node_iter	node_iter;
	struct bkey_packed	*_k;
	unsigned		nr_iters = 0;
	int ret;

	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return ret;

	b = iter->l[0].b;
	node_iter = iter->l[0].iter;

	BUG_ON(bkey_cmp(bkey_start_pos(&insert->k), b->data->min_key) < 0);

	*end = bpos_min(insert->k.p, b->key.k.p);

	ret = count_iters_for_insert(trans, bkey_i_to_s_c(insert), 0, end,
				     &nr_iters, EXTENT_ITERS_MAX / 2, false);
	if (ret < 0)
		return ret;

	while ((_k = bch2_btree_node_iter_peek_filter(&node_iter, b,
						      KEY_TYPE_discard))) {
		struct bkey	unpacked;
		struct bkey_s_c	k = bkey_disassemble(b, _k, &unpacked);
		unsigned offset = 0;

		if (bkey_cmp(bkey_start_pos(k.k), *end) >= 0)
			break;

		if (bkey_cmp(bkey_start_pos(&insert->k),
			     bkey_start_pos(k.k)) > 0)
			offset = bkey_start_offset(&insert->k) -
				bkey_start_offset(k.k);

		ret = count_iters_for_insert(trans, k, offset, end,
					&nr_iters, EXTENT_ITERS_MAX, true);
		if (ret)
			break;

		bch2_btree_node_iter_advance(&node_iter, b);
	}

	return ret < 0 ? ret : 0;
}

int bch2_extent_trim_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	struct bpos end;
	int ret;

	ret = bch2_extent_atomic_end(iter, k, &end);
	if (ret)
		return ret;

	bch2_cut_back(end, k);
	return 0;
}

int bch2_extent_is_atomic(struct bkey_i *k, struct btree_iter *iter)
{
	struct bpos end;
	int ret;

	ret = bch2_extent_atomic_end(iter, k, &end);
	if (ret)
		return ret;

	return !bkey_cmp(end, k->k.p);
}

enum btree_insert_ret
bch2_extent_can_insert(struct btree_trans *trans,
		       struct btree_insert_entry *insert,
		       unsigned *u64s)
{
	struct btree_iter_level *l = &insert->iter->l[0];
	struct btree_node_iter node_iter = l->iter;
	struct bkey_packed *_k;
	struct bkey unpacked;
	int sectors;

	while ((_k = bch2_btree_node_iter_peek_filter(&node_iter, l->b,
						      KEY_TYPE_discard))) {
		struct bkey_s_c k = bkey_disassemble(l->b, _k, &unpacked);
		enum bch_extent_overlap overlap =
			bch2_extent_overlap(&insert->k->k, k.k);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k->k.p) >= 0)
			break;

		overlap = bch2_extent_overlap(&insert->k->k, k.k);

		/*
		 * If we're overwriting an existing extent, we may need to emit
		 * a whiteout - unless we're inserting a new extent at the same
		 * position:
		 */
		if (k.k->needs_whiteout &&
		    (!bkey_whiteout(&insert->k->k) ||
		     bkey_cmp(k.k->p, insert->k->k.p)))
			*u64s += BKEY_U64s;

		/*
		 * If we're partially overwriting an existing extent which has
		 * been written out to disk, we'll need to emit a new version of
		 * that extent:
		 */
		if (bkey_written(l->b, _k) &&
		    overlap != BCH_EXTENT_OVERLAP_ALL)
			*u64s += _k->u64s;

		/* And we may be splitting an existing extent: */
		if (overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			*u64s += _k->u64s;

		if (overlap == BCH_EXTENT_OVERLAP_MIDDLE &&
		    (sectors = bch2_bkey_sectors_compressed(k))) {
			int flags = trans->flags & BTREE_INSERT_NOFAIL
				? BCH_DISK_RESERVATION_NOFAIL : 0;

			switch (bch2_disk_reservation_add(trans->c,
					trans->disk_res,
					sectors, flags)) {
			case 0:
				break;
			case -ENOSPC:
				return BTREE_INSERT_ENOSPC;
			default:
				BUG();
			}
		}

		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;

		bch2_btree_node_iter_advance(&node_iter, l->b);
	}

	return BTREE_INSERT_OK;
}

static void verify_extent_nonoverlapping(struct bch_fs *c,
					 struct btree *b,
					 struct btree_node_iter *_iter,
					 struct bkey_i *insert)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct btree_node_iter iter;
	struct bkey_packed *k;
	struct bkey uk;

	if (!expensive_debug_checks(c))
		return;

	iter = *_iter;
	k = bch2_btree_node_iter_prev_filter(&iter, b, KEY_TYPE_discard);
	BUG_ON(k &&
	       (uk = bkey_unpack_key(b, k),
		bkey_cmp(uk.p, bkey_start_pos(&insert->k)) > 0));

	iter = *_iter;
	k = bch2_btree_node_iter_peek_filter(&iter, b, KEY_TYPE_discard);
#if 0
	BUG_ON(k &&
	       (uk = bkey_unpack_key(b, k),
		bkey_cmp(insert->k.p, bkey_start_pos(&uk))) > 0);
#else
	if (k &&
	    (uk = bkey_unpack_key(b, k),
	     bkey_cmp(insert->k.p, bkey_start_pos(&uk))) > 0) {
		char buf1[100];
		char buf2[100];

		bch2_bkey_to_text(&PBUF(buf1), &insert->k);
		bch2_bkey_to_text(&PBUF(buf2), &uk);

		bch2_dump_btree_node(b);
		panic("insert > next :\n"
		      "insert %s\n"
		      "next   %s\n",
		      buf1, buf2);
	}
#endif

#endif
}

static void extent_bset_insert(struct bch_fs *c, struct btree_iter *iter,
			       struct bkey_i *insert)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *k =
		bch2_btree_node_iter_bset_pos(&l->iter, l->b, bset_tree_last(l->b));

	BUG_ON(insert->k.u64s > bch_btree_keys_u64s_remaining(c, l->b));

	EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);
	verify_extent_nonoverlapping(c, l->b, &l->iter, insert);

	if (debug_check_bkeys(c))
		bch2_bkey_debugcheck(c, l->b, bkey_i_to_s_c(insert));

	bch2_bset_insert(l->b, &l->iter, k, insert, 0);
	bch2_btree_node_iter_fix(iter, l->b, &l->iter, k, 0, k->u64s);
}

static void pack_push_whiteout(struct bch_fs *c, struct btree *b,
			       struct bpos pos)
{
	struct bkey_packed k;

	if (!bkey_pack_pos(&k, pos, b)) {
		struct bkey_i tmp;

		bkey_init(&tmp.k);
		tmp.k.p = pos;
		bkey_copy(&k, &tmp);
	}

	k.needs_whiteout = true;
	push_whiteout(c, b, &k);
}

static void
extent_drop(struct bch_fs *c, struct btree_iter *iter,
	    struct bkey_packed *_k, struct bkey_s k)
{
	struct btree_iter_level *l = &iter->l[0];

	if (!bkey_whiteout(k.k))
		btree_account_key_drop(l->b, _k);

	k.k->size = 0;
	k.k->type = KEY_TYPE_deleted;

	if (!btree_node_old_extent_overwrite(l->b) &&
	    k.k->needs_whiteout) {
		pack_push_whiteout(c, l->b, k.k->p);
		k.k->needs_whiteout = false;
	}

	if (_k >= btree_bset_last(l->b)->start) {
		unsigned u64s = _k->u64s;

		bch2_bset_delete(l->b, _k, _k->u64s);
		bch2_btree_node_iter_fix(iter, l->b, &l->iter, _k, u64s, 0);
	} else {
		extent_save(l->b, _k, k.k);
		bch2_btree_iter_fix_key_modified(iter, l->b, _k);
	}
}

static void
extent_squash(struct bch_fs *c, struct btree_iter *iter,
	      struct bkey_i *insert,
	      struct bkey_packed *_k, struct bkey_s k,
	      enum bch_extent_overlap overlap)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_on_stack tmp, split;

	bkey_on_stack_init(&tmp);
	bkey_on_stack_init(&split);

	if (!btree_node_old_extent_overwrite(l->b)) {
		if (!bkey_whiteout(&insert->k) &&
		    !bkey_cmp(k.k->p, insert->k.p)) {
			insert->k.needs_whiteout = k.k->needs_whiteout;
			k.k->needs_whiteout = false;
		}
	} else {
		insert->k.needs_whiteout |= k.k->needs_whiteout;
	}

	switch (overlap) {
	case BCH_EXTENT_OVERLAP_FRONT:
		if (bkey_written(l->b, _k)) {
			bkey_on_stack_reassemble(&tmp, c, k.s_c);
			bch2_cut_front(insert->k.p, tmp.k);

			/*
			 * needs_whiteout was propagated to new version of @k,
			 * @tmp:
			 */
			if (!btree_node_old_extent_overwrite(l->b))
				k.k->needs_whiteout = false;

			extent_drop(c, iter, _k, k);
			extent_bset_insert(c, iter, tmp.k);
		} else {
			btree_keys_account_val_delta(l->b, _k,
				bch2_cut_front_s(insert->k.p, k));

			extent_save(l->b, _k, k.k);
			/*
			 * No need to call bset_fix_invalidated_key, start of
			 * extent changed but extents are indexed by where they
			 * end
			 */
			bch2_btree_iter_fix_key_modified(iter, l->b, _k);
		}
		break;
	case BCH_EXTENT_OVERLAP_BACK:
		if (bkey_written(l->b, _k)) {
			bkey_on_stack_reassemble(&tmp, c, k.s_c);
			bch2_cut_back(bkey_start_pos(&insert->k), tmp.k);

			/*
			 * @tmp has different position than @k, needs_whiteout
			 * should not be propagated:
			 */
			if (!btree_node_old_extent_overwrite(l->b))
				tmp.k->k.needs_whiteout = false;

			extent_drop(c, iter, _k, k);
			extent_bset_insert(c, iter, tmp.k);
		} else {
			/*
			 * position of @k is changing, emit a whiteout if
			 * needs_whiteout is set:
			 */
			if (!btree_node_old_extent_overwrite(l->b) &&
			    k.k->needs_whiteout) {
				pack_push_whiteout(c, l->b, k.k->p);
				k.k->needs_whiteout = false;
			}

			btree_keys_account_val_delta(l->b, _k,
				bch2_cut_back_s(bkey_start_pos(&insert->k), k));
			extent_save(l->b, _k, k.k);

			bch2_bset_fix_invalidated_key(l->b, _k);
			bch2_btree_node_iter_fix(iter, l->b, &l->iter,
						 _k, _k->u64s, _k->u64s);
		}
		break;
	case BCH_EXTENT_OVERLAP_ALL:
		extent_drop(c, iter, _k, k);
		break;
	case BCH_EXTENT_OVERLAP_MIDDLE:
		bkey_on_stack_reassemble(&split, c, k.s_c);
		bch2_cut_back(bkey_start_pos(&insert->k), split.k);

		if (!btree_node_old_extent_overwrite(l->b))
			split.k->k.needs_whiteout = false;

		/* this is identical to BCH_EXTENT_OVERLAP_FRONT: */
		if (bkey_written(l->b, _k)) {
			bkey_on_stack_reassemble(&tmp, c, k.s_c);
			bch2_cut_front(insert->k.p, tmp.k);

			if (!btree_node_old_extent_overwrite(l->b))
				k.k->needs_whiteout = false;

			extent_drop(c, iter, _k, k);
			extent_bset_insert(c, iter, tmp.k);
		} else {
			btree_keys_account_val_delta(l->b, _k,
				bch2_cut_front_s(insert->k.p, k));

			extent_save(l->b, _k, k.k);
			bch2_btree_iter_fix_key_modified(iter, l->b, _k);
		}

		extent_bset_insert(c, iter, split.k);
		break;
	}

	bkey_on_stack_exit(&split, c);
	bkey_on_stack_exit(&tmp, c);
}

/**
 * bch_extent_insert_fixup - insert a new extent and deal with overlaps
 *
 * this may result in not actually doing the insert, or inserting some subset
 * of the insert key. For cmpxchg operations this is where that logic lives.
 *
 * All subsets of @insert that need to be inserted are inserted using
 * bch2_btree_insert_and_journal(). If @b or @res fills up, this function
 * returns false, setting @iter->pos for the prefix of @insert that actually got
 * inserted.
 *
 * BSET INVARIANTS: this function is responsible for maintaining all the
 * invariants for bsets of extents in memory. things get really hairy with 0
 * size extents
 *
 * within one bset:
 *
 * bkey_start_pos(bkey_next(k)) >= k
 * or bkey_start_offset(bkey_next(k)) >= k->offset
 *
 * i.e. strict ordering, no overlapping extents.
 *
 * multiple bsets (i.e. full btree node):
 *
 * ∀ k, j
 *   k.size != 0 ∧ j.size != 0 →
 *     ¬ (k > bkey_start_pos(j) ∧ k < j)
 *
 * i.e. no two overlapping keys _of nonzero size_
 *
 * We can't realistically maintain this invariant for zero size keys because of
 * the key merging done in bch2_btree_insert_key() - for two mergeable keys k, j
 * there may be another 0 size key between them in another bset, and it will
 * thus overlap with the merged key.
 *
 * In addition, the end of iter->pos indicates how much has been processed.
 * If the end of iter->pos is not the same as the end of insert, then
 * key insertion needs to continue/be retried.
 */
void bch2_insert_fixup_extent(struct btree_trans *trans,
			      struct btree_insert_entry *insert_entry)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter	= insert_entry->iter;
	struct bkey_i *insert	= insert_entry->k;
	struct btree_iter_level *l = &iter->l[0];
	struct btree_node_iter node_iter = l->iter;
	bool do_update		= !bkey_whiteout(&insert->k);
	struct bkey_packed *_k;
	struct bkey unpacked;

	EBUG_ON(iter->level);
	EBUG_ON(!insert->k.size);
	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k)));

	while ((_k = bch2_btree_node_iter_peek_filter(&l->iter, l->b,
						      KEY_TYPE_discard))) {
		struct bkey_s k = __bkey_disassemble(l->b, _k, &unpacked);
		enum bch_extent_overlap overlap =
			bch2_extent_overlap(&insert->k, k.k);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k.p) >= 0)
			break;

		if (!bkey_whiteout(k.k))
			do_update = true;

		if (!do_update) {
			struct bpos cur_end = bpos_min(insert->k.p, k.k->p);

			bch2_cut_front(cur_end, insert);
			bch2_btree_iter_set_pos_same_leaf(iter, cur_end);
		} else {
			extent_squash(c, iter, insert, _k, k, overlap);
		}

		node_iter = l->iter;

		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;
	}

	l->iter = node_iter;
	bch2_btree_iter_set_pos_same_leaf(iter, insert->k.p);

	if (do_update) {
		if (insert->k.type == KEY_TYPE_deleted)
			insert->k.type = KEY_TYPE_discard;

		if (!bkey_whiteout(&insert->k) ||
		    btree_node_old_extent_overwrite(l->b))
			extent_bset_insert(c, iter, insert);

		bch2_btree_journal_key(trans, iter, insert);
	}

	bch2_cut_front(insert->k.p, insert);
}
