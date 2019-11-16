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
	enum bch_extent_overlap overlap;
	struct bkey_packed *_k;
	struct bkey unpacked;
	struct bkey_s_c k;
	int sectors;

	/*
	 * We avoid creating whiteouts whenever possible when deleting, but
	 * those optimizations mean we may potentially insert two whiteouts
	 * instead of one (when we overlap with the front of one extent and the
	 * back of another):
	 */
	if (bkey_whiteout(&insert->k->k))
		*u64s += BKEY_U64s;

	_k = bch2_btree_node_iter_peek_filter(&node_iter, l->b,
					      KEY_TYPE_discard);
	if (!_k)
		return BTREE_INSERT_OK;

	k = bkey_disassemble(l->b, _k, &unpacked);

	overlap = bch2_extent_overlap(&insert->k->k, k.k);

	/* account for having to split existing extent: */
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

static void
extent_squash(struct bch_fs *c, struct btree_iter *iter,
	      struct bkey_i *insert,
	      struct bkey_packed *_k, struct bkey_s k,
	      enum bch_extent_overlap overlap)
{
	struct btree_iter_level *l = &iter->l[0];
	int u64s_delta;

	switch (overlap) {
	case BCH_EXTENT_OVERLAP_FRONT:
		/* insert overlaps with start of k: */
		u64s_delta = bch2_cut_front_s(insert->k.p, k);
		btree_keys_account_val_delta(l->b, _k, u64s_delta);

		EBUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);
		bch2_btree_iter_fix_key_modified(iter, l->b, _k);
		break;

	case BCH_EXTENT_OVERLAP_BACK:
		/* insert overlaps with end of k: */
		u64s_delta = bch2_cut_back_s(bkey_start_pos(&insert->k), k);
		btree_keys_account_val_delta(l->b, _k, u64s_delta);

		EBUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);

		/*
		 * As the auxiliary tree is indexed by the end of the
		 * key and we've just changed the end, update the
		 * auxiliary tree.
		 */
		bch2_bset_fix_invalidated_key(l->b, _k);
		bch2_btree_node_iter_fix(iter, l->b, &l->iter,
					 _k, _k->u64s, _k->u64s);
		break;

	case BCH_EXTENT_OVERLAP_ALL: {
		/* The insert key completely covers k, invalidate k */
		if (!bkey_whiteout(k.k))
			btree_account_key_drop(l->b, _k);

		k.k->size = 0;
		k.k->type = KEY_TYPE_deleted;

		if (_k >= btree_bset_last(l->b)->start) {
			unsigned u64s = _k->u64s;

			bch2_bset_delete(l->b, _k, _k->u64s);
			bch2_btree_node_iter_fix(iter, l->b, &l->iter,
						 _k, u64s, 0);
		} else {
			extent_save(l->b, _k, k.k);
			bch2_btree_iter_fix_key_modified(iter, l->b, _k);
		}

		break;
	}
	case BCH_EXTENT_OVERLAP_MIDDLE: {
		struct bkey_on_stack split;

		bkey_on_stack_init(&split);
		bkey_on_stack_realloc(&split, c, k.k->u64s);

		/*
		 * The insert key falls 'in the middle' of k
		 * The insert key splits k in 3:
		 * - start only in k, preserve
		 * - middle common section, invalidate in k
		 * - end only in k, preserve
		 *
		 * We update the old key to preserve the start,
		 * insert will be the new common section,
		 * we manually insert the end that we are preserving.
		 *
		 * modify k _before_ doing the insert (which will move
		 * what k points to)
		 */
		bkey_reassemble(split.k, k.s_c);
		split.k->k.needs_whiteout |= bkey_written(l->b, _k);

		bch2_cut_back(bkey_start_pos(&insert->k), split.k);
		BUG_ON(bkey_deleted(&split.k->k));

		u64s_delta = bch2_cut_front_s(insert->k.p, k);
		btree_keys_account_val_delta(l->b, _k, u64s_delta);

		BUG_ON(bkey_deleted(k.k));
		extent_save(l->b, _k, k.k);
		bch2_btree_iter_fix_key_modified(iter, l->b, _k);

		extent_bset_insert(c, iter, split.k);
		bkey_on_stack_exit(&split, c);
		break;
	}
	}
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
	bool deleting		= bkey_whiteout(&insert->k);
	bool update_journal	= !deleting;
	bool update_btree	= !deleting;
	struct bkey_i whiteout	= *insert;
	struct bkey_packed *_k;
	struct bkey unpacked;

	EBUG_ON(iter->level);
	EBUG_ON(!insert->k.size);
	EBUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&insert->k)));

	while ((_k = bch2_btree_node_iter_peek_filter(&l->iter, l->b,
						      KEY_TYPE_discard))) {
		struct bkey_s k = __bkey_disassemble(l->b, _k, &unpacked);
		struct bpos cur_end = bpos_min(insert->k.p, k.k->p);
		enum bch_extent_overlap overlap =
			bch2_extent_overlap(&insert->k, k.k);

		if (bkey_cmp(bkey_start_pos(k.k), insert->k.p) >= 0)
			break;

		if (!bkey_whiteout(k.k))
			update_journal = true;

		if (!update_journal) {
			bch2_cut_front(cur_end, insert);
			bch2_cut_front(cur_end, &whiteout);
			bch2_btree_iter_set_pos_same_leaf(iter, cur_end);
			goto next;
		}

		/*
		 * When deleting, if possible just do it by switching the type
		 * of the key we're deleting, instead of creating and inserting
		 * a new whiteout:
		 */
		if (deleting &&
		    !update_btree &&
		    !bkey_cmp(insert->k.p, k.k->p) &&
		    !bkey_cmp(bkey_start_pos(&insert->k), bkey_start_pos(k.k))) {
			if (!bkey_whiteout(k.k)) {
				btree_account_key_drop(l->b, _k);
				_k->type = KEY_TYPE_discard;
				reserve_whiteout(l->b, _k);
				bch2_btree_iter_fix_key_modified(iter,
								 l->b, _k);
			}
			break;
		}

		if (k.k->needs_whiteout || bkey_written(l->b, _k)) {
			insert->k.needs_whiteout = true;
			update_btree = true;
		}

		if (update_btree &&
		    overlap == BCH_EXTENT_OVERLAP_ALL &&
		    bkey_whiteout(k.k) &&
		    k.k->needs_whiteout) {
			unreserve_whiteout(l->b, _k);
			_k->needs_whiteout = false;
		}

		extent_squash(c, iter, insert, _k, k, overlap);

		if (!update_btree)
			bch2_cut_front(cur_end, insert);
next:
		node_iter = l->iter;

		if (overlap == BCH_EXTENT_OVERLAP_FRONT ||
		    overlap == BCH_EXTENT_OVERLAP_MIDDLE)
			break;
	}

	l->iter = node_iter;
	bch2_btree_iter_set_pos_same_leaf(iter, insert->k.p);

	if (update_btree) {
		if (deleting)
			insert->k.type = KEY_TYPE_discard;

		EBUG_ON(bkey_deleted(&insert->k) || !insert->k.size);

		extent_bset_insert(c, iter, insert);
	}

	if (update_journal) {
		struct bkey_i *k = !deleting ? insert : &whiteout;

		if (deleting)
			k->k.type = KEY_TYPE_discard;

		EBUG_ON(bkey_deleted(&k->k) || !k->k.size);

		bch2_btree_journal_key(trans, iter, k);
	}

	bch2_cut_front(insert->k.p, insert);
}
