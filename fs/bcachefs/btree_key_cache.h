/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_KEY_CACHE_H
#define _BCACHEFS_BTREE_KEY_CACHE_H

static inline size_t bch2_nr_btree_keys_need_flush(struct bch_fs *c)
{
	size_t nr_dirty = atomic_long_read(&c->btree_key_cache.nr_dirty);
	size_t nr_keys = atomic_long_read(&c->btree_key_cache.nr_keys);
	size_t max_dirty = 1024 + nr_keys  / 2;

	return max_t(ssize_t, 0, nr_dirty - max_dirty);
}

static inline bool bch2_btree_key_cache_must_wait(struct bch_fs *c)
{
	size_t nr_dirty = atomic_long_read(&c->btree_key_cache.nr_dirty);
	size_t nr_keys = atomic_long_read(&c->btree_key_cache.nr_keys);
	size_t max_dirty = 4096 + (nr_keys * 3) / 4;

	return nr_dirty > max_dirty;
}

int bch2_btree_key_cache_journal_flush(struct journal *,
				struct journal_entry_pin *, u64);

struct bkey_cached *
bch2_btree_key_cache_find(struct bch_fs *, enum btree_id, struct bpos);

int bch2_btree_path_traverse_cached(struct btree_trans *, struct btree_path *,
				    unsigned);

bool bch2_btree_insert_key_cached(struct btree_trans *, unsigned,
			struct btree_path *, struct bkey_i *);
int bch2_btree_key_cache_flush(struct btree_trans *,
			       enum btree_id, struct bpos);
void bch2_btree_key_cache_drop(struct btree_trans *,
			       struct btree_path *);

void bch2_fs_btree_key_cache_exit(struct btree_key_cache *);
void bch2_fs_btree_key_cache_init_early(struct btree_key_cache *);
int bch2_fs_btree_key_cache_init(struct btree_key_cache *);

void bch2_btree_key_cache_to_text(struct printbuf *, struct btree_key_cache *);

void bch2_btree_key_cache_exit(void);
int __init bch2_btree_key_cache_init(void);

#endif /* _BCACHEFS_BTREE_KEY_CACHE_H */
