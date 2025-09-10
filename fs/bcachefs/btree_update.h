/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_UPDATE_H
#define _BCACHEFS_BTREE_UPDATE_H

#include "btree_iter.h"
#include "journal.h"
#include "snapshot.h"

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

#define BCH_TRANS_COMMIT_FLAGS()							\
	x(no_enospc,	"don't check for enospc")					\
	x(no_check_rw,	"don't attempt to take a ref on c->writes")			\
	x(no_journal_res, "don't take a journal reservation, instead "			\
			"pin journal entry referred to by trans->journal_res.seq")	\
	x(journal_reclaim, "operation required for journal reclaim; may return error"	\
			"instead of deadlocking if BCH_WATERMARK_reclaim not specified")\
	x(skip_accounting_apply, "we're in journal replay - accounting updates have already been applied")

enum __bch_trans_commit_flags {
	/* First bits for bch_watermark: */
	__BCH_TRANS_COMMIT_FLAGS_START = BCH_WATERMARK_BITS,
#define x(n, ...)	__BCH_TRANS_COMMIT_##n,
	BCH_TRANS_COMMIT_FLAGS()
#undef x
};

enum bch_trans_commit_flags {
#define x(n, ...)	BCH_TRANS_COMMIT_##n = BIT(__BCH_TRANS_COMMIT_##n),
	BCH_TRANS_COMMIT_FLAGS()
#undef x
};

void bch2_trans_commit_flags_to_text(struct printbuf *, enum bch_trans_commit_flags);

int bch2_btree_delete_at(struct btree_trans *, struct btree_iter *, unsigned);
int bch2_btree_delete(struct btree_trans *, enum btree_id, struct bpos, unsigned);

int bch2_btree_insert_nonextent(struct btree_trans *, enum btree_id,
				struct bkey_i *, enum btree_iter_update_trigger_flags);

int bch2_btree_insert_trans(struct btree_trans *, enum btree_id, struct bkey_i *,
			enum btree_iter_update_trigger_flags);
int bch2_btree_insert(struct bch_fs *, enum btree_id, struct bkey_i *, struct
		disk_reservation *, int flags, enum
		btree_iter_update_trigger_flags iter_flags);

int bch2_btree_delete_range_trans(struct btree_trans *, enum btree_id,
				  struct bpos, struct bpos, unsigned, u64 *);
int bch2_btree_delete_range(struct bch_fs *, enum btree_id,
			    struct bpos, struct bpos, unsigned, u64 *);

int bch2_btree_bit_mod_iter(struct btree_trans *, struct btree_iter *, bool);
int bch2_btree_bit_mod(struct btree_trans *, enum btree_id, struct bpos, bool);
int bch2_btree_bit_mod_buffered(struct btree_trans *, enum btree_id, struct bpos, bool);

static inline int bch2_btree_delete_at_buffered(struct btree_trans *trans,
						enum btree_id btree, struct bpos pos)
{
	return bch2_btree_bit_mod_buffered(trans, btree, pos, false);
}

int __bch2_insert_snapshot_whiteouts(struct btree_trans *, enum btree_id,
				     struct bpos, snapshot_id_list *);

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
	BUG_ON(old_pos.snapshot != new_pos.snapshot);

	if (!btree_type_has_snapshots(btree) ||
	    bkey_eq(old_pos, new_pos))
		return 0;

	snapshot_id_list s;
	int ret = bch2_get_snapshot_overwrites(trans, btree, old_pos, &s);
	if (ret)
		return ret;

	return s.nr
		? __bch2_insert_snapshot_whiteouts(trans, btree, new_pos, &s)
		: 0;
}

int bch2_trans_update_extent_overwrite(struct btree_trans *, struct btree_iter *,
				       enum btree_iter_update_trigger_flags,
				       struct bkey_s_c, struct bkey_s_c);

int bch2_bkey_get_empty_slot(struct btree_trans *, struct btree_iter *,
			     enum btree_id, struct bpos);

int __must_check bch2_trans_update_ip(struct btree_trans *, struct btree_iter *,
				      struct bkey_i *, enum btree_iter_update_trigger_flags,
				      unsigned long);

static inline int __must_check
bch2_trans_update(struct btree_trans *trans, struct btree_iter *iter,
		  struct bkey_i *k, enum btree_iter_update_trigger_flags flags)
{
	return bch2_trans_update_ip(trans, iter, k, flags, _THIS_IP_);
}

static inline void *btree_trans_subbuf_base(struct btree_trans *trans,
					    struct btree_trans_subbuf *buf)
{
	return (u64 *) trans->mem + buf->base;
}

static inline void *btree_trans_subbuf_top(struct btree_trans *trans,
					   struct btree_trans_subbuf *buf)
{
	return (u64 *) trans->mem + buf->base + buf->u64s;
}

void *__bch2_trans_subbuf_alloc(struct btree_trans *,
				struct btree_trans_subbuf *,
				unsigned);

static inline void *
bch2_trans_subbuf_alloc(struct btree_trans *trans,
			struct btree_trans_subbuf *buf,
			unsigned u64s)
{
	if (buf->u64s + u64s > buf->size)
		return __bch2_trans_subbuf_alloc(trans, buf, u64s);

	void *p = btree_trans_subbuf_top(trans, buf);
	buf->u64s += u64s;
	return p;
}

static inline struct jset_entry *btree_trans_journal_entries_start(struct btree_trans *trans)
{
	return btree_trans_subbuf_base(trans, &trans->journal_entries);
}

static inline struct jset_entry *btree_trans_journal_entries_top(struct btree_trans *trans)
{
	return btree_trans_subbuf_top(trans, &trans->journal_entries);
}

static inline struct jset_entry *
bch2_trans_jset_entry_alloc(struct btree_trans *trans, unsigned u64s)
{
	return bch2_trans_subbuf_alloc(trans, &trans->journal_entries, u64s);
}

int bch2_btree_insert_clone_trans(struct btree_trans *, enum btree_id, struct bkey_i *);

int bch2_btree_write_buffer_insert_err(struct bch_fs *, enum btree_id, struct bkey_i *);

static inline int __must_check bch2_trans_update_buffered(struct btree_trans *trans,
					    enum btree_id btree,
					    struct bkey_i *k)
{
	kmsan_check_memory(k, bkey_bytes(&k->k));

	EBUG_ON(k->k.u64s > BTREE_WRITE_BUFERED_U64s_MAX);

	if (unlikely(!btree_type_uses_write_buffer(btree))) {
		int ret = bch2_btree_write_buffer_insert_err(trans->c, btree, k);
		dump_stack();
		return ret;
	}
	/*
	 * Most updates skip the btree write buffer until journal replay is
	 * finished because synchronization with journal replay relies on having
	 * a btree node locked - if we're overwriting a key in the journal that
	 * journal replay hasn't yet replayed, we have to mark it as
	 * overwritten.
	 *
	 * But accounting updates don't overwrite, they're deltas, and they have
	 * to be flushed to the btree strictly in order for journal replay to be
	 * able to tell which updates need to be applied:
	 */
	if (k->k.type != KEY_TYPE_accounting &&
	    unlikely(trans->journal_replay_not_finished))
		return bch2_btree_insert_clone_trans(trans, btree, k);

	struct jset_entry *e = bch2_trans_jset_entry_alloc(trans, jset_u64s(k->k.u64s));
	int ret = PTR_ERR_OR_ZERO(e);
	if (ret)
		return ret;

	journal_entry_init(e, BCH_JSET_ENTRY_write_buffer_keys, btree, 0, k->k.u64s);
	bkey_copy(e->start, k);
	return 0;
}

void bch2_trans_commit_hook(struct btree_trans *,
			    struct btree_trans_commit_hook *);
int __bch2_trans_commit(struct btree_trans *, unsigned);

int bch2_trans_log_str(struct btree_trans *, const char *);
int bch2_trans_log_msg(struct btree_trans *, struct printbuf *);
int bch2_trans_log_bkey(struct btree_trans *, enum btree_id, unsigned, struct bkey_i *);

__printf(2, 3) int bch2_fs_log_msg(struct bch_fs *, const char *, ...);
__printf(2, 3) int bch2_journal_log_msg(struct bch_fs *, const char *, ...);

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

#define bch2_trans_commit_do(_c, _disk_res, _journal_seq, _flags, _do)		\
	bch2_trans_run(_c, commit_do(trans, _disk_res, _journal_seq, _flags, _do))

#define trans_for_each_update(_trans, _i)				\
	for (struct btree_insert_entry *_i = (_trans)->updates;		\
	     (_i) < (_trans)->updates + (_trans)->nr_updates;		\
	     (_i)++)

static inline void bch2_trans_reset_updates(struct btree_trans *trans)
{
	trans_for_each_update(trans, i)
		bch2_path_put(trans, i->path, true);

	trans->nr_updates		= 0;
	trans->journal_entries.u64s	= 0;
	trans->journal_entries.size	= 0;
	trans->accounting.u64s		= 0;
	trans->accounting.size		= 0;
	trans->hooks			= NULL;
	trans->extra_disk_res		= 0;
}

static __always_inline struct bkey_i *__bch2_bkey_make_mut_noupdate(struct btree_trans *trans, struct bkey_s_c k,
						  unsigned type, unsigned min_bytes)
{
	unsigned bytes = max_t(unsigned, min_bytes, bkey_bytes(k.k));
	struct bkey_i *mut;

	if (type && k.k->type != type)
		return ERR_PTR(-ENOENT);

	/* extra padding for varint_decode_fast... */
	mut = bch2_trans_kmalloc_nomemzero(trans, bytes + 8);
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

static __always_inline struct bkey_i *bch2_bkey_make_mut_noupdate(struct btree_trans *trans, struct bkey_s_c k)
{
	return __bch2_bkey_make_mut_noupdate(trans, k, 0, 0);
}

#define bch2_bkey_make_mut_noupdate_typed(_trans, _k, _type)		\
	bkey_i_to_##_type(__bch2_bkey_make_mut_noupdate(_trans, _k,	\
				KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_make_mut(struct btree_trans *trans, struct btree_iter *iter,
					struct bkey_s_c *k,
					enum btree_iter_update_trigger_flags flags,
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

static inline struct bkey_i *bch2_bkey_make_mut(struct btree_trans *trans,
						struct btree_iter *iter, struct bkey_s_c *k,
						enum btree_iter_update_trigger_flags flags)
{
	return __bch2_bkey_make_mut(trans, iter, k, flags, 0, 0);
}

#define bch2_bkey_make_mut_typed(_trans, _iter, _k, _flags, _type)	\
	bkey_i_to_##_type(__bch2_bkey_make_mut(_trans, _iter, _k, _flags,\
				KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_get_mut_noupdate(struct btree_trans *trans,
					 struct btree_iter *iter,
					 unsigned btree_id, struct bpos pos,
					 enum btree_iter_update_trigger_flags flags,
					 unsigned type, unsigned min_bytes)
{
	struct bkey_s_c k = __bch2_bkey_get_iter(trans, iter,
				btree_id, pos, flags|BTREE_ITER_intent, type);
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
					       enum btree_iter_update_trigger_flags flags)
{
	return __bch2_bkey_get_mut_noupdate(trans, iter, btree_id, pos, flags, 0, 0);
}

static inline struct bkey_i *__bch2_bkey_get_mut(struct btree_trans *trans,
					 struct btree_iter *iter,
					 unsigned btree_id, struct bpos pos,
					 enum btree_iter_update_trigger_flags flags,
					 unsigned type, unsigned min_bytes)
{
	struct bkey_i *mut = __bch2_bkey_get_mut_noupdate(trans, iter,
				btree_id, pos, flags|BTREE_ITER_intent, type, min_bytes);
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
						       enum btree_iter_update_trigger_flags flags,
						       unsigned min_bytes)
{
	return __bch2_bkey_get_mut(trans, iter, btree_id, pos, flags, 0, min_bytes);
}

static inline struct bkey_i *bch2_bkey_get_mut(struct btree_trans *trans,
					       struct btree_iter *iter,
					       unsigned btree_id, struct bpos pos,
					       enum btree_iter_update_trigger_flags flags)
{
	return __bch2_bkey_get_mut(trans, iter, btree_id, pos, flags, 0, 0);
}

#define bch2_bkey_get_mut_typed(_trans, _iter, _btree_id, _pos, _flags, _type)\
	bkey_i_to_##_type(__bch2_bkey_get_mut(_trans, _iter,		\
			_btree_id, _pos, _flags,			\
			KEY_TYPE_##_type, sizeof(struct bkey_i_##_type)))

static inline struct bkey_i *__bch2_bkey_alloc(struct btree_trans *trans, struct btree_iter *iter,
					       enum btree_iter_update_trigger_flags flags,
					       unsigned type, unsigned val_size)
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
