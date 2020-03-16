/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_RECOVERY_H
#define _BCACHEFS_RECOVERY_H

struct journal_keys {
	struct journal_key {
		enum btree_id	btree_id:8;
		unsigned	level:8;
		struct bkey_i	*k;
		u32		journal_seq;
		u32		journal_offset;
	}			*d;
	size_t			nr;
	u64			journal_seq_base;
};

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

int bch2_fs_recovery(struct bch_fs *);
int bch2_fs_initialize(struct bch_fs *);

#endif /* _BCACHEFS_RECOVERY_H */
