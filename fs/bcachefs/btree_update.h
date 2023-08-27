/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_UPDATE_H
#define _BCACHEFS_BTREE_UPDATE_H

#include "btree_iter.h"
#include "journal.h"
#include "journal.h"

struct bch_fs;
struct btree;

void bch2_btree_node_prep_for_write(struct btree_trans *,
				    struct btree_path *, struct btree *);
bool bch2_btree_bset_insert_key(struct btree_trans *, struct btree_path *,
				struct btree *, struct btree_node_iter *,
				struct bkey_i *);

int bch2_btree_node_flush0(struct journal *, struct journal_entry_pin *, u64);
int bch2_btree_node_flush1(struct journal *, struct journal_entry_pin *, u64);
void bch2_btree_add_journal_pin(struct bch_fs *, struct btree *, u64);

void bch2_btree_insert_key_leaf(struct btree_trans *, struct btree_path *,
				struct bkey_i *, u64);

enum btree_insert_flags {
	/* First bits for bch_watermark: */
	__BTREE_INSERT_NOFAIL = BCH_WATERMARK_BITS,
	__BTREE_INSERT_NOCHECK_RW,
	__BTREE_INSERT_LAZY_RW,
	__BTREE_INSERT_JOURNAL_REPLAY,
	__BTREE_INSERT_JOURNAL_RECLAIM,
	__BTREE_INSERT_NOWAIT,
	__BTREE_INSERT_GC_LOCK_HELD,
	__BCH_HASH_SET_MUST_CREATE,
	__BCH_HASH_SET_MUST_REPLACE,
};

/* Don't check for -ENOSPC: */
#define BTREE_INSERT_NOFAIL		BIT(__BTREE_INSERT_NOFAIL)

#define BTREE_INSERT_NOCHECK_RW		BIT(__BTREE_INSERT_NOCHECK_RW)
#define BTREE_INSERT_LAZY_RW		BIT(__BTREE_INSERT_LAZY_RW)

/* Insert is for journal replay - don't get journal reservations: */
#define BTREE_INSERT_JOURNAL_REPLAY	BIT(__BTREE_INSERT_JOURNAL_REPLAY)

/* Insert is being called from journal reclaim path: */
#define BTREE_INSERT_JOURNAL_RECLAIM	BIT(__BTREE_INSERT_JOURNAL_RECLAIM)

/* Don't block on allocation failure (for new btree nodes: */
#define BTREE_INSERT_NOWAIT		BIT(__BTREE_INSERT_NOWAIT)
#define BTREE_INSERT_GC_LOCK_HELD	BIT(__BTREE_INSERT_GC_LOCK_HELD)

#define BCH_HASH_SET_MUST_CREATE	BIT(__BCH_HASH_SET_MUST_CREATE)
#define BCH_HASH_SET_MUST_REPLACE	BIT(__BCH_HASH_SET_MUST_REPLACE)

int bch2_btree_delete_extent_at(struct btree_trans *, struct btree_iter *,
				unsigned, unsigned);
int bch2_btree_delete_at(struct btree_trans *, struct btree_iter *, unsigned);
int bch2_btree_delete_at_buffered(struct btree_trans *, enum btree_id, struct bpos);
int bch2_btree_delete(struct btree_trans *, enum btree_id, struct bpos, unsigned);

int bch2_btree_insert_nonextent(struct btree_trans *, enum btree_id,
				struct bkey_i *, enum btree_update_flags);

int bch2_btree_insert_trans(struct btree_trans *, enum btree_id, struct bkey_i *,
			enum btree_update_flags);
int bch2_btree_insert(struct bch_fs *, enum btree_id, struct bkey_i *,
		     struct disk_reservation *, u64 *, int flags);

int bch2_btree_delete_range_trans(struct btree_trans *, enum btree_id,
				  struct bpos, struct bpos, unsigned, u64 *);
int bch2_btree_delete_range(struct bch_fs *, enum btree_id,
			    struct bpos, struct bpos, unsigned, u64 *);

int bch2_btree_bit_mod(struct btree_trans *, enum btree_id, struct bpos, bool);

int __bch2_insert_snapshot_whiteouts(struct btree_trans *, enum btree_id,
				     struct bpos, struct bpos);

/*
 * For use when splitting extents in existing snapshots:
 *
 * If @old_pos is an interior snapshot node, iterate over descendent snapshot
 * nodes: for every descendent snapshot in whiche @old_pos is overwritten and
 * not visible, emit a whiteout at @new_pos.
 */
static inline int bch2_insert_snapshot_whiteouts(struct btree_trans *trans,
						 enum btree_id btree,
						 struct bpos old_pos,
						 struct bpos new_pos)
{
	if (!btree_type_has_snapshots(btree) ||
	    bkey_eq(old_pos, new_pos))
		return 0;

	return __bch2_insert_snapshot_whiteouts(trans, btree, old_pos, new_pos);
}

int bch2_trans_update_extent_overwrite(struct btree_trans *, struct btree_iter *,
				       enum btree_update_flags,
				       struct bkey_s_c, struct bkey_s_c);

int bch2_bkey_get_empty_slot(struct btree_trans *, struct btree_iter *,
			     enum btree_id, struct bpos);

int __must_check bch2_trans_update(struct btree_trans *, struct btree_iter *,
				   struct bkey_i *, enum btree_update_flags);
int __must_check bch2_trans_update_seq(struct btree_trans *, u64, struct btree_iter *,
				       struct bkey_i *, enum btree_update_flags);
int __must_check bch2_trans_update_buffered(struct btree_trans *,
					    enum btree_id, struct bkey_i *);

void bch2_trans_commit_hook(struct btree_trans *,
			    struct btree_trans_commit_hook *);
int __bch2_trans_commit(struct btree_trans *, unsigned);

int bch2_fs_log_msg(struct bch_fs *, const char *, ...);
int bch2_journal_log_msg(struct bch_fs *, const char *, ...);

/**
 * bch2_trans_commit - insert keys at given iterator positions
 *
 * This is main entry point for btree updates.
 *
 * Return values:
 * -EROFS: filesystem read only
 * -EIO: journal or btree node IO error
 */
static inline int bch2_trans_commit(struct btree_trans *trans,
				    struct disk_reservation *disk_res,
				    u64 *journal_seq,
				    unsigned flags)
{
	trans->disk_res		= disk_res;
	trans->journal_seq	= journal_seq;

	return __bch2_trans_commit(trans, flags);
}

#define commit_do(_trans, _disk_res, _journal_seq, _flags, _do)	\
	lockrestart_do(_trans, _do ?: bch2_trans_commit(_trans, (_disk_res),\
					(_journal_seq), (_flags)))

#define nested_commit_do(_trans, _disk_res, _journal_seq, _flags, _do)	\
	nested_lockrestart_do(_trans, _do ?: bch2_trans_commit(_trans, (_disk_res),\
					(_journal_seq), (_flags)))

#define bch2_trans_do(_c, _disk_res, _journal_seq, _flags, _do)		\
({									\
	struct btree_trans trans;					\
	int _ret;							\
									\
	bch2_trans_init(&trans, (_c), 0, 0);				\
	_ret = commit_do(&trans, _disk_res, _journal_seq, _flags, _do);	\
	bch2_trans_exit(&trans);					\
									\
	_ret;								\
})

#define bch2_trans_run(_c, _do)						\
({									\
	struct btree_trans trans;					\
	int _ret;							\
									\
	bch2_trans_init(&trans, (_c), 0, 0);				\
	_ret = (_do);							\
	bch2_trans_exit(&trans);					\
									\
	_ret;								\
})

#define trans_for_each_update(_trans, _i)				\
	for ((_i) = (_trans)->updates;					\
	     (_i) < (_trans)->updates + (_trans)->nr_updates;		\
	     (_i)++)

#define trans_for_each_wb_update(_trans, _i)				\
	for ((_i) = (_trans)->wb_updates;				\
	     (_i) < (_trans)->wb_updates + (_trans)->nr_wb_updates;	\
	     (_i)++)

static inline void bch2_trans_reset_updates(struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update(trans, i)
		bch2_path_put(trans, i->path, true);

	trans->extra_journal_res	= 0;
	trans->nr_updates		= 0;
	trans->nr_wb_updates		= 0;
	trans->wb_updates		= NULL;
	trans->hooks			= NULL;
	trans->extra_journal_entries.nr	= 0;

	if (trans->fs_usage_deltas) {
		trans->fs_usage_deltas->used = 0;
		memset((void *) trans->fs_usage_deltas +
		       offsetof(struct replicas_delta_list, memset_start), 0,
		       (void *) &trans->fs_usage_deltas->memset_end -
		       (void *) &trans->fs_usage_deltas->memset_start);
	}
}

static inline struct bkey_i *__bch2_bkey_make_mut_noupdate(struct btree_trans *trans, struct bkey_s_c k,
						  unsigned type, unsigned min_bytes)
{
	unsigned bytes = max_t(unsigned, min_bytes, bkey_bytes(k.k));
	struct bkey_i *mut;

	if (type && k.k->type != type)
		return ERR_PTR(-ENOENT);

	mut = bch2_trans_kmalloc_nomemzero(trans, bytes);
	if (!IS_ERR(mut)) {
		bkey_reassemble(mut, k);

		if (unlikely(bytes > bkey_bytes(k.k))) {
			memset((void *) mut + bkey_bytes(k.k), 0,
			       bytes - bkey_bytes(k.k));
			mut->k.u64s = DIV_ROUND_UP(bytes, sizeof(u64));
		}
	}
	return mut;
}

static inline struct bkey_i *bch2_bkey_make_mut_noupdate(struct btree_trans *trans, struct bkey_s_c k)
{
	return __bch2_bkey_make_mut_noupdate(trans, k, 0, 0);
}

#define bch2_bkey_make_mut_noupdate_typed(_trans, _k, _type)		\
	bkey_i_to_##_type(__bch2_bkey_make_mut_noupdate(_trans, _k,	\
				KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_make_mut(struct btree_trans *trans, struct btree_iter *iter,
					struct bkey_s_c *k, unsigned flags,
					unsigned type, unsigned min_bytes)
{
	struct bkey_i *mut = __bch2_bkey_make_mut_noupdate(trans, *k, type, min_bytes);
	int ret;

	if (IS_ERR(mut))
		return mut;

	ret = bch2_trans_update(trans, iter, mut, flags);
	if (ret)
		return ERR_PTR(ret);

	*k = bkey_i_to_s_c(mut);
	return mut;
}

static inline struct bkey_i *bch2_bkey_make_mut(struct btree_trans *trans, struct btree_iter *iter,
						struct bkey_s_c *k, unsigned flags)
{
	return __bch2_bkey_make_mut(trans, iter, k, flags, 0, 0);
}

#define bch2_bkey_make_mut_typed(_trans, _iter, _k, _flags, _type)	\
	bkey_i_to_##_type(__bch2_bkey_make_mut(_trans, _iter, _k, _flags,\
				KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_get_mut_noupdate(struct btree_trans *trans,
					 struct btree_iter *iter,
					 unsigned btree_id, struct bpos pos,
					 unsigned flags, unsigned type, unsigned min_bytes)
{
	struct bkey_s_c k = __bch2_bkey_get_iter(trans, iter,
				btree_id, pos, flags|BTREE_ITER_INTENT, type);
	struct bkey_i *ret = IS_ERR(k.k)
		? ERR_CAST(k.k)
		: __bch2_bkey_make_mut_noupdate(trans, k, 0, min_bytes);
	if (IS_ERR(ret))
		bch2_trans_iter_exit(trans, iter);
	return ret;
}

static inline struct bkey_i *bch2_bkey_get_mut_noupdate(struct btree_trans *trans,
					       struct btree_iter *iter,
					       unsigned btree_id, struct bpos pos,
					       unsigned flags)
{
	return __bch2_bkey_get_mut_noupdate(trans, iter, btree_id, pos, flags, 0, 0);
}

static inline struct bkey_i *__bch2_bkey_get_mut(struct btree_trans *trans,
					 struct btree_iter *iter,
					 unsigned btree_id, struct bpos pos,
					 unsigned flags, unsigned type, unsigned min_bytes)
{
	struct bkey_i *mut = __bch2_bkey_get_mut_noupdate(trans, iter,
				btree_id, pos, flags|BTREE_ITER_INTENT, type, min_bytes);
	int ret;

	if (IS_ERR(mut))
		return mut;

	ret = bch2_trans_update(trans, iter, mut, flags);
	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return ERR_PTR(ret);
	}

	return mut;
}

static inline struct bkey_i *bch2_bkey_get_mut_minsize(struct btree_trans *trans,
						       struct btree_iter *iter,
						       unsigned btree_id, struct bpos pos,
						       unsigned flags, unsigned min_bytes)
{
	return __bch2_bkey_get_mut(trans, iter, btree_id, pos, flags, 0, min_bytes);
}

static inline struct bkey_i *bch2_bkey_get_mut(struct btree_trans *trans,
					       struct btree_iter *iter,
					       unsigned btree_id, struct bpos pos,
					       unsigned flags)
{
	return __bch2_bkey_get_mut(trans, iter, btree_id, pos, flags, 0, 0);
}

#define bch2_bkey_get_mut_typed(_trans, _iter, _btree_id, _pos, _flags, _type)\
	bkey_i_to_##_type(__bch2_bkey_get_mut(_trans, _iter,		\
			_btree_id, _pos, _flags,			\
			KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_alloc(struct btree_trans *trans, struct btree_iter *iter,
					       unsigned flags, unsigned type, unsigned val_size)
{
	struct bkey_i *k = bch2_trans_kmalloc(trans, sizeof(*k) + val_size);
	int ret;

	if (IS_ERR(k))
		return k;

	bkey_init(&k->k);
	k->k.p = iter->pos;
	k->k.type = type;
	set_bkey_val_bytes(&k->k, val_size);

	ret = bch2_trans_update(trans, iter, k, flags);
	if (unlikely(ret))
		return ERR_PTR(ret);
	return k;
}

#define bch2_bkey_alloc(_trans, _iter, _flags, _type)			\
	bkey_i_to_##_type(__bch2_bkey_alloc(_trans, _iter, _flags,	\
				KEY_TYPE_##_type, sizeof(struct bch_##_type)))

#endif /* _BCACHEFS_BTREE_UPDATE_H */
