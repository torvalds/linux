// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "buckets.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "replicas.h"
#include "trace.h"

#include <linux/sort.h>

static inline bool same_leaf_as_prev(struct btree_trans *trans,
				     unsigned sorted_idx)
{
	struct btree_insert_entry *i = trans->updates +
		trans->updates_sorted[sorted_idx];
	struct btree_insert_entry *prev = sorted_idx
		? trans->updates + trans->updates_sorted[sorted_idx - 1]
		: NULL;

	return prev &&
		i->iter->l[0].b == prev->iter->l[0].b;
}

#define trans_for_each_update_sorted(_trans, _i, _iter)			\
	for (_iter = 0;							\
	     _iter < _trans->nr_updates &&				\
	     (_i = _trans->updates + _trans->updates_sorted[_iter], 1);	\
	     _iter++)

inline void bch2_btree_node_lock_for_insert(struct bch_fs *c, struct btree *b,
					    struct btree_iter *iter)
{
	bch2_btree_node_lock_write(b, iter);

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

static void btree_trans_lock_write(struct btree_trans *trans, bool lock)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	unsigned iter;

	trans_for_each_update_sorted(trans, i, iter) {
		if (same_leaf_as_prev(trans, iter))
			continue;

		if (lock)
			bch2_btree_node_lock_for_insert(c, i->iter->l[0].b, i->iter);
		else
			bch2_btree_node_unlock_write(i->iter->l[0].b, i->iter);
	}
}

static inline void btree_trans_sort_updates(struct btree_trans *trans)
{
	struct btree_insert_entry *l, *r;
	unsigned nr = 0, pos;

	trans_for_each_update(trans, l) {
		for (pos = 0; pos < nr; pos++) {
			r = trans->updates + trans->updates_sorted[pos];

			if (btree_iter_cmp(l->iter, r->iter) <= 0)
				break;
		}

		memmove(&trans->updates_sorted[pos + 1],
			&trans->updates_sorted[pos],
			(nr - pos) * sizeof(trans->updates_sorted[0]));

		trans->updates_sorted[pos] = l - trans->updates;
		nr++;
	}

	BUG_ON(nr != trans->nr_updates);
}

/* Inserting into a given leaf node (last stage of insert): */

/* Handle overwrites and do insert, for non extents: */
bool bch2_btree_bset_insert_key(struct btree_iter *iter,
				struct btree *b,
				struct btree_node_iter *node_iter,
				struct bkey_i *insert)
{
	const struct bkey_format *f = &b->format;
	struct bkey_packed *k;
	unsigned clobber_u64s;

	EBUG_ON(btree_node_just_written(b));
	EBUG_ON(bset_written(b, btree_bset_last(b)));
	EBUG_ON(bkey_deleted(&insert->k) && bkey_val_u64s(&insert->k));
	EBUG_ON(bkey_cmp(bkey_start_pos(&insert->k), b->data->min_key) < 0 ||
		bkey_cmp(insert->k.p, b->data->max_key) > 0);

	k = bch2_btree_node_iter_peek_all(node_iter, b);
	if (k && !bkey_cmp_packed(b, k, &insert->k)) {
		BUG_ON(bkey_whiteout(k));

		if (!bkey_written(b, k) &&
		    bkey_val_u64s(&insert->k) == bkeyp_val_u64s(f, k) &&
		    !bkey_whiteout(&insert->k)) {
			k->type = insert->k.type;
			memcpy_u64s(bkeyp_val(f, k), &insert->v,
				    bkey_val_u64s(&insert->k));
			return true;
		}

		insert->k.needs_whiteout = k->needs_whiteout;

		btree_account_key_drop(b, k);

		if (k >= btree_bset_last(b)->start) {
			clobber_u64s = k->u64s;

			/*
			 * If we're deleting, and the key we're deleting doesn't
			 * need a whiteout (it wasn't overwriting a key that had
			 * been written to disk) - just delete it:
			 */
			if (bkey_whiteout(&insert->k) && !k->needs_whiteout) {
				bch2_bset_delete(b, k, clobber_u64s);
				bch2_btree_node_iter_fix(iter, b, node_iter,
							 k, clobber_u64s, 0);
				return true;
			}

			goto overwrite;
		}

		k->type = KEY_TYPE_deleted;
		bch2_btree_node_iter_fix(iter, b, node_iter, k,
					 k->u64s, k->u64s);

		if (bkey_whiteout(&insert->k)) {
			reserve_whiteout(b, k);
			return true;
		} else {
			k->needs_whiteout = false;
		}
	} else {
		/*
		 * Deleting, but the key to delete wasn't found - nothing to do:
		 */
		if (bkey_whiteout(&insert->k))
			return false;

		insert->k.needs_whiteout = false;
	}

	k = bch2_btree_node_iter_bset_pos(node_iter, b, bset_tree_last(b));
	clobber_u64s = 0;
overwrite:
	bch2_bset_insert(b, node_iter, k, insert, clobber_u64s);
	bch2_btree_node_iter_fix(iter, b, node_iter, k,
				 clobber_u64s, k->u64s);
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

static inline void __btree_journal_key(struct btree_trans *trans,
				       enum btree_id btree_id,
				       struct bkey_i *insert)
{
	struct journal *j = &trans->c->journal;
	u64 seq = trans->journal_res.seq;
	bool needs_whiteout = insert->k.needs_whiteout;

	/* ick */
	insert->k.needs_whiteout = false;
	bch2_journal_add_keys(j, &trans->journal_res,
			      btree_id, insert);
	insert->k.needs_whiteout = needs_whiteout;

	bch2_journal_set_has_inode(j, &trans->journal_res,
				   insert->k.p.inode);

	if (trans->journal_seq)
		*trans->journal_seq = seq;
}

void bch2_btree_journal_key(struct btree_trans *trans,
			   struct btree_iter *iter,
			   struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct btree *b = iter->l[0].b;
	struct btree_write *w = btree_current_write(b);

	EBUG_ON(iter->level || b->c.level);
	EBUG_ON(trans->journal_res.ref !=
		!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY));

	if (likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		__btree_journal_key(trans, iter->btree_id, insert);
		btree_bset_last(b)->journal_seq =
			cpu_to_le64(trans->journal_res.seq);
	}

	if (unlikely(!journal_pin_active(&w->journal))) {
		u64 seq = likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))
			? trans->journal_res.seq
			: j->replay_journal_seq;

		bch2_journal_pin_add(j, seq, &w->journal,
				     btree_node_write_idx(b) == 0
				     ? btree_node_flush0
				     : btree_node_flush1);
	}

	if (unlikely(!btree_node_dirty(b)))
		set_btree_node_dirty(b);
}

static void bch2_insert_fixup_key(struct btree_trans *trans,
				  struct btree_insert_entry *insert)
{
	struct btree_iter *iter = insert->iter;
	struct btree_iter_level *l = &iter->l[0];

	EBUG_ON(iter->level);
	EBUG_ON(insert->k->k.u64s >
		bch_btree_keys_u64s_remaining(trans->c, l->b));

	if (bch2_btree_bset_insert_key(iter, l->b, &l->iter,
				       insert->k))
		bch2_btree_journal_key(trans, iter, insert->k);
}

/**
 * btree_insert_key - insert a key one key into a leaf node
 */
static void btree_insert_key_leaf(struct btree_trans *trans,
				  struct btree_insert_entry *insert)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter = insert->iter;
	struct btree *b = iter->l[0].b;
	int old_u64s = le16_to_cpu(btree_bset_last(b)->u64s);
	int old_live_u64s = b->nr.live_u64s;
	int live_u64s_added, u64s_added;

	if (!btree_node_is_extents(b))
		bch2_insert_fixup_key(trans, insert);
	else
		bch2_insert_fixup_extent(trans, insert);

	live_u64s_added = (int) b->nr.live_u64s - old_live_u64s;
	u64s_added = (int) le16_to_cpu(btree_bset_last(b)->u64s) - old_u64s;

	if (b->sib_u64s[0] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[0] = max(0, (int) b->sib_u64s[0] + live_u64s_added);
	if (b->sib_u64s[1] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[1] = max(0, (int) b->sib_u64s[1] + live_u64s_added);

	if (u64s_added > live_u64s_added &&
	    bch2_maybe_compact_whiteouts(c, b))
		bch2_btree_iter_reinit_node(iter, b);

	trace_btree_insert_key(c, b, insert->k);
}

/* Normal update interface: */

static inline void btree_insert_entry_checks(struct btree_trans *trans,
					     struct btree_insert_entry *i)
{
	struct bch_fs *c = trans->c;

	BUG_ON(i->iter->level);
	BUG_ON(bkey_cmp(bkey_start_pos(&i->k->k), i->iter->pos));
	EBUG_ON((i->iter->flags & BTREE_ITER_IS_EXTENTS) &&
		bkey_cmp(i->k->k.p, i->iter->l[0].b->key.k.p) > 0);
	EBUG_ON((i->iter->flags & BTREE_ITER_IS_EXTENTS) &&
		!(trans->flags & BTREE_INSERT_ATOMIC));

	BUG_ON(debug_check_bkeys(c) &&
	       !bkey_deleted(&i->k->k) &&
	       bch2_bkey_invalid(c, bkey_i_to_s_c(i->k), i->iter->btree_id));
}

static int bch2_trans_journal_preres_get(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	unsigned u64s = 0;
	int ret;

	trans_for_each_update(trans, i)
		if (0)
			u64s += jset_u64s(i->k->k.u64s);

	if (!u64s)
		return 0;

	ret = bch2_journal_preres_get(&c->journal,
			&trans->journal_preres, u64s,
			JOURNAL_RES_GET_NONBLOCK);
	if (ret != -EAGAIN)
		return ret;

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

static int bch2_trans_journal_res_get(struct btree_trans *trans,
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
		     struct btree_insert_entry *insert,
		     unsigned *u64s)
{
	struct bch_fs *c = trans->c;
	struct btree *b = insert->iter->l[0].b;
	static enum btree_insert_ret ret;

	if (unlikely(btree_node_fake(b)))
		return BTREE_INSERT_BTREE_NODE_FULL;

	ret = !btree_node_is_extents(b)
		? BTREE_INSERT_OK
		: bch2_extent_can_insert(trans, insert, u64s);
	if (ret)
		return ret;

	if (*u64s > bch_btree_keys_u64s_remaining(c, b))
		return BTREE_INSERT_BTREE_NODE_FULL;

	return BTREE_INSERT_OK;
}

static int btree_trans_check_can_insert(struct btree_trans *trans,
					struct btree_insert_entry **stopped_at)
{
	struct btree_insert_entry *i;
	unsigned iter, u64s = 0;
	int ret;

	trans_for_each_update_sorted(trans, i, iter) {
		/* Multiple inserts might go to same leaf: */
		if (!same_leaf_as_prev(trans, iter))
			u64s = 0;

		u64s += i->k->k.u64s;
		ret = btree_key_can_insert(trans, i, &u64s);
		if (ret) {
			*stopped_at = i;
			return ret;
		}
	}

	return 0;
}

static inline void do_btree_insert_one(struct btree_trans *trans,
				       struct btree_insert_entry *insert)
{
	btree_insert_key_leaf(trans, insert);
}

static inline bool update_triggers_transactional(struct btree_trans *trans,
						 struct btree_insert_entry *i)
{
	return likely(!(trans->flags & BTREE_INSERT_MARK_INMEM)) &&
		(i->iter->btree_id == BTREE_ID_EXTENTS ||
		 i->iter->btree_id == BTREE_ID_INODES ||
		 i->iter->btree_id == BTREE_ID_REFLINK);
}

static inline bool update_has_triggers(struct btree_trans *trans,
				       struct btree_insert_entry *i)
{
	return likely(!(trans->flags & BTREE_INSERT_NOMARK)) &&
		btree_node_type_needs_gc(i->iter->btree_id);
}

/*
 * Get journal reservation, take write locks, and attempt to do btree update(s):
 */
static inline int do_btree_insert_at(struct btree_trans *trans,
				     struct btree_insert_entry **stopped_at)
{
	struct bch_fs *c = trans->c;
	struct bch_fs_usage_online *fs_usage = NULL;
	struct btree_insert_entry *i;
	unsigned mark_flags = trans->flags & BTREE_INSERT_BUCKET_INVALIDATE
		? BCH_BUCKET_MARK_BUCKET_INVALIDATE
		: 0;
	int ret;

	trans_for_each_update(trans, i)
		BUG_ON(i->iter->uptodate >= BTREE_ITER_NEED_RELOCK);

	/*
	 * note: running triggers will append more updates to the list of
	 * updates as we're walking it:
	 */
	trans_for_each_update(trans, i)
		if (update_has_triggers(trans, i) &&
		    update_triggers_transactional(trans, i)) {
			ret = bch2_trans_mark_update(trans, i->iter, i->k);
			if (ret == -EINTR)
				trace_trans_restart_mark(trans->ip);
			if (ret)
				goto out_clear_replicas;
		}

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		trans_for_each_update(trans, i)
			btree_insert_entry_checks(trans, i);
	bch2_btree_trans_verify_locks(trans);

	/*
	 * No more updates can be added - sort updates so we can take write
	 * locks in the correct order:
	 */
	btree_trans_sort_updates(trans);

	btree_trans_lock_write(trans, true);

	if (race_fault()) {
		ret = -EINTR;
		trace_trans_restart_fault_inject(trans->ip);
		goto out;
	}

	/*
	 * Check if the insert will fit in the leaf node with the write lock
	 * held, otherwise another thread could write the node changing the
	 * amount of space available:
	 */
	ret = btree_trans_check_can_insert(trans, stopped_at);
	if (ret)
		goto out;

	trans_for_each_update(trans, i) {
		if (!btree_node_type_needs_gc(i->iter->btree_id))
			continue;

		if (!fs_usage) {
			percpu_down_read(&c->mark_lock);
			fs_usage = bch2_fs_usage_scratch_get(c);
		}

		if (!bch2_bkey_replicas_marked_locked(c,
			bkey_i_to_s_c(i->k), true)) {
			ret = BTREE_INSERT_NEED_MARK_REPLICAS;
			goto out;
		}
	}

	/*
	 * Don't get journal reservation until after we know insert will
	 * succeed:
	 */
	if (likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		trans->journal_u64s = 0;

		trans_for_each_update(trans, i)
			trans->journal_u64s += jset_u64s(i->k->k.u64s);

		ret = bch2_trans_journal_res_get(trans, JOURNAL_RES_GET_NONBLOCK);
		if (ret)
			goto out;
	}

	if (!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY)) {
		if (journal_seq_verify(c))
			trans_for_each_update(trans, i)
				i->k->k.version.lo = trans->journal_res.seq;
		else if (inject_invalid_keys(c))
			trans_for_each_update(trans, i)
				i->k->k.version = MAX_VERSION;
	}

	trans_for_each_update(trans, i)
		if (update_has_triggers(trans, i) &&
		    !update_triggers_transactional(trans, i))
			bch2_mark_update(trans, i, &fs_usage->u, mark_flags);

	if (fs_usage && trans->fs_usage_deltas)
		bch2_replicas_delta_list_apply(c, &fs_usage->u,
					       trans->fs_usage_deltas);

	if (fs_usage)
		bch2_trans_fs_usage_apply(trans, fs_usage);

	if (likely(!(trans->flags & BTREE_INSERT_NOMARK)) &&
	    unlikely(c->gc_pos.phase))
		trans_for_each_update(trans, i)
			if (gc_visited(c, gc_pos_btree_node(i->iter->l[0].b)))
				bch2_mark_update(trans, i, NULL,
						 mark_flags|
						 BCH_BUCKET_MARK_GC);

	trans_for_each_update(trans, i)
		do_btree_insert_one(trans, i);
out:
	BUG_ON(ret &&
	       (trans->flags & BTREE_INSERT_JOURNAL_RESERVED) &&
	       trans->journal_res.ref);

	btree_trans_lock_write(trans, false);

	if (fs_usage) {
		bch2_fs_usage_scratch_put(c, fs_usage);
		percpu_up_read(&c->mark_lock);
	}

	bch2_journal_res_put(&c->journal, &trans->journal_res);
out_clear_replicas:
	if (trans->fs_usage_deltas) {
		memset(&trans->fs_usage_deltas->fs_usage, 0,
		       sizeof(trans->fs_usage_deltas->fs_usage));
		trans->fs_usage_deltas->used = 0;
	}

	return ret;
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
		 * still be atomic (in the BTREE_INSERT_ATOMIC sense, what the
		 * caller peeked() and is overwriting won't have changed)
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

		/*
		 * BTREE_ITER_ATOMIC means we have to return -EINTR if we
		 * dropped locks:
		 */
		if (!(flags & BTREE_INSERT_ATOMIC))
			return 0;

		trace_trans_restart_atomic(trans->ip);
	}

	return ret;
}

/**
 * __bch_btree_insert_at - insert keys at given iterator positions
 *
 * This is main entry point for btree updates.
 *
 * Return values:
 * -EINTR: locking changed, this function should be called again. Only returned
 *  if passed BTREE_INSERT_ATOMIC.
 * -EROFS: filesystem read only
 * -EIO: journal or btree node IO error
 */
static int __bch2_trans_commit(struct btree_trans *trans,
			       struct btree_insert_entry **stopped_at)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	unsigned iter;
	int ret;

	trans_for_each_update(trans, i) {
		if (!bch2_btree_iter_upgrade(i->iter, 1)) {
			trace_trans_restart_upgrade(trans->ip);
			ret = -EINTR;
			goto err;
		}

		ret = btree_iter_err(i->iter);
		if (ret)
			goto err;
	}

	ret = do_btree_insert_at(trans, stopped_at);
	if (unlikely(ret))
		goto err;

	if (trans->flags & BTREE_INSERT_NOUNLOCK)
		trans->nounlock = true;

	trans_for_each_update_sorted(trans, i, iter)
		if (!same_leaf_as_prev(trans, iter))
			bch2_foreground_maybe_merge(c, i->iter,
						    0, trans->flags);

	trans->nounlock = false;

	trans_for_each_update(trans, i)
		bch2_btree_iter_downgrade(i->iter);
err:
	/* make sure we didn't drop or screw up locks: */
	bch2_btree_trans_verify_locks(trans);

	return ret;
}

int bch2_trans_commit(struct btree_trans *trans,
		      struct disk_reservation *disk_res,
		      u64 *journal_seq,
		      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i = NULL;
	unsigned orig_nr_updates	= trans->nr_updates;
	unsigned orig_mem_top		= trans->mem_top;
	int ret = 0;

	if (!trans->nr_updates)
		goto out_noupdates;

	/* for the sake of sanity: */
	BUG_ON(trans->nr_updates > 1 && !(flags & BTREE_INSERT_ATOMIC));

	if (flags & BTREE_INSERT_GC_LOCK_HELD)
		lockdep_assert_held(&c->gc_lock);

	if (!trans->commit_start)
		trans->commit_start = local_clock();

	memset(&trans->journal_res, 0, sizeof(trans->journal_res));
	memset(&trans->journal_preres, 0, sizeof(trans->journal_preres));
	trans->disk_res		= disk_res;
	trans->journal_seq	= journal_seq;
	trans->flags		= flags;

	if (unlikely(!(trans->flags & BTREE_INSERT_NOCHECK_RW) &&
		     !percpu_ref_tryget(&c->writes))) {
		if (likely(!(trans->flags & BTREE_INSERT_LAZY_RW)))
			return -EROFS;

		bch2_trans_unlock(trans);

		ret = bch2_fs_read_write_early(c);
		if (ret)
			return ret;

		percpu_ref_get(&c->writes);

		if (!bch2_trans_relock(trans)) {
			ret = -EINTR;
			goto err;
		}
	}
retry:
	ret = bch2_trans_journal_preres_get(trans);
	if (ret)
		goto err;

	ret = __bch2_trans_commit(trans, &i);
	if (ret)
		goto err;
out:
	bch2_journal_preres_put(&c->journal, &trans->journal_preres);

	if (unlikely(!(trans->flags & BTREE_INSERT_NOCHECK_RW)))
		percpu_ref_put(&c->writes);
out_noupdates:
	if (!ret && trans->commit_start) {
		bch2_time_stats_update(&c->times[BCH_TIME_btree_update],
				       trans->commit_start);
		trans->commit_start = 0;
	}

	BUG_ON(!(trans->flags & BTREE_INSERT_ATOMIC) && ret == -EINTR);

	if (!ret) {
		bch2_trans_unlink_iters(trans, ~trans->iters_touched|
					trans->iters_unlink_on_commit);
		trans->iters_touched = 0;
	}
	trans->nr_updates	= 0;
	trans->mem_top		= 0;

	return ret;
err:
	ret = bch2_trans_commit_error(trans, i, ret);

	/* free updates and memory used by triggers, they'll be reexecuted: */
	trans->nr_updates	= orig_nr_updates;
	trans->mem_top		= orig_mem_top;

	/* can't loop if it was passed in and we changed it: */
	if (unlikely(trans->flags & BTREE_INSERT_NO_CLEAR_REPLICAS) && !ret)
		ret = -EINTR;

	if (!ret)
		goto retry;

	goto out;
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
	struct btree_trans trans;
	struct btree_iter *iter;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	iter = bch2_trans_get_iter(&trans, id, bkey_start_pos(&k->k),
				   BTREE_ITER_INTENT);

	bch2_trans_update(&trans, iter, k);

	ret = bch2_trans_commit(&trans, disk_res, journal_seq, flags);
	if (ret == -EINTR)
		goto retry;
	bch2_trans_exit(&trans);

	return ret;
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

		if (iter->flags & BTREE_ITER_IS_EXTENTS) {
			unsigned max_sectors =
				KEY_SIZE_MAX & (~0 << trans->c->block_bits);

			/* create the biggest key we can */
			bch2_key_resize(&delete.k, max_sectors);
			bch2_cut_back(end, &delete.k);

			ret = bch2_extent_trim_atomic(&delete, iter);
			if (ret)
				break;
		}

		bch2_trans_update(trans, iter, &delete);
		ret = bch2_trans_commit(trans, NULL, journal_seq,
					BTREE_INSERT_ATOMIC|
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

	bch2_trans_update(trans, iter, &k);
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
