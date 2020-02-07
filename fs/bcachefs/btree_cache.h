/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_CACHE_H
#define _BCACHEFS_BTREE_CACHE_H

#include "bcachefs.h"
#include "btree_types.h"

struct btree_iter;

extern const char * const bch2_btree_ids[];

void bch2_recalc_btree_reserve(struct bch_fs *);

void bch2_btree_node_hash_remove(struct btree_cache *, struct btree *);
int __bch2_btree_node_hash_insert(struct btree_cache *, struct btree *);
int bch2_btree_node_hash_insert(struct btree_cache *, struct btree *,
				unsigned, enum btree_id);

void bch2_btree_cache_cannibalize_unlock(struct bch_fs *);
int bch2_btree_cache_cannibalize_lock(struct bch_fs *, struct closure *);

struct btree *bch2_btree_node_mem_alloc(struct bch_fs *);

struct btree *bch2_btree_node_get(struct bch_fs *, struct btree_iter *,
				  const struct bkey_i *, unsigned,
				  enum six_lock_type);

struct btree *bch2_btree_node_get_sibling(struct bch_fs *, struct btree_iter *,
				struct btree *, enum btree_node_sibling);

void bch2_btree_node_prefetch(struct bch_fs *, struct btree_iter *,
			      const struct bkey_i *, unsigned);

void bch2_fs_btree_cache_exit(struct bch_fs *);
int bch2_fs_btree_cache_init(struct bch_fs *);
void bch2_fs_btree_cache_init_early(struct btree_cache *);

static inline u64 btree_ptr_hash_val(const struct bkey_i *k)
{
	switch (k->k.type) {
	case KEY_TYPE_btree_ptr:
		return *((u64 *) bkey_i_to_btree_ptr_c(k)->v.start);
	case KEY_TYPE_btree_ptr_v2:
		return bkey_i_to_btree_ptr_v2_c(k)->v.seq;
	default:
		return 0;
	}
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

static inline size_t btree_bytes(struct bch_fs *c)
{
	return c->opts.btree_node_size << 9;
}

static inline size_t btree_max_u64s(struct bch_fs *c)
{
	return (btree_bytes(c) - sizeof(struct btree_node)) / sizeof(u64);
}

static inline size_t btree_page_order(struct bch_fs *c)
{
	return get_order(btree_bytes(c));
}

static inline size_t btree_pages(struct bch_fs *c)
{
	return 1 << btree_page_order(c);
}

static inline unsigned btree_blocks(struct bch_fs *c)
{
	return c->opts.btree_node_size >> c->block_bits;
}

#define BTREE_SPLIT_THRESHOLD(c)		(btree_max_u64s(c) * 3 / 4)

#define BTREE_FOREGROUND_MERGE_THRESHOLD(c)	(btree_max_u64s(c) * 1 / 3)
#define BTREE_FOREGROUND_MERGE_HYSTERESIS(c)			\
	(BTREE_FOREGROUND_MERGE_THRESHOLD(c) +			\
	 (BTREE_FOREGROUND_MERGE_THRESHOLD(c) << 2))

#define btree_node_root(_c, _b)	((_c)->btree_roots[(_b)->c.btree_id].b)

void bch2_btree_node_to_text(struct printbuf *, struct bch_fs *,
			     struct btree *);

#endif /* _BCACHEFS_BTREE_CACHE_H */
