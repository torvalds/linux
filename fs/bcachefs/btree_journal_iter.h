/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_JOURNAL_ITER_H
#define _BCACHEFS_BTREE_JOURNAL_ITER_H

#include "bkey.h"

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
	struct btree_trans	*trans;
	struct btree		*b;
	struct btree_node_iter	node_iter;
	struct bkey		unpacked;

	struct journal_iter	journal;
	struct bpos		pos;
	bool			at_end;
	bool			prefetch;
};

static inline int __journal_key_cmp(enum btree_id	l_btree_id,
				    unsigned		l_level,
				    struct bpos	l_pos,
				    const struct journal_key *r)
{
	return (cmp_int(l_btree_id,	r->btree_id) ?:
		cmp_int(l_level,	r->level) ?:
		bpos_cmp(l_pos,	r->k->k.p));
}

static inline int journal_key_cmp(const struct journal_key *l, const struct journal_key *r)
{
	return __journal_key_cmp(l->btree_id, l->level, l->k->k.p, r);
}

struct bkey_i *bch2_journal_keys_peek_upto(struct bch_fs *, enum btree_id,
				unsigned, struct bpos, struct bpos, size_t *);
struct bkey_i *bch2_journal_keys_peek_slot(struct bch_fs *, enum btree_id,
					   unsigned, struct bpos);

int bch2_btree_and_journal_iter_prefetch(struct btree_trans *, struct btree_path *,
					 struct btree_and_journal_iter *);

int bch2_journal_key_insert_take(struct bch_fs *, enum btree_id,
				 unsigned, struct bkey_i *);
int bch2_journal_key_insert(struct bch_fs *, enum btree_id,
			    unsigned, struct bkey_i *);
int bch2_journal_key_delete(struct bch_fs *, enum btree_id,
			    unsigned, struct bpos);
bool bch2_key_deleted_in_journal(struct btree_trans *, enum btree_id, unsigned, struct bpos);
void bch2_journal_key_overwritten(struct bch_fs *, enum btree_id, unsigned, struct bpos);

void bch2_btree_and_journal_iter_advance(struct btree_and_journal_iter *);
struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *);

void bch2_btree_and_journal_iter_exit(struct btree_and_journal_iter *);
void __bch2_btree_and_journal_iter_init_node_iter(struct btree_trans *,
				struct btree_and_journal_iter *, struct btree *,
				struct btree_node_iter, struct bpos);
void bch2_btree_and_journal_iter_init_node_iter(struct btree_trans *,
				struct btree_and_journal_iter *, struct btree *);

void bch2_journal_keys_put(struct bch_fs *);

static inline void bch2_journal_keys_put_initial(struct bch_fs *c)
{
	if (c->journal_keys.initial_ref_held)
		bch2_journal_keys_put(c);
	c->journal_keys.initial_ref_held = false;
}

void bch2_journal_entries_free(struct bch_fs *);

int bch2_journal_keys_sort(struct bch_fs *);

void bch2_shoot_down_journal_keys(struct bch_fs *, enum btree_id,
				  unsigned, unsigned,
				  struct bpos, struct bpos);

void bch2_journal_keys_dump(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_JOURNAL_ITER_H */
