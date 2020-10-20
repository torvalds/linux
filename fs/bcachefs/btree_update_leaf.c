// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "buckets.h"
#include "debug.h"
#include "error.h"
#include "extent_update.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "replicas.h"
#include "trace.h"

#include <linux/prefetch.h>
#include <linux/sort.h>

static inline bool same_leaf_as_prev(struct btree_trans *trans,
				     struct btree_insert_entry *i)
{
	return i != trans->updates2 &&
		iter_l(i[0].iter)->b == iter_l(i[-1].iter)->b;
}

inline void bch2_btree_node_lock_for_insert(struct bch_fs *c, struct btree *b,
					    struct btree_iter *iter)
{
	bch2_btree_node_lock_write(b, iter);

	if (btree_iter_type(iter) == BTREE_ITER_CACHED)
		return;

	if (unlikely(btree_node_just_written(b)) &&
	    bch2_btree_post_write_cleanup(c, b))
		bch2_btree_iter_reinit_node(iter, b);

	/*
	 * If the last bset has been written, or if it's gotten too big - start
	 * a new bset to insert into:
	 */
	if (want_new_bset(c, b))
		bch2_btree_init_next(c, b, iter);
}

/* Inserting into a given leaf node (last stage of insert): */

/* Handle overwrites and do insert, for non extents: */
bool bch2_btree_bset_insert_key(struct btree_iter *iter,
				struct btree *b,
				struct btree_node_iter *node_iter,
				struct bkey_i *insert)
{
	struct bkey_packed *k;
	unsigned clobber_u64s = 0, new_u64s = 0;

	EBUG_ON(btree_node_just_written(b));
	EBUG_ON(bset_written(b, btree_bset_last(b)));
	EBUG_ON(bkey_deleted(&insert->k) && bkey_val_u64s(&insert->k));
	EBUG_ON(bkey_cmp(b->data->min_key, POS_MIN) &&
		bkey_cmp(bkey_start_pos(&insert->k),
			 bkey_predecessor(b->data->min_key)) < 0);
	EBUG_ON(bkey_cmp(insert->k.p, b->data->min_key) < 0);
	EBUG_ON(bkey_cmp(insert->k.p, b->data->max_key) > 0);
	EBUG_ON(insert->k.u64s >
		bch_btree_keys_u64s_remaining(iter->trans->c, b));
	EBUG_ON(iter->flags & BTREE_ITER_IS_EXTENTS);

	k = bch2_btree_node_iter_peek_all(node_iter, b);
	if (k && bkey_cmp_packed(b, k, &insert->k))
		k = NULL;

	/* @k is the key being overwritten/deleted, if any: */
	EBUG_ON(k && bkey_whiteout(k));

	/* Deleting, but not found? nothing to do: */
	if (bkey_whiteout(&insert->k) && !k)
		return false;

	if (bkey_whiteout(&insert->k)) {
		/* Deleting: */
		btree_account_key_drop(b, k);
		k->type = KEY_TYPE_deleted;

		if (k->needs_whiteout)
			push_whiteout(iter->trans->c, b, insert->k.p);
		k->needs_whiteout = false;

		if (k >= btree_bset_last(b)->start) {
			clobber_u64s = k->u64s;
			bch2_bset_delete(b, k, clobber_u64s);
			goto fix_iter;
		} else {
			bch2_btree_iter_fix_key_modified(iter, b, k);
		}

		return true;
	}

	if (k) {
		/* Overwriting: */
		btree_account_key_drop(b, k);
		k->type = KEY_TYPE_deleted;

		insert->k.needs_whiteout = k->needs_whiteout;
		k->needs_whiteout = false;

		if (k >= btree_bset_last(b)->start) {
			clobber_u64s = k->u64s;
			goto overwrite;
		} else {
			bch2_btree_iter_fix_key_modified(iter, b, k);
		}
	}

	k = bch2_btree_node_iter_bset_pos(node_iter, b, bset_tree_last(b));
overwrite:
	bch2_bset_insert(b, node_iter, k, insert, clobber_u64s);
	new_u64s = k->u64s;
fix_iter:
	if (clobber_u64s != new_u64s)
		bch2_btree_node_iter_fix(iter, b, node_iter, k,
					 clobber_u64s, new_u64s);
	return true;
}

static void __btree_node_flush(struct journal *j, struct journal_entry_pin *pin,
			       unsigned i, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct btree_write *w = container_of(pin, struct btree_write, journal);
	struct btree *b = container_of(w, struct btree, writes[i]);

	btree_node_lock_type(c, b, SIX_LOCK_read);
	bch2_btree_node_write_cond(c, b,
		(btree_current_write(b) == w && w->journal.seq == seq));
	six_unlock_read(&b->c.lock);
}

static void btree_node_flush0(struct journal *j, struct journal_entry_pin *pin, u64 seq)
{
	return __btree_node_flush(j, pin, 0, seq);
}

static void btree_node_flush1(struct journal *j, struct journal_entry_pin *pin, u64 seq)
{
	return __btree_node_flush(j, pin, 1, seq);
}

inline void bch2_btree_add_journal_pin(struct bch_fs *c,
				       struct btree *b, u64 seq)
{
	struct btree_write *w = btree_current_write(b);

	bch2_journal_pin_add(&c->journal, seq, &w->journal,
			     btree_node_write_idx(b) == 0
			     ? btree_node_flush0
			     : btree_node_flush1);
}

/**
 * btree_insert_key - insert a key one key into a leaf node
 */
static bool btree_insert_key_leaf(struct btree_trans *trans,
				  struct btree_iter *iter,
				  struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;
	struct btree *b = iter_l(iter)->b;
	struct bset_tree *t = bset_tree_last(b);
	struct bset *i = bset(b, t);
	int old_u64s = bset_u64s(t);
	int old_live_u64s = b->nr.live_u64s;
	int live_u64s_added, u64s_added;

	EBUG_ON(!iter->level &&
		!test_bit(BCH_FS_BTREE_INTERIOR_REPLAY_DONE, &c->flags));

	if (unlikely(!bch2_btree_bset_insert_key(iter, b,
					&iter_l(iter)->iter, insert)))
		return false;

	i->journal_seq = cpu_to_le64(max(trans->journal_res.seq,
					 le64_to_cpu(i->journal_seq)));

	bch2_btree_add_journal_pin(c, b, trans->journal_res.seq);

	if (unlikely(!btree_node_dirty(b)))
		set_btree_node_dirty(b);

	live_u64s_added = (int) b->nr.live_u64s - old_live_u64s;
	u64s_added = (int) bset_u64s(t) - old_u64s;

	if (b->sib_u64s[0] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[0] = max(0, (int) b->sib_u64s[0] + live_u64s_added);
	if (b->sib_u64s[1] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[1] = max(0, (int) b->sib_u64s[1] + live_u64s_added);

	if (u64s_added > live_u64s_added &&
	    bch2_maybe_compact_whiteouts(c, b))
		bch2_btree_iter_reinit_node(iter, b);

	trace_btree_insert_key(c, b, insert);
	return true;
}

/* Cached btree updates: */

/* Normal update interface: */

static inline void btree_insert_entry_checks(struct btree_trans *trans,
					     struct btree_iter *iter,
					     struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;

	BUG_ON(bkey_cmp(insert->k.p, iter->pos));
	BUG_ON(debug_check_bkeys(c) &&
	       bch2_bkey_invalid(c, bkey_i_to_s_c(insert),
				 __btree_node_type(iter->level, iter->btree_id)));
}

static noinline int
bch2_trans_journal_preres_get_cold(struct btree_trans *trans, unsigned u64s)
{
	struct bch_fs *c = trans->c;
	int ret;

	bch2_trans_unlock(trans);

	ret = bch2_journal_preres_get(&c->journal,
			&trans->journal_preres, u64s, 0);
	if (ret)
		return ret;

	if (!bch2_trans_relock(trans)) {
		trace_trans_restart_journal_preres_get(trans->ip);
		return -EINTR;
	}

	return 0;
}

static inline int bch2_trans_journal_res_get(struct btree_trans *trans,
					     unsigned flags)
{
	struct bch_fs *c = trans->c;
	int ret;

	if (trans->flags & BTREE_INSERT_JOURNAL_RESERVED)
		flags |= JOURNAL_RES_GET_RESERVED;

	ret = bch2_journal_res_get(&c->journal, &trans->journal_res,
				   trans->journal_u64s, flags);

	return ret == -EAGAIN ? BTREE_INSERT_NEED_JOURNAL_RES : ret;
}

static enum btree_insert_ret
btree_key_can_insert(struct btree_trans *trans,
		     struct btree_iter *iter,
		     unsigned u64s)
{
	struct bch_fs *c = trans->c;
	struct btree *b = iter_l(iter)->b;

	if (!bch2_btree_node_insert_fits(c, b, u64s))
		return BTREE_INSERT_BTREE_NODE_FULL;

	return BTREE_INSERT_OK;
}

static enum btree_insert_ret
btree_key_can_insert_cached(struct btree_trans *trans,
			    struct btree_iter *iter,
			    unsigned u64s)
{
	struct bkey_cached *ck = (void *) iter->l[0].b;
	unsigned new_u64s;
	struct bkey_i *new_k;

	BUG_ON(iter->level);

	if (u64s <= ck->u64s)
		return BTREE_INSERT_OK;

	new_u64s	= roundup_pow_of_two(u64s);
	new_k		= krealloc(ck->k, new_u64s * sizeof(u64), GFP_NOFS);
	if (!new_k)
		return -ENOMEM;

	ck->u64s	= new_u64s;
	ck->k		= new_k;
	return BTREE_INSERT_OK;
}

static inline void do_btree_insert_one(struct btree_trans *trans,
				       struct btree_iter *iter,
				       struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	bool did_work;

	EBUG_ON(trans->journal_res.ref !=
		!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY));

	insert->k.needs_whiteout = false;

	did_work = (btree_iter_type(iter) != BTREE_ITER_CACHED)
		? btree_insert_key_leaf(trans, iter, insert)
		: bch2_btree_insert_key_cached(trans, iter, insert);
	if (!did_work)
		return;

	if (likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		bch2_journal_add_keys(j, &trans->journal_res,
				      iter->btree_id, insert);

		bch2_journal_set_has_inode(j, &trans->journal_res,
					   insert->k.p.inode);

		if (trans->journal_seq)
			*trans->journal_seq = trans->journal_res.seq;
	}
}

static inline bool iter_has_trans_triggers(struct btree_iter *iter)
{
	return BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS & (1U << iter->btree_id);
}

static inline bool iter_has_nontrans_triggers(struct btree_iter *iter)
{
	return (((BTREE_NODE_TYPE_HAS_TRIGGERS &
		  ~BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS)) |
		(1U << BTREE_ID_EC)) &
		(1U << iter->btree_id);
}

static noinline void bch2_btree_iter_unlock_noinline(struct btree_iter *iter)
{
	__bch2_btree_iter_unlock(iter);
}

static noinline void bch2_trans_mark_gc(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;

	trans_for_each_update(trans, i) {
		/*
		 * XXX: synchronization of cached update triggers with gc
		 */
		BUG_ON(btree_iter_type(i->iter) == BTREE_ITER_CACHED);

		if (gc_visited(c, gc_pos_btree_node(i->iter->l[0].b)))
			bch2_mark_update(trans, i->iter, i->k, NULL,
					 i->trigger_flags|BTREE_TRIGGER_GC);
	}
}

static inline int
bch2_trans_commit_write_locked(struct btree_trans *trans,
			       struct btree_insert_entry **stopped_at)
{
	struct bch_fs *c = trans->c;
	struct bch_fs_usage_online *fs_usage = NULL;
	struct btree_insert_entry *i;
	unsigned u64s = 0;
	bool marking = false;
	int ret;

	if (race_fault()) {
		trace_trans_restart_fault_inject(trans->ip);
		return -EINTR;
	}

	/*
	 * Check if the insert will fit in the leaf node with the write lock
	 * held, otherwise another thread could write the node changing the
	 * amount of space available:
	 */

	prefetch(&trans->c->journal.flags);

	trans_for_each_update2(trans, i) {
		/* Multiple inserts might go to same leaf: */
		if (!same_leaf_as_prev(trans, i))
			u64s = 0;

		u64s += i->k->k.u64s;
		ret = btree_iter_type(i->iter) != BTREE_ITER_CACHED
			? btree_key_can_insert(trans, i->iter, u64s)
			: btree_key_can_insert_cached(trans, i->iter, u64s);
		if (ret) {
			*stopped_at = i;
			return ret;
		}

		if (btree_node_type_needs_gc(i->iter->btree_id))
			marking = true;
	}

	if (marking) {
		percpu_down_read(&c->mark_lock);
		fs_usage = bch2_fs_usage_scratch_get(c);
	}

	/*
	 * Don't get journal reservation until after we know insert will
	 * succeed:
	 */
	if (likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		ret = bch2_trans_journal_res_get(trans,
				JOURNAL_RES_GET_NONBLOCK);
		if (ret)
			goto err;
	} else {
		trans->journal_res.seq = c->journal.replay_journal_seq;
	}

	if (unlikely(trans->extra_journal_entry_u64s)) {
		memcpy_u64s_small(journal_res_entry(&c->journal, &trans->journal_res),
				  trans->extra_journal_entries,
				  trans->extra_journal_entry_u64s);

		trans->journal_res.offset	+= trans->extra_journal_entry_u64s;
		trans->journal_res.u64s		-= trans->extra_journal_entry_u64s;
	}

	/*
	 * Not allowed to fail after we've gotten our journal reservation - we
	 * have to use it:
	 */

	if (!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY)) {
		if (journal_seq_verify(c))
			trans_for_each_update2(trans, i)
				i->k->k.version.lo = trans->journal_res.seq;
		else if (inject_invalid_keys(c))
			trans_for_each_update2(trans, i)
				i->k->k.version = MAX_VERSION;
	}

	/* Must be called under mark_lock: */
	if (marking && trans->fs_usage_deltas &&
	    bch2_replicas_delta_list_apply(c, &fs_usage->u,
					   trans->fs_usage_deltas)) {
		ret = BTREE_INSERT_NEED_MARK_REPLICAS;
		goto err;
	}

	trans_for_each_update(trans, i)
		if (iter_has_nontrans_triggers(i->iter))
			bch2_mark_update(trans, i->iter, i->k,
					 &fs_usage->u, i->trigger_flags);

	if (marking)
		bch2_trans_fs_usage_apply(trans, fs_usage);

	if (unlikely(c->gc_pos.phase))
		bch2_trans_mark_gc(trans);

	trans_for_each_update2(trans, i)
		do_btree_insert_one(trans, i->iter, i->k);
err:
	if (marking) {
		bch2_fs_usage_scratch_put(c, fs_usage);
		percpu_up_read(&c->mark_lock);
	}

	return ret;
}

/*
 * Get journal reservation, take write locks, and attempt to do btree update(s):
 */
static inline int do_bch2_trans_commit(struct btree_trans *trans,
				       struct btree_insert_entry **stopped_at)
{
	struct btree_insert_entry *i;
	struct btree_iter *iter;
	int ret;

	trans_for_each_update2(trans, i)
		BUG_ON(!btree_node_intent_locked(i->iter, i->iter->level));

	ret = bch2_journal_preres_get(&trans->c->journal,
			&trans->journal_preres, trans->journal_preres_u64s,
			JOURNAL_RES_GET_NONBLOCK|
			((trans->flags & BTREE_INSERT_JOURNAL_RECLAIM)
			 ? JOURNAL_RES_GET_RECLAIM : 0));
	if (unlikely(ret == -EAGAIN))
		ret = bch2_trans_journal_preres_get_cold(trans,
						trans->journal_preres_u64s);
	if (unlikely(ret))
		return ret;

	/*
	 * Can't be holding any read locks when we go to take write locks:
	 * another thread could be holding an intent lock on the same node we
	 * have a read lock on, and it'll block trying to take a write lock
	 * (because we hold a read lock) and it could be blocking us by holding
	 * its own read lock (while we're trying to to take write locks).
	 *
	 * note - this must be done after bch2_trans_journal_preres_get_cold()
	 * or anything else that might call bch2_trans_relock(), since that
	 * would just retake the read locks:
	 */
	trans_for_each_iter(trans, iter) {
		if (iter->nodes_locked != iter->nodes_intent_locked) {
			if ((iter->flags & BTREE_ITER_KEEP_UNTIL_COMMIT) ||
			    (trans->iters_live & (1ULL << iter->idx))) {
				if (!bch2_btree_iter_upgrade(iter, 1)) {
					trace_trans_restart_upgrade(trans->ip);
					return -EINTR;
				}
			} else {
				bch2_btree_iter_unlock_noinline(iter);
			}
		}
	}

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		trans_for_each_update2(trans, i)
			btree_insert_entry_checks(trans, i->iter, i->k);
	bch2_btree_trans_verify_locks(trans);

	trans_for_each_update2(trans, i)
		if (!same_leaf_as_prev(trans, i))
			bch2_btree_node_lock_for_insert(trans->c,
					iter_l(i->iter)->b, i->iter);

	ret = bch2_trans_commit_write_locked(trans, stopped_at);

	trans_for_each_update2(trans, i)
		if (!same_leaf_as_prev(trans, i))
			bch2_btree_node_unlock_write_inlined(iter_l(i->iter)->b,
							     i->iter);

	if (!ret && trans->journal_pin)
		bch2_journal_pin_add(&trans->c->journal, trans->journal_res.seq,
				     trans->journal_pin, NULL);

	/*
	 * Drop journal reservation after dropping write locks, since dropping
	 * the journal reservation may kick off a journal write:
	 */
	bch2_journal_res_put(&trans->c->journal, &trans->journal_res);

	if (unlikely(ret))
		return ret;

	if (trans->flags & BTREE_INSERT_NOUNLOCK)
		trans->nounlock = true;

	trans_for_each_update2(trans, i)
		if (btree_iter_type(i->iter) != BTREE_ITER_CACHED &&
		    !same_leaf_as_prev(trans, i))
			bch2_foreground_maybe_merge(trans->c, i->iter,
						    0, trans->flags);

	trans->nounlock = false;

	bch2_trans_downgrade(trans);

	return 0;
}

static noinline
int bch2_trans_commit_error(struct btree_trans *trans,
			    struct btree_insert_entry *i,
			    int ret)
{
	struct bch_fs *c = trans->c;
	unsigned flags = trans->flags;

	/*
	 * BTREE_INSERT_NOUNLOCK means don't unlock _after_ successful btree
	 * update; if we haven't done anything yet it doesn't apply
	 */
	flags &= ~BTREE_INSERT_NOUNLOCK;

	switch (ret) {
	case BTREE_INSERT_BTREE_NODE_FULL:
		ret = bch2_btree_split_leaf(c, i->iter, flags);

		/*
		 * if the split succeeded without dropping locks the insert will
		 * still be atomic (what the caller peeked() and is overwriting
		 * won't have changed)
		 */
#if 0
		/*
		 * XXX:
		 * split -> btree node merging (of parent node) might still drop
		 * locks when we're not passing it BTREE_INSERT_NOUNLOCK
		 *
		 * we don't want to pass BTREE_INSERT_NOUNLOCK to split as that
		 * will inhibit merging - but we don't have a reliable way yet
		 * (do we?) of checking if we dropped locks in this path
		 */
		if (!ret)
			goto retry;
#endif

		/*
		 * don't care if we got ENOSPC because we told split it
		 * couldn't block:
		 */
		if (!ret ||
		    ret == -EINTR ||
		    (flags & BTREE_INSERT_NOUNLOCK)) {
			trace_trans_restart_btree_node_split(trans->ip);
			ret = -EINTR;
		}
		break;
	case BTREE_INSERT_ENOSPC:
		ret = -ENOSPC;
		break;
	case BTREE_INSERT_NEED_MARK_REPLICAS:
		bch2_trans_unlock(trans);

		trans_for_each_update(trans, i) {
			ret = bch2_mark_bkey_replicas(c, bkey_i_to_s_c(i->k));
			if (ret)
				return ret;
		}

		if (bch2_trans_relock(trans))
			return 0;

		trace_trans_restart_mark_replicas(trans->ip);
		ret = -EINTR;
		break;
	case BTREE_INSERT_NEED_JOURNAL_RES:
		bch2_trans_unlock(trans);

		ret = bch2_trans_journal_res_get(trans, JOURNAL_RES_GET_CHECK);
		if (ret)
			return ret;

		if (bch2_trans_relock(trans))
			return 0;

		trace_trans_restart_journal_res_get(trans->ip);
		ret = -EINTR;
		break;
	default:
		BUG_ON(ret >= 0);
		break;
	}

	if (ret == -EINTR) {
		int ret2 = bch2_btree_iter_traverse_all(trans);

		if (ret2) {
			trace_trans_restart_traverse(trans->ip);
			return ret2;
		}

		trace_trans_restart_atomic(trans->ip);
	}

	return ret;
}

static noinline int
bch2_trans_commit_get_rw_cold(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	int ret;

	if (likely(!(trans->flags & BTREE_INSERT_LAZY_RW)))
		return -EROFS;

	bch2_trans_unlock(trans);

	ret = bch2_fs_read_write_early(c);
	if (ret)
		return ret;

	percpu_ref_get(&c->writes);
	return 0;
}

static void bch2_trans_update2(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_i *insert)
{
	struct btree_insert_entry *i, n = (struct btree_insert_entry) {
		.iter = iter, .k = insert
	};

	btree_insert_entry_checks(trans, n.iter, n.k);

	BUG_ON(iter->uptodate > BTREE_ITER_NEED_PEEK);

	EBUG_ON(trans->nr_updates2 >= trans->nr_iters);

	iter->flags |= BTREE_ITER_KEEP_UNTIL_COMMIT;

	trans_for_each_update2(trans, i) {
		if (btree_iter_cmp(n.iter, i->iter) == 0) {
			*i = n;
			return;
		}

		if (btree_iter_cmp(n.iter, i->iter) <= 0)
			break;
	}

	array_insert_item(trans->updates2, trans->nr_updates2,
			  i - trans->updates2, n);
}

static int extent_update_to_keys(struct btree_trans *trans,
				 struct btree_iter *orig_iter,
				 struct bkey_i *insert)
{
	struct btree_iter *iter;
	int ret;

	ret = bch2_extent_can_insert(trans, orig_iter, insert);
	if (ret)
		return ret;

	if (bkey_deleted(&insert->k))
		return 0;

	iter = bch2_trans_copy_iter(trans, orig_iter);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	iter->flags |= BTREE_ITER_INTENT;
	__bch2_btree_iter_set_pos(iter, insert->k.p, false);
	bch2_trans_update2(trans, iter, insert);
	bch2_trans_iter_put(trans, iter);
	return 0;
}

static int extent_handle_overwrites(struct btree_trans *trans,
				    enum btree_id btree_id,
				    struct bpos start, struct bpos end)
{
	struct btree_iter *iter = NULL, *update_iter;
	struct bkey_i *update;
	struct bkey_s_c k;
	int ret = 0;

	iter = bch2_trans_get_iter(trans, btree_id, start, BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(iter);
	if (ret)
		return ret;

	k = bch2_btree_iter_peek_with_updates(iter);

	while (k.k && !(ret = bkey_err(k))) {
		if (bkey_cmp(end, bkey_start_pos(k.k)) <= 0)
			break;

		if (bkey_cmp(bkey_start_pos(k.k), start) < 0) {
			update_iter = bch2_trans_copy_iter(trans, iter);
			if ((ret = PTR_ERR_OR_ZERO(update_iter)))
				goto err;

			update = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bkey_reassemble(update, k);
			bch2_cut_back(start, update);

			__bch2_btree_iter_set_pos(update_iter, update->k.p, false);
			bch2_trans_update2(trans, update_iter, update);
			bch2_trans_iter_put(trans, update_iter);
		}

		if (bkey_cmp(k.k->p, end) > 0) {
			update_iter = bch2_trans_copy_iter(trans, iter);
			if ((ret = PTR_ERR_OR_ZERO(update_iter)))
				goto err;

			update = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bkey_reassemble(update, k);
			bch2_cut_front(end, update);

			__bch2_btree_iter_set_pos(update_iter, update->k.p, false);
			bch2_trans_update2(trans, update_iter, update);
			bch2_trans_iter_put(trans, update_iter);
		} else {
			update_iter = bch2_trans_copy_iter(trans, iter);
			if ((ret = PTR_ERR_OR_ZERO(update_iter)))
				goto err;

			update = bch2_trans_kmalloc(trans, sizeof(struct bkey));
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			update->k = *k.k;
			set_bkey_val_u64s(&update->k, 0);
			update->k.type = KEY_TYPE_deleted;
			update->k.size = 0;

			__bch2_btree_iter_set_pos(update_iter, update->k.p, false);
			bch2_trans_update2(trans, update_iter, update);
			bch2_trans_iter_put(trans, update_iter);
		}

		k = bch2_btree_iter_next_with_updates(iter);
	}
err:
	if (!IS_ERR_OR_NULL(iter))
		bch2_trans_iter_put(trans, iter);
	return ret;
}

int __bch2_trans_commit(struct btree_trans *trans)
{
	struct btree_insert_entry *i = NULL;
	struct btree_iter *iter;
	bool trans_trigger_run;
	unsigned u64s;
	int ret = 0;

	BUG_ON(trans->need_reset);

	if (!trans->nr_updates)
		goto out_noupdates;

	if (trans->flags & BTREE_INSERT_GC_LOCK_HELD)
		lockdep_assert_held(&trans->c->gc_lock);

	memset(&trans->journal_preres, 0, sizeof(trans->journal_preres));

	trans->journal_u64s		= trans->extra_journal_entry_u64s;
	trans->journal_preres_u64s	= 0;

	if (!(trans->flags & BTREE_INSERT_NOCHECK_RW) &&
	    unlikely(!percpu_ref_tryget(&trans->c->writes))) {
		ret = bch2_trans_commit_get_rw_cold(trans);
		if (ret)
			return ret;
	}

#ifdef CONFIG_BCACHEFS_DEBUG
	trans_for_each_update(trans, i)
		if (btree_iter_type(i->iter) != BTREE_ITER_CACHED &&
		    !(i->trigger_flags & BTREE_TRIGGER_NORUN))
			bch2_btree_key_cache_verify_clean(trans,
					i->iter->btree_id, i->iter->pos);
#endif

	/*
	 * Running triggers will append more updates to the list of updates as
	 * we're walking it:
	 */
	do {
		trans_trigger_run = false;

		trans_for_each_update(trans, i) {
			if (unlikely(i->iter->uptodate > BTREE_ITER_NEED_PEEK &&
				     (ret = bch2_btree_iter_traverse(i->iter)))) {
				trace_trans_restart_traverse(trans->ip);
				goto out;
			}

			/*
			 * We're not using bch2_btree_iter_upgrade here because
			 * we know trans->nounlock can't be set:
			 */
			if (unlikely(i->iter->locks_want < 1 &&
				     !__bch2_btree_iter_upgrade(i->iter, 1))) {
				trace_trans_restart_upgrade(trans->ip);
				ret = -EINTR;
				goto out;
			}

			if (iter_has_trans_triggers(i->iter) &&
			    !i->trans_triggers_run) {
				i->trans_triggers_run = true;
				trans_trigger_run = true;

				ret = bch2_trans_mark_update(trans, i->iter, i->k,
							     i->trigger_flags);
				if (unlikely(ret)) {
					if (ret == -EINTR)
						trace_trans_restart_mark(trans->ip);
					goto out;
				}
			}
		}
	} while (trans_trigger_run);

	/* Turn extents updates into keys: */
	trans_for_each_update(trans, i)
		if (i->iter->flags & BTREE_ITER_IS_EXTENTS) {
			struct bpos start = bkey_start_pos(&i->k->k);

			while (i + 1 < trans->updates + trans->nr_updates &&
			       i[0].iter->btree_id == i[1].iter->btree_id &&
			       !bkey_cmp(i[0].k->k.p, bkey_start_pos(&i[1].k->k)))
				i++;

			ret = extent_handle_overwrites(trans, i->iter->btree_id,
						       start, i->k->k.p);
			if (ret)
				goto out;
		}

	trans_for_each_update(trans, i) {
		if (i->iter->flags & BTREE_ITER_IS_EXTENTS) {
			ret = extent_update_to_keys(trans, i->iter, i->k);
			if (ret)
				goto out;
		} else {
			bch2_trans_update2(trans, i->iter, i->k);
		}
	}

	trans_for_each_update2(trans, i) {
		BUG_ON(i->iter->uptodate > BTREE_ITER_NEED_PEEK);
		BUG_ON(i->iter->locks_want < 1);

		u64s = jset_u64s(i->k->k.u64s);
		if (btree_iter_type(i->iter) == BTREE_ITER_CACHED &&
		    likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY)))
			trans->journal_preres_u64s += u64s;
		trans->journal_u64s += u64s;
	}
retry:
	memset(&trans->journal_res, 0, sizeof(trans->journal_res));

	ret = do_bch2_trans_commit(trans, &i);

	/* make sure we didn't drop or screw up locks: */
	bch2_btree_trans_verify_locks(trans);

	if (ret)
		goto err;

	trans_for_each_iter(trans, iter)
		if ((trans->iters_live & (1ULL << iter->idx)) &&
		    (iter->flags & BTREE_ITER_SET_POS_AFTER_COMMIT)) {
			if (trans->flags & BTREE_INSERT_NOUNLOCK)
				bch2_btree_iter_set_pos_same_leaf(iter, iter->pos_after_commit);
			else
				bch2_btree_iter_set_pos(iter, iter->pos_after_commit);
		}
out:
	bch2_journal_preres_put(&trans->c->journal, &trans->journal_preres);

	if (likely(!(trans->flags & BTREE_INSERT_NOCHECK_RW)))
		percpu_ref_put(&trans->c->writes);
out_noupdates:
	bch2_trans_reset(trans, !ret ? TRANS_RESET_NOTRAVERSE : 0);

	return ret;
err:
	ret = bch2_trans_commit_error(trans, i, ret);
	if (ret)
		goto out;

	goto retry;
}

int bch2_trans_update(struct btree_trans *trans, struct btree_iter *iter,
		      struct bkey_i *k, enum btree_trigger_flags flags)
{
	struct btree_insert_entry *i, n = (struct btree_insert_entry) {
		.trigger_flags = flags, .iter = iter, .k = k
	};

	EBUG_ON(bkey_cmp(iter->pos,
			 (iter->flags & BTREE_ITER_IS_EXTENTS)
			 ? bkey_start_pos(&k->k)
			 : k->k.p));

	iter->flags |= BTREE_ITER_KEEP_UNTIL_COMMIT;

	if (btree_node_type_is_extents(iter->btree_id)) {
		iter->pos_after_commit = k->k.p;
		iter->flags |= BTREE_ITER_SET_POS_AFTER_COMMIT;
	}

	/*
	 * Pending updates are kept sorted: first, find position of new update:
	 */
	trans_for_each_update(trans, i)
		if (btree_iter_cmp(iter, i->iter) <= 0)
			break;

	/*
	 * Now delete/trim any updates the new update overwrites:
	 */
	if (i > trans->updates &&
	    i[-1].iter->btree_id == iter->btree_id &&
	    bkey_cmp(iter->pos, i[-1].k->k.p) < 0)
		bch2_cut_back(n.iter->pos, i[-1].k);

	while (i < trans->updates + trans->nr_updates &&
	       iter->btree_id == i->iter->btree_id &&
	       bkey_cmp(n.k->k.p, i->k->k.p) >= 0)
		array_remove_item(trans->updates, trans->nr_updates,
				  i - trans->updates);

	if (i < trans->updates + trans->nr_updates &&
	    iter->btree_id == i->iter->btree_id &&
	    bkey_cmp(n.k->k.p, i->iter->pos) > 0) {
		/*
		 * When we have an extent that overwrites the start of another
		 * update, trimming that extent will mean the iterator's
		 * position has to change since the iterator position has to
		 * match the extent's start pos - but we don't want to change
		 * the iterator pos if some other code is using it, so we may
		 * need to clone it:
		 */
		if (trans->iters_live & (1ULL << i->iter->idx)) {
			i->iter = bch2_trans_copy_iter(trans, i->iter);
			if (IS_ERR(i->iter)) {
				trans->need_reset = true;
				return PTR_ERR(i->iter);
			}

			i->iter->flags |= BTREE_ITER_KEEP_UNTIL_COMMIT;
			bch2_trans_iter_put(trans, i->iter);
		}

		bch2_cut_front(n.k->k.p, i->k);
		bch2_btree_iter_set_pos(i->iter, n.k->k.p);
	}

	EBUG_ON(trans->nr_updates >= trans->nr_iters);

	array_insert_item(trans->updates, trans->nr_updates,
			  i - trans->updates, n);
	return 0;
}

int __bch2_btree_insert(struct btree_trans *trans,
			enum btree_id id, struct bkey_i *k)
{
	struct btree_iter *iter;
	int ret;

	iter = bch2_trans_get_iter(trans, id, bkey_start_pos(&k->k),
				   BTREE_ITER_INTENT);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	ret   = bch2_btree_iter_traverse(iter) ?:
		bch2_trans_update(trans, iter, k, 0);
	bch2_trans_iter_put(trans, iter);
	return ret;
}

/**
 * bch2_btree_insert - insert keys into the extent btree
 * @c:			pointer to struct bch_fs
 * @id:			btree to insert into
 * @insert_keys:	list of keys to insert
 * @hook:		insert callback
 */
int bch2_btree_insert(struct bch_fs *c, enum btree_id id,
		      struct bkey_i *k,
		      struct disk_reservation *disk_res,
		      u64 *journal_seq, int flags)
{
	return bch2_trans_do(c, disk_res, journal_seq, flags,
			     __bch2_btree_insert(&trans, id, k));
}

int bch2_btree_delete_at_range(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bpos end,
			       u64 *journal_seq)
{
	struct bkey_s_c k;
	int ret = 0;
retry:
	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k)) &&
	       bkey_cmp(iter->pos, end) < 0) {
		struct bkey_i delete;

		bch2_trans_begin(trans);

		bkey_init(&delete.k);

		/*
		 * For extents, iter.pos won't necessarily be the same as
		 * bkey_start_pos(k.k) (for non extents they always will be the
		 * same). It's important that we delete starting from iter.pos
		 * because the range we want to delete could start in the middle
		 * of k.
		 *
		 * (bch2_btree_iter_peek() does guarantee that iter.pos >=
		 * bkey_start_pos(k.k)).
		 */
		delete.k.p = iter->pos;

		if (btree_node_type_is_extents(iter->btree_id)) {
			unsigned max_sectors =
				KEY_SIZE_MAX & (~0 << trans->c->block_bits);

			/* create the biggest key we can */
			bch2_key_resize(&delete.k, max_sectors);
			bch2_cut_back(end, &delete);

			ret = bch2_extent_trim_atomic(&delete, iter);
			if (ret)
				break;
		}

		bch2_trans_update(trans, iter, &delete, 0);
		ret = bch2_trans_commit(trans, NULL, journal_seq,
					BTREE_INSERT_NOFAIL);
		if (ret)
			break;

		bch2_trans_cond_resched(trans);
	}

	if (ret == -EINTR) {
		ret = 0;
		goto retry;
	}

	return ret;

}

int bch2_btree_delete_at(struct btree_trans *trans,
			 struct btree_iter *iter, unsigned flags)
{
	struct bkey_i k;

	bkey_init(&k.k);
	k.k.p = iter->pos;

	bch2_trans_update(trans, iter, &k, 0);
	return bch2_trans_commit(trans, NULL, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_USE_RESERVE|flags);
}

/*
 * bch_btree_delete_range - delete everything within a given range
 *
 * Range is a half open interval - [start, end)
 */
int bch2_btree_delete_range(struct bch_fs *c, enum btree_id id,
			    struct bpos start, struct bpos end,
			    u64 *journal_seq)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	int ret = 0;

	/*
	 * XXX: whether we need mem/more iters depends on whether this btree id
	 * has triggers
	 */
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 512);

	iter = bch2_trans_get_iter(&trans, id, start, BTREE_ITER_INTENT);

	ret = bch2_btree_delete_at_range(&trans, iter, end, journal_seq);
	ret = bch2_trans_exit(&trans) ?: ret;

	BUG_ON(ret == -EINTR);
	return ret;
}
