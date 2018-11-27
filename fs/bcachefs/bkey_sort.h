/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_SORT_H
#define _BCACHEFS_BKEY_SORT_H

struct btree_node_iter_large {
	u16		used;

	struct btree_node_iter_set data[MAX_BSETS];
};

void bch2_btree_node_iter_large_push(struct btree_node_iter_large *,
				     struct btree *,
				     const struct bkey_packed *,
				     const struct bkey_packed *);

struct sort_iter {
	struct btree	*b;
	unsigned		used;

	struct sort_iter_set {
		struct bkey_packed *k, *end;
	} data[MAX_BSETS + 1];
};

static inline void sort_iter_init(struct sort_iter *iter, struct btree *b)
{
	memset(iter, 0, sizeof(*iter));
	iter->b = b;
}

static inline void sort_iter_add(struct sort_iter *iter,
				 struct bkey_packed *k,
				 struct bkey_packed *end)
{
	BUG_ON(iter->used >= ARRAY_SIZE(iter->data));

	if (k != end)
		iter->data[iter->used++] = (struct sort_iter_set) { k, end };
}

struct btree_nr_keys
bch2_key_sort_fix_overlapping(struct bset *, struct btree *,
			      struct btree_node_iter_large *);
struct btree_nr_keys
bch2_extent_sort_fix_overlapping(struct bch_fs *, struct bset *,
				 struct btree *,
				 struct btree_node_iter_large *);

struct btree_nr_keys
bch2_sort_repack_merge(struct bch_fs *,
		       struct bset *, struct btree *,
		       struct btree_node_iter *,
		       struct bkey_format *,
		       bool,
		       key_filter_fn,
		       key_merge_fn);

unsigned bch2_sort_keys(struct bkey_packed *,
			struct sort_iter *, bool);
unsigned bch2_sort_extents(struct bkey_packed *,
			   struct sort_iter *, bool);

unsigned bch2_sort_key_whiteouts(struct bkey_packed *,
				 struct sort_iter *);
unsigned bch2_sort_extent_whiteouts(struct bkey_packed *,
				    struct sort_iter *);

#endif /* _BCACHEFS_BKEY_SORT_H */
