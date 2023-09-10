/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_ITER_H
#define _BCACHEFS_BTREE_ITER_H

#include "bset.h"
#include "btree_types.h"
#include "trace.h"

static inline int __bkey_err(const struct bkey *k)
{
	return PTR_ERR_OR_ZERO(k);
}

#define bkey_err(_k)	__bkey_err((_k).k)

static inline void __btree_path_get(struct btree_path *path, bool intent)
{
	path->ref++;
	path->intent_ref += intent;
}

static inline bool __btree_path_put(struct btree_path *path, bool intent)
{
	EBUG_ON(!path->ref);
	EBUG_ON(!path->intent_ref && intent);
	path->intent_ref -= intent;
	return --path->ref == 0;
}

static inline void btree_path_set_dirty(struct btree_path *path,
					enum btree_path_uptodate u)
{
	path->uptodate = max_t(unsigned, path->uptodate, u);
}

static inline struct btree *btree_path_node(struct btree_path *path,
					    unsigned level)
{
	return level < BTREE_MAX_DEPTH ? path->l[level].b : NULL;
}

static inline bool btree_node_lock_seq_matches(const struct btree_path *path,
					const struct btree *b, unsigned level)
{
	return path->l[level].lock_seq == six_lock_seq(&b->c.lock);
}

static inline struct btree *btree_node_parent(struct btree_path *path,
					      struct btree *b)
{
	return btree_path_node(path, b->c.level + 1);
}

/* Iterate over paths within a transaction: */

void __bch2_btree_trans_sort_paths(struct btree_trans *);

static inline void btree_trans_sort_paths(struct btree_trans *trans)
{
	if (!IS_ENABLED(CONFIG_BCACHEFS_DEBUG) &&
	    trans->paths_sorted)
		return;
	__bch2_btree_trans_sort_paths(trans);
}

static inline struct btree_path *
__trans_next_path(struct btree_trans *trans, unsigned idx)
{
	u64 l;

	if (idx == BTREE_ITER_MAX)
		return NULL;

	l = trans->paths_allocated >> idx;
	if (!l)
		return NULL;

	idx += __ffs64(l);
	EBUG_ON(idx >= BTREE_ITER_MAX);
	EBUG_ON(trans->paths[idx].idx != idx);
	return &trans->paths[idx];
}

#define trans_for_each_path_from(_trans, _path, _start)			\
	for (_path = __trans_next_path((_trans), _start);		\
	     (_path);							\
	     _path = __trans_next_path((_trans), (_path)->idx + 1))

#define trans_for_each_path(_trans, _path)				\
	trans_for_each_path_from(_trans, _path, 0)

static inline struct btree_path *
__trans_next_path_safe(struct btree_trans *trans, unsigned *idx)
{
	u64 l;

	if (*idx == BTREE_ITER_MAX)
		return NULL;

	l = trans->paths_allocated >> *idx;
	if (!l)
		return NULL;

	*idx += __ffs64(l);
	EBUG_ON(*idx >= BTREE_ITER_MAX);
	return &trans->paths[*idx];
}

/*
 * This version is intended to be safe for use on a btree_trans that is owned by
 * another thread, for bch2_btree_trans_to_text();
 */
#define trans_for_each_path_safe_from(_trans, _path, _idx, _start)	\
	for (_idx = _start;						\
	     (_path = __trans_next_path_safe((_trans), &_idx));		\
	     _idx++)

#define trans_for_each_path_safe(_trans, _path, _idx)			\
	trans_for_each_path_safe_from(_trans, _path, _idx, 0)

static inline struct btree_path *next_btree_path(struct btree_trans *trans, struct btree_path *path)
{
	unsigned idx = path ? path->sorted_idx + 1 : 0;

	EBUG_ON(idx > trans->nr_sorted);

	return idx < trans->nr_sorted
		? trans->paths + trans->sorted[idx]
		: NULL;
}

static inline struct btree_path *prev_btree_path(struct btree_trans *trans, struct btree_path *path)
{
	unsigned idx = path ? path->sorted_idx : trans->nr_sorted;

	return idx
		? trans->paths + trans->sorted[idx - 1]
		: NULL;
}

#define trans_for_each_path_inorder(_trans, _path, _i)			\
	for (_i = 0;							\
	     ((_path) = (_trans)->paths + trans->sorted[_i]), (_i) < (_trans)->nr_sorted;\
	     _i++)

#define trans_for_each_path_inorder_reverse(_trans, _path, _i)		\
	for (_i = trans->nr_sorted - 1;					\
	     ((_path) = (_trans)->paths + trans->sorted[_i]), (_i) >= 0;\
	     --_i)

static inline bool __path_has_node(const struct btree_path *path,
				   const struct btree *b)
{
	return path->l[b->c.level].b == b &&
		btree_node_lock_seq_matches(path, b, b->c.level);
}

static inline struct btree_path *
__trans_next_path_with_node(struct btree_trans *trans, struct btree *b,
			    unsigned idx)
{
	struct btree_path *path = __trans_next_path(trans, idx);

	while (path && !__path_has_node(path, b))
		path = __trans_next_path(trans, path->idx + 1);

	return path;
}

#define trans_for_each_path_with_node(_trans, _b, _path)		\
	for (_path = __trans_next_path_with_node((_trans), (_b), 0);	\
	     (_path);							\
	     _path = __trans_next_path_with_node((_trans), (_b),	\
						 (_path)->idx + 1))

struct btree_path *__bch2_btree_path_make_mut(struct btree_trans *, struct btree_path *,
			 bool, unsigned long);

static inline struct btree_path * __must_check
bch2_btree_path_make_mut(struct btree_trans *trans,
			 struct btree_path *path, bool intent,
			 unsigned long ip)
{
	if (path->ref > 1 || path->preserve)
		path = __bch2_btree_path_make_mut(trans, path, intent, ip);
	path->should_be_locked = false;
	return path;
}

struct btree_path * __must_check
__bch2_btree_path_set_pos(struct btree_trans *, struct btree_path *,
			struct bpos, bool, unsigned long, int);

static inline struct btree_path * __must_check
bch2_btree_path_set_pos(struct btree_trans *trans,
		   struct btree_path *path, struct bpos new_pos,
		   bool intent, unsigned long ip)
{
	int cmp = bpos_cmp(new_pos, path->pos);

	return cmp
		? __bch2_btree_path_set_pos(trans, path, new_pos, intent, ip, cmp)
		: path;
}

int __must_check bch2_btree_path_traverse_one(struct btree_trans *, struct btree_path *,
					      unsigned, unsigned long);

static inline int __must_check bch2_btree_path_traverse(struct btree_trans *trans,
					  struct btree_path *path, unsigned flags)
{
	if (path->uptodate < BTREE_ITER_NEED_RELOCK)
		return 0;

	return bch2_btree_path_traverse_one(trans, path, flags, _RET_IP_);
}

int __must_check bch2_btree_path_traverse(struct btree_trans *,
					  struct btree_path *, unsigned);
struct btree_path *bch2_path_get(struct btree_trans *, enum btree_id, struct bpos,
				 unsigned, unsigned, unsigned, unsigned long);
struct bkey_s_c bch2_btree_path_peek_slot(struct btree_path *, struct bkey *);

/*
 * bch2_btree_path_peek_slot() for a cached iterator might return a key in a
 * different snapshot:
 */
static inline struct bkey_s_c bch2_btree_path_peek_slot_exact(struct btree_path *path, struct bkey *u)
{
	struct bkey_s_c k = bch2_btree_path_peek_slot(path, u);

	if (k.k && bpos_eq(path->pos, k.k->p))
		return k;

	bkey_init(u);
	u->p = path->pos;
	return (struct bkey_s_c) { u, NULL };
}

struct bkey_i *bch2_btree_journal_peek_slot(struct btree_trans *,
					struct btree_iter *, struct bpos);

void bch2_btree_path_level_init(struct btree_trans *, struct btree_path *, struct btree *);

int __bch2_trans_mutex_lock(struct btree_trans *, struct mutex *);

static inline int bch2_trans_mutex_lock(struct btree_trans *trans, struct mutex *lock)
{
	return mutex_trylock(lock)
		? 0
		: __bch2_trans_mutex_lock(trans, lock);
}

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_trans_verify_paths(struct btree_trans *);
void bch2_assert_pos_locked(struct btree_trans *, enum btree_id,
			    struct bpos, bool);
#else
static inline void bch2_trans_verify_paths(struct btree_trans *trans) {}
static inline void bch2_assert_pos_locked(struct btree_trans *trans, enum btree_id id,
					  struct bpos pos, bool key_cache) {}
#endif

void bch2_btree_path_fix_key_modified(struct btree_trans *trans,
				      struct btree *, struct bkey_packed *);
void bch2_btree_node_iter_fix(struct btree_trans *trans, struct btree_path *,
			      struct btree *, struct btree_node_iter *,
			      struct bkey_packed *, unsigned, unsigned);

int bch2_btree_path_relock_intent(struct btree_trans *, struct btree_path *);

void bch2_path_put(struct btree_trans *, struct btree_path *, bool);

int bch2_trans_relock(struct btree_trans *);
int bch2_trans_relock_notrace(struct btree_trans *);
void bch2_trans_unlock(struct btree_trans *);
bool bch2_trans_locked(struct btree_trans *);

static inline int trans_was_restarted(struct btree_trans *trans, u32 restart_count)
{
	return restart_count != trans->restart_count
		? -BCH_ERR_transaction_restart_nested
		: 0;
}

void __noreturn bch2_trans_restart_error(struct btree_trans *, u32);

static inline void bch2_trans_verify_not_restarted(struct btree_trans *trans,
						   u32 restart_count)
{
	if (trans_was_restarted(trans, restart_count))
		bch2_trans_restart_error(trans, restart_count);
}

void __noreturn bch2_trans_in_restart_error(struct btree_trans *);

static inline void bch2_trans_verify_not_in_restart(struct btree_trans *trans)
{
	if (trans->restarted)
		bch2_trans_in_restart_error(trans);
}

__always_inline
static int btree_trans_restart_nounlock(struct btree_trans *trans, int err)
{
	BUG_ON(err <= 0);
	BUG_ON(!bch2_err_matches(-err, BCH_ERR_transaction_restart));

	trans->restarted = err;
	trans->last_restarted_ip = _THIS_IP_;
	return -err;
}

__always_inline
static int btree_trans_restart(struct btree_trans *trans, int err)
{
	btree_trans_restart_nounlock(trans, err);
	return -err;
}

bool bch2_btree_node_upgrade(struct btree_trans *,
			     struct btree_path *, unsigned);

void __bch2_btree_path_downgrade(struct btree_trans *, struct btree_path *, unsigned);

static inline void bch2_btree_path_downgrade(struct btree_trans *trans,
					     struct btree_path *path)
{
	unsigned new_locks_want = path->level + !!path->intent_ref;

	if (path->locks_want > new_locks_want)
		__bch2_btree_path_downgrade(trans, path, new_locks_want);
}

void bch2_trans_downgrade(struct btree_trans *);

void bch2_trans_node_add(struct btree_trans *trans, struct btree *);
void bch2_trans_node_reinit_iter(struct btree_trans *, struct btree *);

int __must_check __bch2_btree_iter_traverse(struct btree_iter *iter);
int __must_check bch2_btree_iter_traverse(struct btree_iter *);

struct btree *bch2_btree_iter_peek_node(struct btree_iter *);
struct btree *bch2_btree_iter_peek_node_and_restart(struct btree_iter *);
struct btree *bch2_btree_iter_next_node(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_upto(struct btree_iter *, struct bpos);
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_all_levels(struct btree_iter *);

static inline struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *iter)
{
	return bch2_btree_iter_peek_upto(iter, SPOS_MAX);
}

struct bkey_s_c bch2_btree_iter_peek_prev(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev_slot(struct btree_iter *);

bool bch2_btree_iter_advance(struct btree_iter *);
bool bch2_btree_iter_rewind(struct btree_iter *);

static inline void __bch2_btree_iter_set_pos(struct btree_iter *iter, struct bpos new_pos)
{
	iter->k.type = KEY_TYPE_deleted;
	iter->k.p.inode		= iter->pos.inode	= new_pos.inode;
	iter->k.p.offset	= iter->pos.offset	= new_pos.offset;
	iter->k.p.snapshot	= iter->pos.snapshot	= new_pos.snapshot;
	iter->k.size = 0;
}

static inline void bch2_btree_iter_set_pos(struct btree_iter *iter, struct bpos new_pos)
{
	if (unlikely(iter->update_path))
		bch2_path_put(iter->trans, iter->update_path,
			      iter->flags & BTREE_ITER_INTENT);
	iter->update_path = NULL;

	if (!(iter->flags & BTREE_ITER_ALL_SNAPSHOTS))
		new_pos.snapshot = iter->snapshot;

	__bch2_btree_iter_set_pos(iter, new_pos);
}

static inline void bch2_btree_iter_set_pos_to_extent_start(struct btree_iter *iter)
{
	BUG_ON(!(iter->flags & BTREE_ITER_IS_EXTENTS));
	iter->pos = bkey_start_pos(&iter->k);
}

static inline void bch2_btree_iter_set_snapshot(struct btree_iter *iter, u32 snapshot)
{
	struct bpos pos = iter->pos;

	iter->snapshot = snapshot;
	pos.snapshot = snapshot;
	bch2_btree_iter_set_pos(iter, pos);
}

void bch2_trans_iter_exit(struct btree_trans *, struct btree_iter *);

static inline unsigned __bch2_btree_iter_flags(struct btree_trans *trans,
					       unsigned btree_id,
					       unsigned flags)
{
	if (flags & BTREE_ITER_ALL_LEVELS)
		flags |= BTREE_ITER_ALL_SNAPSHOTS|__BTREE_ITER_ALL_SNAPSHOTS;

	if (!(flags & (BTREE_ITER_ALL_SNAPSHOTS|BTREE_ITER_NOT_EXTENTS)) &&
	    btree_node_type_is_extents(btree_id))
		flags |= BTREE_ITER_IS_EXTENTS;

	if (!(flags & __BTREE_ITER_ALL_SNAPSHOTS) &&
	    !btree_type_has_snapshots(btree_id))
		flags &= ~BTREE_ITER_ALL_SNAPSHOTS;

	if (!(flags & BTREE_ITER_ALL_SNAPSHOTS) &&
	    btree_type_has_snapshots(btree_id))
		flags |= BTREE_ITER_FILTER_SNAPSHOTS;

	if (trans->journal_replay_not_finished)
		flags |= BTREE_ITER_WITH_JOURNAL;

	return flags;
}

static inline unsigned bch2_btree_iter_flags(struct btree_trans *trans,
					     unsigned btree_id,
					     unsigned flags)
{
	if (!btree_id_cached(trans->c, btree_id)) {
		flags &= ~BTREE_ITER_CACHED;
		flags &= ~BTREE_ITER_WITH_KEY_CACHE;
	} else if (!(flags & BTREE_ITER_CACHED))
		flags |= BTREE_ITER_WITH_KEY_CACHE;

	return __bch2_btree_iter_flags(trans, btree_id, flags);
}

static inline void bch2_trans_iter_init_common(struct btree_trans *trans,
					  struct btree_iter *iter,
					  unsigned btree_id, struct bpos pos,
					  unsigned locks_want,
					  unsigned depth,
					  unsigned flags,
					  unsigned long ip)
{
	memset(iter, 0, sizeof(*iter));
	iter->trans	= trans;
	iter->btree_id	= btree_id;
	iter->flags	= flags;
	iter->snapshot	= pos.snapshot;
	iter->pos	= pos;
	iter->k.p	= pos;

#ifdef CONFIG_BCACHEFS_DEBUG
	iter->ip_allocated = ip;
#endif
	iter->path = bch2_path_get(trans, btree_id, iter->pos,
				   locks_want, depth, flags, ip);
}

void bch2_trans_iter_init_outlined(struct btree_trans *, struct btree_iter *,
			  enum btree_id, struct bpos, unsigned);

static inline void bch2_trans_iter_init(struct btree_trans *trans,
			  struct btree_iter *iter,
			  unsigned btree_id, struct bpos pos,
			  unsigned flags)
{
	if (__builtin_constant_p(btree_id) &&
	    __builtin_constant_p(flags))
		bch2_trans_iter_init_common(trans, iter, btree_id, pos, 0, 0,
				bch2_btree_iter_flags(trans, btree_id, flags),
				_THIS_IP_);
	else
		bch2_trans_iter_init_outlined(trans, iter, btree_id, pos, flags);
}

void bch2_trans_node_iter_init(struct btree_trans *, struct btree_iter *,
			       enum btree_id, struct bpos,
			       unsigned, unsigned, unsigned);
void bch2_trans_copy_iter(struct btree_iter *, struct btree_iter *);

static inline void set_btree_iter_dontneed(struct btree_iter *iter)
{
	if (!iter->trans->restarted)
		iter->path->preserve = false;
}

void *__bch2_trans_kmalloc(struct btree_trans *, size_t);

static inline void *bch2_trans_kmalloc(struct btree_trans *trans, size_t size)
{
	size = roundup(size, 8);

	if (likely(trans->mem_top + size <= trans->mem_bytes)) {
		void *p = trans->mem + trans->mem_top;

		trans->mem_top += size;
		memset(p, 0, size);
		return p;
	} else {
		return __bch2_trans_kmalloc(trans, size);
	}
}

static inline void *bch2_trans_kmalloc_nomemzero(struct btree_trans *trans, size_t size)
{
	size = roundup(size, 8);

	if (likely(trans->mem_top + size <= trans->mem_bytes)) {
		void *p = trans->mem + trans->mem_top;

		trans->mem_top += size;
		return p;
	} else {
		return __bch2_trans_kmalloc(trans, size);
	}
}

static inline struct bkey_s_c __bch2_bkey_get_iter(struct btree_trans *trans,
				struct btree_iter *iter,
				unsigned btree_id, struct bpos pos,
				unsigned flags, unsigned type)
{
	struct bkey_s_c k;

	bch2_trans_iter_init(trans, iter, btree_id, pos, flags);
	k = bch2_btree_iter_peek_slot(iter);

	if (!bkey_err(k) && type && k.k->type != type)
		k = bkey_s_c_err(-BCH_ERR_ENOENT_bkey_type_mismatch);
	if (unlikely(bkey_err(k)))
		bch2_trans_iter_exit(trans, iter);
	return k;
}

static inline struct bkey_s_c bch2_bkey_get_iter(struct btree_trans *trans,
				struct btree_iter *iter,
				unsigned btree_id, struct bpos pos,
				unsigned flags)
{
	return __bch2_bkey_get_iter(trans, iter, btree_id, pos, flags, 0);
}

#define bch2_bkey_get_iter_typed(_trans, _iter, _btree_id, _pos, _flags, _type)\
	bkey_s_c_to_##_type(__bch2_bkey_get_iter(_trans, _iter,			\
				       _btree_id, _pos, _flags, KEY_TYPE_##_type))

static inline int __bch2_bkey_get_val_typed(struct btree_trans *trans,
				unsigned btree_id, struct bpos pos,
				unsigned flags, unsigned type,
				unsigned val_size, void *val)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	k = __bch2_bkey_get_iter(trans, &iter, btree_id, pos, flags, type);
	ret = bkey_err(k);
	if (!ret) {
		unsigned b = min_t(unsigned, bkey_val_bytes(k.k), val_size);

		memcpy(val, k.v, b);
		if (unlikely(b < sizeof(*val)))
			memset((void *) val + b, 0, sizeof(*val) - b);
		bch2_trans_iter_exit(trans, &iter);
	}

	return ret;
}

#define bch2_bkey_get_val_typed(_trans, _btree_id, _pos, _flags, _type, _val)\
	__bch2_bkey_get_val_typed(_trans, _btree_id, _pos, _flags,	\
				  KEY_TYPE_##_type, sizeof(*_val), _val)

u32 bch2_trans_begin(struct btree_trans *);

/*
 * XXX
 * this does not handle transaction restarts from bch2_btree_iter_next_node()
 * correctly
 */
#define __for_each_btree_node(_trans, _iter, _btree_id, _start,		\
			      _locks_want, _depth, _flags, _b, _ret)	\
	for (bch2_trans_node_iter_init((_trans), &(_iter), (_btree_id),	\
				_start, _locks_want, _depth, _flags);	\
	     (_b) = bch2_btree_iter_peek_node_and_restart(&(_iter)),	\
	     !((_ret) = PTR_ERR_OR_ZERO(_b)) && (_b);			\
	     (_b) = bch2_btree_iter_next_node(&(_iter)))

#define for_each_btree_node(_trans, _iter, _btree_id, _start,		\
			    _flags, _b, _ret)				\
	__for_each_btree_node(_trans, _iter, _btree_id, _start,		\
			      0, 0, _flags, _b, _ret)

static inline struct bkey_s_c bch2_btree_iter_peek_prev_type(struct btree_iter *iter,
							     unsigned flags)
{
	BUG_ON(flags & BTREE_ITER_ALL_LEVELS);

	return  flags & BTREE_ITER_SLOTS      ? bch2_btree_iter_peek_slot(iter) :
						bch2_btree_iter_peek_prev(iter);
}

static inline struct bkey_s_c bch2_btree_iter_peek_type(struct btree_iter *iter,
							unsigned flags)
{
	return  flags & BTREE_ITER_ALL_LEVELS ? bch2_btree_iter_peek_all_levels(iter) :
		flags & BTREE_ITER_SLOTS      ? bch2_btree_iter_peek_slot(iter) :
						bch2_btree_iter_peek(iter);
}

static inline struct bkey_s_c bch2_btree_iter_peek_upto_type(struct btree_iter *iter,
							     struct bpos end,
							     unsigned flags)
{
	if (!(flags & BTREE_ITER_SLOTS))
		return bch2_btree_iter_peek_upto(iter, end);

	if (bkey_gt(iter->pos, end))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_slot(iter);
}

static inline int btree_trans_too_many_iters(struct btree_trans *trans)
{
	if (hweight64(trans->paths_allocated) > BTREE_ITER_MAX - 8) {
		trace_and_count(trans->c, trans_restart_too_many_iters, trans, _THIS_IP_);
		return btree_trans_restart(trans, BCH_ERR_transaction_restart_too_many_iters);
	}

	return 0;
}

struct bkey_s_c bch2_btree_iter_peek_and_restart_outlined(struct btree_iter *);

static inline struct bkey_s_c
__bch2_btree_iter_peek_and_restart(struct btree_trans *trans,
				   struct btree_iter *iter, unsigned flags)
{
	struct bkey_s_c k;

	while (btree_trans_too_many_iters(trans) ||
	       (k = bch2_btree_iter_peek_type(iter, flags),
		bch2_err_matches(bkey_err(k), BCH_ERR_transaction_restart)))
		bch2_trans_begin(trans);

	return k;
}

static inline struct bkey_s_c
__bch2_btree_iter_peek_upto_and_restart(struct btree_trans *trans,
					struct btree_iter *iter,
					struct bpos end,
					unsigned flags)
{
	struct bkey_s_c k;

	while (btree_trans_too_many_iters(trans) ||
	       (k = bch2_btree_iter_peek_upto_type(iter, end, flags),
		bch2_err_matches(bkey_err(k), BCH_ERR_transaction_restart)))
		bch2_trans_begin(trans);

	return k;
}

#define lockrestart_do(_trans, _do)					\
({									\
	u32 _restart_count;						\
	int _ret;							\
									\
	do {								\
		_restart_count = bch2_trans_begin(_trans);		\
		_ret = (_do);						\
	} while (bch2_err_matches(_ret, BCH_ERR_transaction_restart));	\
									\
	if (!_ret)							\
		bch2_trans_verify_not_restarted(_trans, _restart_count);\
									\
	_ret;								\
})

/*
 * nested_lockrestart_do(), nested_commit_do():
 *
 * These are like lockrestart_do() and commit_do(), with two differences:
 *
 *  - We don't call bch2_trans_begin() unless we had a transaction restart
 *  - We return -BCH_ERR_transaction_restart_nested if we succeeded after a
 *  transaction restart
 */
#define nested_lockrestart_do(_trans, _do)				\
({									\
	u32 _restart_count, _orig_restart_count;			\
	int _ret;							\
									\
	_restart_count = _orig_restart_count = (_trans)->restart_count;	\
									\
	while (bch2_err_matches(_ret = (_do), BCH_ERR_transaction_restart))\
		_restart_count = bch2_trans_begin(_trans);		\
									\
	if (!_ret)							\
		bch2_trans_verify_not_restarted(_trans, _restart_count);\
									\
	_ret ?: trans_was_restarted(_trans, _restart_count);		\
})

#define for_each_btree_key2(_trans, _iter, _btree_id,			\
			    _start, _flags, _k, _do)			\
({									\
	int _ret = 0;							\
									\
	bch2_trans_iter_init((_trans), &(_iter), (_btree_id),		\
			     (_start), (_flags));			\
									\
	while (1) {							\
		u32 _restart_count = bch2_trans_begin(_trans);		\
									\
		_ret = 0;						\
		(_k) = bch2_btree_iter_peek_type(&(_iter), (_flags));	\
		if (!(_k).k)						\
			break;						\
									\
		_ret = bkey_err(_k) ?: (_do);				\
		if (bch2_err_matches(_ret, BCH_ERR_transaction_restart))\
			continue;					\
		if (_ret)						\
			break;						\
		bch2_trans_verify_not_restarted(_trans, _restart_count);\
		if (!bch2_btree_iter_advance(&(_iter)))			\
			break;						\
	}								\
									\
	bch2_trans_iter_exit((_trans), &(_iter));			\
	_ret;								\
})

#define for_each_btree_key2_upto(_trans, _iter, _btree_id,		\
			    _start, _end, _flags, _k, _do)		\
({									\
	int _ret = 0;							\
									\
	bch2_trans_iter_init((_trans), &(_iter), (_btree_id),		\
			     (_start), (_flags));			\
									\
	while (1) {							\
		u32 _restart_count = bch2_trans_begin(_trans);		\
									\
		_ret = 0;						\
		(_k) = bch2_btree_iter_peek_upto_type(&(_iter), _end, (_flags));\
		if (!(_k).k)						\
			break;						\
									\
		_ret = bkey_err(_k) ?: (_do);				\
		if (bch2_err_matches(_ret, BCH_ERR_transaction_restart))\
			continue;					\
		if (_ret)						\
			break;						\
		bch2_trans_verify_not_restarted(_trans, _restart_count);\
		if (!bch2_btree_iter_advance(&(_iter)))			\
			break;						\
	}								\
									\
	bch2_trans_iter_exit((_trans), &(_iter));			\
	_ret;								\
})

#define for_each_btree_key_reverse(_trans, _iter, _btree_id,		\
				   _start, _flags, _k, _do)		\
({									\
	int _ret = 0;							\
									\
	bch2_trans_iter_init((_trans), &(_iter), (_btree_id),		\
			     (_start), (_flags));			\
									\
	while (1) {							\
		u32 _restart_count = bch2_trans_begin(_trans);		\
		(_k) = bch2_btree_iter_peek_prev_type(&(_iter), (_flags));\
		if (!(_k).k) {						\
			_ret = 0;					\
			break;						\
		}							\
									\
		_ret = bkey_err(_k) ?: (_do);				\
		if (bch2_err_matches(_ret, BCH_ERR_transaction_restart))\
			continue;					\
		if (_ret)						\
			break;						\
		bch2_trans_verify_not_restarted(_trans, _restart_count);\
		if (!bch2_btree_iter_rewind(&(_iter)))			\
			break;						\
	}								\
									\
	bch2_trans_iter_exit((_trans), &(_iter));			\
	_ret;								\
})

#define for_each_btree_key_commit(_trans, _iter, _btree_id,		\
				  _start, _iter_flags, _k,		\
				  _disk_res, _journal_seq, _commit_flags,\
				  _do)					\
	for_each_btree_key2(_trans, _iter, _btree_id, _start, _iter_flags, _k,\
			    (_do) ?: bch2_trans_commit(_trans, (_disk_res),\
					(_journal_seq), (_commit_flags)))

#define for_each_btree_key_reverse_commit(_trans, _iter, _btree_id,	\
				  _start, _iter_flags, _k,		\
				  _disk_res, _journal_seq, _commit_flags,\
				  _do)					\
	for_each_btree_key_reverse(_trans, _iter, _btree_id, _start, _iter_flags, _k,\
			    (_do) ?: bch2_trans_commit(_trans, (_disk_res),\
					(_journal_seq), (_commit_flags)))

#define for_each_btree_key_upto_commit(_trans, _iter, _btree_id,	\
				  _start, _end, _iter_flags, _k,	\
				  _disk_res, _journal_seq, _commit_flags,\
				  _do)					\
	for_each_btree_key2_upto(_trans, _iter, _btree_id, _start, _end, _iter_flags, _k,\
			    (_do) ?: bch2_trans_commit(_trans, (_disk_res),\
					(_journal_seq), (_commit_flags)))

#define for_each_btree_key(_trans, _iter, _btree_id,			\
			   _start, _flags, _k, _ret)			\
	for (bch2_trans_iter_init((_trans), &(_iter), (_btree_id),	\
				  (_start), (_flags));			\
	     (_k) = __bch2_btree_iter_peek_and_restart((_trans), &(_iter), _flags),\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_upto(_trans, _iter, _btree_id,		\
				_start, _end, _flags, _k, _ret)		\
	for (bch2_trans_iter_init((_trans), &(_iter), (_btree_id),	\
				  (_start), (_flags));			\
	     (_k) = __bch2_btree_iter_peek_upto_and_restart((_trans),	\
						&(_iter), _end, _flags),\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_norestart(_trans, _iter, _btree_id,		\
			   _start, _flags, _k, _ret)			\
	for (bch2_trans_iter_init((_trans), &(_iter), (_btree_id),	\
				  (_start), (_flags));			\
	     (_k) = bch2_btree_iter_peek_type(&(_iter), _flags),	\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_upto_norestart(_trans, _iter, _btree_id,	\
			   _start, _end, _flags, _k, _ret)		\
	for (bch2_trans_iter_init((_trans), &(_iter), (_btree_id),	\
				  (_start), (_flags));			\
	     (_k) = bch2_btree_iter_peek_upto_type(&(_iter), _end, _flags),\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_continue(_trans, _iter, _flags, _k, _ret)	\
	for (;								\
	     (_k) = __bch2_btree_iter_peek_and_restart((_trans), &(_iter), _flags),\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_continue_norestart(_iter, _flags, _k, _ret)	\
	for (;								\
	     (_k) = bch2_btree_iter_peek_type(&(_iter), _flags),	\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     bch2_btree_iter_advance(&(_iter)))

#define for_each_btree_key_upto_continue_norestart(_iter, _end, _flags, _k, _ret)\
	for (;									\
	     (_k) = bch2_btree_iter_peek_upto_type(&(_iter), _end, _flags),	\
	     !((_ret) = bkey_err(_k)) && (_k).k;				\
	     bch2_btree_iter_advance(&(_iter)))

#define drop_locks_do(_trans, _do)					\
({									\
	bch2_trans_unlock(_trans);					\
	_do ?: bch2_trans_relock(_trans);				\
})

#define allocate_dropping_locks_errcode(_trans, _do)			\
({									\
	gfp_t _gfp = GFP_NOWAIT|__GFP_NOWARN;				\
	int _ret = _do;							\
									\
	if (bch2_err_matches(_ret, ENOMEM)) {				\
		_gfp = GFP_KERNEL;					\
		_ret = drop_locks_do(trans, _do);			\
	}								\
	_ret;								\
})

#define allocate_dropping_locks(_trans, _ret, _do)			\
({									\
	gfp_t _gfp = GFP_NOWAIT|__GFP_NOWARN;				\
	typeof(_do) _p = _do;						\
									\
	_ret = 0;							\
	if (unlikely(!_p)) {						\
		_gfp = GFP_KERNEL;					\
		_ret = drop_locks_do(trans, ((_p = _do), 0));		\
	}								\
	_p;								\
})

/* new multiple iterator interface: */

void bch2_trans_updates_to_text(struct printbuf *, struct btree_trans *);
void bch2_btree_path_to_text(struct printbuf *, struct btree_path *);
void bch2_trans_paths_to_text(struct printbuf *, struct btree_trans *);
void bch2_dump_trans_updates(struct btree_trans *);
void bch2_dump_trans_paths_updates(struct btree_trans *);
void __bch2_trans_init(struct btree_trans *, struct bch_fs *, unsigned);
void bch2_trans_exit(struct btree_trans *);

extern const char *bch2_btree_transaction_fns[BCH_TRANSACTIONS_NR];
unsigned bch2_trans_get_fn_idx(const char *);

#define bch2_trans_init(_trans, _c, _nr_iters, _mem)			\
do {									\
	static unsigned trans_fn_idx;					\
									\
	if (unlikely(!trans_fn_idx))					\
		trans_fn_idx = bch2_trans_get_fn_idx(__func__);		\
									\
	__bch2_trans_init(_trans, _c, trans_fn_idx);			\
} while (0)

void bch2_btree_trans_to_text(struct printbuf *, struct btree_trans *);

void bch2_fs_btree_iter_exit(struct bch_fs *);
int bch2_fs_btree_iter_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_ITER_H */
