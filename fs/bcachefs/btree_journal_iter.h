/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_JOURNAL_ITER_H
#define _BCACHEFS_BTREE_JOURNAL_ITER_H

struct journal_iter {
	struct list_head	list;
	enum btree_id		btree_id;
	unsigned		level;
	size_t			idx;
	struct journal_keys	*keys;
};

/*
 * Iterate over keys in the btree, with keys from the journal overlaid on top:
 */

struct btree_and_journal_iter {
	struct btree		*b;
	struct btree_node_iter	node_iter;
	struct bkey		unpacked;

	struct journal_iter	journal;
	struct bpos		pos;
	bool			at_end;
};

struct bkey_i *bch2_journal_keys_peek_upto(struct bch_fs *, enum btree_id,
				unsigned, struct bpos, struct bpos, size_t *);
struct bkey_i *bch2_journal_keys_peek_slot(struct bch_fs *, enum btree_id,
					   unsigned, struct bpos);

int bch2_journal_key_insert_take(struct bch_fs *, enum btree_id,
				 unsigned, struct bkey_i *);
int bch2_journal_key_insert(struct bch_fs *, enum btree_id,
			    unsigned, struct bkey_i *);
int bch2_journal_key_delete(struct bch_fs *, enum btree_id,
			    unsigned, struct bpos);
void bch2_journal_key_overwritten(struct bch_fs *, enum btree_id,
				  unsigned, struct bpos);

void bch2_btree_and_journal_iter_advance(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *);

void bch2_btree_and_journal_iter_exit(struct btree_and_journal_iter *);
void __bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *,
				struct bch_fs *, struct btree *,
				struct btree_node_iter, struct bpos);
void bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *,
						struct bch_fs *,
						struct btree *);

void bch2_journal_keys_free(struct journal_keys *);
void bch2_journal_entries_free(struct bch_fs *);

int bch2_journal_keys_sort(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_JOURNAL_ITER_H */
