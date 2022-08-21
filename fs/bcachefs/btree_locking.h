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

extern struct lock_class_key bch2_btree_node_lock_key;

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
};

static inline int btree_node_locked_type(struct btree_path *path,
					 unsigned level)
{
	return BTREE_NODE_UNLOCKED + ((path->nodes_locked >> (level << 1)) & 3);
}

static inline bool btree_node_intent_locked(struct btree_path *path,
					    unsigned level)
{
	return btree_node_locked_type(path, level) == BTREE_NODE_INTENT_LOCKED;
}

static inline bool btree_node_read_locked(struct btree_path *path,
					  unsigned level)
{
	return btree_node_locked_type(path, level) == BTREE_NODE_READ_LOCKED;
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
	mark_btree_node_locked_noreset(path, level, BTREE_NODE_UNLOCKED);
}

static inline void mark_btree_node_locked(struct btree_trans *trans,
					  struct btree_path *path,
					  unsigned level,
					  enum six_lock_type type)
{
	mark_btree_node_locked_noreset(path, level, type);
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
	path->l[level].lock_taken_time = ktime_get_ns();
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
	struct btree_transaction_stats *s = btree_trans_stats(trans);

	if (s)
		__bch2_time_stats_update(&s->lock_hold_times,
					 path->l[level].lock_taken_time,
					 ktime_get_ns());
#endif
}

static inline enum bch_time_stats lock_to_time_stat(enum six_lock_type type)
{
	switch (type) {
	case SIX_LOCK_read:
		return BCH_TIME_btree_lock_contended_read;
	case SIX_LOCK_intent:
		return BCH_TIME_btree_lock_contended_intent;
	case SIX_LOCK_write:
		return BCH_TIME_btree_lock_contended_write;
	default:
		BUG();
	}
}

/* unlock: */

static inline void btree_node_unlock(struct btree_trans *trans,
				     struct btree_path *path, unsigned level)
{
	int lock_type = btree_node_locked_type(path, level);

	EBUG_ON(level >= BTREE_MAX_DEPTH);

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

	EBUG_ON(path->l[b->c.level].b != b);
	EBUG_ON(path->l[b->c.level].lock_seq + 1 != b->c.lock.state.seq);

	trans_for_each_path_with_node(trans, b, linked)
		linked->l[b->c.level].lock_seq += 2;

	six_unlock_write(&b->c.lock);
}

void bch2_btree_node_unlock_write(struct btree_trans *,
			struct btree_path *, struct btree *);

/* lock: */

static inline int btree_node_lock_type(struct btree_trans *trans,
				       struct btree_path *path,
				       struct btree *b,
				       struct bpos pos, unsigned level,
				       enum six_lock_type type,
				       six_lock_should_sleep_fn should_sleep_fn, void *p)
{
	struct bch_fs *c = trans->c;
	u64 start_time;
	int ret;

	if (six_trylock_type(&b->c.lock, type))
		return 0;

	start_time = local_clock();

	trans->locking_path_idx = path->idx;
	trans->locking_pos	= pos;
	trans->locking_btree_id	= path->btree_id;
	trans->locking_level	= level;
	trans->locking_lock_type = type;
	trans->locking		= &b->c;
	ret = six_lock_type(&b->c.lock, type, should_sleep_fn, p);
	trans->locking = NULL;

	if (ret)
		return ret;

	bch2_time_stats_update(&c->times[lock_to_time_stat(type)], start_time);
	return 0;
}

/*
 * Lock a btree node if we already have it locked on one of our linked
 * iterators:
 */
static inline bool btree_node_lock_increment(struct btree_trans *trans,
					     struct btree *b, unsigned level,
					     enum btree_node_locked_type want)
{
	struct btree_path *path;

	trans_for_each_path(trans, path)
		if (path->l[level].b == b &&
		    btree_node_locked_type(path, level) >= want) {
			six_lock_increment(&b->c.lock, want);
			return true;
		}

	return false;
}

int __bch2_btree_node_lock(struct btree_trans *, struct btree_path *,
			   struct btree *, struct bpos, unsigned,
			   enum six_lock_type,
			   six_lock_should_sleep_fn, void *,
			   unsigned long);

static inline int btree_node_lock(struct btree_trans *trans,
			struct btree_path *path,
			struct btree *b, struct bpos pos, unsigned level,
			enum six_lock_type type,
			six_lock_should_sleep_fn should_sleep_fn, void *p,
			unsigned long ip)
{
	int ret = 0;

	EBUG_ON(level >= BTREE_MAX_DEPTH);
	EBUG_ON(!(trans->paths_allocated & (1ULL << path->idx)));

	if (likely(six_trylock_type(&b->c.lock, type)) ||
	    btree_node_lock_increment(trans, b, level, type) ||
	    !(ret = __bch2_btree_node_lock(trans, path, b, pos, level, type,
					   should_sleep_fn, p, ip))) {
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
		path->l[b->c.level].lock_taken_time = ktime_get_ns();
#endif
	}

	return ret;
}

void __bch2_btree_node_lock_write(struct btree_trans *, struct btree *);

static inline void bch2_btree_node_lock_write(struct btree_trans *trans,
					      struct btree_path *path,
					      struct btree *b)
{
	EBUG_ON(path->l[b->c.level].b != b);
	EBUG_ON(path->l[b->c.level].lock_seq != b->c.lock.state.seq);
	EBUG_ON(!btree_node_intent_locked(path, b->c.level));

	if (unlikely(!six_trylock_write(&b->c.lock)))
		__bch2_btree_node_lock_write(trans, b);
}

/* relock: */

bool bch2_btree_path_relock_norestart(struct btree_trans *,
				      struct btree_path *, unsigned long);
int __bch2_btree_path_relock(struct btree_trans *,
			     struct btree_path *, unsigned long);

static inline int bch2_btree_path_relock(struct btree_trans *trans,
				struct btree_path *path, unsigned long trace_ip)
{
	return btree_node_locked(path, path->level)
		? 0
		: __bch2_btree_path_relock(trans, path, trace_ip);
}

bool __bch2_btree_node_relock(struct btree_trans *, struct btree_path *, unsigned);

static inline bool bch2_btree_node_relock(struct btree_trans *trans,
					  struct btree_path *path, unsigned level)
{
	EBUG_ON(btree_node_locked(path, level) &&
		btree_node_locked_type(path, level) !=
		__btree_lock_want(path, level));

	return likely(btree_node_locked(path, level)) ||
		__bch2_btree_node_relock(trans, path, level);
}

/* upgrade */

bool bch2_btree_path_upgrade_noupgrade_sibs(struct btree_trans *,
			       struct btree_path *, unsigned);
bool __bch2_btree_path_upgrade(struct btree_trans *,
			       struct btree_path *, unsigned);

static inline bool bch2_btree_path_upgrade(struct btree_trans *trans,
					   struct btree_path *path,
					   unsigned new_locks_want)
{
	new_locks_want = min(new_locks_want, BTREE_MAX_DEPTH);

	return path->locks_want < new_locks_want
		? __bch2_btree_path_upgrade(trans, path, new_locks_want)
		: path->uptodate == BTREE_ITER_UPTODATE;
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
				struct btree_path *, struct btree *, unsigned);


#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_path_verify_locks(struct btree_path *);
void bch2_trans_verify_locks(struct btree_trans *);
#else
static inline void bch2_btree_path_verify_locks(struct btree_path *path) {}
static inline void bch2_trans_verify_locks(struct btree_trans *trans) {}
#endif

#endif /* _BCACHEFS_BTREE_LOCKING_H */
