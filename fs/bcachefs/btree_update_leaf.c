// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
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

inline void bch2_btree_node_lock_for_insert(struct bch_fs *c, struct btree *b,
					    struct btree_iter *iter)
{
	bch2_btree_node_lock_write(b, iter);

	if (btree_node_just_written(b) &&
	    bch2_btree_post_write_cleanup(c, b))
		bch2_btree_iter_reinit_node(iter, b);

	/*
	 * If the last bset has been written, or if it's gotten too big - start
	 * a new bset to insert into:
	 */
	if (want_new_bset(c, b))
		bch2_btree_init_next(c, b, iter);
}

static void btree_trans_lock_write(struct bch_fs *c, struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update_leaf(trans, i)
		bch2_btree_node_lock_for_insert(c, i->iter->l[0].b, i->iter);
}

static void btree_trans_unlock_write(struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update_leaf(trans, i)
		bch2_btree_node_unlock_write(i->iter->l[0].b, i->iter);
}

static bool btree_trans_relock(struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update_iter(trans, i)
		return bch2_btree_iter_relock(i->iter);
	return true;
}

static void btree_trans_unlock(struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update_iter(trans, i) {
		bch2_btree_iter_unlock(i->iter);
		break;
	}
}

static inline int btree_trans_cmp(struct btree_insert_entry l,
				  struct btree_insert_entry r)
{
	return (l.deferred > r.deferred) - (l.deferred < r.deferred) ?:
		btree_iter_cmp(l.iter, r.iter);
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
				bch2_btree_iter_verify(iter, b);
				return true;
			}

			goto overwrite;
		}

		k->type = KEY_TYPE_deleted;
		bch2_btree_node_iter_fix(iter, b, node_iter, k,
					 k->u64s, k->u64s);
		bch2_btree_iter_verify(iter, b);

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
	if (k->u64s != clobber_u64s || bkey_whiteout(&insert->k))
		bch2_btree_node_iter_fix(iter, b, node_iter, k,
					 clobber_u64s, k->u64s);
	bch2_btree_iter_verify(iter, b);
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
	six_unlock_read(&b->lock);
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

	EBUG_ON(iter->level || b->level);
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

/* Deferred btree updates: */

static void deferred_update_flush(struct journal *j,
				  struct journal_entry_pin *pin,
				  u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct deferred_update *d =
		container_of(pin, struct deferred_update, journal);
	struct journal_preres res = { 0 };
	u64 tmp[32];
	struct bkey_i *k = (void *) tmp;
	int ret;

	if (d->allocated_u64s > ARRAY_SIZE(tmp)) {
		k = kmalloc(d->allocated_u64s * sizeof(u64), GFP_NOFS);

		BUG_ON(!k); /* XXX */
	}

	spin_lock(&d->lock);
	if (d->dirty) {
		BUG_ON(jset_u64s(d->k.k.u64s) > d->res.u64s);

		swap(res, d->res);

		BUG_ON(d->k.k.u64s > d->allocated_u64s);

		bkey_copy(k, &d->k);
		d->dirty = false;
		spin_unlock(&d->lock);

		ret = bch2_btree_insert(c, d->btree_id, k, NULL, NULL,
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_USE_RESERVE|
					BTREE_INSERT_JOURNAL_RESERVED);
		bch2_fs_fatal_err_on(ret && !bch2_journal_error(j),
				     c, "error flushing deferred btree update: %i", ret);

		spin_lock(&d->lock);
	}

	if (!d->dirty)
		bch2_journal_pin_drop(j, &d->journal);
	spin_unlock(&d->lock);

	bch2_journal_preres_put(j, &res);
	if (k != (void *) tmp)
		kfree(k);
}

static void btree_insert_key_deferred(struct btree_trans *trans,
				      struct btree_insert_entry *insert)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct deferred_update *d = insert->d;
	int difference;

	BUG_ON(trans->flags & BTREE_INSERT_JOURNAL_REPLAY);
	BUG_ON(insert->k->u64s > d->allocated_u64s);

	__btree_journal_key(trans, d->btree_id, insert->k);

	spin_lock(&d->lock);
	BUG_ON(jset_u64s(insert->k->u64s) >
	       trans->journal_preres.u64s);

	difference = jset_u64s(insert->k->u64s) - d->res.u64s;
	if (difference > 0) {
		trans->journal_preres.u64s	-= difference;
		d->res.u64s			+= difference;
	}

	bkey_copy(&d->k, insert->k);
	d->dirty = true;

	bch2_journal_pin_update(j, trans->journal_res.seq, &d->journal,
				deferred_update_flush);
	spin_unlock(&d->lock);
}

void bch2_deferred_update_free(struct bch_fs *c,
			       struct deferred_update *d)
{
	deferred_update_flush(&c->journal, &d->journal, 0);

	BUG_ON(journal_pin_active(&d->journal));

	bch2_journal_pin_flush(&c->journal, &d->journal);
	kfree(d);
}

struct deferred_update *
bch2_deferred_update_alloc(struct bch_fs *c,
			   enum btree_id btree_id,
			   unsigned u64s)
{
	struct deferred_update *d;

	BUG_ON(u64s > U8_MAX);

	d = kmalloc(offsetof(struct deferred_update, k) +
		    u64s * sizeof(u64), GFP_NOFS);
	BUG_ON(!d);

	memset(d, 0, offsetof(struct deferred_update, k));

	spin_lock_init(&d->lock);
	d->allocated_u64s	= u64s;
	d->btree_id		= btree_id;

	return d;
}

/* Normal update interface: */

static inline void btree_insert_entry_checks(struct btree_trans *trans,
					     struct btree_insert_entry *i)
{
	struct bch_fs *c = trans->c;
	enum btree_id btree_id = !i->deferred
		? i->iter->btree_id
		: i->d->btree_id;

	if (!i->deferred) {
		BUG_ON(i->iter->level);
		BUG_ON(bkey_cmp(bkey_start_pos(&i->k->k), i->iter->pos));
		EBUG_ON((i->iter->flags & BTREE_ITER_IS_EXTENTS) &&
			!bch2_extent_is_atomic(i->k, i->iter));

		EBUG_ON((i->iter->flags & BTREE_ITER_IS_EXTENTS) &&
			!(trans->flags & BTREE_INSERT_ATOMIC));

		bch2_btree_iter_verify_locks(i->iter);
	}

	BUG_ON(debug_check_bkeys(c) &&
	       !bkey_deleted(&i->k->k) &&
	       bch2_bkey_invalid(c, bkey_i_to_s_c(i->k), btree_id));
}

static int bch2_trans_journal_preres_get(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	unsigned u64s = 0;
	int ret;

	trans_for_each_update(trans, i)
		if (i->deferred)
			u64s += jset_u64s(i->k->k.u64s);

	if (!u64s)
		return 0;

	ret = bch2_journal_preres_get(&c->journal,
			&trans->journal_preres, u64s,
			JOURNAL_RES_GET_NONBLOCK);
	if (ret != -EAGAIN)
		return ret;

	btree_trans_unlock(trans);

	ret = bch2_journal_preres_get(&c->journal,
			&trans->journal_preres, u64s, 0);
	if (ret)
		return ret;

	if (!btree_trans_relock(trans)) {
		trans_restart(" (iter relock after journal preres get blocked)");
		return -EINTR;
	}

	return 0;
}

static int bch2_trans_journal_res_get(struct btree_trans *trans,
				      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	unsigned u64s = 0;
	int ret;

	if (unlikely(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))
		return 0;

	if (trans->flags & BTREE_INSERT_JOURNAL_RESERVED)
		flags |= JOURNAL_RES_GET_RESERVED;

	trans_for_each_update(trans, i)
		u64s += jset_u64s(i->k->k.u64s);

	ret = bch2_journal_res_get(&c->journal, &trans->journal_res,
				   u64s, flags);

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
	unsigned u64s = 0;
	int ret;

	trans_for_each_update_iter(trans, i) {
		/* Multiple inserts might go to same leaf: */
		if (!same_leaf_as_prev(trans, i))
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
	if (likely(!insert->deferred))
		btree_insert_key_leaf(trans, insert);
	else
		btree_insert_key_deferred(trans, insert);
}

/*
 * Get journal reservation, take write locks, and attempt to do btree update(s):
 */
static inline int do_btree_insert_at(struct btree_trans *trans,
				     struct btree_insert_entry **stopped_at)
{
	struct bch_fs *c = trans->c;
	struct bch_fs_usage *fs_usage = NULL;
	struct btree_insert_entry *i;
	struct btree_iter *linked;
	int ret;

	trans_for_each_update_iter(trans, i)
		BUG_ON(i->iter->uptodate >= BTREE_ITER_NEED_RELOCK);

	btree_trans_lock_write(c, trans);

	trans_for_each_update_iter(trans, i) {
		if (i->deferred ||
		    !btree_node_type_needs_gc(i->iter->btree_id))
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

	if (race_fault()) {
		ret = -EINTR;
		trans_restart(" (race)");
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

	/*
	 * Don't get journal reservation until after we know insert will
	 * succeed:
	 */
	ret = bch2_trans_journal_res_get(trans, JOURNAL_RES_GET_NONBLOCK);
	if (ret)
		goto out;

	if (!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY)) {
		if (journal_seq_verify(c))
			trans_for_each_update(trans, i)
				i->k->k.version.lo = trans->journal_res.seq;
		else if (inject_invalid_keys(c))
			trans_for_each_update(trans, i)
				i->k->k.version = MAX_VERSION;
	}

	if (trans->flags & BTREE_INSERT_NOUNLOCK) {
		/*
		 * linked iterators that weren't being updated may or may not
		 * have been traversed/locked, depending on what the caller was
		 * doing:
		 */
		trans_for_each_update_iter(trans, i) {
			for_each_btree_iter(i->iter, linked)
				if (linked->uptodate < BTREE_ITER_NEED_RELOCK)
					linked->flags |= BTREE_ITER_NOUNLOCK;
			break;
		}
	}

	trans_for_each_update_iter(trans, i)
		bch2_mark_update(trans, i, fs_usage);
	if (fs_usage)
		bch2_trans_fs_usage_apply(trans, fs_usage);

	trans_for_each_update(trans, i)
		do_btree_insert_one(trans, i);
out:
	BUG_ON(ret &&
	       (trans->flags & BTREE_INSERT_JOURNAL_RESERVED) &&
	       trans->journal_res.ref);

	btree_trans_unlock_write(trans);

	if (fs_usage) {
		bch2_fs_usage_scratch_put(c, fs_usage);
		percpu_up_read(&c->mark_lock);
	}

	bch2_journal_res_put(&c->journal, &trans->journal_res);

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
		if (!ret || (flags & BTREE_INSERT_NOUNLOCK)) {
			trans_restart(" (split)");
			ret = -EINTR;
		}
		break;
	case BTREE_INSERT_ENOSPC:
		ret = -ENOSPC;
		break;
	case BTREE_INSERT_NEED_MARK_REPLICAS:
		bch2_trans_unlock(trans);

		trans_for_each_update_iter(trans, i) {
			ret = bch2_mark_bkey_replicas(c, bkey_i_to_s_c(i->k));
			if (ret)
				return ret;
		}

		if (btree_trans_relock(trans))
			return 0;

		trans_restart(" (iter relock after marking replicas)");
		ret = -EINTR;
		break;
	case BTREE_INSERT_NEED_JOURNAL_RES:
		btree_trans_unlock(trans);

		ret = bch2_trans_journal_res_get(trans, JOURNAL_RES_GET_CHECK);
		if (ret)
			return ret;

		if (btree_trans_relock(trans))
			return 0;

		trans_restart(" (iter relock after journal res get blocked)");
		ret = -EINTR;
		break;
	default:
		BUG_ON(ret >= 0);
		break;
	}

	if (ret == -EINTR) {
		trans_for_each_update_iter(trans, i) {
			int ret2 = bch2_btree_iter_traverse(i->iter);
			if (ret2) {
				trans_restart(" (traverse)");
				return ret2;
			}

			BUG_ON(i->iter->uptodate > BTREE_ITER_NEED_PEEK);
		}

		/*
		 * BTREE_ITER_ATOMIC means we have to return -EINTR if we
		 * dropped locks:
		 */
		if (!(flags & BTREE_INSERT_ATOMIC))
			return 0;

		trans_restart(" (atomic)");
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
	struct btree_iter *linked;
	int ret;

	trans_for_each_update_iter(trans, i) {
		unsigned old_locks_want = i->iter->locks_want;
		unsigned old_uptodate = i->iter->uptodate;

		if (!bch2_btree_iter_upgrade(i->iter, 1, true)) {
			trans_restart(" (failed upgrade, locks_want %u uptodate %u)",
				      old_locks_want, old_uptodate);
			ret = -EINTR;
			goto err;
		}

		if (i->iter->flags & BTREE_ITER_ERROR) {
			ret = -EIO;
			goto err;
		}
	}

	ret = do_btree_insert_at(trans, stopped_at);
	if (unlikely(ret))
		goto err;

	trans_for_each_update_leaf(trans, i)
		bch2_foreground_maybe_merge(c, i->iter, 0, trans->flags);

	trans_for_each_update_iter(trans, i)
		bch2_btree_iter_downgrade(i->iter);
err:
	/* make sure we didn't drop or screw up locks: */
	trans_for_each_update_iter(trans, i) {
		bch2_btree_iter_verify_locks(i->iter);
		break;
	}

	trans_for_each_update_iter(trans, i) {
		for_each_btree_iter(i->iter, linked)
			linked->flags &= ~BTREE_ITER_NOUNLOCK;
		break;
	}

	return ret;
}

int bch2_trans_commit(struct btree_trans *trans,
		      struct disk_reservation *disk_res,
		      u64 *journal_seq,
		      unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
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

	bubble_sort(trans->updates, trans->nr_updates, btree_trans_cmp);

	trans_for_each_update(trans, i)
		btree_insert_entry_checks(trans, i);

	if (unlikely(!(trans->flags & BTREE_INSERT_NOCHECK_RW) &&
		     !percpu_ref_tryget(&c->writes))) {
		if (likely(!(trans->flags & BTREE_INSERT_LAZY_RW)))
			return -EROFS;

		btree_trans_unlock(trans);

		ret = bch2_fs_read_write_early(c);
		if (ret)
			return ret;

		percpu_ref_get(&c->writes);

		if (!btree_trans_relock(trans)) {
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

	trans->nr_updates = 0;

	BUG_ON(!(trans->flags & BTREE_INSERT_ATOMIC) && ret == -EINTR);

	return ret;
err:
	ret = bch2_trans_commit_error(trans, i, ret);
	if (!ret)
		goto retry;

	goto out;
}

int bch2_btree_delete_at(struct btree_trans *trans,
			 struct btree_iter *iter, unsigned flags)
{
	struct bkey_i k;

	bkey_init(&k.k);
	k.k.p = iter->pos;

	bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, &k));
	return bch2_trans_commit(trans, NULL, NULL,
				 BTREE_INSERT_NOFAIL|
				 BTREE_INSERT_USE_RESERVE|flags);
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

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, id, bkey_start_pos(&k->k),
				   BTREE_ITER_INTENT);

	bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, k));

	ret = bch2_trans_commit(&trans, disk_res, journal_seq, flags);
	bch2_trans_exit(&trans);

	return ret;
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
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c);

	iter = bch2_trans_get_iter(&trans, id, start, BTREE_ITER_INTENT);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = btree_iter_err(k)) &&
	       bkey_cmp(iter->pos, end) < 0) {
		unsigned max_sectors = KEY_SIZE_MAX & (~0 << c->block_bits);
		/* really shouldn't be using a bare, unpadded bkey_i */
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
			/* create the biggest key we can */
			bch2_key_resize(&delete.k, max_sectors);
			bch2_cut_back(end, &delete.k);
			bch2_extent_trim_atomic(&delete, iter);
		}

		bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, &delete));

		ret = bch2_trans_commit(&trans, NULL, journal_seq,
					BTREE_INSERT_ATOMIC|
					BTREE_INSERT_NOFAIL);
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			break;

		bch2_btree_iter_cond_resched(iter);
	}

	bch2_trans_exit(&trans);
	return ret;
}
