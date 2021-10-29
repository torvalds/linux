/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_H
#define _BCACHEFS_RECOVERY_H

#define for_each_journal_key(keys, i)				\
	for (i = (keys).d; i < (keys).d + (keys).nr; (i)++)

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

	enum last_key_returned {
		none,
		btree,
		journal,
	}			last;
};

int bch2_journal_key_insert(struct bch_fs *, enum btree_id,
			    unsigned, struct bkey_i *);
int bch2_journal_key_delete(struct bch_fs *, enum btree_id,
			    unsigned, struct bpos);

void bch2_btree_and_journal_iter_advance(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_next(struct btree_and_journal_iter *);

void bch2_btree_and_journal_iter_exit(struct btree_and_journal_iter *);
void bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *,
						struct bch_fs *,
						struct btree *);

typedef int (*btree_walk_key_fn)(struct btree_trans *, struct bkey_s_c);

int bch2_btree_and_journal_walk(struct btree_trans *, enum btree_id, btree_walk_key_fn);

void bch2_journal_keys_free(struct journal_keys *);
void bch2_journal_entries_free(struct list_head *);

int bch2_fs_recovery(struct bch_fs *);
int bch2_fs_initialize(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_H */
