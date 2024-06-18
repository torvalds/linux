// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright (C) 2014 Datera Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "backpointers.h"
#include "bkey_methods.h"
#include "bkey_buf.h"
#include "btree_journal_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_node_scan.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "btree_gc.h"
#include "buckets.h"
#include "clock.h"
#include "debug.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "keylist.h"
#include "move.h"
#include "recovery_passes.h"
#include "reflink.h"
#include "replicas.h"
#include "super-io.h"
#include "trace.h"

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/preempt.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>

#define DROP_THIS_NODE		10
#define DROP_PREV_NODE		11
#define DID_FILL_FROM_SCAN	12

static struct bkey_s unsafe_bkey_s_c_to_s(struct bkey_s_c k)
{
	return (struct bkey_s) {{{
		(struct bkey *) k.k,
		(struct bch_val *) k.v
	}}};
}

static inline void __gc_pos_set(struct bch_fs *c, struct gc_pos new_pos)
{
	preempt_disable();
	write_seqcount_begin(&c->gc_pos_lock);
	c->gc_pos = new_pos;
	write_seqcount_end(&c->gc_pos_lock);
	preempt_enable();
}

static inline void gc_pos_set(struct bch_fs *c, struct gc_pos new_pos)
{
	BUG_ON(gc_pos_cmp(new_pos, c->gc_pos) < 0);
	__gc_pos_set(c, new_pos);
}

static void btree_ptr_to_v2(struct btree *b, struct bkey_i_btree_ptr_v2 *dst)
{
	switch (b->key.k.type) {
	case KEY_TYPE_btree_ptr: {
		struct bkey_i_btree_ptr *src = bkey_i_to_btree_ptr(&b->key);

		dst->k.p		= src->k.p;
		dst->v.mem_ptr		= 0;
		dst->v.seq		= b->data->keys.seq;
		dst->v.sectors_written	= 0;
		dst->v.flags		= 0;
		dst->v.min_key		= b->data->min_key;
		set_bkey_val_bytes(&dst->k, sizeof(dst->v) + bkey_val_bytes(&src->k));
		memcpy(dst->v.start, src->v.start, bkey_val_bytes(&src->k));
		break;
	}
	case KEY_TYPE_btree_ptr_v2:
		bkey_copy(&dst->k_i, &b->key);
		break;
	default:
		BUG();
	}
}

static int set_node_min(struct bch_fs *c, struct btree *b, struct bpos new_min)
{
	struct bkey_i_btree_ptr_v2 *new;
	int ret;

	if (c->opts.verbose) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		prt_str(&buf, " -> ");
		bch2_bpos_to_text(&buf, new_min);

		bch_info(c, "%s(): %s", __func__, buf.buf);
		printbuf_exit(&buf);
	}

	new = kmalloc_array(BKEY_BTREE_PTR_U64s_MAX, sizeof(u64), GFP_KERNEL);
	if (!new)
		return -BCH_ERR_ENOMEM_gc_repair_key;

	btree_ptr_to_v2(b, new);
	b->data->min_key	= new_min;
	new->v.min_key		= new_min;
	SET_BTREE_PTR_RANGE_UPDATED(&new->v, true);

	ret = bch2_journal_key_insert_take(c, b->c.btree_id, b->c.level + 1, &new->k_i);
	if (ret) {
		kfree(new);
		return ret;
	}

	bch2_btree_node_drop_keys_outside_node(b);
	bkey_copy(&b->key, &new->k_i);
	return 0;
}

static int set_node_max(struct bch_fs *c, struct btree *b, struct bpos new_max)
{
	struct bkey_i_btree_ptr_v2 *new;
	int ret;

	if (c->opts.verbose) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		prt_str(&buf, " -> ");
		bch2_bpos_to_text(&buf, new_max);

		bch_info(c, "%s(): %s", __func__, buf.buf);
		printbuf_exit(&buf);
	}

	ret = bch2_journal_key_delete(c, b->c.btree_id, b->c.level + 1, b->key.k.p);
	if (ret)
		return ret;

	new = kmalloc_array(BKEY_BTREE_PTR_U64s_MAX, sizeof(u64), GFP_KERNEL);
	if (!new)
		return -BCH_ERR_ENOMEM_gc_repair_key;

	btree_ptr_to_v2(b, new);
	b->data->max_key	= new_max;
	new->k.p		= new_max;
	SET_BTREE_PTR_RANGE_UPDATED(&new->v, true);

	ret = bch2_journal_key_insert_take(c, b->c.btree_id, b->c.level + 1, &new->k_i);
	if (ret) {
		kfree(new);
		return ret;
	}

	bch2_btree_node_drop_keys_outside_node(b);

	mutex_lock(&c->btree_cache.lock);
	bch2_btree_node_hash_remove(&c->btree_cache, b);

	bkey_copy(&b->key, &new->k_i);
	ret = __bch2_btree_node_hash_insert(&c->btree_cache, b);
	BUG_ON(ret);
	mutex_unlock(&c->btree_cache.lock);
	return 0;
}

static int btree_check_node_boundaries(struct bch_fs *c, struct btree *b,
				       struct btree *prev, struct btree *cur,
				       struct bpos *pulled_from_scan)
{
	struct bpos expected_start = !prev
		? b->data->min_key
		: bpos_successor(prev->key.k.p);
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	BUG_ON(b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
	       !bpos_eq(bkey_i_to_btree_ptr_v2(&b->key)->v.min_key,
			b->data->min_key));

	if (bpos_eq(expected_start, cur->data->min_key))
		return 0;

	prt_printf(&buf, "  at btree %s level %u:\n  parent: ",
		   bch2_btree_id_str(b->c.btree_id), b->c.level);
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));

	if (prev) {
		prt_printf(&buf, "\n  prev: ");
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&prev->key));
	}

	prt_str(&buf, "\n  next: ");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&cur->key));

	if (bpos_lt(expected_start, cur->data->min_key)) {				/* gap */
		if (b->c.level == 1 &&
		    bpos_lt(*pulled_from_scan, cur->data->min_key)) {
			ret = bch2_get_scanned_nodes(c, b->c.btree_id, 0,
						     expected_start,
						     bpos_predecessor(cur->data->min_key));
			if (ret)
				goto err;

			*pulled_from_scan = cur->data->min_key;
			ret = DID_FILL_FROM_SCAN;
		} else {
			if (mustfix_fsck_err(c, btree_node_topology_bad_min_key,
					     "btree node with incorrect min_key%s", buf.buf))
				ret = set_node_min(c, cur, expected_start);
		}
	} else {									/* overlap */
		if (prev && BTREE_NODE_SEQ(cur->data) > BTREE_NODE_SEQ(prev->data)) {	/* cur overwrites prev */
			if (bpos_ge(prev->data->min_key, cur->data->min_key)) {		/* fully? */
				if (mustfix_fsck_err(c, btree_node_topology_overwritten_by_next_node,
						     "btree node overwritten by next node%s", buf.buf))
					ret = DROP_PREV_NODE;
			} else {
				if (mustfix_fsck_err(c, btree_node_topology_bad_max_key,
						     "btree node with incorrect max_key%s", buf.buf))
					ret = set_node_max(c, prev,
							   bpos_predecessor(cur->data->min_key));
			}
		} else {
			if (bpos_ge(expected_start, cur->data->max_key)) {		/* fully? */
				if (mustfix_fsck_err(c, btree_node_topology_overwritten_by_prev_node,
						     "btree node overwritten by prev node%s", buf.buf))
					ret = DROP_THIS_NODE;
			} else {
				if (mustfix_fsck_err(c, btree_node_topology_bad_min_key,
						     "btree node with incorrect min_key%s", buf.buf))
					ret = set_node_min(c, cur, expected_start);
			}
		}
	}
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int btree_repair_node_end(struct bch_fs *c, struct btree *b,
				 struct btree *child, struct bpos *pulled_from_scan)
{
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (bpos_eq(child->key.k.p, b->key.k.p))
		return 0;

	prt_printf(&buf, "at btree %s level %u:\n  parent: ",
		   bch2_btree_id_str(b->c.btree_id), b->c.level);
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));

	prt_str(&buf, "\n  child: ");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&child->key));

	if (mustfix_fsck_err(c, btree_node_topology_bad_max_key,
			     "btree node with incorrect max_key%s", buf.buf)) {
		if (b->c.level == 1 &&
		    bpos_lt(*pulled_from_scan, b->key.k.p)) {
			ret = bch2_get_scanned_nodes(c, b->c.btree_id, 0,
						bpos_successor(child->key.k.p), b->key.k.p);
			if (ret)
				goto err;

			*pulled_from_scan = b->key.k.p;
			ret = DID_FILL_FROM_SCAN;
		} else {
			ret = set_node_max(c, child, b->key.k.p);
		}
	}
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int bch2_btree_repair_topology_recurse(struct btree_trans *trans, struct btree *b,
					      struct bpos *pulled_from_scan)
{
	struct bch_fs *c = trans->c;
	struct btree_and_journal_iter iter;
	struct bkey_s_c k;
	struct bkey_buf prev_k, cur_k;
	struct btree *prev = NULL, *cur = NULL;
	bool have_child, new_pass = false;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (!b->c.level)
		return 0;

	bch2_bkey_buf_init(&prev_k);
	bch2_bkey_buf_init(&cur_k);
again:
	cur = prev = NULL;
	have_child = new_pass = false;
	bch2_btree_and_journal_iter_init_node_iter(trans, &iter, b);
	iter.prefetch = true;

	while ((k = bch2_btree_and_journal_iter_peek(&iter)).k) {
		BUG_ON(bpos_lt(k.k->p, b->data->min_key));
		BUG_ON(bpos_gt(k.k->p, b->data->max_key));

		bch2_btree_and_journal_iter_advance(&iter);
		bch2_bkey_buf_reassemble(&cur_k, c, k);

		cur = bch2_btree_node_get_noiter(trans, cur_k.k,
					b->c.btree_id, b->c.level - 1,
					false);
		ret = PTR_ERR_OR_ZERO(cur);

		printbuf_reset(&buf);
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(cur_k.k));

		if (mustfix_fsck_err_on(bch2_err_matches(ret, EIO), c,
				btree_node_unreadable,
				"Topology repair: unreadable btree node at btree %s level %u:\n"
				"  %s",
				bch2_btree_id_str(b->c.btree_id),
				b->c.level - 1,
				buf.buf)) {
			bch2_btree_node_evict(trans, cur_k.k);
			cur = NULL;
			ret = bch2_journal_key_delete(c, b->c.btree_id,
						      b->c.level, cur_k.k->k.p);
			if (ret)
				break;

			if (!btree_id_is_alloc(b->c.btree_id)) {
				ret = bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_scan_for_btree_nodes);
				if (ret)
					break;
			}
			continue;
		}

		bch_err_msg(c, ret, "getting btree node");
		if (ret)
			break;

		if (bch2_btree_node_is_stale(c, cur)) {
			bch_info(c, "btree node %s older than nodes found by scanning", buf.buf);
			six_unlock_read(&cur->c.lock);
			bch2_btree_node_evict(trans, cur_k.k);
			ret = bch2_journal_key_delete(c, b->c.btree_id,
						      b->c.level, cur_k.k->k.p);
			cur = NULL;
			if (ret)
				break;
			continue;
		}

		ret = btree_check_node_boundaries(c, b, prev, cur, pulled_from_scan);
		if (ret == DID_FILL_FROM_SCAN) {
			new_pass = true;
			ret = 0;
		}

		if (ret == DROP_THIS_NODE) {
			six_unlock_read(&cur->c.lock);
			bch2_btree_node_evict(trans, cur_k.k);
			ret = bch2_journal_key_delete(c, b->c.btree_id,
						      b->c.level, cur_k.k->k.p);
			cur = NULL;
			if (ret)
				break;
			continue;
		}

		if (prev)
			six_unlock_read(&prev->c.lock);
		prev = NULL;

		if (ret == DROP_PREV_NODE) {
			bch_info(c, "dropped prev node");
			bch2_btree_node_evict(trans, prev_k.k);
			ret = bch2_journal_key_delete(c, b->c.btree_id,
						      b->c.level, prev_k.k->k.p);
			if (ret)
				break;

			bch2_btree_and_journal_iter_exit(&iter);
			goto again;
		} else if (ret)
			break;

		prev = cur;
		cur = NULL;
		bch2_bkey_buf_copy(&prev_k, c, cur_k.k);
	}

	if (!ret && !IS_ERR_OR_NULL(prev)) {
		BUG_ON(cur);
		ret = btree_repair_node_end(c, b, prev, pulled_from_scan);
		if (ret == DID_FILL_FROM_SCAN) {
			new_pass = true;
			ret = 0;
		}
	}

	if (!IS_ERR_OR_NULL(prev))
		six_unlock_read(&prev->c.lock);
	prev = NULL;
	if (!IS_ERR_OR_NULL(cur))
		six_unlock_read(&cur->c.lock);
	cur = NULL;

	if (ret)
		goto err;

	bch2_btree_and_journal_iter_exit(&iter);

	if (new_pass)
		goto again;

	bch2_btree_and_journal_iter_init_node_iter(trans, &iter, b);
	iter.prefetch = true;

	while ((k = bch2_btree_and_journal_iter_peek(&iter)).k) {
		bch2_bkey_buf_reassemble(&cur_k, c, k);
		bch2_btree_and_journal_iter_advance(&iter);

		cur = bch2_btree_node_get_noiter(trans, cur_k.k,
					b->c.btree_id, b->c.level - 1,
					false);
		ret = PTR_ERR_OR_ZERO(cur);

		bch_err_msg(c, ret, "getting btree node");
		if (ret)
			goto err;

		ret = bch2_btree_repair_topology_recurse(trans, cur, pulled_from_scan);
		six_unlock_read(&cur->c.lock);
		cur = NULL;

		if (ret == DROP_THIS_NODE) {
			bch2_btree_node_evict(trans, cur_k.k);
			ret = bch2_journal_key_delete(c, b->c.btree_id,
						      b->c.level, cur_k.k->k.p);
			new_pass = true;
		}

		if (ret)
			goto err;

		have_child = true;
	}

	printbuf_reset(&buf);
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));

	if (mustfix_fsck_err_on(!have_child, c,
			btree_node_topology_interior_node_empty,
			"empty interior btree node at btree %s level %u\n"
			"  %s",
			bch2_btree_id_str(b->c.btree_id),
			b->c.level, buf.buf))
		ret = DROP_THIS_NODE;
err:
fsck_err:
	if (!IS_ERR_OR_NULL(prev))
		six_unlock_read(&prev->c.lock);
	if (!IS_ERR_OR_NULL(cur))
		six_unlock_read(&cur->c.lock);

	bch2_btree_and_journal_iter_exit(&iter);

	if (!ret && new_pass)
		goto again;

	BUG_ON(!ret && bch2_btree_node_check_topology(trans, b));

	bch2_bkey_buf_exit(&prev_k, c);
	bch2_bkey_buf_exit(&cur_k, c);
	printbuf_exit(&buf);
	return ret;
}

int bch2_check_topology(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bpos pulled_from_scan = POS_MIN;
	int ret = 0;

	for (unsigned i = 0; i < btree_id_nr_alive(c) && !ret; i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);
		bool reconstructed_root = false;

		if (r->error) {
			ret = bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_scan_for_btree_nodes);
			if (ret)
				break;
reconstruct_root:
			bch_info(c, "btree root %s unreadable, must recover from scan", bch2_btree_id_str(i));

			r->alive = false;
			r->error = 0;

			if (!bch2_btree_has_scanned_nodes(c, i)) {
				mustfix_fsck_err(c, btree_root_unreadable_and_scan_found_nothing,
						 "no nodes found for btree %s, continue?", bch2_btree_id_str(i));
				bch2_btree_root_alloc_fake_trans(trans, i, 0);
			} else {
				bch2_btree_root_alloc_fake_trans(trans, i, 1);
				bch2_shoot_down_journal_keys(c, i, 1, BTREE_MAX_DEPTH, POS_MIN, SPOS_MAX);
				ret = bch2_get_scanned_nodes(c, i, 0, POS_MIN, SPOS_MAX);
				if (ret)
					break;
			}

			reconstructed_root = true;
		}

		struct btree *b = r->b;

		btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_read);
		ret = bch2_btree_repair_topology_recurse(trans, b, &pulled_from_scan);
		six_unlock_read(&b->c.lock);

		if (ret == DROP_THIS_NODE) {
			bch2_btree_node_hash_remove(&c->btree_cache, b);
			mutex_lock(&c->btree_cache.lock);
			list_move(&b->list, &c->btree_cache.freeable);
			mutex_unlock(&c->btree_cache.lock);

			r->b = NULL;

			if (!reconstructed_root)
				goto reconstruct_root;

			bch_err(c, "empty btree root %s", bch2_btree_id_str(i));
			bch2_btree_root_alloc_fake_trans(trans, i, 0);
			r->alive = false;
			ret = 0;
		}
	}
fsck_err:
	bch2_trans_put(trans);
	return ret;
}

/* marking of btree keys/nodes: */

static int bch2_gc_mark_key(struct btree_trans *trans, enum btree_id btree_id,
			    unsigned level, struct btree **prev,
			    struct btree_iter *iter, struct bkey_s_c k,
			    bool initial)
{
	struct bch_fs *c = trans->c;

	if (iter) {
		struct btree_path *path = btree_iter_path(trans, iter);
		struct btree *b = path_l(path)->b;

		if (*prev != b) {
			int ret = bch2_btree_node_check_topology(trans, b);
			if (ret)
				return ret;
		}
		*prev = b;
	}

	struct bkey deleted = KEY(0, 0, 0);
	struct bkey_s_c old = (struct bkey_s_c) { &deleted, NULL };
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	deleted.p = k.k->p;

	if (initial) {
		BUG_ON(bch2_journal_seq_verify &&
		       k.k->version.lo > atomic64_read(&c->journal.seq));

		if (fsck_err_on(k.k->version.lo > atomic64_read(&c->key_version), c,
				bkey_version_in_future,
				"key version number higher than recorded %llu\n  %s",
				atomic64_read(&c->key_version),
				(bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			atomic64_set(&c->key_version, k.k->version.lo);
	}

	if (mustfix_fsck_err_on(level && !bch2_dev_btree_bitmap_marked(c, k),
				c, btree_bitmap_not_marked,
				"btree ptr not marked in member info btree allocated bitmap\n  %s",
				(printbuf_reset(&buf),
				 bch2_bkey_val_to_text(&buf, c, k),
				 buf.buf))) {
		mutex_lock(&c->sb_lock);
		bch2_dev_btree_bitmap_mark(c, k);
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	/*
	 * We require a commit before key_trigger() because
	 * key_trigger(BTREE_TRIGGER_GC) is not idempotant; we'll calculate the
	 * wrong result if we run it multiple times.
	 */
	unsigned flags = !iter ? BTREE_TRIGGER_is_root : 0;

	ret = bch2_key_trigger(trans, btree_id, level, old, unsafe_bkey_s_c_to_s(k),
			       BTREE_TRIGGER_check_repair|flags);
	if (ret)
		goto out;

	if (trans->nr_updates) {
		ret = bch2_trans_commit(trans, NULL, NULL, 0) ?:
			-BCH_ERR_transaction_restart_nested;
		goto out;
	}

	ret = bch2_key_trigger(trans, btree_id, level, old, unsafe_bkey_s_c_to_s(k),
			       BTREE_TRIGGER_gc|flags);
out:
fsck_err:
	printbuf_exit(&buf);
	bch_err_fn(c, ret);
	return ret;
}

static int bch2_gc_btree(struct btree_trans *trans, enum btree_id btree, bool initial)
{
	struct bch_fs *c = trans->c;
	int level = 0, target_depth = btree_node_type_needs_gc(__btree_node_type(0, btree)) ? 0 : 1;
	int ret = 0;

	/* We need to make sure every leaf node is readable before going RW */
	if (initial)
		target_depth = 0;

	/* root */
	mutex_lock(&c->btree_root_lock);
	struct btree *b = bch2_btree_id_root(c, btree)->b;
	if (!btree_node_fake(b)) {
		gc_pos_set(c, gc_pos_btree(btree, b->c.level + 1, SPOS_MAX));
		ret = lockrestart_do(trans,
			bch2_gc_mark_key(trans, b->c.btree_id, b->c.level + 1,
					 NULL, NULL, bkey_i_to_s_c(&b->key), initial));
		level = b->c.level;
	}
	mutex_unlock(&c->btree_root_lock);

	if (ret)
		return ret;

	for (; level >= target_depth; --level) {
		struct btree *prev = NULL;
		struct btree_iter iter;
		bch2_trans_node_iter_init(trans, &iter, btree, POS_MIN, 0, level,
					  BTREE_ITER_prefetch);

		ret = for_each_btree_key_continue(trans, iter, 0, k, ({
			gc_pos_set(c, gc_pos_btree(btree, level, k.k->p));
			bch2_gc_mark_key(trans, btree, level, &prev, &iter, k, initial);
		}));
		if (ret)
			break;
	}

	return ret;
}

static inline int btree_id_gc_phase_cmp(enum btree_id l, enum btree_id r)
{
	return cmp_int(gc_btree_order(l), gc_btree_order(r));
}

static int bch2_gc_btrees(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	enum btree_id ids[BTREE_ID_NR];
	unsigned i;
	int ret = 0;

	for (i = 0; i < BTREE_ID_NR; i++)
		ids[i] = i;
	bubble_sort(ids, BTREE_ID_NR, btree_id_gc_phase_cmp);

	for (i = 0; i < btree_id_nr_alive(c) && !ret; i++) {
		unsigned btree = i < BTREE_ID_NR ? ids[i] : i;

		if (IS_ERR_OR_NULL(bch2_btree_id_root(c, btree)->b))
			continue;

		ret = bch2_gc_btree(trans, btree, true);

		if (mustfix_fsck_err_on(bch2_err_matches(ret, EIO),
					c, btree_node_read_error,
			       "btree node read error for %s",
			       bch2_btree_id_str(btree)))
			ret = bch2_run_explicit_recovery_pass(c, BCH_RECOVERY_PASS_check_topology);
	}
fsck_err:
	bch2_trans_put(trans);
	bch_err_fn(c, ret);
	return ret;
}

static int bch2_mark_superblocks(struct bch_fs *c)
{
	mutex_lock(&c->sb_lock);
	gc_pos_set(c, gc_phase(GC_PHASE_sb));

	int ret = bch2_trans_mark_dev_sbs_flags(c, BTREE_TRIGGER_gc);
	mutex_unlock(&c->sb_lock);
	return ret;
}

static void bch2_gc_free(struct bch_fs *c)
{
	genradix_free(&c->reflink_gc_table);
	genradix_free(&c->gc_stripes);

	for_each_member_device(c, ca) {
		kvfree(rcu_dereference_protected(ca->buckets_gc, 1));
		ca->buckets_gc = NULL;

		free_percpu(ca->usage_gc);
		ca->usage_gc = NULL;
	}

	free_percpu(c->usage_gc);
	c->usage_gc = NULL;
}

static int bch2_gc_done(struct bch_fs *c)
{
	struct bch_dev *ca = NULL;
	struct printbuf buf = PRINTBUF;
	unsigned i;
	int ret = 0;

	percpu_down_write(&c->mark_lock);

#define copy_field(_err, _f, _msg, ...)						\
	if (fsck_err_on(dst->_f != src->_f, c, _err,				\
			_msg ": got %llu, should be %llu" , ##__VA_ARGS__,	\
			dst->_f, src->_f))					\
		dst->_f = src->_f
#define copy_dev_field(_err, _f, _msg, ...)					\
	copy_field(_err, _f, "dev %u has wrong " _msg, ca->dev_idx, ##__VA_ARGS__)
#define copy_fs_field(_err, _f, _msg, ...)					\
	copy_field(_err, _f, "fs has wrong " _msg, ##__VA_ARGS__)

	for (i = 0; i < ARRAY_SIZE(c->usage); i++)
		bch2_fs_usage_acc_to_base(c, i);

	__for_each_member_device(c, ca) {
		struct bch_dev_usage *dst = ca->usage_base;
		struct bch_dev_usage *src = (void *)
			bch2_acc_percpu_u64s((u64 __percpu *) ca->usage_gc,
					     dev_usage_u64s());

		for (i = 0; i < BCH_DATA_NR; i++) {
			copy_dev_field(dev_usage_buckets_wrong,
				       d[i].buckets,	"%s buckets", bch2_data_type_str(i));
			copy_dev_field(dev_usage_sectors_wrong,
				       d[i].sectors,	"%s sectors", bch2_data_type_str(i));
			copy_dev_field(dev_usage_fragmented_wrong,
				       d[i].fragmented,	"%s fragmented", bch2_data_type_str(i));
		}
	}

	{
		unsigned nr = fs_usage_u64s(c);
		struct bch_fs_usage *dst = c->usage_base;
		struct bch_fs_usage *src = (void *)
			bch2_acc_percpu_u64s((u64 __percpu *) c->usage_gc, nr);

		copy_fs_field(fs_usage_hidden_wrong,
			      b.hidden,		"hidden");
		copy_fs_field(fs_usage_btree_wrong,
			      b.btree,		"btree");

		copy_fs_field(fs_usage_data_wrong,
			      b.data,	"data");
		copy_fs_field(fs_usage_cached_wrong,
			      b.cached,	"cached");
		copy_fs_field(fs_usage_reserved_wrong,
			      b.reserved,	"reserved");
		copy_fs_field(fs_usage_nr_inodes_wrong,
			      b.nr_inodes,"nr_inodes");

		for (i = 0; i < BCH_REPLICAS_MAX; i++)
			copy_fs_field(fs_usage_persistent_reserved_wrong,
				      persistent_reserved[i],
				      "persistent_reserved[%i]", i);

		for (i = 0; i < c->replicas.nr; i++) {
			struct bch_replicas_entry_v1 *e =
				cpu_replicas_entry(&c->replicas, i);

			printbuf_reset(&buf);
			bch2_replicas_entry_to_text(&buf, e);

			copy_fs_field(fs_usage_replicas_wrong,
				      replicas[i], "%s", buf.buf);
		}
	}

#undef copy_fs_field
#undef copy_dev_field
#undef copy_stripe_field
#undef copy_field
fsck_err:
	bch2_dev_put(ca);
	bch_err_fn(c, ret);
	percpu_up_write(&c->mark_lock);
	printbuf_exit(&buf);
	return ret;
}

static int bch2_gc_start(struct bch_fs *c)
{
	BUG_ON(c->usage_gc);

	c->usage_gc = __alloc_percpu_gfp(fs_usage_u64s(c) * sizeof(u64),
					 sizeof(u64), GFP_KERNEL);
	if (!c->usage_gc) {
		bch_err(c, "error allocating c->usage_gc");
		return -BCH_ERR_ENOMEM_gc_start;
	}

	for_each_member_device(c, ca) {
		BUG_ON(ca->usage_gc);

		ca->usage_gc = alloc_percpu(struct bch_dev_usage);
		if (!ca->usage_gc) {
			bch_err(c, "error allocating ca->usage_gc");
			bch2_dev_put(ca);
			return -BCH_ERR_ENOMEM_gc_start;
		}

		this_cpu_write(ca->usage_gc->d[BCH_DATA_free].buckets,
			       ca->mi.nbuckets - ca->mi.first_bucket);
	}

	return 0;
}

/* returns true if not equal */
static inline bool bch2_alloc_v4_cmp(struct bch_alloc_v4 l,
				     struct bch_alloc_v4 r)
{
	return  l.gen != r.gen				||
		l.oldest_gen != r.oldest_gen		||
		l.data_type != r.data_type		||
		l.dirty_sectors	!= r.dirty_sectors	||
		l.cached_sectors != r.cached_sectors	 ||
		l.stripe_redundancy != r.stripe_redundancy ||
		l.stripe != r.stripe;
}

static int bch2_alloc_write_key(struct btree_trans *trans,
				struct btree_iter *iter,
				struct bch_dev *ca,
				struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_i_alloc_v4 *a;
	struct bch_alloc_v4 old_gc, gc, old_convert, new;
	const struct bch_alloc_v4 *old;
	int ret;

	if (!bucket_valid(ca, k.k->p.offset))
		return 0;

	old = bch2_alloc_to_v4(k, &old_convert);
	gc = new = *old;

	percpu_down_read(&c->mark_lock);
	__bucket_m_to_alloc(&gc, *gc_bucket(ca, iter->pos.offset));

	old_gc = gc;

	if ((old->data_type == BCH_DATA_sb ||
	     old->data_type == BCH_DATA_journal) &&
	    !bch2_dev_is_online(ca)) {
		gc.data_type = old->data_type;
		gc.dirty_sectors = old->dirty_sectors;
	}

	/*
	 * gc.data_type doesn't yet include need_discard & need_gc_gen states -
	 * fix that here:
	 */
	alloc_data_type_set(&gc, gc.data_type);

	if (gc.data_type != old_gc.data_type ||
	    gc.dirty_sectors != old_gc.dirty_sectors)
		bch2_dev_usage_update(c, ca, &old_gc, &gc, 0, true);
	percpu_up_read(&c->mark_lock);

	if (fsck_err_on(new.data_type != gc.data_type, c,
			alloc_key_data_type_wrong,
			"bucket %llu:%llu gen %u has wrong data_type"
			": got %s, should be %s",
			iter->pos.inode, iter->pos.offset,
			gc.gen,
			bch2_data_type_str(new.data_type),
			bch2_data_type_str(gc.data_type)))
		new.data_type = gc.data_type;

#define copy_bucket_field(_errtype, _f)					\
	if (fsck_err_on(new._f != gc._f, c, _errtype,			\
			"bucket %llu:%llu gen %u data type %s has wrong " #_f	\
			": got %u, should be %u",			\
			iter->pos.inode, iter->pos.offset,		\
			gc.gen,						\
			bch2_data_type_str(gc.data_type),		\
			new._f, gc._f))					\
		new._f = gc._f;						\

	copy_bucket_field(alloc_key_gen_wrong,
			  gen);
	copy_bucket_field(alloc_key_dirty_sectors_wrong,
			  dirty_sectors);
	copy_bucket_field(alloc_key_cached_sectors_wrong,
			  cached_sectors);
	copy_bucket_field(alloc_key_stripe_wrong,
			  stripe);
	copy_bucket_field(alloc_key_stripe_redundancy_wrong,
			  stripe_redundancy);
#undef copy_bucket_field

	if (!bch2_alloc_v4_cmp(*old, new))
		return 0;

	a = bch2_alloc_to_v4_mut(trans, k);
	ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		return ret;

	a->v = new;

	/*
	 * The trigger normally makes sure this is set, but we're not running
	 * triggers:
	 */
	if (a->v.data_type == BCH_DATA_cached && !a->v.io_time[READ])
		a->v.io_time[READ] = max_t(u64, 1, atomic64_read(&c->io_clock[READ].now));

	ret = bch2_trans_update(trans, iter, &a->k_i, BTREE_TRIGGER_norun);
fsck_err:
	return ret;
}

static int bch2_gc_alloc_done(struct bch_fs *c)
{
	int ret = 0;

	for_each_member_device(c, ca) {
		ret = bch2_trans_run(c,
			for_each_btree_key_upto_commit(trans, iter, BTREE_ID_alloc,
					POS(ca->dev_idx, ca->mi.first_bucket),
					POS(ca->dev_idx, ca->mi.nbuckets - 1),
					BTREE_ITER_slots|BTREE_ITER_prefetch, k,
					NULL, NULL, BCH_TRANS_COMMIT_lazy_rw,
				bch2_alloc_write_key(trans, &iter, ca, k)));
		if (ret) {
			bch2_dev_put(ca);
			break;
		}
	}

	bch_err_fn(c, ret);
	return ret;
}

static int bch2_gc_alloc_start(struct bch_fs *c)
{
	for_each_member_device(c, ca) {
		struct bucket_array *buckets = kvmalloc(sizeof(struct bucket_array) +
				ca->mi.nbuckets * sizeof(struct bucket),
				GFP_KERNEL|__GFP_ZERO);
		if (!buckets) {
			bch2_dev_put(ca);
			bch_err(c, "error allocating ca->buckets[gc]");
			return -BCH_ERR_ENOMEM_gc_alloc_start;
		}

		buckets->first_bucket	= ca->mi.first_bucket;
		buckets->nbuckets	= ca->mi.nbuckets;
		buckets->nbuckets_minus_first =
			buckets->nbuckets - buckets->first_bucket;
		rcu_assign_pointer(ca->buckets_gc, buckets);
	}

	struct bch_dev *ca = NULL;
	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_alloc, POS_MIN,
					 BTREE_ITER_prefetch, k, ({
			ca = bch2_dev_iterate(c, ca, k.k->p.inode);
			if (!ca) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode + 1, 0));
				continue;
			}

			if (bucket_valid(ca, k.k->p.offset)) {
				struct bch_alloc_v4 a_convert;
				const struct bch_alloc_v4 *a = bch2_alloc_to_v4(k, &a_convert);

				struct bucket *g = gc_bucket(ca, k.k->p.offset);
				g->gen_valid	= 1;
				g->gen		= a->gen;
			}
			0;
		})));
	bch2_dev_put(ca);
	bch_err_fn(c, ret);
	return ret;
}

static int bch2_gc_write_reflink_key(struct btree_trans *trans,
				     struct btree_iter *iter,
				     struct bkey_s_c k,
				     size_t *idx)
{
	struct bch_fs *c = trans->c;
	const __le64 *refcount = bkey_refcount_c(k);
	struct printbuf buf = PRINTBUF;
	struct reflink_gc *r;
	int ret = 0;

	if (!refcount)
		return 0;

	while ((r = genradix_ptr(&c->reflink_gc_table, *idx)) &&
	       r->offset < k.k->p.offset)
		++*idx;

	if (!r ||
	    r->offset != k.k->p.offset ||
	    r->size != k.k->size) {
		bch_err(c, "unexpected inconsistency walking reflink table at gc finish");
		return -EINVAL;
	}

	if (fsck_err_on(r->refcount != le64_to_cpu(*refcount), c,
			reflink_v_refcount_wrong,
			"reflink key has wrong refcount:\n"
			"  %s\n"
			"  should be %u",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf),
			r->refcount)) {
		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			goto out;

		if (!r->refcount)
			new->k.type = KEY_TYPE_deleted;
		else
			*bkey_refcount(bkey_i_to_s(new)) = cpu_to_le64(r->refcount);
		ret = bch2_trans_update(trans, iter, new, 0);
	}
out:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int bch2_gc_reflink_done(struct bch_fs *c)
{
	size_t idx = 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_reflink, POS_MIN,
				BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_gc_write_reflink_key(trans, &iter, k, &idx)));
	c->reflink_gc_nr = 0;
	return ret;
}

static int bch2_gc_reflink_start(struct bch_fs *c)
{
	c->reflink_gc_nr = 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_reflink, POS_MIN,
				   BTREE_ITER_prefetch, k, ({
			const __le64 *refcount = bkey_refcount_c(k);

			if (!refcount)
				continue;

			struct reflink_gc *r = genradix_ptr_alloc(&c->reflink_gc_table,
							c->reflink_gc_nr++, GFP_KERNEL);
			if (!r) {
				ret = -BCH_ERR_ENOMEM_gc_reflink_start;
				break;
			}

			r->offset	= k.k->p.offset;
			r->size		= k.k->size;
			r->refcount	= 0;
			0;
		})));

	bch_err_fn(c, ret);
	return ret;
}

static int bch2_gc_write_stripes_key(struct btree_trans *trans,
				     struct btree_iter *iter,
				     struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	const struct bch_stripe *s;
	struct gc_stripe *m;
	bool bad = false;
	unsigned i;
	int ret = 0;

	if (k.k->type != KEY_TYPE_stripe)
		return 0;

	s = bkey_s_c_to_stripe(k).v;
	m = genradix_ptr(&c->gc_stripes, k.k->p.offset);

	for (i = 0; i < s->nr_blocks; i++) {
		u32 old = stripe_blockcount_get(s, i);
		u32 new = (m ? m->block_sectors[i] : 0);

		if (old != new) {
			prt_printf(&buf, "stripe block %u has wrong sector count: got %u, should be %u\n",
				   i, old, new);
			bad = true;
		}
	}

	if (bad)
		bch2_bkey_val_to_text(&buf, c, k);

	if (fsck_err_on(bad, c, stripe_sector_count_wrong,
			"%s", buf.buf)) {
		struct bkey_i_stripe *new;

		new = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			return ret;

		bkey_reassemble(&new->k_i, k);

		for (i = 0; i < new->v.nr_blocks; i++)
			stripe_blockcount_set(&new->v, i, m ? m->block_sectors[i] : 0);

		ret = bch2_trans_update(trans, iter, &new->k_i, 0);
	}
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int bch2_gc_stripes_done(struct bch_fs *c)
{
	return bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_stripes, POS_MIN,
				BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_gc_write_stripes_key(trans, &iter, k)));
}

/**
 * bch2_check_allocations - walk all references to buckets, and recompute them:
 *
 * @c:			filesystem object
 *
 * Returns: 0 on success, or standard errcode on failure
 *
 * Order matters here:
 *  - Concurrent GC relies on the fact that we have a total ordering for
 *    everything that GC walks - see  gc_will_visit_node(),
 *    gc_will_visit_root()
 *
 *  - also, references move around in the course of index updates and
 *    various other crap: everything needs to agree on the ordering
 *    references are allowed to move around in - e.g., we're allowed to
 *    start with a reference owned by an open_bucket (the allocator) and
 *    move it to the btree, but not the reverse.
 *
 *    This is necessary to ensure that gc doesn't miss references that
 *    move around - if references move backwards in the ordering GC
 *    uses, GC could skip past them
 */
int bch2_check_allocations(struct bch_fs *c)
{
	int ret;

	lockdep_assert_held(&c->state_lock);

	down_write(&c->gc_lock);

	bch2_btree_interior_updates_flush(c);

	ret   = bch2_gc_start(c) ?:
		bch2_gc_alloc_start(c) ?:
		bch2_gc_reflink_start(c);
	if (ret)
		goto out;

	gc_pos_set(c, gc_phase(GC_PHASE_start));

	ret = bch2_mark_superblocks(c);
	BUG_ON(ret);

	ret = bch2_gc_btrees(c);
	if (ret)
		goto out;

	c->gc_count++;

	bch2_journal_block(&c->journal);
out:
	ret   = bch2_gc_alloc_done(c) ?:
		bch2_gc_done(c) ?:
		bch2_gc_stripes_done(c) ?:
		bch2_gc_reflink_done(c);

	bch2_journal_unblock(&c->journal);

	percpu_down_write(&c->mark_lock);
	/* Indicates that gc is no longer in progress: */
	__gc_pos_set(c, gc_phase(GC_PHASE_not_running));

	bch2_gc_free(c);
	percpu_up_write(&c->mark_lock);

	up_write(&c->gc_lock);

	/*
	 * At startup, allocations can happen directly instead of via the
	 * allocator thread - issue wakeup in case they blocked on gc_lock:
	 */
	closure_wake_up(&c->freelist_wait);
	bch_err_fn(c, ret);
	return ret;
}

static int gc_btree_gens_key(struct btree_trans *trans,
			     struct btree_iter *iter,
			     struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bkey_i *u;
	int ret;

	if (unlikely(test_bit(BCH_FS_going_ro, &c->flags)))
		return -EROFS;

	percpu_down_read(&c->mark_lock);
	rcu_read_lock();
	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
		if (!ca)
			continue;

		if (dev_ptr_stale(ca, ptr) > 16) {
			rcu_read_unlock();
			percpu_up_read(&c->mark_lock);
			goto update;
		}
	}

	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
		if (!ca)
			continue;

		u8 *gen = &ca->oldest_gen[PTR_BUCKET_NR(ca, ptr)];
		if (gen_after(*gen, ptr->gen))
			*gen = ptr->gen;
	}
	rcu_read_unlock();
	percpu_up_read(&c->mark_lock);
	return 0;
update:
	u = bch2_bkey_make_mut(trans, iter, &k, 0);
	ret = PTR_ERR_OR_ZERO(u);
	if (ret)
		return ret;

	bch2_extent_normalize(c, bkey_i_to_s(u));
	return 0;
}

static int bch2_alloc_write_oldest_gen(struct btree_trans *trans, struct bch_dev *ca,
				       struct btree_iter *iter, struct bkey_s_c k)
{
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(k, &a_convert);
	struct bkey_i_alloc_v4 *a_mut;
	int ret;

	if (a->oldest_gen == ca->oldest_gen[iter->pos.offset])
		return 0;

	a_mut = bch2_alloc_to_v4_mut(trans, k);
	ret = PTR_ERR_OR_ZERO(a_mut);
	if (ret)
		return ret;

	a_mut->v.oldest_gen = ca->oldest_gen[iter->pos.offset];
	alloc_data_type_set(&a_mut->v, a_mut->v.data_type);

	return bch2_trans_update(trans, iter, &a_mut->k_i, 0);
}

int bch2_gc_gens(struct bch_fs *c)
{
	u64 b, start_time = local_clock();
	int ret;

	/*
	 * Ideally we would be using state_lock and not gc_lock here, but that
	 * introduces a deadlock in the RO path - we currently take the state
	 * lock at the start of going RO, thus the gc thread may get stuck:
	 */
	if (!mutex_trylock(&c->gc_gens_lock))
		return 0;

	trace_and_count(c, gc_gens_start, c);
	down_read(&c->gc_lock);

	for_each_member_device(c, ca) {
		struct bucket_gens *gens = bucket_gens(ca);

		BUG_ON(ca->oldest_gen);

		ca->oldest_gen = kvmalloc(gens->nbuckets, GFP_KERNEL);
		if (!ca->oldest_gen) {
			bch2_dev_put(ca);
			ret = -BCH_ERR_ENOMEM_gc_gens;
			goto err;
		}

		for (b = gens->first_bucket;
		     b < gens->nbuckets; b++)
			ca->oldest_gen[b] = gens->b[b];
	}

	for (unsigned i = 0; i < BTREE_ID_NR; i++)
		if (btree_type_has_ptrs(i)) {
			c->gc_gens_btree = i;
			c->gc_gens_pos = POS_MIN;

			ret = bch2_trans_run(c,
				for_each_btree_key_commit(trans, iter, i,
						POS_MIN,
						BTREE_ITER_prefetch|BTREE_ITER_all_snapshots,
						k,
						NULL, NULL,
						BCH_TRANS_COMMIT_no_enospc,
					gc_btree_gens_key(trans, &iter, k)));
			if (ret)
				goto err;
		}

	struct bch_dev *ca = NULL;
	ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter, BTREE_ID_alloc,
				POS_MIN,
				BTREE_ITER_prefetch,
				k,
				NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc, ({
			ca = bch2_dev_iterate(c, ca, k.k->p.inode);
			if (!ca) {
				bch2_btree_iter_set_pos(&iter, POS(k.k->p.inode + 1, 0));
				continue;
			}
			bch2_alloc_write_oldest_gen(trans, ca, &iter, k);
		})));
	bch2_dev_put(ca);

	if (ret)
		goto err;

	c->gc_gens_btree	= 0;
	c->gc_gens_pos		= POS_MIN;

	c->gc_count++;

	bch2_time_stats_update(&c->times[BCH_TIME_btree_gc], start_time);
	trace_and_count(c, gc_gens_end, c);
err:
	for_each_member_device(c, ca) {
		kvfree(ca->oldest_gen);
		ca->oldest_gen = NULL;
	}

	up_read(&c->gc_lock);
	mutex_unlock(&c->gc_gens_lock);
	if (!bch2_err_matches(ret, EROFS))
		bch_err_fn(c, ret);
	return ret;
}

static void bch2_gc_gens_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, gc_gens_work);
	bch2_gc_gens(c);
	bch2_write_ref_put(c, BCH_WRITE_REF_gc_gens);
}

void bch2_gc_gens_async(struct bch_fs *c)
{
	if (bch2_write_ref_tryget(c, BCH_WRITE_REF_gc_gens) &&
	    !queue_work(c->write_ref_wq, &c->gc_gens_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_gc_gens);
}

void bch2_fs_gc_init(struct bch_fs *c)
{
	seqcount_init(&c->gc_pos_lock);

	INIT_WORK(&c->gc_gens_work, bch2_gc_gens_work);
}
