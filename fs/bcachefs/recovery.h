/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_H
#define _BCACHEFS_RECOVERY_H

#define for_each_journal_key(keys, i)				\
	for (i = (keys).d; i < (keys).d + (keys).nr; (i)++)

struct journal_iter {
	enum btree_id		btree_id;
	unsigned		level;
	struct journal_keys	*keys;
	struct journal_key	*k;
};

/*
 * Iterate over keys in the btree, with keys from the journal overlaid on top:
 */

struct btree_and_journal_iter {
	struct btree_iter	*btree;

	struct btree		*b;
	struct btree_node_iter	node_iter;
	struct bkey		unpacked;

	struct journal_iter	journal;

	enum last_key_returned {
		none,
		btree,
		journal,
	}			last;
};

void bch2_btree_and_journal_iter_advance(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_next(struct btree_and_journal_iter *);

void bch2_btree_and_journal_iter_init(struct btree_and_journal_iter *,
				      struct btree_trans *,
				      struct journal_keys *,
				      enum btree_id, struct bpos);
void bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *,
						struct journal_keys *,
						struct btree *);

typedef int (*btree_walk_node_fn)(struct bch_fs *c, struct btree *b);
typedef int (*btree_walk_key_fn)(struct bch_fs *c, enum btree_id id,
				 unsigned level, struct bkey_s_c k);

int bch2_btree_and_journal_walk(struct bch_fs *, struct journal_keys *, enum btree_id,
				btree_walk_node_fn, btree_walk_key_fn);

void bch2_journal_keys_free(struct journal_keys *);
void bch2_journal_entries_free(struct list_head *);

int bch2_fs_recovery(struct bch_fs *);
int bch2_fs_initialize(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_H */
