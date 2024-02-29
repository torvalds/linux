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
 * key_invalid: checks validity of @k, returns 0 if good or -EINVAL if bad. If
 * invalid, entire key will be deleted.
 *
 * When invalid, error string is returned via @err. @rw indicates whether key is
 * being read or written; more aggressive checks can be enabled when rw == WRITE.
 */
struct bkey_ops {
	int		(*key_invalid)(struct bch_fs *c, struct bkey_s_c k,
				       enum bkey_invalid_flags flags, struct printbuf *err);
	void		(*val_to_text)(struct printbuf *, struct bch_fs *,
				       struct bkey_s_c);
	void		(*swab)(struct bkey_s);
	bool		(*key_normalize)(struct bch_fs *, struct bkey_s);
	bool		(*key_merge)(struct bch_fs *, struct bkey_s, struct bkey_s_c);
	int		(*trigger)(struct btree_trans *, enum btree_id, unsigned,
				   struct bkey_s_c, struct bkey_s, unsigned);
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

int bch2_bkey_val_invalid(struct bch_fs *, struct bkey_s_c,
			  enum bkey_invalid_flags, struct printbuf *);
int __bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c, enum btree_node_type,
			enum bkey_invalid_flags, struct printbuf *);
int bch2_bkey_invalid(struct bch_fs *, struct bkey_s_c, enum btree_node_type,
		      enum bkey_invalid_flags, struct printbuf *);
int bch2_bkey_in_btree_node(struct bch_fs *, struct btree *,
			    struct bkey_s_c, struct printbuf *);

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
		bpos_eq(l->p, bkey_start_pos(r));
}

bool bch2_bkey_merge(struct bch_fs *, struct bkey_s, struct bkey_s_c);

enum btree_update_flags {
	__BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE = __BTREE_ITER_FLAGS_END,
	__BTREE_UPDATE_NOJOURNAL,
	__BTREE_UPDATE_KEY_CACHE_RECLAIM,

	__BTREE_TRIGGER_NORUN,
	__BTREE_TRIGGER_TRANSACTIONAL,
	__BTREE_TRIGGER_ATOMIC,
	__BTREE_TRIGGER_GC,
	__BTREE_TRIGGER_INSERT,
	__BTREE_TRIGGER_OVERWRITE,
	__BTREE_TRIGGER_BUCKET_INVALIDATE,
};

#define BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE (1U << __BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE)
#define BTREE_UPDATE_NOJOURNAL		(1U << __BTREE_UPDATE_NOJOURNAL)
#define BTREE_UPDATE_KEY_CACHE_RECLAIM	(1U << __BTREE_UPDATE_KEY_CACHE_RECLAIM)

/* Don't run triggers at all */
#define BTREE_TRIGGER_NORUN		(1U << __BTREE_TRIGGER_NORUN)

/*
 * If set, we're running transactional triggers as part of a transaction commit:
 * triggers may generate new updates
 *
 * If cleared, and either BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE are set,
 * we're running atomic triggers during a transaction commit: we have our
 * journal reservation, we're holding btree node write locks, and we know the
 * transaction is going to commit (returning an error here is a fatal error,
 * causing us to go emergency read-only)
 */
#define BTREE_TRIGGER_TRANSACTIONAL	(1U << __BTREE_TRIGGER_TRANSACTIONAL)
#define BTREE_TRIGGER_ATOMIC		(1U << __BTREE_TRIGGER_ATOMIC)

/* We're in gc/fsck: running triggers to recalculate e.g. disk usage */
#define BTREE_TRIGGER_GC		(1U << __BTREE_TRIGGER_GC)

/* @new is entering the btree */
#define BTREE_TRIGGER_INSERT		(1U << __BTREE_TRIGGER_INSERT)

/* @old is leaving the btree */
#define BTREE_TRIGGER_OVERWRITE		(1U << __BTREE_TRIGGER_OVERWRITE)

/* signal from bucket invalidate path to alloc trigger */
#define BTREE_TRIGGER_BUCKET_INVALIDATE	(1U << __BTREE_TRIGGER_BUCKET_INVALIDATE)

static inline int bch2_key_trigger(struct btree_trans *trans,
		enum btree_id btree, unsigned level,
		struct bkey_s_c old, struct bkey_s new,
		unsigned flags)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(old.k->type ?: new.k->type);

	return ops->trigger
		? ops->trigger(trans, btree, level, old, new, flags)
		: 0;
}

static inline int bch2_key_trigger_old(struct btree_trans *trans,
				       enum btree_id btree_id, unsigned level,
				       struct bkey_s_c old, unsigned flags)
{
	struct bkey_i deleted;

	bkey_init(&deleted.k);
	deleted.k.p = old.k->p;

	return bch2_key_trigger(trans, btree_id, level, old, bkey_i_to_s(&deleted),
				BTREE_TRIGGER_OVERWRITE|flags);
}

static inline int bch2_key_trigger_new(struct btree_trans *trans,
				       enum btree_id btree_id, unsigned level,
				       struct bkey_s new, unsigned flags)
{
	struct bkey_i deleted;

	bkey_init(&deleted.k);
	deleted.k.p = new.k->p;

	return bch2_key_trigger(trans, btree_id, level, bkey_i_to_s_c(&deleted), new,
				BTREE_TRIGGER_INSERT|flags);
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
	    big_endian != CPU_BIG_ENDIAN)
		__bch2_bkey_compat(level, btree_id, version,
				   big_endian, write, f, k);

}

#endif /* _BCACHEFS_BKEY_METHODS_H */
