/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_SORT_H
#define _BCACHEFS_BKEY_SORT_H

struct sort_iter {
	struct btree		*b;
	unsigned		used;
	unsigned		size;

	struct sort_iter_set {
		struct bkey_packed *k, *end;
	} data[];
};

static inline void sort_iter_init(struct sort_iter *iter, struct btree *b, unsigned size)
{
	iter->b = b;
	iter->used = 0;
	iter->size = size;
}

struct sort_iter_stack {
	struct sort_iter	iter;
	struct sort_iter_set	sets[MAX_BSETS + 1];
};

static inline void sort_iter_stack_init(struct sort_iter_stack *iter, struct btree *b)
{
	sort_iter_init(&iter->iter, b, ARRAY_SIZE(iter->sets));
}

static inline void sort_iter_add(struct sort_iter *iter,
				 struct bkey_packed *k,
				 struct bkey_packed *end)
{
	BUG_ON(iter->used >= iter->size);

	if (k != end)
		iter->data[iter->used++] = (struct sort_iter_set) { k, end };
}

struct btree_nr_keys
bch2_key_sort_fix_overlapping(struct bch_fs *, struct bset *,
			      struct sort_iter *);

struct btree_nr_keys
bch2_sort_repack(struct bset *, struct btree *,
		 struct btree_node_iter *,
		 struct bkey_format *, bool);

unsigned bch2_sort_keys_keep_unwritten_whiteouts(struct bkey_packed *, struct sort_iter *);
unsigned bch2_sort_keys(struct bkey_packed *, struct sort_iter *);

#endif /* _BCACHEFS_BKEY_SORT_H */
