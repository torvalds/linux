// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_locking.h"
#include "btree_types.h"

struct lock_class_key bch2_btree_node_lock_key;

/* Btree node locking: */

static inline void six_lock_readers_add(struct six_lock *lock, int nr)
{
	if (lock->readers)
		this_cpu_add(*lock->readers, nr);
	else if (nr > 0)
		atomic64_add(__SIX_VAL(read_lock, nr), &lock->state.counter);
	else
		atomic64_sub(__SIX_VAL(read_lock, -nr), &lock->state.counter);
}

struct six_lock_count bch2_btree_node_lock_counts(struct btree_trans *trans,
						  struct btree_path *skip,
						  struct btree_bkey_cached_common *b,
						  unsigned level)
{
	struct btree_path *path;
	struct six_lock_count ret;

	memset(&ret, 0, sizeof(ret));

	if (IS_ERR_OR_NULL(b))
		return ret;

	trans_for_each_path(trans, path)
		if (path != skip && &path->l[level].b->c == b) {
			int t = btree_node_locked_type(path, level);

			if (t != BTREE_NODE_UNLOCKED)
				ret.n[t]++;
		}

	return ret;
}

/* unlock */

void bch2_btree_node_unlock_write(struct btree_trans *trans,
			struct btree_path *path, struct btree *b)
{
	bch2_btree_node_unlock_write_inlined(trans, path, b);
}

/* lock */

void __bch2_btree_node_lock_write(struct btree_trans *trans,
				  struct btree_bkey_cached_common *b)
{
	int readers = bch2_btree_node_lock_counts(trans, NULL, b, b->level).n[SIX_LOCK_read];

	/*
	 * Must drop our read locks before calling six_lock_write() -
	 * six_unlock() won't do wakeups until the reader count
	 * goes to 0, and it's safe because we have the node intent
	 * locked:
	 */
	six_lock_readers_add(&b->lock, -readers);
	btree_node_lock_nopath_nofail(trans, b, SIX_LOCK_write);
	six_lock_readers_add(&b->lock, readers);
}

static inline bool path_has_read_locks(struct btree_path *path)
{
	unsigned l;

	for (l = 0; l < BTREE_MAX_DEPTH; l++)
		if (btree_node_read_locked(path, l))
			return true;
	return false;
}

/* Slowpath: */
int __bch2_btree_node_lock(struct btree_trans *trans,
			   struct btree_path *path,
			   struct btree_bkey_cached_common *b,
			   struct bpos pos, unsigned level,
			   enum six_lock_type type,
			   six_lock_should_sleep_fn should_sleep_fn, void *p,
			   unsigned long ip)
{
	struct btree_path *linked;
	unsigned reason;

	/* Check if it's safe to block: */
	trans_for_each_path(trans, linked) {
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
		    path_has_read_locks(linked)) {
			reason = 1;
			goto deadlock;
		}

		if (linked->btree_id != path->btree_id) {
			if (linked->btree_id < path->btree_id)
				continue;

			reason = 3;
			goto deadlock;
		}

		/*
		 * Within the same btree, non-cached paths come before cached
		 * paths:
		 */
		if (linked->cached != path->cached) {
			if (!linked->cached)
				continue;

			reason = 4;
			goto deadlock;
		}

		/*
		 * Interior nodes must be locked before their descendants: if
		 * another path has possible descendants locked of the node
		 * we're about to lock, it must have the ancestors locked too:
		 */
		if (level > btree_path_highest_level_locked(linked)) {
			reason = 5;
			goto deadlock;
		}

		/* Must lock btree nodes in key order: */
		if (btree_node_locked(linked, level) &&
		    bpos_cmp(pos, btree_node_pos(&linked->l[level].b->c)) <= 0) {
			reason = 7;
			goto deadlock;
		}
	}

	return btree_node_lock_type(trans, path, b, pos, level,
				    type, should_sleep_fn, p);
deadlock:
	trace_and_count(trans->c, trans_restart_would_deadlock, trans, ip, reason, linked, path, &pos);
	return btree_trans_restart(trans, BCH_ERR_transaction_restart_would_deadlock);
}

/* relock */

static inline bool btree_path_get_locks(struct btree_trans *trans,
					struct btree_path *path,
					bool upgrade)
{
	unsigned l = path->level;
	int fail_idx = -1;

	do {
		if (!btree_path_node(path, l))
			break;

		if (!(upgrade
		      ? bch2_btree_node_upgrade(trans, path, l)
		      : bch2_btree_node_relock(trans, path, l)))
			fail_idx = l;

		l++;
	} while (l < path->locks_want);

	/*
	 * When we fail to get a lock, we have to ensure that any child nodes
	 * can't be relocked so bch2_btree_path_traverse has to walk back up to
	 * the node that we failed to relock:
	 */
	if (fail_idx >= 0) {
		__bch2_btree_path_unlock(trans, path);
		btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);

		do {
			path->l[fail_idx].b = upgrade
				? ERR_PTR(-BCH_ERR_no_btree_node_upgrade)
				: ERR_PTR(-BCH_ERR_no_btree_node_relock);
			--fail_idx;
		} while (fail_idx >= 0);
	}

	if (path->uptodate == BTREE_ITER_NEED_RELOCK)
		path->uptodate = BTREE_ITER_UPTODATE;

	bch2_trans_verify_locks(trans);

	return path->uptodate < BTREE_ITER_NEED_RELOCK;
}

bool __bch2_btree_node_relock(struct btree_trans *trans,
			      struct btree_path *path, unsigned level)
{
	struct btree *b = btree_path_node(path, level);
	int want = __btree_lock_want(path, level);

	if (race_fault())
		goto fail;

	if (six_relock_type(&b->c.lock, want, path->l[level].lock_seq) ||
	    (btree_node_lock_seq_matches(path, b, level) &&
	     btree_node_lock_increment(trans, &b->c, level, want))) {
		mark_btree_node_locked(trans, path, level, want);
		return true;
	}
fail:
	trace_and_count(trans->c, btree_path_relock_fail, trans, _RET_IP_, path, level);
	return false;
}

/* upgrade */

bool bch2_btree_node_upgrade(struct btree_trans *trans,
			     struct btree_path *path, unsigned level)
{
	struct btree *b = path->l[level].b;
	struct six_lock_count count = bch2_btree_node_lock_counts(trans, path, &b->c, level);

	if (!is_btree_node(path, level))
		return false;

	switch (btree_lock_want(path, level)) {
	case BTREE_NODE_UNLOCKED:
		BUG_ON(btree_node_locked(path, level));
		return true;
	case BTREE_NODE_READ_LOCKED:
		BUG_ON(btree_node_intent_locked(path, level));
		return bch2_btree_node_relock(trans, path, level);
	case BTREE_NODE_INTENT_LOCKED:
		break;
	case BTREE_NODE_WRITE_LOCKED:
		BUG();
	}

	if (btree_node_intent_locked(path, level))
		return true;

	if (race_fault())
		return false;

	if (btree_node_locked(path, level)) {
		bool ret;

		six_lock_readers_add(&b->c.lock, -count.n[SIX_LOCK_read]);
		ret = six_lock_tryupgrade(&b->c.lock);
		six_lock_readers_add(&b->c.lock, count.n[SIX_LOCK_read]);

		if (ret)
			goto success;
	} else {
		if (six_relock_type(&b->c.lock, SIX_LOCK_intent, path->l[level].lock_seq))
			goto success;
	}

	/*
	 * Do we already have an intent lock via another path? If so, just bump
	 * lock count:
	 */
	if (btree_node_lock_seq_matches(path, b, level) &&
	    btree_node_lock_increment(trans, &b->c, level, BTREE_NODE_INTENT_LOCKED)) {
		btree_node_unlock(trans, path, level);
		goto success;
	}

	trace_and_count(trans->c, btree_path_upgrade_fail, trans, _RET_IP_, path, level);
	return false;
success:
	mark_btree_node_locked_noreset(path, level, SIX_LOCK_intent);
	return true;
}

/* Btree path locking: */

/*
 * Only for btree_cache.c - only relocks intent locks
 */
int bch2_btree_path_relock_intent(struct btree_trans *trans,
				  struct btree_path *path)
{
	unsigned l;

	for (l = path->level;
	     l < path->locks_want && btree_path_node(path, l);
	     l++) {
		if (!bch2_btree_node_relock(trans, path, l)) {
			__bch2_btree_path_unlock(trans, path);
			btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
			trace_and_count(trans->c, trans_restart_relock_path_intent, trans, _RET_IP_, path);
			return btree_trans_restart(trans, BCH_ERR_transaction_restart_relock_path_intent);
		}
	}

	return 0;
}

__flatten
bool bch2_btree_path_relock_norestart(struct btree_trans *trans,
			struct btree_path *path, unsigned long trace_ip)
{
	return btree_path_get_locks(trans, path, false);
}

int __bch2_btree_path_relock(struct btree_trans *trans,
			struct btree_path *path, unsigned long trace_ip)
{
	if (!bch2_btree_path_relock_norestart(trans, path, trace_ip)) {
		trace_and_count(trans->c, trans_restart_relock_path, trans, trace_ip, path);
		return btree_trans_restart(trans, BCH_ERR_transaction_restart_relock_path);
	}

	return 0;
}

__flatten
bool bch2_btree_path_upgrade_norestart(struct btree_trans *trans,
			struct btree_path *path, unsigned long trace_ip)
{
	return btree_path_get_locks(trans, path, true);
}

bool bch2_btree_path_upgrade_noupgrade_sibs(struct btree_trans *trans,
			       struct btree_path *path,
			       unsigned new_locks_want)
{
	EBUG_ON(path->locks_want >= new_locks_want);

	path->locks_want = new_locks_want;

	return btree_path_get_locks(trans, path, true);
}

bool __bch2_btree_path_upgrade(struct btree_trans *trans,
			       struct btree_path *path,
			       unsigned new_locks_want)
{
	struct btree_path *linked;

	if (bch2_btree_path_upgrade_noupgrade_sibs(trans, path, new_locks_want))
		return true;

	/*
	 * XXX: this is ugly - we'd prefer to not be mucking with other
	 * iterators in the btree_trans here.
	 *
	 * On failure to upgrade the iterator, setting iter->locks_want and
	 * calling get_locks() is sufficient to make bch2_btree_path_traverse()
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
	 * bch2_btree_path_traverse_all().
	 */
	if (!path->cached && !trans->in_traverse_all)
		trans_for_each_path(trans, linked)
			if (linked != path &&
			    linked->cached == path->cached &&
			    linked->btree_id == path->btree_id &&
			    linked->locks_want < new_locks_want) {
				linked->locks_want = new_locks_want;
				btree_path_get_locks(trans, linked, true);
			}

	return false;
}

void __bch2_btree_path_downgrade(struct btree_trans *trans,
				 struct btree_path *path,
				 unsigned new_locks_want)
{
	unsigned l;

	EBUG_ON(path->locks_want < new_locks_want);

	path->locks_want = new_locks_want;

	while (path->nodes_locked &&
	       (l = btree_path_highest_level_locked(path)) >= path->locks_want) {
		if (l > path->level) {
			btree_node_unlock(trans, path, l);
		} else {
			if (btree_node_intent_locked(path, l)) {
				six_lock_downgrade(&path->l[l].b->c.lock);
				mark_btree_node_locked_noreset(path, l, SIX_LOCK_read);
			}
			break;
		}
	}

	bch2_btree_path_verify_locks(path);
}

/* Btree transaction locking: */

void bch2_trans_downgrade(struct btree_trans *trans)
{
	struct btree_path *path;

	trans_for_each_path(trans, path)
		bch2_btree_path_downgrade(trans, path);
}

int bch2_trans_relock(struct btree_trans *trans)
{
	struct btree_path *path;

	if (unlikely(trans->restarted))
		return - ((int) trans->restarted);

	trans_for_each_path(trans, path)
		if (path->should_be_locked &&
		    !bch2_btree_path_relock_norestart(trans, path, _RET_IP_)) {
			trace_and_count(trans->c, trans_restart_relock, trans, _RET_IP_, path);
			return btree_trans_restart(trans, BCH_ERR_transaction_restart_relock);
		}
	return 0;
}

void bch2_trans_unlock(struct btree_trans *trans)
{
	struct btree_path *path;

	trans_for_each_path(trans, path)
		__bch2_btree_path_unlock(trans, path);
}

/* Debug */

#ifdef CONFIG_BCACHEFS_DEBUG

void bch2_btree_path_verify_locks(struct btree_path *path)
{
	unsigned l;

	if (!path->nodes_locked) {
		BUG_ON(path->uptodate == BTREE_ITER_UPTODATE &&
		       btree_path_node(path, path->level));
		return;
	}

	for (l = 0; l < BTREE_MAX_DEPTH; l++) {
		int want = btree_lock_want(path, l);
		int have = btree_node_locked_type(path, l);

		BUG_ON(!is_btree_node(path, l) && have != BTREE_NODE_UNLOCKED);

		BUG_ON(is_btree_node(path, l) &&
		       (want == BTREE_NODE_UNLOCKED ||
			have != BTREE_NODE_WRITE_LOCKED) &&
		       want != have);
	}
}

void bch2_trans_verify_locks(struct btree_trans *trans)
{
	struct btree_path *path;

	trans_for_each_path(trans, path)
		bch2_btree_path_verify_locks(path);
}

#endif
