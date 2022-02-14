/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_METHODS_H
#define _BCACHEFS_BKEY_METHODS_H

#include "bkey.h"

struct bch_fs;
struct btree;
struct bkey;
enum btree_node_type;

extern const char * const bch2_bkey_types[];

struct bkey_ops {
	/* Returns reason for being invalid if invalid, else NULL: */
	const char *	(*key_invalid)(const struct bch_fs *,
				       struct bkey_s_c);
	void		(*val_to_text)(struct printbuf *, struct bch_fs *,
				       struct bkey_s_c);
	void		(*swab)(struct bkey_s);
	bool		(*key_normalize)(struct bch_fs *, struct bkey_s);
	bool		(*key_merge)(struct bch_fs *, struct bkey_s, struct bkey_s_c);
	void		(*compat)(enum btree_id id, unsigned version,
				  unsigned big_endian, int write,
				  struct bkey_s);
};

extern const struct bkey_ops bch2_bkey_ops[];

const char *bch2_bkey_val_invalid(struct bch_fs *, struct bkey_s_c);
const char *__bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c,
				enum btree_node_type);
const char *bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c,
			      enum btree_node_type);
const char *bch2_bkey_in_btree_node(struct btree *, struct bkey_s_c);

void bch2_bpos_to_text(struct printbuf *, struct bpos);
void bch2_bkey_to_text(struct printbuf *, const struct bkey *);
void bch2_val_to_text(struct printbuf *, struct bch_fs *,
		      struct bkey_s_c);
void bch2_bkey_val_to_text(struct printbuf *, struct bch_fs *,
			   struct bkey_s_c);

void bch2_bkey_swab_val(struct bkey_s);

bool bch2_bkey_normalize(struct bch_fs *, struct bkey_s);

static inline bool bch2_bkey_maybe_mergable(const struct bkey *l, const struct bkey *r)
{
	return l->type == r->type &&
		!bversion_cmp(l->version, r->version) &&
		!bpos_cmp(l->p, bkey_start_pos(r));
}

bool bch2_bkey_merge(struct bch_fs *, struct bkey_s, struct bkey_s_c);

void bch2_bkey_renumber(enum btree_node_type, struct bkey_packed *, int);

void __bch2_bkey_compat(unsigned, enum btree_id, unsigned, unsigned,
			int, struct bkey_format *, struct bkey_packed *);

static inline void bch2_bkey_compat(unsigned level, enum btree_id btree_id,
			       unsigned version, unsigned big_endian,
			       int write,
			       struct bkey_format *f,
			       struct bkey_packed *k)
{
	if (version < bcachefs_metadata_version_current ||
	    big_endian != CPU_BIG_ENDIAN)
		__bch2_bkey_compat(level, btree_id, version,
				   big_endian, write, f, k);

}

#endif /* _BCACHEFS_BKEY_METHODS_H */
