/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BKEY_METHODS_H
#define _BCACHEFS_BKEY_METHODS_H

#include "bkey.h"

struct bch_fs;
struct btree;
struct btree_trans;
struct bkey;
enum btree_node_type;

extern const char * const bch2_bkey_types[];
extern const struct bkey_ops bch2_bkey_null_ops;

/*
 * key_validate: checks validity of @k, returns 0 if good or -EINVAL if bad. If
 * invalid, entire key will be deleted.
 *
 * When invalid, error string is returned via @err. @rw indicates whether key is
 * being read or written; more aggressive checks can be enabled when rw == WRITE.
 */
struct bkey_ops {
	int		(*key_validate)(struct bch_fs *c, struct bkey_s_c k,
					enum bch_validate_flags flags);
	void		(*val_to_text)(struct printbuf *, struct bch_fs *,
				       struct bkey_s_c);
	void		(*swab)(struct bkey_s);
	bool		(*key_normalize)(struct bch_fs *, struct bkey_s);
	bool		(*key_merge)(struct bch_fs *, struct bkey_s, struct bkey_s_c);
	int		(*trigger)(struct btree_trans *, enum btree_id, unsigned,
				   struct bkey_s_c, struct bkey_s,
				   enum btree_iter_update_trigger_flags);
	void		(*compat)(enum btree_id id, unsigned version,
				  unsigned big_endian, int write,
				  struct bkey_s);

	/* Size of value type when first created: */
	unsigned	min_val_size;
};

extern const struct bkey_ops bch2_bkey_ops[];

static inline const struct bkey_ops *bch2_bkey_type_ops(enum bch_bkey_type type)
{
	return likely(type < KEY_TYPE_MAX)
		? &bch2_bkey_ops[type]
		: &bch2_bkey_null_ops;
}

int bch2_bkey_val_validate(struct bch_fs *, struct bkey_s_c, enum bch_validate_flags);
int __bch2_bkey_validate(struct bch_fs *, struct bkey_s_c, enum btree_node_type,
			 enum bch_validate_flags);
int bch2_bkey_validate(struct bch_fs *, struct bkey_s_c, enum btree_node_type,
		       enum bch_validate_flags);
int bch2_bkey_in_btree_node(struct bch_fs *, struct btree *, struct bkey_s_c,
			    enum bch_validate_flags);

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
		!bversion_cmp(l->bversion, r->bversion) &&
		bpos_eq(l->p, bkey_start_pos(r));
}

bool bch2_bkey_merge(struct bch_fs *, struct bkey_s, struct bkey_s_c);

static inline int bch2_key_trigger(struct btree_trans *trans,
		enum btree_id btree, unsigned level,
		struct bkey_s_c old, struct bkey_s new,
		enum btree_iter_update_trigger_flags flags)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(old.k->type ?: new.k->type);

	return ops->trigger
		? ops->trigger(trans, btree, level, old, new, flags)
		: 0;
}

static inline int bch2_key_trigger_old(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s_c old,
			enum btree_iter_update_trigger_flags flags)
{
	struct bkey_i deleted;

	bkey_init(&deleted.k);
	deleted.k.p = old.k->p;

	return bch2_key_trigger(trans, btree_id, level, old, bkey_i_to_s(&deleted),
				BTREE_TRIGGER_overwrite|flags);
}

static inline int bch2_key_trigger_new(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s new,
			enum btree_iter_update_trigger_flags flags)
{
	struct bkey_i deleted;

	bkey_init(&deleted.k);
	deleted.k.p = new.k->p;

	return bch2_key_trigger(trans, btree_id, level, bkey_i_to_s_c(&deleted), new,
				BTREE_TRIGGER_insert|flags);
}

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
	    big_endian != CPU_BIG_ENDIAN ||
	    IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		__bch2_bkey_compat(level, btree_id, version,
				   big_endian, write, f, k);

}

#endif /* _BCACHEFS_BKEY_METHODS_H */
