/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_LOCKING_H
#define _BCACHEFS_BTREE_LOCKING_H

/*
 * Only for internal btree use:
 *
 * The btree iterator tracks what locks it wants to take, and what locks it
 * currently has - here we have wrappers for locking/unlocking btree nodes and
 * updating the iterator state
 */

#include "btree_iter.h"
#include "six.h"

void bch2_btree_lock_init(struct btree_bkey_cached_common *, enum six_lock_init_flags);

void bch2_trans_unlock_noassert(struct btree_trans *);

static inline bool is_btree_node(struct btree_path *path, unsigned l)
{
	return l < BTREE_MAX_DEPTH && !IS_ERR_OR_NULL(path->l[l].b);
}

static inline struct btree_transaction_stats *btree_trans_stats(struct btree_trans *trans)
{
	return trans->fn_idx < ARRAY_SIZE(trans->c->btree_transaction_stats)
		? &trans->c->btree_transaction_stats[trans->fn_idx]
		: NULL;
}

/* matches six lock types */
enum btree_node_locked_type {
	BTREE_NODE_UNLOCKED		= -1,
	BTREE_NODE_READ_LOCKED		= SIX_LOCK_read,
	BTREE_NODE_INTENT_LOCKED	= SIX_LOCK_intent,
	BTREE_NODE_WRITE_LOCKED		= SIX_LOCK_write,
};

static inline int btree_node_locked_type(struct btree_path *path,
					 unsigned level)
{
	return BTREE_NODE_UNLOCKED + ((path->nodes_locked >> (level << 1)) & 3);
}

static inline bool btree_node_write_locked(struct btree_path *path, unsigned l)
{
	return btree_node_locked_type(path, l) == BTREE_NODE_WRITE_LOCKED;
}

static inline bool btree_node_intent_locked(struct btree_path *path, unsigned l)
{
	return btree_node_locked_type(path, l) == BTREE_NODE_INTENT_LOCKED;
}

static inline bool btree_node_read_locked(struct btree_path *path, unsigned l)
{
	return btree_node_locked_type(path, l) == BTREE_NODE_READ_LOCKED;
}

static inline bool btree_node_locked(struct btree_path *path, unsigned level)
{
	return btree_node_locked_type(path, level) != BTREE_NODE_UNLOCKED;
}

static inline void mark_btree_node_locked_noreset(struct btree_path *path,
						  unsigned level,
						  enum btree_node_locked_type type)
{
	/* relying on this to avoid a branch */
	BUILD_BUG_ON(SIX_LOCK_read   != 0);
	BUILD_BUG_ON(SIX_LOCK_intent != 1);

	path->nodes_locked &= ~(3U << (level << 1));
	path->nodes_locked |= (type + 1) << (level << 1);
}

static inline void mark_btree_node_unlocked(struct btree_path *path,
					    unsigned level)
{
	EBUG_ON(btree_node_write_locked(path, level));
	mark_btree_node_locked_noreset(path, level, BTREE_NODE_UNLOCKED);
}

static inline void mark_btree_node_locked(struct btree_trans *trans,
					  struct btree_path *path,
					  unsigned level,
					  enum btree_node_locked_type type)
{
	mark_btree_node_locked_noreset(path, level, (enum btree_node_locked_type) type);
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
	path->l[level].lock_taken_time = local_clock();
#endif
}

static inline enum six_lock_type __btree_lock_want(struct btree_path *path, int level)
{
	return level < path->locks_want
		? SIX_LOCK_intent
		: SIX_LOCK_read;
}

static inline enum btree_node_locked_type
btree_lock_want(struct btree_path *path, int level)
{
	if (level < path->level)
		return BTREE_NODE_UNLOCKED;
	if (level < path->locks_want)
		return BTREE_NODE_INTENT_LOCKED;
	if (level == path->level)
		return BTREE_NODE_READ_LOCKED;
	return BTREE_NODE_UNLOCKED;
}

static void btree_trans_lock_hold_time_update(struct btree_trans *trans,
					      struct btree_path *path, unsigned level)
{
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
	__bch2_time_stats_update(&btree_trans_stats(trans)->lock_hold_times,
				 path->l[level].lock_taken_time,
				 local_clock());
#endif
}

/* unlock: */

static inline void btree_node_unlock(struct btree_trans *trans,
				     struct btree_path *path, unsigned level)
{
	int lock_type = btree_node_locked_type(path, level);

	EBUG_ON(level >= BTREE_MAX_DEPTH);
	EBUG_ON(lock_type == BTREE_NODE_WRITE_LOCKED);

	if (lock_type != BTREE_NODE_UNLOCKED) {
		six_unlock_type(&path->l[level].b->c.lock, lock_type);
		btree_trans_lock_hold_time_update(trans, path, level);
	}
	mark_btree_node_unlocked(path, level);
}

static inline int btree_path_lowest_level_locked(struct btree_path *path)
{
	return __ffs(path->nodes_locked) >> 1;
}

static inline int btree_path_highest_level_locked(struct btree_path *path)
{
	return __fls(path->nodes_locked) >> 1;
}

static inline void __bch2_btree_path_unlock(struct btree_trans *trans,
					    struct btree_path *path)
{
	btree_path_set_dirty(path, BTREE_ITER_NEED_RELOCK);

	while (path->nodes_locked)
		btree_node_unlock(trans, path, btree_path_lowest_level_locked(path));
}

/*
 * Updates the saved lock sequence number, so that bch2_btree_node_relock() will
 * succeed:
 */
static inline void
bch2_btree_node_unlock_write_inlined(struct btree_trans *trans, struct btree_path *path,
				     struct btree *b)
{
	struct btree_path *linked;
	unsigned i;

	EBUG_ON(path->l[b->c.level].b != b);
	EBUG_ON(path->l[b->c.level].lock_seq != six_lock_seq(&b->c.lock));
	EBUG_ON(btree_node_locked_type(path, b->c.level) != SIX_LOCK_write);

	mark_btree_node_locked_noreset(path, b->c.level, BTREE_NODE_INTENT_LOCKED);

	trans_for_each_path_with_node(trans, b, linked, i)
		linked->l[b->c.level].lock_seq++;

	six_unlock_write(&b->c.lock);
}

void bch2_btree_node_unlock_write(struct btree_trans *,
			struct btree_path *, struct btree *);

int bch2_six_check_for_deadlock(struct six_lock *lock, void *p);

/* lock: */

static inline void trans_set_locked(struct btree_trans *trans)
{
	if (!trans->locked) {
		lock_acquire_exclusive(&trans->dep_map, 0, 0, NULL, _THIS_IP_);
		trans->locked = true;
		trans->last_unlock_ip = 0;

		trans->pf_memalloc_nofs = (current->flags & PF_MEMALLOC_NOFS) != 0;
		current->flags |= PF_MEMALLOC_NOFS;
	}
}

static inline void trans_set_unlocked(struct btree_trans *trans)
{
	if (trans->locked) {
		lock_release(&trans->dep_map, _THIS_IP_);
		trans->locked = false;
		trans->last_unlock_ip = _RET_IP_;

		if (!trans->pf_memalloc_nofs)
			current->flags &= ~PF_MEMALLOC_NOFS;
	}
}

static inline int __btree_node_lock_nopath(struct btree_trans *trans,
					 struct btree_bkey_cached_common *b,
					 enum six_lock_type type,
					 bool lock_may_not_fail,
					 unsigned long ip)
{
	int ret;

	trans->lock_may_not_fail = lock_may_not_fail;
	trans->lock_must_abort	= false;
	trans->locking		= b;

	ret = six_lock_ip_waiter(&b->lock, type, &trans->locking_wait,
				 bch2_six_check_for_deadlock, trans, ip);
	WRITE_ONCE(trans->locking, NULL);
	WRITE_ONCE(trans->locking_wait.start_time, 0);
	return ret;
}

static inline int __must_check
btree_node_lock_nopath(struct btree_trans *trans,
		       struct btree_bkey_cached_common *b,
		       enum six_lock_type type,
		       unsigned long ip)
{
	return __btree_node_lock_nopath(trans, b, type, false, ip);
}

static inline void btree_node_lock_nopath_nofail(struct btree_trans *trans,
					 struct btree_bkey_cached_common *b,
					 enum six_lock_type type)
{
	int ret = __btree_node_lock_nopath(trans, b, type, true, _THIS_IP_);

	BUG_ON(ret);
}

/*
 * Lock a btree node if we already have it locked on one of our linked
 * iterators:
 */
static inline bool btree_node_lock_increment(struct btree_trans *trans,
					     struct btree_bkey_cached_common *b,
					     unsigned level,
					     enum btree_node_locked_type want)
{
	struct btree_path *path;
	unsigned i;

	trans_for_each_path(trans, path, i)
		if (&path->l[level].b->c == b &&
		    btree_node_locked_type(path, level) >= want) {
			six_lock_increment(&b->lock, (enum six_lock_type) want);
			return true;
		}

	return false;
}

static inline int btree_node_lock(struct btree_trans *trans,
			struct btree_path *path,
			struct btree_bkey_cached_common *b,
			unsigned level,
			enum six_lock_type type,
			unsigned long ip)
{
	int ret = 0;

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	if (likely(six_trylock_type(&b->lock, type)) ||
	    btree_node_lock_increment(trans, b, level, (enum btree_node_locked_type) type) ||
	    !(ret = btree_node_lock_nopath(trans, b, type, btree_path_ip_allocated(path)))) {
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
		path->l[b->level].lock_taken_time = local_clock();
#endif
	}

	return ret;
}

int __bch2_btree_node_lock_write(struct btree_trans *, struct btree_path *,
				 struct btree_bkey_cached_common *b, bool);

static inline int __btree_node_lock_write(struct btree_trans *trans,
					  struct btree_path *path,
					  struct btree_bkey_cached_common *b,
					  bool lock_may_not_fail)
{
	EBUG_ON(&path->l[b->level].b->c != b);
	EBUG_ON(path->l[b->level].lock_seq != six_lock_seq(&b->lock));
	EBUG_ON(!btree_node_intent_locked(path, b->level));

	/*
	 * six locks are unfair, and read locks block while a thread wants a
	 * write lock: thus, we need to tell the cycle detector we have a write
	 * lock _before_ taking the lock:
	 */
	mark_btree_node_locked_noreset(path, b->level, BTREE_NODE_WRITE_LOCKED);

	return likely(six_trylock_write(&b->lock))
		? 0
		: __bch2_btree_node_lock_write(trans, path, b, lock_may_not_fail);
}

static inline int __must_check
bch2_btree_node_lock_write(struct btree_trans *trans,
			   struct btree_path *path,
			   struct btree_bkey_cached_common *b)
{
	return __btree_node_lock_write(trans, path, b, false);
}

void bch2_btree_node_lock_write_nofail(struct btree_trans *,
				       struct btree_path *,
				       struct btree_bkey_cached_common *);

/* relock: */

bool bch2_btree_path_relock_norestart(struct btree_trans *, struct btree_path *);
int __bch2_btree_path_relock(struct btree_trans *,
			     struct btree_path *, unsigned long);

static inline int bch2_btree_path_relock(struct btree_trans *trans,
				struct btree_path *path, unsigned long trace_ip)
{
	return btree_node_locked(path, path->level)
		? 0
		: __bch2_btree_path_relock(trans, path, trace_ip);
}

bool __bch2_btree_node_relock(struct btree_trans *, struct btree_path *, unsigned, bool trace);

static inline bool bch2_btree_node_relock(struct btree_trans *trans,
					  struct btree_path *path, unsigned level)
{
	EBUG_ON(btree_node_locked(path, level) &&
		!btree_node_write_locked(path, level) &&
		btree_node_locked_type(path, level) != __btree_lock_want(path, level));

	return likely(btree_node_locked(path, level)) ||
		(!IS_ERR_OR_NULL(path->l[level].b) &&
		 __bch2_btree_node_relock(trans, path, level, true));
}

static inline bool bch2_btree_node_relock_notrace(struct btree_trans *trans,
						  struct btree_path *path, unsigned level)
{
	EBUG_ON(btree_node_locked(path, level) &&
		!btree_node_write_locked(path, level) &&
		btree_node_locked_type(path, level) != __btree_lock_want(path, level));

	return likely(btree_node_locked(path, level)) ||
		(!IS_ERR_OR_NULL(path->l[level].b) &&
		 __bch2_btree_node_relock(trans, path, level, false));
}

/* upgrade */

bool bch2_btree_path_upgrade_noupgrade_sibs(struct btree_trans *,
			       struct btree_path *, unsigned,
			       struct get_locks_fail *);

bool __bch2_btree_path_upgrade(struct btree_trans *,
			       struct btree_path *, unsigned,
			       struct get_locks_fail *);

static inline int bch2_btree_path_upgrade(struct btree_trans *trans,
					  struct btree_path *path,
					  unsigned new_locks_want)
{
	struct get_locks_fail f = {};
	unsigned old_locks_want = path->locks_want;

	new_locks_want = min(new_locks_want, BTREE_MAX_DEPTH);

	if (path->locks_want < new_locks_want
	    ? __bch2_btree_path_upgrade(trans, path, new_locks_want, &f)
	    : path->nodes_locked)
		return 0;

	trace_and_count(trans->c, trans_restart_upgrade, trans, _THIS_IP_, path,
			old_locks_want, new_locks_want, &f);
	return btree_trans_restart(trans, BCH_ERR_transaction_restart_upgrade);
}

/* misc: */

static inline void btree_path_set_should_be_locked(struct btree_path *path)
{
	EBUG_ON(!btree_node_locked(path, path->level));
	EBUG_ON(path->uptodate);

	path->should_be_locked = true;
}

static inline void __btree_path_set_level_up(struct btree_trans *trans,
				      struct btree_path *path,
				      unsigned l)
{
	btree_node_unlock(trans, path, l);
	path->l[l].b = ERR_PTR(-BCH_ERR_no_btree_node_up);
}

static inline void btree_path_set_level_up(struct btree_trans *trans,
				    struct btree_path *path)
{
	__btree_path_set_level_up(trans, path, path->level++);
	btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
}

/* debug */

struct six_lock_count bch2_btree_node_lock_counts(struct btree_trans *,
				struct btree_path *,
				struct btree_bkey_cached_common *b,
				unsigned);

int bch2_check_for_deadlock(struct btree_trans *, struct printbuf *);

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_path_verify_locks(struct btree_path *);
void bch2_trans_verify_locks(struct btree_trans *);
#else
static inline void bch2_btree_path_verify_locks(struct btree_path *path) {}
static inline void bch2_trans_verify_locks(struct btree_trans *trans) {}
#endif

#endif /* _BCACHEFS_BTREE_LOCKING_H */
