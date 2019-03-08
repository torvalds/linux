#ifndef _BCACHEFS_BTREE_KEY_CACHE_H
#define _BCACHEFS_BTREE_KEY_CACHE_H

int bch2_btree_iter_traverse_cached(struct btree_iter *);

bool bch2_btree_insert_key_cached(struct btree_trans *,
			struct btree_iter *, struct bkey_i *);
int bch2_btree_key_cache_flush(struct btree_trans *,
			       enum btree_id, struct bpos);
#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_key_cache_verify_clean(struct btree_trans *,
				enum btree_id, struct bpos);
#else
static inline void
bch2_btree_key_cache_verify_clean(struct btree_trans *trans,
				enum btree_id id, struct bpos pos) {}
#endif

void bch2_fs_btree_key_cache_exit(struct btree_key_cache *);
void bch2_fs_btree_key_cache_init_early(struct btree_key_cache *);
int bch2_fs_btree_key_cache_init(struct btree_key_cache *);

#endif /* _BCACHEFS_BTREE_KEY_CACHE_H */
