// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "replicas.h"
#include "trace.h"

#include <linux/prefetch.h>

static void btree_iter_set_search_pos(struct btree_iter *, struct bpos);
static inline void btree_trans_sort_iters(struct btree_trans *);
static struct btree_iter *btree_iter_child_alloc(struct btree_trans *,
					struct btree_iter *, unsigned long);
static struct btree_iter *btree_trans_iter_alloc(struct btree_trans *,
						 struct btree_iter *);
static void btree_iter_copy(struct btree_trans *, struct btree_iter *, struct btree_iter *);

static inline int btree_iter_cmp(const struct btree_iter *l,
				 const struct btree_iter *r)
{
	return   cmp_int(l->btree_id,	r->btree_id) ?:
		-cmp_int(l->cached,	r->cached) ?:
		 bkey_cmp(l->real_pos,	r->real_pos) ?:
		-cmp_int(l->level,	r->level);
}

static inline struct bpos bkey_successor(struct btree_iter *iter, struct bpos p)
{
	/* Are we iterating over keys in all snapshots? */
	if (iter->flags & BTREE_ITER_ALL_SNAPSHOTS) {
		p = bpos_successor(p);
	} else {
		p = bpos_nosnap_successor(p);
		p.snapshot = iter->snapshot;
	}

	return p;
}

static inline struct bpos bkey_predecessor(struct btree_iter *iter, struct bpos p)
{
	/* Are we iterating over keys in all snapshots? */
	if (iter->flags & BTREE_ITER_ALL_SNAPSHOTS) {
		p = bpos_predecessor(p);
	} else {
		p = bpos_nosnap_predecessor(p);
		p.snapshot = iter->snapshot;
	}

	return p;
}

static inline bool is_btree_node(struct btree_iter *iter, unsigned l)
{
	return l < BTREE_MAX_DEPTH &&
		(unsigned long) iter->l[l].b >= 128;
}

static inline struct bpos btree_iter_search_key(struct btree_iter *iter)
{
	struct bpos pos = iter->pos;

	if ((iter->flags & BTREE_ITER_IS_EXTENTS) &&
	    bkey_cmp(pos, POS_MAX))
		pos = bkey_successor(iter, pos);
	return pos;
}

static inline bool btree_iter_pos_before_node(struct btree_iter *iter,
					      struct btree *b)
{
	return bpos_cmp(iter->real_pos, b->data->min_key) < 0;
}

static inline bool btree_iter_pos_after_node(struct btree_iter *iter,
					     struct btree *b)
{
	return bpos_cmp(b->key.k.p, iter->real_pos) < 0;
}

static inline bool btree_iter_pos_in_node(struct btree_iter *iter,
					  struct btree *b)
{
	return iter->btree_id == b->c.btree_id &&
		!btree_iter_pos_before_node(iter, b) &&
		!btree_iter_pos_after_node(iter, b);
}

/* Btree node locking: */

void bch2_btree_node_unlock_write(struct btree_trans *trans,
			struct btree_iter *iter, struct btree *b)
{
	bch2_btree_node_unlock_write_inlined(trans, iter, b);
}

void __bch2_btree_node_lock_write(struct btree_trans *trans, struct btree *b)
{
	struct btree_iter *iter;
	unsigned readers = 0;

	trans_for_each_iter(trans, iter)
		if (iter->l[b->c.level].b == b &&
		    btree_node_read_locked(iter, b->c.level))
			readers++;

	/*
	 * Must drop our read locks before calling six_lock_write() -
	 * six_unlock() won't do wakeups until the reader count
	 * goes to 0, and it's safe because we have the node intent
	 * locked:
	 */
	if (!b->c.lock.readers)
		atomic64_sub(__SIX_VAL(read_lock, readers),
			     &b->c.lock.state.counter);
	else
		this_cpu_sub(*b->c.lock.readers, readers);

	btree_node_lock_type(trans->c, b, SIX_LOCK_write);

	if (!b->c.lock.readers)
		atomic64_add(__SIX_VAL(read_lock, readers),
			     &b->c.lock.state.counter);
	else
		this_cpu_add(*b->c.lock.readers, readers);
}

bool __bch2_btree_node_relock(struct btree_trans *trans,
			      struct btree_iter *iter, unsigned level)
{
	struct btree *b = btree_iter_node(iter, level);
	int want = __btree_lock_want(iter, level);

	if (!is_btree_node(iter, level))
		return false;

	if (race_fault())
		return false;

	if (six_relock_type(&b->c.lock, want, iter->l[level].lock_seq) ||
	    (btree_node_lock_seq_matches(iter, b, level) &&
	     btree_node_lock_increment(trans, b, level, want))) {
		mark_btree_node_locked(iter, level, want);
		return true;
	} else {
		return false;
	}
}

static bool bch2_btree_node_upgrade(struct btree_trans *trans,
				    struct btree_iter *iter, unsigned level)
{
	struct btree *b = iter->l[level].b;

	EBUG_ON(btree_lock_want(iter, level) != BTREE_NODE_INTENT_LOCKED);

	if (!is_btree_node(iter, level))
		return false;

	if (btree_node_intent_locked(iter, level))
		return true;

	if (race_fault())
		return false;

	if (btree_node_locked(iter, level)
	    ? six_lock_tryupgrade(&b->c.lock)
	    : six_relock_type(&b->c.lock, SIX_LOCK_intent, iter->l[level].lock_seq))
		goto success;

	if (btree_node_lock_seq_matches(iter, b, level) &&
	    btree_node_lock_increment(trans, b, level, BTREE_NODE_INTENT_LOCKED)) {
		btree_node_unlock(iter, level);
		goto success;
	}

	return false;
success:
	mark_btree_node_intent_locked(iter, level);
	return true;
}

static inline bool btree_iter_get_locks(struct btree_trans *trans,
					struct btree_iter *iter,
					bool upgrade, unsigned long trace_ip)
{
	unsigned l = iter->level;
	int fail_idx = -1;

	do {
		if (!btree_iter_node(iter, l))
			break;

		if (!(upgrade
		      ? bch2_btree_node_upgrade(trans, iter, l)
		      : bch2_btree_node_relock(trans, iter, l))) {
			(upgrade
			 ? trace_node_upgrade_fail
			 : trace_node_relock_fail)(trans->ip, trace_ip,
					iter->cached,
					iter->btree_id, &iter->real_pos,
					l, iter->l[l].lock_seq,
					is_btree_node(iter, l)
					? 0
					: (unsigned long) iter->l[l].b,
					is_btree_node(iter, l)
					? iter->l[l].b->c.lock.state.seq
					: 0);
			fail_idx = l;
			btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		}

		l++;
	} while (l < iter->locks_want);

	/*
	 * When we fail to get a lock, we have to ensure that any child nodes
	 * can't be relocked so bch2_btree_iter_traverse has to walk back up to
	 * the node that we failed to relock:
	 */
	while (fail_idx >= 0) {
		btree_node_unlock(iter, fail_idx);
		iter->l[fail_idx].b = BTREE_ITER_NO_NODE_GET_LOCKS;
		--fail_idx;
	}

	if (iter->uptodate == BTREE_ITER_NEED_RELOCK)
		iter->uptodate = BTREE_ITER_UPTODATE;

	bch2_trans_verify_locks(trans);

	return iter->uptodate < BTREE_ITER_NEED_RELOCK;
}

static struct bpos btree_node_pos(struct btree_bkey_cached_common *_b,
				  bool cached)
{
	return !cached
		? container_of(_b, struct btree, c)->key.k.p
		: container_of(_b, struct bkey_cached, c)->key.pos;
}

/* Slowpath: */
bool __bch2_btree_node_lock(struct btree_trans *trans,
			    struct btree_iter *iter,
			    struct btree *b, struct bpos pos, unsigned level,
			    enum six_lock_type type,
			    six_lock_should_sleep_fn should_sleep_fn, void *p,
			    unsigned long ip)
{
	struct btree_iter *linked, *deadlock_iter = NULL;
	u64 start_time = local_clock();
	unsigned reason = 9;
	bool ret;

	/* Check if it's safe to block: */
	trans_for_each_iter(trans, linked) {
		if (!linked->nodes_locked)
			continue;

		/*
		 * Can't block taking an intent lock if we have _any_ nodes read
		 * locked:
		 *
		 * - Our read lock blocks another thread with an intent lock on
		 *   the same node from getting a write lock, and thus from
		 *   dropping its intent lock
		 *
		 * - And the other thread may have multiple nodes intent locked:
		 *   both the node we want to intent lock, and the node we
		 *   already have read locked - deadlock:
		 */
		if (type == SIX_LOCK_intent &&
		    linked->nodes_locked != linked->nodes_intent_locked) {
			deadlock_iter = linked;
			reason = 1;
		}

		if (linked->btree_id != iter->btree_id) {
			if (linked->btree_id > iter->btree_id) {
				deadlock_iter = linked;
				reason = 3;
			}
			continue;
		}

		/*
		 * Within the same btree, cached iterators come before non
		 * cached iterators:
		 */
		if (linked->cached != iter->cached) {
			if (iter->cached) {
				deadlock_iter = linked;
				reason = 4;
			}
			continue;
		}

		/*
		 * Interior nodes must be locked before their descendants: if
		 * another iterator has possible descendants locked of the node
		 * we're about to lock, it must have the ancestors locked too:
		 */
		if (level > __fls(linked->nodes_locked)) {
			deadlock_iter = linked;
			reason = 5;
		}

		/* Must lock btree nodes in key order: */
		if (btree_node_locked(linked, level) &&
		    bpos_cmp(pos, btree_node_pos((void *) linked->l[level].b,
						 linked->cached)) <= 0) {
			deadlock_iter = linked;
			reason = 7;
		}
	}

	if (unlikely(deadlock_iter)) {
		trace_trans_restart_would_deadlock(trans->ip, ip,
				trans->in_traverse_all, reason,
				deadlock_iter->btree_id,
				deadlock_iter->cached,
				&deadlock_iter->real_pos,
				iter->btree_id,
				iter->cached,
				&pos);
		btree_trans_restart(trans);
		return false;
	}

	if (six_trylock_type(&b->c.lock, type))
		return true;

#ifdef CONFIG_BCACHEFS_DEBUG
	trans->locking_iter_idx = iter->idx;
	trans->locking_pos	= pos;
	trans->locking_btree_id	= iter->btree_id;
	trans->locking_level	= level;
	trans->locking		= b;
#endif

	ret = six_lock_type(&b->c.lock, type, should_sleep_fn, p) == 0;

#ifdef CONFIG_BCACHEFS_DEBUG
	trans->locking = NULL;
#endif
	if (ret)
		bch2_time_stats_update(&trans->c->times[lock_to_time_stat(type)],
				       start_time);
	return ret;
}

/* Btree iterator locking: */

#ifdef CONFIG_BCACHEFS_DEBUG
static void bch2_btree_iter_verify_locks(struct btree_iter *iter)
{
	unsigned l;

	for (l = 0; btree_iter_node(iter, l); l++) {
		if (iter->uptodate >= BTREE_ITER_NEED_RELOCK &&
		    !btree_node_locked(iter, l))
			continue;

		BUG_ON(btree_lock_want(iter, l) !=
		       btree_node_locked_type(iter, l));
	}
}

void bch2_trans_verify_locks(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		bch2_btree_iter_verify_locks(iter);
}
#else
static inline void bch2_btree_iter_verify_locks(struct btree_iter *iter) {}
#endif

/*
 * Only for btree_cache.c - only relocks intent locks
 */
bool bch2_btree_iter_relock_intent(struct btree_trans *trans,
				   struct btree_iter *iter)
{
	unsigned l;

	for (l = iter->level;
	     l < iter->locks_want && btree_iter_node(iter, l);
	     l++) {
		if (!bch2_btree_node_relock(trans, iter, l)) {
			trace_node_relock_fail(trans->ip, _RET_IP_,
					iter->cached,
					iter->btree_id, &iter->real_pos,
					l, iter->l[l].lock_seq,
					is_btree_node(iter, l)
					? 0
					: (unsigned long) iter->l[l].b,
					is_btree_node(iter, l)
					? iter->l[l].b->c.lock.state.seq
					: 0);
			btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
			btree_trans_restart(trans);
			return false;
		}
	}

	return true;
}

__flatten
static bool bch2_btree_iter_relock(struct btree_trans *trans,
			struct btree_iter *iter, unsigned long trace_ip)
{
	bool ret = btree_iter_get_locks(trans, iter, false, trace_ip);

	if (!ret)
		btree_trans_restart(trans);
	return ret;
}

bool __bch2_btree_iter_upgrade(struct btree_trans *trans,
			       struct btree_iter *iter,
			       unsigned new_locks_want)
{
	struct btree_iter *linked;

	EBUG_ON(iter->locks_want >= new_locks_want);

	iter->locks_want = new_locks_want;

	if (btree_iter_get_locks(trans, iter, true, _THIS_IP_))
		return true;

	/*
	 * XXX: this is ugly - we'd prefer to not be mucking with other
	 * iterators in the btree_trans here.
	 *
	 * On failure to upgrade the iterator, setting iter->locks_want and
	 * calling get_locks() is sufficient to make bch2_btree_iter_traverse()
	 * get the locks we want on transaction restart.
	 *
	 * But if this iterator was a clone, on transaction restart what we did
	 * to this iterator isn't going to be preserved.
	 *
	 * Possibly we could add an iterator field for the parent iterator when
	 * an iterator is a copy - for now, we'll just upgrade any other
	 * iterators with the same btree id.
	 *
	 * The code below used to be needed to ensure ancestor nodes get locked
	 * before interior nodes - now that's handled by
	 * bch2_btree_iter_traverse_all().
	 */
	trans_for_each_iter(trans, linked)
		if (linked != iter &&
		    linked->cached == iter->cached &&
		    linked->btree_id == iter->btree_id &&
		    linked->locks_want < new_locks_want) {
			linked->locks_want = new_locks_want;
			btree_iter_get_locks(trans, linked, true, _THIS_IP_);
		}

	if (iter->should_be_locked)
		btree_trans_restart(trans);
	return false;
}

void __bch2_btree_iter_downgrade(struct btree_iter *iter,
				 unsigned new_locks_want)
{
	unsigned l;

	EBUG_ON(iter->locks_want < new_locks_want);

	iter->locks_want = new_locks_want;

	while (iter->nodes_locked &&
	       (l = __fls(iter->nodes_locked)) >= iter->locks_want) {
		if (l > iter->level) {
			btree_node_unlock(iter, l);
		} else {
			if (btree_node_intent_locked(iter, l)) {
				six_lock_downgrade(&iter->l[l].b->c.lock);
				iter->nodes_intent_locked ^= 1 << l;
			}
			break;
		}
	}

	bch2_btree_iter_verify_locks(iter);
}

void bch2_trans_downgrade(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		bch2_btree_iter_downgrade(iter);
}

/* Btree transaction locking: */

static inline bool btree_iter_should_be_locked(struct btree_iter *iter)
{
	return (iter->flags & BTREE_ITER_KEEP_UNTIL_COMMIT) ||
		iter->should_be_locked;
}

bool bch2_trans_relock(struct btree_trans *trans)
{
	struct btree_iter *iter;

	if (unlikely(trans->restarted))
		return false;

	trans_for_each_iter(trans, iter)
		if (btree_iter_should_be_locked(iter) &&
		    !bch2_btree_iter_relock(trans, iter, _RET_IP_)) {
			trace_trans_restart_relock(trans->ip, _RET_IP_,
					iter->btree_id, &iter->real_pos);
			BUG_ON(!trans->restarted);
			return false;
		}
	return true;
}

void bch2_trans_unlock(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		__bch2_btree_iter_unlock(iter);
}

/* Btree iterator: */

#ifdef CONFIG_BCACHEFS_DEBUG

static void bch2_btree_iter_verify_cached(struct btree_trans *trans,
					  struct btree_iter *iter)
{
	struct bkey_cached *ck;
	bool locked = btree_node_locked(iter, 0);

	if (!bch2_btree_node_relock(trans, iter, 0))
		return;

	ck = (void *) iter->l[0].b;
	BUG_ON(ck->key.btree_id != iter->btree_id ||
	       bkey_cmp(ck->key.pos, iter->pos));

	if (!locked)
		btree_node_unlock(iter, 0);
}

static void bch2_btree_iter_verify_level(struct btree_trans *trans,
				struct btree_iter *iter, unsigned level)
{
	struct btree_iter_level *l;
	struct btree_node_iter tmp;
	bool locked;
	struct bkey_packed *p, *k;
	char buf1[100], buf2[100], buf3[100];
	const char *msg;

	if (!bch2_debug_check_iterators)
		return;

	l	= &iter->l[level];
	tmp	= l->iter;
	locked	= btree_node_locked(iter, level);

	if (iter->cached) {
		if (!level)
			bch2_btree_iter_verify_cached(trans, iter);
		return;
	}

	BUG_ON(iter->level < iter->min_depth);

	if (!btree_iter_node(iter, level))
		return;

	if (!bch2_btree_node_relock(trans, iter, level))
		return;

	BUG_ON(!btree_iter_pos_in_node(iter, l->b));

	bch2_btree_node_iter_verify(&l->iter, l->b);

	/*
	 * For interior nodes, the iterator will have skipped past
	 * deleted keys:
	 *
	 * For extents, the iterator may have skipped past deleted keys (but not
	 * whiteouts)
	 */
	p = level || btree_node_type_is_extents(iter->btree_id)
		? bch2_btree_node_iter_prev(&tmp, l->b)
		: bch2_btree_node_iter_prev_all(&tmp, l->b);
	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);

	if (p && bkey_iter_pos_cmp(l->b, p, &iter->real_pos) >= 0) {
		msg = "before";
		goto err;
	}

	if (k && bkey_iter_pos_cmp(l->b, k, &iter->real_pos) < 0) {
		msg = "after";
		goto err;
	}

	if (!locked)
		btree_node_unlock(iter, level);
	return;
err:
	strcpy(buf2, "(none)");
	strcpy(buf3, "(none)");

	bch2_bpos_to_text(&PBUF(buf1), iter->real_pos);

	if (p) {
		struct bkey uk = bkey_unpack_key(l->b, p);
		bch2_bkey_to_text(&PBUF(buf2), &uk);
	}

	if (k) {
		struct bkey uk = bkey_unpack_key(l->b, k);
		bch2_bkey_to_text(&PBUF(buf3), &uk);
	}

	panic("iterator should be %s key at level %u:\n"
	      "iter pos %s\n"
	      "prev key %s\n"
	      "cur  key %s\n",
	      msg, level, buf1, buf2, buf3);
}

static void bch2_btree_iter_verify(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct bch_fs *c = trans->c;
	unsigned i;

	EBUG_ON(iter->btree_id >= BTREE_ID_NR);

	BUG_ON(!(iter->flags & BTREE_ITER_ALL_SNAPSHOTS) &&
	       iter->pos.snapshot != iter->snapshot);

	BUG_ON((iter->flags & BTREE_ITER_IS_EXTENTS) &&
	       (iter->flags & BTREE_ITER_ALL_SNAPSHOTS));

	BUG_ON(!(iter->flags & __BTREE_ITER_ALL_SNAPSHOTS) &&
	       (iter->flags & BTREE_ITER_ALL_SNAPSHOTS) &&
	       !btree_type_has_snapshots(iter->btree_id));

	for (i = 0; i < (!iter->cached ? BTREE_MAX_DEPTH : 1); i++) {
		if (!iter->l[i].b) {
			BUG_ON(c->btree_roots[iter->btree_id].b->c.level > i);
			break;
		}

		bch2_btree_iter_verify_level(trans, iter, i);
	}

	bch2_btree_iter_verify_locks(iter);
}

static void bch2_btree_iter_verify_entry_exit(struct btree_iter *iter)
{
	BUG_ON(!(iter->flags & BTREE_ITER_ALL_SNAPSHOTS) &&
	       iter->pos.snapshot != iter->snapshot);

	BUG_ON(bkey_cmp(iter->pos, bkey_start_pos(&iter->k)) < 0 ||
	       bkey_cmp(iter->pos, iter->k.p) > 0);
}

void bch2_trans_verify_iters(struct btree_trans *trans, struct btree *b)
{
	struct btree_iter *iter;

	if (!bch2_debug_check_iterators)
		return;

	trans_for_each_iter_with_node(trans, b, iter)
		bch2_btree_iter_verify_level(trans, iter, b->c.level);
}

#else

static inline void bch2_btree_iter_verify_level(struct btree_trans *trans,
					struct btree_iter *iter, unsigned l) {}
static inline void bch2_btree_iter_verify(struct btree_iter *iter) {}
static inline void bch2_btree_iter_verify_entry_exit(struct btree_iter *iter) {}

#endif

static void btree_node_iter_set_set_pos(struct btree_node_iter *iter,
					struct btree *b,
					struct bset_tree *t,
					struct bkey_packed *k)
{
	struct btree_node_iter_set *set;

	btree_node_iter_for_each(iter, set)
		if (set->end == t->end_offset) {
			set->k = __btree_node_key_to_offset(b, k);
			bch2_btree_node_iter_sort(iter, b);
			return;
		}

	bch2_btree_node_iter_push(iter, b, k, btree_bkey_last(b, t));
}

static void __bch2_btree_iter_fix_key_modified(struct btree_iter *iter,
					       struct btree *b,
					       struct bkey_packed *where)
{
	struct btree_iter_level *l = &iter->l[b->c.level];

	if (where != bch2_btree_node_iter_peek_all(&l->iter, l->b))
		return;

	if (bkey_iter_pos_cmp(l->b, where, &iter->real_pos) < 0)
		bch2_btree_node_iter_advance(&l->iter, l->b);
}

void bch2_btree_iter_fix_key_modified(struct btree_trans *trans,
				      struct btree *b,
				      struct bkey_packed *where)
{
	struct btree_iter *iter;

	trans_for_each_iter_with_node(trans, b, iter) {
		__bch2_btree_iter_fix_key_modified(iter, b, where);
		bch2_btree_iter_verify_level(trans, iter, b->c.level);
	}
}

static void __bch2_btree_node_iter_fix(struct btree_iter *iter,
				      struct btree *b,
				      struct btree_node_iter *node_iter,
				      struct bset_tree *t,
				      struct bkey_packed *where,
				      unsigned clobber_u64s,
				      unsigned new_u64s)
{
	const struct bkey_packed *end = btree_bkey_last(b, t);
	struct btree_node_iter_set *set;
	unsigned offset = __btree_node_key_to_offset(b, where);
	int shift = new_u64s - clobber_u64s;
	unsigned old_end = t->end_offset - shift;
	unsigned orig_iter_pos = node_iter->data[0].k;
	bool iter_current_key_modified =
		orig_iter_pos >= offset &&
		orig_iter_pos <= offset + clobber_u64s;

	btree_node_iter_for_each(node_iter, set)
		if (set->end == old_end)
			goto found;

	/* didn't find the bset in the iterator - might have to readd it: */
	if (new_u64s &&
	    bkey_iter_pos_cmp(b, where, &iter->real_pos) >= 0) {
		bch2_btree_node_iter_push(node_iter, b, where, end);
		goto fixup_done;
	} else {
		/* Iterator is after key that changed */
		return;
	}
found:
	set->end = t->end_offset;

	/* Iterator hasn't gotten to the key that changed yet: */
	if (set->k < offset)
		return;

	if (new_u64s &&
	    bkey_iter_pos_cmp(b, where, &iter->real_pos) >= 0) {
		set->k = offset;
	} else if (set->k < offset + clobber_u64s) {
		set->k = offset + new_u64s;
		if (set->k == set->end)
			bch2_btree_node_iter_set_drop(node_iter, set);
	} else {
		/* Iterator is after key that changed */
		set->k = (int) set->k + shift;
		return;
	}

	bch2_btree_node_iter_sort(node_iter, b);
fixup_done:
	if (node_iter->data[0].k != orig_iter_pos)
		iter_current_key_modified = true;

	/*
	 * When a new key is added, and the node iterator now points to that
	 * key, the iterator might have skipped past deleted keys that should
	 * come after the key the iterator now points to. We have to rewind to
	 * before those deleted keys - otherwise
	 * bch2_btree_node_iter_prev_all() breaks:
	 */
	if (!bch2_btree_node_iter_end(node_iter) &&
	    iter_current_key_modified &&
	    (b->c.level ||
	     btree_node_type_is_extents(iter->btree_id))) {
		struct bset_tree *t;
		struct bkey_packed *k, *k2, *p;

		k = bch2_btree_node_iter_peek_all(node_iter, b);

		for_each_bset(b, t) {
			bool set_pos = false;

			if (node_iter->data[0].end == t->end_offset)
				continue;

			k2 = bch2_btree_node_iter_bset_pos(node_iter, b, t);

			while ((p = bch2_bkey_prev_all(b, t, k2)) &&
			       bkey_iter_cmp(b, k, p) < 0) {
				k2 = p;
				set_pos = true;
			}

			if (set_pos)
				btree_node_iter_set_set_pos(node_iter,
							    b, t, k2);
		}
	}
}

void bch2_btree_node_iter_fix(struct btree_trans *trans,
			      struct btree_iter *iter,
			      struct btree *b,
			      struct btree_node_iter *node_iter,
			      struct bkey_packed *where,
			      unsigned clobber_u64s,
			      unsigned new_u64s)
{
	struct bset_tree *t = bch2_bkey_to_bset_inlined(b, where);
	struct btree_iter *linked;

	if (node_iter != &iter->l[b->c.level].iter) {
		__bch2_btree_node_iter_fix(iter, b, node_iter, t,
					   where, clobber_u64s, new_u64s);

		if (bch2_debug_check_iterators)
			bch2_btree_node_iter_verify(node_iter, b);
	}

	trans_for_each_iter_with_node(trans, b, linked) {
		__bch2_btree_node_iter_fix(linked, b,
					   &linked->l[b->c.level].iter, t,
					   where, clobber_u64s, new_u64s);
		bch2_btree_iter_verify_level(trans, linked, b->c.level);
	}
}

static inline struct bkey_s_c __btree_iter_unpack(struct btree_iter *iter,
						  struct btree_iter_level *l,
						  struct bkey *u,
						  struct bkey_packed *k)
{
	struct bkey_s_c ret;

	if (unlikely(!k)) {
		/*
		 * signal to bch2_btree_iter_peek_slot() that we're currently at
		 * a hole
		 */
		u->type = KEY_TYPE_deleted;
		return bkey_s_c_null;
	}

	ret = bkey_disassemble(l->b, k, u);

	/*
	 * XXX: bch2_btree_bset_insert_key() generates invalid keys when we
	 * overwrite extents - it sets k->type = KEY_TYPE_deleted on the key
	 * being overwritten but doesn't change k->size. But this is ok, because
	 * those keys are never written out, we just have to avoid a spurious
	 * assertion here:
	 */
	if (bch2_debug_check_bkeys && !bkey_deleted(ret.k))
		bch2_bkey_debugcheck(iter->trans->c, l->b, ret);

	return ret;
}

/* peek_all() doesn't skip deleted keys */
static inline struct bkey_s_c btree_iter_level_peek_all(struct btree_iter *iter,
							struct btree_iter_level *l)
{
	return __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_peek_all(&l->iter, l->b));
}

static inline struct bkey_s_c btree_iter_level_peek(struct btree_iter *iter,
						    struct btree_iter_level *l)
{
	struct bkey_s_c k = __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_peek(&l->iter, l->b));

	iter->real_pos = k.k ? k.k->p : l->b->key.k.p;
	iter->trans->iters_sorted = false;
	return k;
}

static inline struct bkey_s_c btree_iter_level_prev(struct btree_iter *iter,
						    struct btree_iter_level *l)
{
	struct bkey_s_c k = __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_prev(&l->iter, l->b));

	iter->real_pos = k.k ? k.k->p : l->b->data->min_key;
	iter->trans->iters_sorted = false;
	return k;
}

static inline bool btree_iter_advance_to_pos(struct btree_iter *iter,
					     struct btree_iter_level *l,
					     int max_advance)
{
	struct bkey_packed *k;
	int nr_advanced = 0;

	while ((k = bch2_btree_node_iter_peek_all(&l->iter, l->b)) &&
	       bkey_iter_pos_cmp(l->b, k, &iter->real_pos) < 0) {
		if (max_advance > 0 && nr_advanced >= max_advance)
			return false;

		bch2_btree_node_iter_advance(&l->iter, l->b);
		nr_advanced++;
	}

	return true;
}

/*
 * Verify that iterator for parent node points to child node:
 */
static void btree_iter_verify_new_node(struct btree_trans *trans,
				       struct btree_iter *iter, struct btree *b)
{
	struct btree_iter_level *l;
	unsigned plevel;
	bool parent_locked;
	struct bkey_packed *k;

	if (!IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		return;

	plevel = b->c.level + 1;
	if (!btree_iter_node(iter, plevel))
		return;

	parent_locked = btree_node_locked(iter, plevel);

	if (!bch2_btree_node_relock(trans, iter, plevel))
		return;

	l = &iter->l[plevel];
	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);
	if (!k ||
	    bkey_deleted(k) ||
	    bkey_cmp_left_packed(l->b, k, &b->key.k.p)) {
		char buf1[100];
		char buf2[100];
		char buf3[100];
		char buf4[100];
		struct bkey uk = bkey_unpack_key(b, k);

		bch2_dump_btree_node(trans->c, l->b);
		bch2_bpos_to_text(&PBUF(buf1), iter->real_pos);
		bch2_bkey_to_text(&PBUF(buf2), &uk);
		bch2_bpos_to_text(&PBUF(buf3), b->data->min_key);
		bch2_bpos_to_text(&PBUF(buf3), b->data->max_key);
		panic("parent iter doesn't point to new node:\n"
		      "iter pos %s %s\n"
		      "iter key %s\n"
		      "new node %s-%s\n",
		      bch2_btree_ids[iter->btree_id], buf1,
		      buf2, buf3, buf4);
	}

	if (!parent_locked)
		btree_node_unlock(iter, b->c.level + 1);
}

static inline void __btree_iter_level_init(struct btree_iter *iter,
					   unsigned level)
{
	struct btree_iter_level *l = &iter->l[level];

	bch2_btree_node_iter_init(&l->iter, l->b, &iter->real_pos);

	/*
	 * Iterators to interior nodes should always be pointed at the first non
	 * whiteout:
	 */
	if (level)
		bch2_btree_node_iter_peek(&l->iter, l->b);
}

static inline void btree_iter_level_init(struct btree_trans *trans,
					 struct btree_iter *iter,
					 struct btree *b)
{
	BUG_ON(iter->cached);

	btree_iter_verify_new_node(trans, iter, b);

	EBUG_ON(!btree_iter_pos_in_node(iter, b));
	EBUG_ON(b->c.lock.state.seq & 1);

	iter->l[b->c.level].lock_seq = b->c.lock.state.seq;
	iter->l[b->c.level].b = b;
	__btree_iter_level_init(iter, b->c.level);
}

/*
 * A btree node is being replaced - update the iterator to point to the new
 * node:
 */
void bch2_trans_node_add(struct btree_trans *trans, struct btree *b)
{
	enum btree_node_locked_type t;
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		if (!iter->cached &&
		    btree_iter_pos_in_node(iter, b)) {
			/*
			 * bch2_trans_node_drop() has already been called -
			 * the old node we're replacing has already been
			 * unlocked and the pointer invalidated
			 */
			BUG_ON(btree_node_locked(iter, b->c.level));

			t = btree_lock_want(iter, b->c.level);
			if (t != BTREE_NODE_UNLOCKED) {
				six_lock_increment(&b->c.lock, (enum six_lock_type) t);
				mark_btree_node_locked(iter, b->c.level, (enum six_lock_type) t);
			}

			btree_iter_level_init(trans, iter, b);
		}
}

void bch2_trans_node_drop(struct btree_trans *trans, struct btree *b)
{
	struct btree_iter *iter;
	unsigned level = b->c.level;

	trans_for_each_iter(trans, iter)
		if (iter->l[level].b == b) {
			btree_node_unlock(iter, level);
			iter->l[level].b = BTREE_ITER_NO_NODE_DROP;
		}
}

/*
 * A btree node has been modified in such a way as to invalidate iterators - fix
 * them:
 */
void bch2_trans_node_reinit_iter(struct btree_trans *trans, struct btree *b)
{
	struct btree_iter *iter;

	trans_for_each_iter_with_node(trans, b, iter)
		__btree_iter_level_init(iter, b->c.level);
}

static int lock_root_check_fn(struct six_lock *lock, void *p)
{
	struct btree *b = container_of(lock, struct btree, c.lock);
	struct btree **rootp = p;

	return b == *rootp ? 0 : -1;
}

static inline int btree_iter_lock_root(struct btree_trans *trans,
				       struct btree_iter *iter,
				       unsigned depth_want,
				       unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree *b, **rootp = &c->btree_roots[iter->btree_id].b;
	enum six_lock_type lock_type;
	unsigned i;

	EBUG_ON(iter->nodes_locked);

	while (1) {
		b = READ_ONCE(*rootp);
		iter->level = READ_ONCE(b->c.level);

		if (unlikely(iter->level < depth_want)) {
			/*
			 * the root is at a lower depth than the depth we want:
			 * got to the end of the btree, or we're walking nodes
			 * greater than some depth and there are no nodes >=
			 * that depth
			 */
			iter->level = depth_want;
			for (i = iter->level; i < BTREE_MAX_DEPTH; i++)
				iter->l[i].b = NULL;
			return 1;
		}

		lock_type = __btree_lock_want(iter, iter->level);
		if (unlikely(!btree_node_lock(trans, iter, b, SPOS_MAX,
					      iter->level, lock_type,
					      lock_root_check_fn, rootp,
					      trace_ip))) {
			if (trans->restarted)
				return -EINTR;
			continue;
		}

		if (likely(b == READ_ONCE(*rootp) &&
			   b->c.level == iter->level &&
			   !race_fault())) {
			for (i = 0; i < iter->level; i++)
				iter->l[i].b = BTREE_ITER_NO_NODE_LOCK_ROOT;
			iter->l[iter->level].b = b;
			for (i = iter->level + 1; i < BTREE_MAX_DEPTH; i++)
				iter->l[i].b = NULL;

			mark_btree_node_locked(iter, iter->level, lock_type);
			btree_iter_level_init(trans, iter, b);
			return 0;
		}

		six_unlock_type(&b->c.lock, lock_type);
	}
}

noinline
static int btree_iter_prefetch(struct btree_trans *trans, struct btree_iter *iter)
{
	struct bch_fs *c = trans->c;
	struct btree_iter_level *l = &iter->l[iter->level];
	struct btree_node_iter node_iter = l->iter;
	struct bkey_packed *k;
	struct bkey_buf tmp;
	unsigned nr = test_bit(BCH_FS_STARTED, &c->flags)
		? (iter->level > 1 ? 0 :  2)
		: (iter->level > 1 ? 1 : 16);
	bool was_locked = btree_node_locked(iter, iter->level);
	int ret = 0;

	bch2_bkey_buf_init(&tmp);

	while (nr && !ret) {
		if (!bch2_btree_node_relock(trans, iter, iter->level))
			break;

		bch2_btree_node_iter_advance(&node_iter, l->b);
		k = bch2_btree_node_iter_peek(&node_iter, l->b);
		if (!k)
			break;

		bch2_bkey_buf_unpack(&tmp, c, l->b, k);
		ret = bch2_btree_node_prefetch(c, trans, iter, tmp.k,
					iter->btree_id, iter->level - 1);
	}

	if (!was_locked)
		btree_node_unlock(iter, iter->level);

	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

static noinline void btree_node_mem_ptr_set(struct btree_trans *trans,
					    struct btree_iter *iter,
					    unsigned plevel, struct btree *b)
{
	struct btree_iter_level *l = &iter->l[plevel];
	bool locked = btree_node_locked(iter, plevel);
	struct bkey_packed *k;
	struct bch_btree_ptr_v2 *bp;

	if (!bch2_btree_node_relock(trans, iter, plevel))
		return;

	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);
	BUG_ON(k->type != KEY_TYPE_btree_ptr_v2);

	bp = (void *) bkeyp_val(&l->b->format, k);
	bp->mem_ptr = (unsigned long)b;

	if (!locked)
		btree_node_unlock(iter, plevel);
}

static __always_inline int btree_iter_down(struct btree_trans *trans,
					   struct btree_iter *iter,
					   unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_iter_level *l = &iter->l[iter->level];
	struct btree *b;
	unsigned level = iter->level - 1;
	enum six_lock_type lock_type = __btree_lock_want(iter, level);
	struct bkey_buf tmp;
	int ret;

	EBUG_ON(!btree_node_locked(iter, iter->level));

	bch2_bkey_buf_init(&tmp);
	bch2_bkey_buf_unpack(&tmp, c, l->b,
			 bch2_btree_node_iter_peek(&l->iter, l->b));

	b = bch2_btree_node_get(trans, iter, tmp.k, level, lock_type, trace_ip);
	ret = PTR_ERR_OR_ZERO(b);
	if (unlikely(ret))
		goto err;

	mark_btree_node_locked(iter, level, lock_type);
	btree_iter_level_init(trans, iter, b);

	if (tmp.k->k.type == KEY_TYPE_btree_ptr_v2 &&
	    unlikely(b != btree_node_mem_ptr(tmp.k)))
		btree_node_mem_ptr_set(trans, iter, level + 1, b);

	if (iter->flags & BTREE_ITER_PREFETCH)
		ret = btree_iter_prefetch(trans, iter);

	if (btree_node_read_locked(iter, level + 1))
		btree_node_unlock(iter, level + 1);
	iter->level = level;

	bch2_btree_iter_verify_locks(iter);
err:
	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

static int btree_iter_traverse_one(struct btree_trans *,
			struct btree_iter *, unsigned long);

static int __btree_iter_traverse_all(struct btree_trans *trans, int ret,
				     unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter, *prev = NULL;
	int i;

	if (trans->in_traverse_all)
		return -EINTR;

	trans->in_traverse_all = true;
retry_all:
	trans->restarted = false;

	trans_for_each_iter(trans, iter)
		iter->should_be_locked = false;

	btree_trans_sort_iters(trans);

	trans_for_each_iter_inorder_reverse(trans, iter, i) {
		if (prev) {
			if (iter->btree_id == prev->btree_id &&
			    iter->locks_want < prev->locks_want)
				__bch2_btree_iter_upgrade(trans, iter, prev->locks_want);
			else if (!iter->locks_want && prev->locks_want)
				__bch2_btree_iter_upgrade(trans, iter, 1);
		}

		prev = iter;
	}

	bch2_trans_unlock(trans);
	cond_resched();

	if (unlikely(ret == -ENOMEM)) {
		struct closure cl;

		closure_init_stack(&cl);

		do {
			ret = bch2_btree_cache_cannibalize_lock(c, &cl);
			closure_sync(&cl);
		} while (ret);
	}

	if (unlikely(ret == -EIO)) {
		trans->error = true;
		goto out;
	}

	BUG_ON(ret && ret != -EINTR);

	/* Now, redo traversals in correct order: */
	i = 0;
	while (i < trans->nr_sorted) {
		iter = trans->iters + trans->sorted[i];

		EBUG_ON(!(trans->iters_linked & (1ULL << iter->idx)));

		ret = btree_iter_traverse_one(trans, iter, _THIS_IP_);
		if (ret)
			goto retry_all;

		EBUG_ON(!(trans->iters_linked & (1ULL << iter->idx)));

		if (iter->nodes_locked)
			i++;
	}

	/*
	 * BTREE_ITER_NEED_RELOCK is ok here - if we called bch2_trans_unlock()
	 * and relock(), relock() won't relock since iter->should_be_locked
	 * isn't set yet, which is all fine
	 */
	trans_for_each_iter(trans, iter)
		BUG_ON(iter->uptodate >= BTREE_ITER_NEED_TRAVERSE);
out:
	bch2_btree_cache_cannibalize_unlock(c);

	trans->in_traverse_all = false;

	trace_trans_traverse_all(trans->ip, trace_ip);
	return ret;
}

static int bch2_btree_iter_traverse_all(struct btree_trans *trans)
{
	return __btree_iter_traverse_all(trans, 0, _RET_IP_);
}

static inline bool btree_iter_good_node(struct btree_trans *trans,
					struct btree_iter *iter,
					unsigned l, int check_pos)
{
	if (!is_btree_node(iter, l) ||
	    !bch2_btree_node_relock(trans, iter, l))
		return false;

	if (check_pos < 0 && btree_iter_pos_before_node(iter, iter->l[l].b))
		return false;
	if (check_pos > 0 && btree_iter_pos_after_node(iter, iter->l[l].b))
		return false;
	return true;
}

static inline unsigned btree_iter_up_until_good_node(struct btree_trans *trans,
						     struct btree_iter *iter,
						     int check_pos)
{
	unsigned l = iter->level;

	while (btree_iter_node(iter, l) &&
	       !btree_iter_good_node(trans, iter, l, check_pos)) {
		btree_node_unlock(iter, l);
		iter->l[l].b = BTREE_ITER_NO_NODE_UP;
		l++;
	}

	return l;
}

/*
 * This is the main state machine for walking down the btree - walks down to a
 * specified depth
 *
 * Returns 0 on success, -EIO on error (error reading in a btree node).
 *
 * On error, caller (peek_node()/peek_key()) must return NULL; the error is
 * stashed in the iterator and returned from bch2_trans_exit().
 */
static int btree_iter_traverse_one(struct btree_trans *trans,
				   struct btree_iter *iter,
				   unsigned long trace_ip)
{
	unsigned l, depth_want = iter->level;
	int ret = 0;

	/*
	 * Ensure we obey iter->should_be_locked: if it's set, we can't unlock
	 * and re-traverse the iterator without a transaction restart:
	 */
	if (iter->should_be_locked) {
		ret = bch2_btree_iter_relock(trans, iter, trace_ip) ? 0 : -EINTR;
		goto out;
	}

	if (iter->cached) {
		ret = bch2_btree_iter_traverse_cached(trans, iter);
		goto out;
	}

	if (unlikely(iter->level >= BTREE_MAX_DEPTH))
		goto out;

	iter->level = btree_iter_up_until_good_node(trans, iter, 0);

	/* If we need intent locks, take them too: */
	for (l = iter->level + 1;
	     l < iter->locks_want && btree_iter_node(iter, l);
	     l++)
		if (!bch2_btree_node_relock(trans, iter, l))
			while (iter->level <= l) {
				btree_node_unlock(iter, iter->level);
				iter->l[iter->level].b = BTREE_ITER_NO_NODE_UP;
				iter->level++;
			}

	/*
	 * Note: iter->nodes[iter->level] may be temporarily NULL here - that
	 * would indicate to other code that we got to the end of the btree,
	 * here it indicates that relocking the root failed - it's critical that
	 * btree_iter_lock_root() comes next and that it can't fail
	 */
	while (iter->level > depth_want) {
		ret = btree_iter_node(iter, iter->level)
			? btree_iter_down(trans, iter, trace_ip)
			: btree_iter_lock_root(trans, iter, depth_want, trace_ip);
		if (unlikely(ret)) {
			if (ret == 1) {
				/*
				 * No nodes at this level - got to the end of
				 * the btree:
				 */
				ret = 0;
				goto out;
			}

			__bch2_btree_iter_unlock(iter);
			iter->level = depth_want;

			if (ret == -EIO) {
				iter->flags |= BTREE_ITER_ERROR;
				iter->l[iter->level].b =
					BTREE_ITER_NO_NODE_ERROR;
			} else {
				iter->l[iter->level].b =
					BTREE_ITER_NO_NODE_DOWN;
			}
			goto out;
		}
	}

	iter->uptodate = BTREE_ITER_UPTODATE;
out:
	BUG_ON((ret == -EINTR) != !!trans->restarted);
	trace_iter_traverse(trans->ip, trace_ip,
			    iter->cached,
			    iter->btree_id, &iter->real_pos, ret);
	bch2_btree_iter_verify(iter);
	return ret;
}

static int __must_check __bch2_btree_iter_traverse(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	int ret;

	ret =   bch2_trans_cond_resched(trans) ?:
		btree_iter_traverse_one(trans, iter, _RET_IP_);
	if (unlikely(ret) && hweight64(trans->iters_linked) == 1) {
		ret = __btree_iter_traverse_all(trans, ret, _RET_IP_);
		BUG_ON(ret == -EINTR);
	}

	return ret;
}

/*
 * Note:
 * bch2_btree_iter_traverse() is for external users, btree_iter_traverse() is
 * for internal btree iterator users
 *
 * bch2_btree_iter_traverse sets iter->real_pos to iter->pos,
 * btree_iter_traverse() does not:
 */
static inline int __must_check
btree_iter_traverse(struct btree_iter *iter)
{
	return iter->uptodate >= BTREE_ITER_NEED_RELOCK
		? __bch2_btree_iter_traverse(iter)
		: 0;
}

int __must_check
bch2_btree_iter_traverse(struct btree_iter *iter)
{
	int ret;

	btree_iter_set_search_pos(iter, btree_iter_search_key(iter));

	ret = btree_iter_traverse(iter);
	if (ret)
		return ret;

	iter->should_be_locked = true;
	return 0;
}

/* Iterate across nodes (leaf and interior nodes) */

struct btree *bch2_btree_iter_peek_node(struct btree_iter *iter)
{
	struct btree *b = NULL;
	int ret;

	EBUG_ON(iter->cached);
	bch2_btree_iter_verify(iter);

	ret = btree_iter_traverse(iter);
	if (ret)
		goto out;

	b = btree_iter_node(iter, iter->level);
	if (!b)
		goto out;

	BUG_ON(bpos_cmp(b->key.k.p, iter->pos) < 0);

	bkey_init(&iter->k);
	iter->k.p = iter->pos = iter->real_pos = b->key.k.p;
	iter->trans->iters_sorted = false;
	iter->should_be_locked = true;
out:
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);

	return b;
}

struct btree *bch2_btree_iter_next_node(struct btree_iter *iter)
{
	struct btree *b = NULL;
	int ret;

	EBUG_ON(iter->cached);
	bch2_btree_iter_verify(iter);

	/* already got to end? */
	if (!btree_iter_node(iter, iter->level))
		goto out;

	bch2_trans_cond_resched(iter->trans);

	btree_node_unlock(iter, iter->level);
	iter->l[iter->level].b = BTREE_ITER_NO_NODE_UP;
	iter->level++;

	btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
	ret = btree_iter_traverse(iter);
	if (ret)
		goto out;

	/* got to end? */
	b = btree_iter_node(iter, iter->level);
	if (!b)
		goto out;

	if (bpos_cmp(iter->pos, b->key.k.p) < 0) {
		/*
		 * Haven't gotten to the end of the parent node: go back down to
		 * the next child node
		 */
		btree_iter_set_search_pos(iter, bpos_successor(iter->pos));

		/* Unlock to avoid screwing up our lock invariants: */
		btree_node_unlock(iter, iter->level);

		iter->level = iter->min_depth;
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		bch2_btree_iter_verify(iter);

		ret = btree_iter_traverse(iter);
		if (ret) {
			b = NULL;
			goto out;
		}

		b = iter->l[iter->level].b;
	}

	bkey_init(&iter->k);
	iter->k.p = iter->pos = iter->real_pos = b->key.k.p;
	iter->trans->iters_sorted = false;
	iter->should_be_locked = true;
out:
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);

	return b;
}

/* Iterate across keys (in leaf nodes only) */

static void btree_iter_set_search_pos(struct btree_iter *iter, struct bpos new_pos)
{
	struct btree_trans *trans = iter->trans;
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bpos old_pos = iter->real_pos;
#endif
	int cmp = bpos_cmp(new_pos, iter->real_pos);
	unsigned l = iter->level;

	EBUG_ON(trans->restarted);

	if (!cmp)
		goto out;

	iter->real_pos = new_pos;
	iter->should_be_locked = false;
	trans->iters_sorted = false;

	if (unlikely(iter->cached)) {
		btree_node_unlock(iter, 0);
		iter->l[0].b = BTREE_ITER_NO_NODE_CACHED;
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		return;
	}

	l = btree_iter_up_until_good_node(trans, iter, cmp);

	if (btree_iter_node(iter, l)) {
		/*
		 * We might have to skip over many keys, or just a few: try
		 * advancing the node iterator, and if we have to skip over too
		 * many keys just reinit it (or if we're rewinding, since that
		 * is expensive).
		 */
		if (cmp < 0 ||
		    !btree_iter_advance_to_pos(iter, &iter->l[l], 8))
			__btree_iter_level_init(iter, l);

		/* Don't leave it locked if we're not supposed to: */
		if (btree_lock_want(iter, l) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(iter, l);
	}
out:
	if (l != iter->level)
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);

	bch2_btree_iter_verify(iter);
#ifdef CONFIG_BCACHEFS_DEBUG
	trace_iter_set_search_pos(trans->ip, _RET_IP_,
				  iter->btree_id,
				  &old_pos, &new_pos, l);
#endif
}

inline bool bch2_btree_iter_advance(struct btree_iter *iter)
{
	struct bpos pos = iter->k.p;
	bool ret = bpos_cmp(pos, SPOS_MAX) != 0;

	if (ret && !(iter->flags & BTREE_ITER_IS_EXTENTS))
		pos = bkey_successor(iter, pos);
	bch2_btree_iter_set_pos(iter, pos);
	return ret;
}

inline bool bch2_btree_iter_rewind(struct btree_iter *iter)
{
	struct bpos pos = bkey_start_pos(&iter->k);
	bool ret = (iter->flags & BTREE_ITER_ALL_SNAPSHOTS
		    ? bpos_cmp(pos, POS_MIN)
		    : bkey_cmp(pos, POS_MIN)) != 0;

	if (ret && !(iter->flags & BTREE_ITER_IS_EXTENTS))
		pos = bkey_predecessor(iter, pos);
	bch2_btree_iter_set_pos(iter, pos);
	return ret;
}

static noinline struct bkey_i *__btree_trans_peek_updates(struct btree_iter *iter)
{
	struct btree_insert_entry *i;
	struct bkey_i *ret = NULL;

	trans_for_each_update(iter->trans, i) {
		if (i->btree_id < iter->btree_id)
			continue;
		if (i->btree_id > iter->btree_id)
			break;
		if (bpos_cmp(i->k->k.p, iter->real_pos) < 0)
			continue;
		if (!ret || bpos_cmp(i->k->k.p, ret->k.p) < 0)
			ret = i->k;
	}

	return ret;
}

static inline struct bkey_i *btree_trans_peek_updates(struct btree_iter *iter)
{
	return iter->flags & BTREE_ITER_WITH_UPDATES
		? __btree_trans_peek_updates(iter)
		: NULL;
}

/**
 * bch2_btree_iter_peek: returns first key greater than or equal to iterator's
 * current position
 */
struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bpos search_key = btree_iter_search_key(iter);
	struct bkey_i *next_update;
	struct bkey_s_c k;
	int ret;

	EBUG_ON(iter->cached || iter->level);
	bch2_btree_iter_verify(iter);
	bch2_btree_iter_verify_entry_exit(iter);

	while (1) {
		btree_iter_set_search_pos(iter, search_key);

		ret = btree_iter_traverse(iter);
		if (unlikely(ret)) {
			/* ensure that iter->k is consistent with iter->pos: */
			bch2_btree_iter_set_pos(iter, iter->pos);
			k = bkey_s_c_err(ret);
			goto out;
		}

		next_update = btree_trans_peek_updates(iter);
		k = btree_iter_level_peek_all(iter, l);

		/* * In the btree, deleted keys sort before non deleted: */
		if (k.k && bkey_deleted(k.k) &&
		    (!next_update ||
		     bpos_cmp(k.k->p, next_update->k.p) <= 0)) {
			search_key = k.k->p;
			continue;
		}

		if (next_update &&
		    bpos_cmp(next_update->k.p,
			     k.k ? k.k->p : l->b->key.k.p) <= 0) {
			iter->k = next_update->k;
			k = bkey_i_to_s_c(next_update);
		}

		if (likely(k.k)) {
			if (likely(!bkey_deleted(k.k)))
				break;

			/* Advance to next key: */
			search_key = bkey_successor(iter, k.k->p);
		} else if (likely(bpos_cmp(l->b->key.k.p, SPOS_MAX))) {
			/* Advance to next leaf node: */
			search_key = bpos_successor(l->b->key.k.p);
		} else {
			/* End of btree: */
			bch2_btree_iter_set_pos(iter, SPOS_MAX);
			iter->real_pos = SPOS_MAX;
			k = bkey_s_c_null;
			goto out;
		}
	}

	/*
	 * iter->pos should be mononotically increasing, and always be equal to
	 * the key we just returned - except extents can straddle iter->pos:
	 */
	if (!(iter->flags & BTREE_ITER_IS_EXTENTS))
		iter->pos = k.k->p;
	else if (bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0)
		iter->pos = bkey_start_pos(k.k);
	iter->real_pos = k.k->p;
out:
	iter->should_be_locked = true;
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);
	return k;
}

/**
 * bch2_btree_iter_next: returns first key greater than iterator's current
 * position
 */
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *iter)
{
	if (!bch2_btree_iter_advance(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek(iter);
}

/**
 * bch2_btree_iter_peek_prev: returns first key less than or equal to
 * iterator's current position
 */
struct bkey_s_c bch2_btree_iter_peek_prev(struct btree_iter *iter)
{
	struct bpos search_key = iter->pos;
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c k;
	int ret;

	EBUG_ON(iter->cached || iter->level);
	EBUG_ON(iter->flags & BTREE_ITER_WITH_UPDATES);
	bch2_btree_iter_verify(iter);
	bch2_btree_iter_verify_entry_exit(iter);

	while (1) {
		btree_iter_set_search_pos(iter, search_key);

		ret = btree_iter_traverse(iter);
		if (unlikely(ret)) {
			/* ensure that iter->k is consistent with iter->pos: */
			bch2_btree_iter_set_pos(iter, iter->pos);
			k = bkey_s_c_err(ret);
			goto out;
		}

		k = btree_iter_level_peek(iter, l);
		if (!k.k ||
		    ((iter->flags & BTREE_ITER_IS_EXTENTS)
		     ? bkey_cmp(bkey_start_pos(k.k), iter->pos) >= 0
		     : bkey_cmp(k.k->p, iter->pos) > 0))
			k = btree_iter_level_prev(iter, l);

		if (likely(k.k)) {
			break;
		} else if (likely(bpos_cmp(l->b->data->min_key, POS_MIN))) {
			/* Advance to previous leaf node: */
			search_key = bpos_predecessor(l->b->data->min_key);
		} else {
			/* Start of btree: */
			bch2_btree_iter_set_pos(iter, POS_MIN);
			k = bkey_s_c_null;
			goto out;
		}
	}

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0);

	/* Extents can straddle iter->pos: */
	if (bkey_cmp(k.k->p, iter->pos) < 0)
		iter->pos = k.k->p;
out:
	iter->should_be_locked = true;
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);
	return k;
}

/**
 * bch2_btree_iter_prev: returns first key less than iterator's current
 * position
 */
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *iter)
{
	if (!bch2_btree_iter_rewind(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_prev(iter);
}

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct bpos search_key;
	struct bkey_s_c k;
	int ret;

	EBUG_ON(iter->level);
	bch2_btree_iter_verify(iter);
	bch2_btree_iter_verify_entry_exit(iter);

	/* extents can't span inode numbers: */
	if ((iter->flags & BTREE_ITER_IS_EXTENTS) &&
	    unlikely(iter->pos.offset == KEY_OFFSET_MAX)) {
		if (iter->pos.inode == KEY_INODE_MAX)
			return bkey_s_c_null;

		bch2_btree_iter_set_pos(iter, bpos_nosnap_successor(iter->pos));
	}

	search_key = btree_iter_search_key(iter);
	btree_iter_set_search_pos(iter, search_key);

	ret = btree_iter_traverse(iter);
	if (unlikely(ret))
		return bkey_s_c_err(ret);

	if (!(iter->flags & BTREE_ITER_IS_EXTENTS)) {
		struct bkey_i *next_update;

		next_update = btree_trans_peek_updates(iter);

		if (!iter->cached) {
			k = btree_iter_level_peek_all(iter, &iter->l[0]);
			EBUG_ON(k.k && bkey_deleted(k.k) && bpos_cmp(k.k->p, iter->pos) == 0);
		} else {
			struct bkey_cached *ck = (void *) iter->l[0].b;
			EBUG_ON(iter->btree_id != ck->key.btree_id ||
				bkey_cmp(iter->pos, ck->key.pos));
			BUG_ON(!ck->valid);

			k = bkey_i_to_s_c(ck->k);
		}

		if (next_update &&
		    (!k.k || bpos_cmp(next_update->k.p, k.k->p) <= 0)) {
			iter->k = next_update->k;
			k = bkey_i_to_s_c(next_update);
		}

		if (!k.k ||
		    ((iter->flags & BTREE_ITER_ALL_SNAPSHOTS)
		     ? bpos_cmp(iter->pos, k.k->p)
		     : bkey_cmp(iter->pos, k.k->p))) {
			bkey_init(&iter->k);
			iter->k.p = iter->pos;
			k = (struct bkey_s_c) { &iter->k, NULL };
		}
	} else {
		struct bpos next;

		if (iter->flags & BTREE_ITER_INTENT) {
			struct btree_iter *child =
				btree_iter_child_alloc(trans, iter, _THIS_IP_);

			btree_iter_copy(trans, child, iter);
			k = bch2_btree_iter_peek(child);

			if (k.k && !bkey_err(k))
				iter->k = child->k;
		} else {
			struct bpos pos = iter->pos;

			k = bch2_btree_iter_peek(iter);
			iter->pos = pos;
		}

		if (unlikely(bkey_err(k)))
			return k;

		next = k.k ? bkey_start_pos(k.k) : POS_MAX;

		if (bkey_cmp(iter->pos, next) < 0) {
			bkey_init(&iter->k);
			iter->k.p = iter->pos;
			bch2_key_resize(&iter->k,
					min_t(u64, KEY_SIZE_MAX,
					      (next.inode == iter->pos.inode
					       ? next.offset
					       : KEY_OFFSET_MAX) -
					      iter->pos.offset));

			k = (struct bkey_s_c) { &iter->k, NULL };
			EBUG_ON(!k.k->size);
		}
	}

	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);
	iter->should_be_locked = true;

	return k;
}

struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *iter)
{
	if (!bch2_btree_iter_advance(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_slot(iter);
}

struct bkey_s_c bch2_btree_iter_prev_slot(struct btree_iter *iter)
{
	if (!bch2_btree_iter_rewind(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_slot(iter);
}

static inline void bch2_btree_iter_init(struct btree_trans *trans,
			struct btree_iter *iter, enum btree_id btree_id)
{
	struct bch_fs *c = trans->c;
	unsigned i;

	iter->trans			= trans;
	iter->uptodate			= BTREE_ITER_NEED_TRAVERSE;
	iter->btree_id			= btree_id;
	iter->real_pos			= POS_MIN;
	iter->level			= 0;
	iter->min_depth			= 0;
	iter->locks_want		= 0;
	iter->nodes_locked		= 0;
	iter->nodes_intent_locked	= 0;
	for (i = 0; i < ARRAY_SIZE(iter->l); i++)
		iter->l[i].b		= BTREE_ITER_NO_NODE_INIT;

	prefetch(c->btree_roots[btree_id].b);
}

/* new transactional stuff: */

#ifdef CONFIG_BCACHEFS_DEBUG
static void btree_trans_verify_sorted_refs(struct btree_trans *trans)
{
	struct btree_iter *iter;
	unsigned i;

	BUG_ON(trans->nr_sorted != hweight64(trans->iters_linked));

	trans_for_each_iter(trans, iter) {
		BUG_ON(iter->sorted_idx >= trans->nr_sorted);
		BUG_ON(trans->sorted[iter->sorted_idx] != iter->idx);
	}

	for (i = 0; i < trans->nr_sorted; i++) {
		unsigned idx = trans->sorted[i];

		EBUG_ON(!(trans->iters_linked & (1ULL << idx)));
		BUG_ON(trans->iters[idx].sorted_idx != i);
	}
}
#else
static inline void btree_trans_verify_sorted_refs(struct btree_trans *trans) {}
#endif

static void btree_trans_verify_sorted(struct btree_trans *trans)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct btree_iter *iter, *prev = NULL;
	unsigned i;

	trans_for_each_iter_inorder(trans, iter, i) {
		BUG_ON(prev && btree_iter_cmp(prev, iter) > 0);
		prev = iter;
	}
#endif
}

static noinline void __btree_trans_sort_iters(struct btree_trans *trans)
{
	int i, l = 0, r = trans->nr_sorted, inc = 1;
	bool swapped;

	/*
	 * Cocktail shaker sort: this is efficient because iterators will be
	 * mostly sorteda.
	 */
	do {
		swapped = false;

		for (i = inc > 0 ? l : r - 2;
		     i + 1 < r && i >= l;
		     i += inc) {
			if (btree_iter_cmp(trans->iters + trans->sorted[i],
					   trans->iters + trans->sorted[i + 1]) > 0) {
				swap(trans->sorted[i], trans->sorted[i + 1]);
				trans->iters[trans->sorted[i]].sorted_idx = i;
				trans->iters[trans->sorted[i + 1]].sorted_idx = i + 1;
				swapped = true;
			}
		}

		if (inc > 0)
			--r;
		else
			l++;
		inc = -inc;
	} while (swapped);

	trans->iters_sorted = true;

	btree_trans_verify_sorted(trans);
}

static inline void btree_trans_sort_iters(struct btree_trans *trans)
{
	btree_trans_verify_sorted_refs(trans);

	if (trans->iters_sorted) {
		if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
			btree_trans_verify_sorted(trans);
		return;
	}
	__btree_trans_sort_iters(trans);
}

static inline void btree_iter_list_remove(struct btree_trans *trans,
					  struct btree_iter *iter)
{
	unsigned i;

	EBUG_ON(iter->sorted_idx >= trans->nr_sorted);
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	trans->nr_sorted--;
	memmove_u64s_down_small(trans->sorted + iter->sorted_idx,
				trans->sorted + iter->sorted_idx + 1,
				DIV_ROUND_UP(trans->nr_sorted - iter->sorted_idx, 8));
#else
	array_remove_item(trans->sorted, trans->nr_sorted, iter->sorted_idx);
#endif
	for (i = iter->sorted_idx; i < trans->nr_sorted; i++)
		trans->iters[trans->sorted[i]].sorted_idx = i;

	iter->sorted_idx = U8_MAX;
}

static inline void btree_iter_list_add(struct btree_trans *trans,
				       struct btree_iter *pos,
				       struct btree_iter *iter)
{
	unsigned i;

	iter->sorted_idx = pos ? pos->sorted_idx + 1 : 0;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	memmove_u64s_up_small(trans->sorted + iter->sorted_idx + 1,
			      trans->sorted + iter->sorted_idx,
			      DIV_ROUND_UP(trans->nr_sorted - iter->sorted_idx, 8));
	trans->nr_sorted++;
	trans->sorted[iter->sorted_idx] = iter->idx;
#else
	array_insert_item(trans->sorted, trans->nr_sorted, iter->sorted_idx, iter->idx);
#endif

	for (i = iter->sorted_idx; i < trans->nr_sorted; i++)
		trans->iters[trans->sorted[i]].sorted_idx = i;

	btree_trans_verify_sorted_refs(trans);
}

static void btree_iter_child_free(struct btree_trans *trans, struct btree_iter *iter)
{
	struct btree_iter *child = btree_iter_child(trans, iter);

	if (child) {
		bch2_trans_iter_free(trans, child);
		iter->child_idx = U8_MAX;
	}
}

static struct btree_iter *btree_iter_child_alloc(struct btree_trans *trans,
						 struct btree_iter *iter,
						 unsigned long ip)
{
	struct btree_iter *child = btree_iter_child(trans, iter);

	if (!child) {
		child = btree_trans_iter_alloc(trans, iter);
		child->ip_allocated	= ip;
		iter->child_idx		= child->idx;

		trans->iters_live	|= 1ULL << child->idx;
		trans->iters_touched	|= 1ULL << child->idx;
	}

	return child;
}

static inline void __bch2_trans_iter_free(struct btree_trans *trans,
					  unsigned idx)
{
	btree_iter_child_free(trans, &trans->iters[idx]);

	btree_iter_list_remove(trans, &trans->iters[idx]);

	__bch2_btree_iter_unlock(&trans->iters[idx]);
	trans->iters_linked		&= ~(1ULL << idx);
	trans->iters_live		&= ~(1ULL << idx);
	trans->iters_touched		&= ~(1ULL << idx);

	btree_trans_verify_sorted_refs(trans);
}

static bool have_iter_at_pos(struct btree_trans *trans,
			     struct btree_iter *iter)
{
	struct btree_iter *n;

	n = prev_btree_iter(trans, iter);
	if (n && !btree_iter_cmp(n, iter))
		return true;

	n = next_btree_iter(trans, iter);
	if (n && !btree_iter_cmp(n, iter))
		return true;

	return false;
}

int bch2_trans_iter_put(struct btree_trans *trans,
			struct btree_iter *iter)
{
	int ret;

	if (IS_ERR_OR_NULL(iter))
		return 0;

	BUG_ON(trans->iters + iter->idx != iter);
	BUG_ON(!btree_iter_live(trans, iter));

	ret = btree_iter_err(iter);

	if (!(iter->flags & BTREE_ITER_KEEP_UNTIL_COMMIT) &&
	    (!(trans->iters_touched & (1ULL << iter->idx)) ||
	     have_iter_at_pos(trans, iter)))
		__bch2_trans_iter_free(trans, iter->idx);

	trans->iters_live	&= ~(1ULL << iter->idx);
	return ret;
}

int bch2_trans_iter_free(struct btree_trans *trans,
			 struct btree_iter *iter)
{
	if (IS_ERR_OR_NULL(iter))
		return 0;

	set_btree_iter_dontneed(trans, iter);

	return bch2_trans_iter_put(trans, iter);
}

noinline __cold
void bch2_dump_trans_iters_updates(struct btree_trans *trans)
{
	struct btree_iter *iter;
	struct btree_insert_entry *i;
	unsigned idx;
	char buf1[300], buf2[100];

	btree_trans_sort_iters(trans);

	trans_for_each_iter_inorder(trans, iter, idx)
		printk(KERN_ERR "iter: btree %s pos %s real_pos %s%s%s%s %pS\n",
		       bch2_btree_ids[iter->btree_id],
		       (bch2_bpos_to_text(&PBUF(buf1), iter->pos), buf1),
		       (bch2_bpos_to_text(&PBUF(buf2), iter->real_pos), buf2),
		       btree_iter_live(trans, iter) ? " live" : "",
		       (trans->iters_touched & (1ULL << iter->idx)) ? " touched" : "",
		       iter->flags & BTREE_ITER_KEEP_UNTIL_COMMIT ? " keep" : "",
		       (void *) iter->ip_allocated);

	trans_for_each_update(trans, i)
		printk(KERN_ERR "update: btree %s %s %pS\n",
		       bch2_btree_ids[i->btree_id],
		       (bch2_bkey_val_to_text(&PBUF(buf1), trans->c, bkey_i_to_s_c(i->k)), buf1),
		       (void *) i->ip_allocated);
}

static struct btree_iter *btree_trans_iter_alloc(struct btree_trans *trans,
						 struct btree_iter *pos)
{
	struct btree_iter *iter;
	unsigned idx;

	btree_trans_verify_sorted_refs(trans);

	if (unlikely(trans->iters_linked ==
		     ~((~0ULL << 1) << (BTREE_ITER_MAX - 1)))) {
		bch2_dump_trans_iters_updates(trans);
		panic("trans iter oveflow\n");
	}

	idx = __ffs64(~trans->iters_linked);
	iter = &trans->iters[idx];

	iter->trans		= trans;
	iter->idx		= idx;
	iter->child_idx		= U8_MAX;
	iter->sorted_idx	= U8_MAX;
	iter->flags		= 0;
	iter->nodes_locked	= 0;
	iter->nodes_intent_locked = 0;
	trans->iters_linked	|= 1ULL << idx;

	btree_iter_list_add(trans, pos, iter);
	return iter;
}

static void btree_iter_copy(struct btree_trans *trans, struct btree_iter *dst,
			    struct btree_iter *src)
{
	unsigned i, offset = offsetof(struct btree_iter, flags);

	__bch2_btree_iter_unlock(dst);
	btree_iter_child_free(trans, dst);

	memcpy((void *) dst + offset,
	       (void *) src + offset,
	       sizeof(struct btree_iter) - offset);

	for (i = 0; i < BTREE_MAX_DEPTH; i++)
		if (btree_node_locked(dst, i))
			six_lock_increment(&dst->l[i].b->c.lock,
					   __btree_lock_want(dst, i));

	dst->flags &= ~BTREE_ITER_KEEP_UNTIL_COMMIT;
	trans->iters_sorted = false;
}

struct btree_iter *__bch2_trans_get_iter(struct btree_trans *trans,
					 enum btree_id btree_id, struct bpos pos,
					 unsigned locks_want,
					 unsigned depth,
					 unsigned flags)
{
	struct btree_iter *iter, *best = NULL;
	struct bpos real_pos, pos_min = POS_MIN;

	EBUG_ON(trans->restarted);

	if (!(flags & (BTREE_ITER_ALL_SNAPSHOTS|BTREE_ITER_NOT_EXTENTS)) &&
	    btree_node_type_is_extents(btree_id))
		flags |= BTREE_ITER_IS_EXTENTS;

	if (!btree_type_has_snapshots(btree_id) &&
	    !(flags & __BTREE_ITER_ALL_SNAPSHOTS))
		flags &= ~BTREE_ITER_ALL_SNAPSHOTS;

	if (!(flags & BTREE_ITER_ALL_SNAPSHOTS))
		pos.snapshot = btree_type_has_snapshots(btree_id)
			? U32_MAX : 0;

	real_pos = pos;

	if ((flags & BTREE_ITER_IS_EXTENTS) &&
	    bkey_cmp(pos, POS_MAX))
		real_pos = bpos_nosnap_successor(pos);

	trans_for_each_iter(trans, iter) {
		if (iter->cached != (flags & BTREE_ITER_CACHED))
			continue;

		if (iter->btree_id != btree_id)
			continue;

		if (best) {
			int cmp = bkey_cmp(bpos_diff(best->real_pos, real_pos),
					   bpos_diff(iter->real_pos, real_pos));

			if (cmp < 0 ||
			    ((cmp == 0 && btree_iter_keep(trans, iter))))
				continue;
		}

		best = iter;
	}

	if (!best) {
		iter = btree_trans_iter_alloc(trans, best);
		bch2_btree_iter_init(trans, iter, btree_id);
	} else if (btree_iter_keep(trans, best)) {
		iter = btree_trans_iter_alloc(trans, best);
		btree_iter_copy(trans, iter, best);
	} else {
		iter = best;
	}

	trans->iters_live	|= 1ULL << iter->idx;
	trans->iters_touched	|= 1ULL << iter->idx;

	iter->cached	= flags & BTREE_ITER_CACHED;
	iter->flags	= flags;
	iter->snapshot	= pos.snapshot;

	/*
	 * If the iterator has locks_want greater than requested, we explicitly
	 * do not downgrade it here - on transaction restart because btree node
	 * split needs to upgrade locks, we might be putting/getting the
	 * iterator again. Downgrading iterators only happens via an explicit
	 * bch2_trans_downgrade().
	 */

	locks_want = min(locks_want, BTREE_MAX_DEPTH);
	if (locks_want > iter->locks_want) {
		iter->locks_want = locks_want;
		btree_iter_get_locks(trans, iter, true, _THIS_IP_);
	}

	while (iter->level != depth) {
		btree_node_unlock(iter, iter->level);
		iter->l[iter->level].b = BTREE_ITER_NO_NODE_INIT;
		iter->uptodate = BTREE_ITER_NEED_TRAVERSE;
		if (iter->level < depth)
			iter->level++;
		else
			iter->level--;
	}

	iter->min_depth	= depth;

	bch2_btree_iter_set_pos(iter, pos);
	btree_iter_set_search_pos(iter, real_pos);

	trace_trans_get_iter(_RET_IP_, trans->ip,
			     btree_id,
			     &real_pos, locks_want, iter->uptodate,
			     best ? &best->real_pos	: &pos_min,
			     best ? best->locks_want	: U8_MAX,
			     best ? best->uptodate	: U8_MAX);

	return iter;
}

struct btree_iter *bch2_trans_get_node_iter(struct btree_trans *trans,
					    enum btree_id btree_id,
					    struct bpos pos,
					    unsigned locks_want,
					    unsigned depth,
					    unsigned flags)
{
	struct btree_iter *iter =
		__bch2_trans_get_iter(trans, btree_id, pos,
				      locks_want, depth,
				      BTREE_ITER_NOT_EXTENTS|
				      __BTREE_ITER_ALL_SNAPSHOTS|
				      BTREE_ITER_ALL_SNAPSHOTS|
				      flags);

	BUG_ON(bkey_cmp(iter->pos, pos));
	BUG_ON(iter->locks_want != min(locks_want, BTREE_MAX_DEPTH));
	BUG_ON(iter->level	!= depth);
	BUG_ON(iter->min_depth	!= depth);
	iter->ip_allocated = _RET_IP_;

	return iter;
}

struct btree_iter *__bch2_trans_copy_iter(struct btree_trans *trans,
					  struct btree_iter *src)
{
	struct btree_iter *iter;

	iter = btree_trans_iter_alloc(trans, src);
	btree_iter_copy(trans, iter, src);

	trans->iters_live |= 1ULL << iter->idx;
	/*
	 * We don't need to preserve this iter since it's cheap to copy it
	 * again - this will cause trans_iter_put() to free it right away:
	 */
	set_btree_iter_dontneed(trans, iter);

	return iter;
}

void *bch2_trans_kmalloc(struct btree_trans *trans, size_t size)
{
	size_t new_top = trans->mem_top + size;
	void *p;

	if (new_top > trans->mem_bytes) {
		size_t old_bytes = trans->mem_bytes;
		size_t new_bytes = roundup_pow_of_two(new_top);
		void *new_mem;

		WARN_ON_ONCE(new_bytes > BTREE_TRANS_MEM_MAX);

		new_mem = krealloc(trans->mem, new_bytes, GFP_NOFS);
		if (!new_mem && new_bytes <= BTREE_TRANS_MEM_MAX) {
			new_mem = mempool_alloc(&trans->c->btree_trans_mem_pool, GFP_KERNEL);
			new_bytes = BTREE_TRANS_MEM_MAX;
			kfree(trans->mem);
		}

		if (!new_mem)
			return ERR_PTR(-ENOMEM);

		trans->mem = new_mem;
		trans->mem_bytes = new_bytes;

		if (old_bytes) {
			trace_trans_restart_mem_realloced(trans->ip, _RET_IP_, new_bytes);
			btree_trans_restart(trans);
			return ERR_PTR(-EINTR);
		}
	}

	p = trans->mem + trans->mem_top;
	trans->mem_top += size;
	memset(p, 0, size);
	return p;
}

inline void bch2_trans_unlink_iters(struct btree_trans *trans)
{
	u64 iters = trans->iters_linked &
		~trans->iters_touched &
		~trans->iters_live;

	while (iters) {
		unsigned idx = __ffs64(iters);

		iters &= ~(1ULL << idx);
		__bch2_trans_iter_free(trans, idx);
	}
}

/**
 * bch2_trans_begin() - reset a transaction after a interrupted attempt
 * @trans: transaction to reset
 *
 * While iterating over nodes or updating nodes a attempt to lock a btree
 * node may return EINTR when the trylock fails. When this occurs
 * bch2_trans_begin() should be called and the transaction retried.
 */
void bch2_trans_begin(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		iter->flags &= ~BTREE_ITER_KEEP_UNTIL_COMMIT;

	/*
	 * XXX: we shouldn't be doing this if the transaction was restarted, but
	 * currently we still overflow transaction iterators if we do that
	 * */
	bch2_trans_unlink_iters(trans);
	trans->iters_touched &= trans->iters_live;

	trans->extra_journal_res	= 0;
	trans->nr_updates		= 0;
	trans->mem_top			= 0;

	trans->hooks			= NULL;
	trans->extra_journal_entries	= NULL;
	trans->extra_journal_entry_u64s	= 0;

	if (trans->fs_usage_deltas) {
		trans->fs_usage_deltas->used = 0;
		memset((void *) trans->fs_usage_deltas +
		       offsetof(struct replicas_delta_list, memset_start), 0,
		       (void *) &trans->fs_usage_deltas->memset_end -
		       (void *) &trans->fs_usage_deltas->memset_start);
	}

	bch2_trans_cond_resched(trans);

	if (trans->restarted)
		bch2_btree_iter_traverse_all(trans);

	trans->restarted = false;
}

static void bch2_trans_alloc_iters(struct btree_trans *trans, struct bch_fs *c)
{
	size_t iters_bytes	= sizeof(struct btree_iter) * BTREE_ITER_MAX;
	size_t updates_bytes	= sizeof(struct btree_insert_entry) * BTREE_ITER_MAX;
	void *p = NULL;

	BUG_ON(trans->used_mempool);

#ifdef __KERNEL__
	p = this_cpu_xchg(c->btree_iters_bufs->iter, NULL);
#endif
	if (!p)
		p = mempool_alloc(&trans->c->btree_iters_pool, GFP_NOFS);

	trans->iters		= p; p += iters_bytes;
	trans->updates		= p; p += updates_bytes;
}

void bch2_trans_init(struct btree_trans *trans, struct bch_fs *c,
		     unsigned expected_nr_iters,
		     size_t expected_mem_bytes)
	__acquires(&c->btree_trans_barrier)
{
	memset(trans, 0, sizeof(*trans));
	trans->c		= c;
	trans->ip		= _RET_IP_;

	/*
	 * reallocating iterators currently completely breaks
	 * bch2_trans_iter_put(), we always allocate the max:
	 */
	bch2_trans_alloc_iters(trans, c);

	if (expected_mem_bytes) {
		expected_mem_bytes = roundup_pow_of_two(expected_mem_bytes);
		trans->mem = kmalloc(expected_mem_bytes, GFP_KERNEL);

		if (!unlikely(trans->mem)) {
			trans->mem = mempool_alloc(&c->btree_trans_mem_pool, GFP_KERNEL);
			trans->mem_bytes = BTREE_TRANS_MEM_MAX;
		} else {
			trans->mem_bytes = expected_mem_bytes;
		}
	}

	trans->srcu_idx = srcu_read_lock(&c->btree_trans_barrier);

#ifdef CONFIG_BCACHEFS_DEBUG
	trans->pid = current->pid;
	mutex_lock(&c->btree_trans_lock);
	list_add(&trans->list, &c->btree_trans_list);
	mutex_unlock(&c->btree_trans_lock);
#endif
}

int bch2_trans_exit(struct btree_trans *trans)
	__releases(&c->btree_trans_barrier)
{
	struct bch_fs *c = trans->c;

	bch2_trans_unlock(trans);

#ifdef CONFIG_BCACHEFS_DEBUG
	if (trans->iters_live) {
		struct btree_iter *iter;

		trans_for_each_iter(trans, iter)
			btree_iter_child_free(trans, iter);
	}

	if (trans->iters_live) {
		struct btree_iter *iter;

		bch_err(c, "btree iterators leaked!");
		trans_for_each_iter(trans, iter)
			if (btree_iter_live(trans, iter))
				printk(KERN_ERR "  btree %s allocated at %pS\n",
				       bch2_btree_ids[iter->btree_id],
				       (void *) iter->ip_allocated);
		/* Be noisy about this: */
		bch2_fatal_error(c);
	}

	mutex_lock(&trans->c->btree_trans_lock);
	list_del(&trans->list);
	mutex_unlock(&trans->c->btree_trans_lock);
#endif

	srcu_read_unlock(&c->btree_trans_barrier, trans->srcu_idx);

	bch2_journal_preres_put(&trans->c->journal, &trans->journal_preres);

	if (trans->fs_usage_deltas) {
		if (trans->fs_usage_deltas->size + sizeof(trans->fs_usage_deltas) ==
		    REPLICAS_DELTA_LIST_MAX)
			mempool_free(trans->fs_usage_deltas,
				     &trans->c->replicas_delta_pool);
		else
			kfree(trans->fs_usage_deltas);
	}

	if (trans->mem_bytes == BTREE_TRANS_MEM_MAX)
		mempool_free(trans->mem, &trans->c->btree_trans_mem_pool);
	else
		kfree(trans->mem);

#ifdef __KERNEL__
	/*
	 * Userspace doesn't have a real percpu implementation:
	 */
	trans->iters = this_cpu_xchg(c->btree_iters_bufs->iter, trans->iters);
#endif

	if (trans->iters)
		mempool_free(trans->iters, &trans->c->btree_iters_pool);

	trans->mem	= (void *) 0x1;
	trans->iters	= (void *) 0x1;

	return trans->error ? -EIO : 0;
}

static void __maybe_unused
bch2_btree_iter_node_to_text(struct printbuf *out,
			     struct btree_bkey_cached_common *_b,
			     bool cached)
{
	pr_buf(out, "    l=%u %s:",
	       _b->level, bch2_btree_ids[_b->btree_id]);
	bch2_bpos_to_text(out, btree_node_pos(_b, cached));
}

#ifdef CONFIG_BCACHEFS_DEBUG
static bool trans_has_locks(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		if (iter->nodes_locked)
			return true;
	return false;
}
#endif

void bch2_btree_trans_to_text(struct printbuf *out, struct bch_fs *c)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct btree_trans *trans;
	struct btree_iter *iter;
	struct btree *b;
	unsigned l;

	mutex_lock(&c->btree_trans_lock);
	list_for_each_entry(trans, &c->btree_trans_list, list) {
		if (!trans_has_locks(trans))
			continue;

		pr_buf(out, "%i %ps\n", trans->pid, (void *) trans->ip);

		trans_for_each_iter(trans, iter) {
			if (!iter->nodes_locked)
				continue;

			pr_buf(out, "  iter %u %c %s:",
			       iter->idx,
			       iter->cached ? 'c' : 'b',
			       bch2_btree_ids[iter->btree_id]);
			bch2_bpos_to_text(out, iter->pos);
			pr_buf(out, "\n");

			for (l = 0; l < BTREE_MAX_DEPTH; l++) {
				if (btree_node_locked(iter, l)) {
					pr_buf(out, "    %s l=%u ",
					       btree_node_intent_locked(iter, l) ? "i" : "r", l);
					bch2_btree_iter_node_to_text(out,
							(void *) iter->l[l].b,
							iter->cached);
					pr_buf(out, "\n");
				}
			}
		}

		b = READ_ONCE(trans->locking);
		if (b) {
			iter = &trans->iters[trans->locking_iter_idx];
			pr_buf(out, "  locking iter %u %c l=%u %s:",
			       trans->locking_iter_idx,
			       iter->cached ? 'c' : 'b',
			       trans->locking_level,
			       bch2_btree_ids[trans->locking_btree_id]);
			bch2_bpos_to_text(out, trans->locking_pos);

			pr_buf(out, " node ");
			bch2_btree_iter_node_to_text(out,
					(void *) b,
					iter->cached);
			pr_buf(out, "\n");
		}
	}
	mutex_unlock(&c->btree_trans_lock);
#endif
}

void bch2_fs_btree_iter_exit(struct bch_fs *c)
{
	mempool_exit(&c->btree_trans_mem_pool);
	mempool_exit(&c->btree_iters_pool);
	cleanup_srcu_struct(&c->btree_trans_barrier);
}

int bch2_fs_btree_iter_init(struct bch_fs *c)
{
	unsigned nr = BTREE_ITER_MAX;

	INIT_LIST_HEAD(&c->btree_trans_list);
	mutex_init(&c->btree_trans_lock);

	return  init_srcu_struct(&c->btree_trans_barrier) ?:
		mempool_init_kmalloc_pool(&c->btree_iters_pool, 1,
			sizeof(struct btree_iter) * nr +
			sizeof(struct btree_insert_entry) * nr) ?:
		mempool_init_kmalloc_pool(&c->btree_trans_mem_pool, 1,
					  BTREE_TRANS_MEM_MAX);
}
