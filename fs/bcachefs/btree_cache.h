/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_CACHE_H
#define _BCACHEFS_BTREE_CACHE_H

#include "bcachefs.h"
#include "btree_types.h"
#include "bkey_methods.h"

extern const char * const bch2_btree_node_flags[];

struct btree_iter;

void bch2_recalc_btree_reserve(struct bch_fs *);

void bch2_btree_node_to_freelist(struct bch_fs *, struct btree *);

void __bch2_btree_node_hash_remove(struct btree_cache *, struct btree *);
void bch2_btree_node_hash_remove(struct btree_cache *, struct btree *);

int __bch2_btree_node_hash_insert(struct btree_cache *, struct btree *);
int bch2_btree_node_hash_insert(struct btree_cache *, struct btree *,
				unsigned, enum btree_id);

void bch2_node_pin(struct bch_fs *, struct btree *);
void bch2_btree_cache_unpin(struct bch_fs *);

void bch2_btree_node_update_key_early(struct btree_trans *, enum btree_id, unsigned,
				      struct bkey_s_c, struct bkey_i *);

void bch2_btree_cache_cannibalize_unlock(struct btree_trans *);
int bch2_btree_cache_cannibalize_lock(struct btree_trans *, struct closure *);

struct btree *__bch2_btree_node_mem_alloc(struct bch_fs *);
struct btree *bch2_btree_node_mem_alloc(struct btree_trans *, bool);

struct btree *bch2_btree_node_get(struct btree_trans *, struct btree_path *,
				  const struct bkey_i *, unsigned,
				  enum six_lock_type, unsigned long);

struct btree *bch2_btree_node_get_noiter(struct btree_trans *, const struct bkey_i *,
					 enum btree_id, unsigned, bool);

int bch2_btree_node_prefetch(struct btree_trans *, struct btree_path *,
			     const struct bkey_i *, enum btree_id, unsigned);

void bch2_btree_node_evict(struct btree_trans *, const struct bkey_i *);

void bch2_fs_btree_cache_exit(struct bch_fs *);
int bch2_fs_btree_cache_init(struct bch_fs *);
void bch2_fs_btree_cache_init_early(struct btree_cache *);

static inline u64 btree_ptr_hash_val(const struct bkey_i *k)
{
	switch (k->k.type) {
	case KEY_TYPE_btree_ptr:
		return *((u64 *) bkey_i_to_btree_ptr_c(k)->v.start);
	case KEY_TYPE_btree_ptr_v2:
		/*
		 * The cast/deref is only necessary to avoid sparse endianness
		 * warnings:
		 */
		return *((u64 *) &bkey_i_to_btree_ptr_v2_c(k)->v.seq);
	default:
		return 0;
	}
}

static inline struct btree *btree_node_mem_ptr(const struct bkey_i *k)
{
	return k->k.type == KEY_TYPE_btree_ptr_v2
		? (void *)(unsigned long)bkey_i_to_btree_ptr_v2_c(k)->v.mem_ptr
		: NULL;
}

/* is btree node in hash table? */
static inline bool btree_node_hashed(struct btree *b)
{
	return b->hash_val != 0;
}

#define for_each_cached_btree(_b, _c, _tbl, _iter, _pos)		\
	for ((_tbl) = rht_dereference_rcu((_c)->btree_cache.table.tbl,	\
					  &(_c)->btree_cache.table),	\
	     _iter = 0;	_iter < (_tbl)->size; _iter++)			\
		rht_for_each_entry_rcu((_b), (_pos), _tbl, _iter, hash)

static inline size_t btree_buf_bytes(const struct btree *b)
{
	return 1UL << b->byte_order;
}

static inline size_t btree_buf_max_u64s(const struct btree *b)
{
	return (btree_buf_bytes(b) - sizeof(struct btree_node)) / sizeof(u64);
}

static inline size_t btree_max_u64s(const struct bch_fs *c)
{
	return (c->opts.btree_node_size - sizeof(struct btree_node)) / sizeof(u64);
}

static inline size_t btree_sectors(const struct bch_fs *c)
{
	return c->opts.btree_node_size >> SECTOR_SHIFT;
}

static inline unsigned btree_blocks(const struct bch_fs *c)
{
	return btree_sectors(c) >> c->block_bits;
}

#define BTREE_SPLIT_THRESHOLD(c)		(btree_max_u64s(c) * 2 / 3)

#define BTREE_FOREGROUND_MERGE_THRESHOLD(c)	(btree_max_u64s(c) * 1 / 3)
#define BTREE_FOREGROUND_MERGE_HYSTERESIS(c)			\
	(BTREE_FOREGROUND_MERGE_THRESHOLD(c) +			\
	 (BTREE_FOREGROUND_MERGE_THRESHOLD(c) >> 2))

static inline unsigned btree_id_nr_alive(struct bch_fs *c)
{
	return BTREE_ID_NR + c->btree_roots_extra.nr;
}

static inline struct btree_root *bch2_btree_id_root(struct bch_fs *c, unsigned id)
{
	if (likely(id < BTREE_ID_NR)) {
		return &c->btree_roots_known[id];
	} else {
		unsigned idx = id - BTREE_ID_NR;

		/* This can happen when we're called from btree_node_scan */
		if (idx >= c->btree_roots_extra.nr)
			return NULL;

		return &c->btree_roots_extra.data[idx];
	}
}

static inline struct btree *btree_node_root(struct bch_fs *c, struct btree *b)
{
	struct btree_root *r = bch2_btree_id_root(c, b->c.btree_id);

	return r ? r->b : NULL;
}

const char *bch2_btree_id_str(enum btree_id);	/* avoid */
void bch2_btree_id_to_text(struct printbuf *, enum btree_id);
void bch2_btree_id_level_to_text(struct printbuf *, enum btree_id, unsigned);

void __bch2_btree_pos_to_text(struct printbuf *, struct bch_fs *,
			      enum btree_id, unsigned, struct bkey_s_c);
void bch2_btree_pos_to_text(struct printbuf *, struct bch_fs *, const struct btree *);
void bch2_btree_node_to_text(struct printbuf *, struct bch_fs *, const struct btree *);
void bch2_btree_cache_to_text(struct printbuf *, const struct btree_cache *);

#endif /* _BCACHEFS_BTREE_CACHE_H */
