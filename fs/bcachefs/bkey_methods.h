/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_METHODS_H
#define _BCACHEFS_BKEY_METHODS_H

#include "bkey.h"

struct bch_fs;
struct btree;
struct bkey;
enum btree_node_type;

extern const char * const bch_bkey_types[];

enum merge_result {
	BCH_MERGE_NOMERGE,

	/*
	 * The keys were mergeable, but would have overflowed size - so instead
	 * l was changed to the maximum size, and both keys were modified:
	 */
	BCH_MERGE_PARTIAL,
	BCH_MERGE_MERGE,
};

struct bkey_ops {
	/* Returns reason for being invalid if invalid, else NULL: */
	const char *	(*key_invalid)(const struct bch_fs *,
				       struct bkey_s_c);
	void		(*key_debugcheck)(struct bch_fs *, struct btree *,
					  struct bkey_s_c);
	void		(*val_to_text)(struct printbuf *, struct bch_fs *,
				       struct bkey_s_c);
	void		(*swab)(const struct bkey_format *, struct bkey_packed *);
	bool		(*key_normalize)(struct bch_fs *, struct bkey_s);
	enum merge_result (*key_merge)(struct bch_fs *,
				       struct bkey_s, struct bkey_s);
};

const char *bch2_bkey_val_invalid(struct bch_fs *, struct bkey_s_c);
const char *__bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c,
				enum btree_node_type);
const char *bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c,
			      enum btree_node_type);
const char *bch2_bkey_in_btree_node(struct btree *, struct bkey_s_c);

void bch2_bkey_debugcheck(struct bch_fs *, struct btree *, struct bkey_s_c);

void bch2_bpos_to_text(struct printbuf *, struct bpos);
void bch2_bkey_to_text(struct printbuf *, const struct bkey *);
void bch2_val_to_text(struct printbuf *, struct bch_fs *,
		      struct bkey_s_c);
void bch2_bkey_val_to_text(struct printbuf *, struct bch_fs *,
			   struct bkey_s_c);

void bch2_bkey_swab(const struct bkey_format *, struct bkey_packed *);

bool bch2_bkey_normalize(struct bch_fs *, struct bkey_s);

enum merge_result bch2_bkey_merge(struct bch_fs *,
				  struct bkey_s, struct bkey_s);

void bch2_bkey_renumber(enum btree_node_type, struct bkey_packed *, int);

#endif /* _BCACHEFS_BKEY_METHODS_H */
