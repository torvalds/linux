// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "backpointers.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_types.h"
#include "alloc_background.h"
#include "dirent.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "inode.h"
#include "io_misc.h"
#include "lru.h"
#include "quota.h"
#include "reflink.h"
#include "snapshot.h"
#include "subvolume.h"
#include "xattr.h"

const char * const bch2_bkey_types[] = {
#define x(name, nr) #name,
	BCH_BKEY_TYPES()
#undef x
	NULL
};

static int deleted_key_invalid(struct bch_fs *c, struct bkey_s_c k,
			       enum bch_validate_flags flags, struct printbuf *err)
{
	return 0;
}

#define bch2_bkey_ops_deleted ((struct bkey_ops) {	\
	.key_invalid = deleted_key_invalid,		\
})

#define bch2_bkey_ops_whiteout ((struct bkey_ops) {	\
	.key_invalid = deleted_key_invalid,		\
})

static int empty_val_key_invalid(struct bch_fs *c, struct bkey_s_c k,
				 enum bch_validate_flags flags, struct printbuf *err)
{
	int ret = 0;

	bkey_fsck_err_on(bkey_val_bytes(k.k), c, err,
			 bkey_val_size_nonzero,
			 "incorrect value size (%zu != 0)",
			 bkey_val_bytes(k.k));
fsck_err:
	return ret;
}

#define bch2_bkey_ops_error ((struct bkey_ops) {	\
	.key_invalid = empty_val_key_invalid,		\
})

static int key_type_cookie_invalid(struct bch_fs *c, struct bkey_s_c k,
				   enum bch_validate_flags flags, struct printbuf *err)
{
	return 0;
}

static void key_type_cookie_to_text(struct printbuf *out, struct bch_fs *c,
				    struct bkey_s_c k)
{
	struct bkey_s_c_cookie ck = bkey_s_c_to_cookie(k);

	prt_printf(out, "%llu", le64_to_cpu(ck.v->cookie));
}

#define bch2_bkey_ops_cookie ((struct bkey_ops) {	\
	.key_invalid	= key_type_cookie_invalid,	\
	.val_to_text	= key_type_cookie_to_text,	\
	.min_val_size	= 8,				\
})

#define bch2_bkey_ops_hash_whiteout ((struct bkey_ops) {\
	.key_invalid = empty_val_key_invalid,		\
})

static int key_type_inline_data_invalid(struct bch_fs *c, struct bkey_s_c k,
					enum bch_validate_flags flags, struct printbuf *err)
{
	return 0;
}

static void key_type_inline_data_to_text(struct printbuf *out, struct bch_fs *c,
					 struct bkey_s_c k)
{
	struct bkey_s_c_inline_data d = bkey_s_c_to_inline_data(k);
	unsigned datalen = bkey_inline_data_bytes(k.k);

	prt_printf(out, "datalen %u: %*phN",
	       datalen, min(datalen, 32U), d.v->data);
}

#define bch2_bkey_ops_inline_data ((struct bkey_ops) {	\
	.key_invalid	= key_type_inline_data_invalid,	\
	.val_to_text	= key_type_inline_data_to_text,	\
})

static bool key_type_set_merge(struct bch_fs *c, struct bkey_s l, struct bkey_s_c r)
{
	bch2_key_resize(l.k, l.k->size + r.k->size);
	return true;
}

#define bch2_bkey_ops_set ((struct bkey_ops) {		\
	.key_invalid	= empty_val_key_invalid,	\
	.key_merge	= key_type_set_merge,		\
})

const struct bkey_ops bch2_bkey_ops[] = {
#define x(name, nr) [KEY_TYPE_##name]	= bch2_bkey_ops_##name,
	BCH_BKEY_TYPES()
#undef x
};

const struct bkey_ops bch2_bkey_null_ops = {
};

int bch2_bkey_val_invalid(struct bch_fs *c, struct bkey_s_c k,
			  enum bch_validate_flags flags,
			  struct printbuf *err)
{
	if (test_bit(BCH_FS_no_invalid_checks, &c->flags))
		return 0;

	const struct bkey_ops *ops = bch2_bkey_type_ops(k.k->type);
	int ret = 0;

	bkey_fsck_err_on(bkey_val_bytes(k.k) < ops->min_val_size, c, err,
			 bkey_val_size_too_small,
			 "bad val size (%zu < %u)",
			 bkey_val_bytes(k.k), ops->min_val_size);

	if (!ops->key_invalid)
		return 0;

	ret = ops->key_invalid(c, k, flags, err);
fsck_err:
	return ret;
}

static u64 bch2_key_types_allowed[] = {
	[BKEY_TYPE_btree] =
		BIT_ULL(KEY_TYPE_deleted)|
		BIT_ULL(KEY_TYPE_btree_ptr)|
		BIT_ULL(KEY_TYPE_btree_ptr_v2),
#define x(name, nr, flags, keys)	[BKEY_TYPE_##name] = BIT_ULL(KEY_TYPE_deleted)|keys,
	BCH_BTREE_IDS()
#undef x
};

const char *bch2_btree_node_type_str(enum btree_node_type type)
{
	return type == BKEY_TYPE_btree ? "internal btree node" : bch2_btree_id_str(type - 1);
}

int __bch2_bkey_invalid(struct bch_fs *c, struct bkey_s_c k,
			enum btree_node_type type,
			enum bch_validate_flags flags,
			struct printbuf *err)
{
	if (test_bit(BCH_FS_no_invalid_checks, &c->flags))
		return 0;

	int ret = 0;

	bkey_fsck_err_on(k.k->u64s < BKEY_U64s, c, err,
			 bkey_u64s_too_small,
			 "u64s too small (%u < %zu)", k.k->u64s, BKEY_U64s);

	if (type >= BKEY_TYPE_NR)
		return 0;

	bkey_fsck_err_on(k.k->type < KEY_TYPE_MAX &&
			 (type == BKEY_TYPE_btree || (flags & BCH_VALIDATE_commit)) &&
			 !(bch2_key_types_allowed[type] & BIT_ULL(k.k->type)), c, err,
			 bkey_invalid_type_for_btree,
			 "invalid key type for btree %s (%s)",
			 bch2_btree_node_type_str(type),
			 k.k->type < KEY_TYPE_MAX
			 ? bch2_bkey_types[k.k->type]
			 : "(unknown)");

	if (btree_node_type_is_extents(type) && !bkey_whiteout(k.k)) {
		bkey_fsck_err_on(k.k->size == 0, c, err,
				 bkey_extent_size_zero,
				 "size == 0");

		bkey_fsck_err_on(k.k->size > k.k->p.offset, c, err,
				 bkey_extent_size_greater_than_offset,
				 "size greater than offset (%u > %llu)",
				 k.k->size, k.k->p.offset);
	} else {
		bkey_fsck_err_on(k.k->size, c, err,
				 bkey_size_nonzero,
				 "size != 0");
	}

	if (type != BKEY_TYPE_btree) {
		enum btree_id btree = type - 1;

		if (btree_type_has_snapshots(btree)) {
			bkey_fsck_err_on(!k.k->p.snapshot, c, err,
					 bkey_snapshot_zero,
					 "snapshot == 0");
		} else if (!btree_type_has_snapshot_field(btree)) {
			bkey_fsck_err_on(k.k->p.snapshot, c, err,
					 bkey_snapshot_nonzero,
					 "nonzero snapshot");
		} else {
			/*
			 * btree uses snapshot field but it's not required to be
			 * nonzero
			 */
		}

		bkey_fsck_err_on(bkey_eq(k.k->p, POS_MAX), c, err,
				 bkey_at_pos_max,
				 "key at POS_MAX");
	}
fsck_err:
	return ret;
}

int bch2_bkey_invalid(struct bch_fs *c, struct bkey_s_c k,
		      enum btree_node_type type,
		      enum bch_validate_flags flags,
		      struct printbuf *err)
{
	return __bch2_bkey_invalid(c, k, type, flags, err) ?:
		bch2_bkey_val_invalid(c, k, flags, err);
}

int bch2_bkey_in_btree_node(struct bch_fs *c, struct btree *b,
			    struct bkey_s_c k, struct printbuf *err)
{
	int ret = 0;

	bkey_fsck_err_on(bpos_lt(k.k->p, b->data->min_key), c, err,
			 bkey_before_start_of_btree_node,
			 "key before start of btree node");

	bkey_fsck_err_on(bpos_gt(k.k->p, b->data->max_key), c, err,
			 bkey_after_end_of_btree_node,
			 "key past end of btree node");
fsck_err:
	return ret;
}

void bch2_bpos_to_text(struct printbuf *out, struct bpos pos)
{
	if (bpos_eq(pos, POS_MIN))
		prt_printf(out, "POS_MIN");
	else if (bpos_eq(pos, POS_MAX))
		prt_printf(out, "POS_MAX");
	else if (bpos_eq(pos, SPOS_MAX))
		prt_printf(out, "SPOS_MAX");
	else {
		if (pos.inode == U64_MAX)
			prt_printf(out, "U64_MAX");
		else
			prt_printf(out, "%llu", pos.inode);
		prt_printf(out, ":");
		if (pos.offset == U64_MAX)
			prt_printf(out, "U64_MAX");
		else
			prt_printf(out, "%llu", pos.offset);
		prt_printf(out, ":");
		if (pos.snapshot == U32_MAX)
			prt_printf(out, "U32_MAX");
		else
			prt_printf(out, "%u", pos.snapshot);
	}
}

void bch2_bkey_to_text(struct printbuf *out, const struct bkey *k)
{
	if (k) {
		prt_printf(out, "u64s %u type ", k->u64s);

		if (k->type < KEY_TYPE_MAX)
			prt_printf(out, "%s ", bch2_bkey_types[k->type]);
		else
			prt_printf(out, "%u ", k->type);

		bch2_bpos_to_text(out, k->p);

		prt_printf(out, " len %u ver %llu", k->size, k->version.lo);
	} else {
		prt_printf(out, "(null)");
	}
}

void bch2_val_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(k.k->type);

	if (likely(ops->val_to_text))
		ops->val_to_text(out, c, k);
}

void bch2_bkey_val_to_text(struct printbuf *out, struct bch_fs *c,
			   struct bkey_s_c k)
{
	bch2_bkey_to_text(out, k.k);

	if (bkey_val_bytes(k.k)) {
		prt_printf(out, ": ");
		bch2_val_to_text(out, c, k);
	}
}

void bch2_bkey_swab_val(struct bkey_s k)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(k.k->type);

	if (ops->swab)
		ops->swab(k);
}

bool bch2_bkey_normalize(struct bch_fs *c, struct bkey_s k)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(k.k->type);

	return ops->key_normalize
		? ops->key_normalize(c, k)
		: false;
}

bool bch2_bkey_merge(struct bch_fs *c, struct bkey_s l, struct bkey_s_c r)
{
	const struct bkey_ops *ops = bch2_bkey_type_ops(l.k->type);

	return ops->key_merge &&
		bch2_bkey_maybe_mergable(l.k, r.k) &&
		(u64) l.k->size + r.k->size <= KEY_SIZE_MAX &&
		!bch2_key_merging_disabled &&
		ops->key_merge(c, l, r);
}

static const struct old_bkey_type {
	u8		btree_node_type;
	u8		old;
	u8		new;
} bkey_renumber_table[] = {
	{BKEY_TYPE_btree,	128, KEY_TYPE_btree_ptr		},
	{BKEY_TYPE_extents,	128, KEY_TYPE_extent		},
	{BKEY_TYPE_extents,	129, KEY_TYPE_extent		},
	{BKEY_TYPE_extents,	130, KEY_TYPE_reservation	},
	{BKEY_TYPE_inodes,	128, KEY_TYPE_inode		},
	{BKEY_TYPE_inodes,	130, KEY_TYPE_inode_generation	},
	{BKEY_TYPE_dirents,	128, KEY_TYPE_dirent		},
	{BKEY_TYPE_dirents,	129, KEY_TYPE_hash_whiteout	},
	{BKEY_TYPE_xattrs,	128, KEY_TYPE_xattr		},
	{BKEY_TYPE_xattrs,	129, KEY_TYPE_hash_whiteout	},
	{BKEY_TYPE_alloc,	128, KEY_TYPE_alloc		},
	{BKEY_TYPE_quotas,	128, KEY_TYPE_quota		},
};

void bch2_bkey_renumber(enum btree_node_type btree_node_type,
			struct bkey_packed *k,
			int write)
{
	const struct old_bkey_type *i;

	for (i = bkey_renumber_table;
	     i < bkey_renumber_table + ARRAY_SIZE(bkey_renumber_table);
	     i++)
		if (btree_node_type == i->btree_node_type &&
		    k->type == (write ? i->new : i->old)) {
			k->type = write ? i->old : i->new;
			break;
		}
}

void __bch2_bkey_compat(unsigned level, enum btree_id btree_id,
			unsigned version, unsigned big_endian,
			int write,
			struct bkey_format *f,
			struct bkey_packed *k)
{
	const struct bkey_ops *ops;
	struct bkey uk;
	unsigned nr_compat = 5;
	int i;

	/*
	 * Do these operations in reverse order in the write path:
	 */

	for (i = 0; i < nr_compat; i++)
	switch (!write ? i : nr_compat - 1 - i) {
	case 0:
		if (big_endian != CPU_BIG_ENDIAN)
			bch2_bkey_swab_key(f, k);
		break;
	case 1:
		if (version < bcachefs_metadata_version_bkey_renumber)
			bch2_bkey_renumber(__btree_node_type(level, btree_id), k, write);
		break;
	case 2:
		if (version < bcachefs_metadata_version_inode_btree_change &&
		    btree_id == BTREE_ID_inodes) {
			if (!bkey_packed(k)) {
				struct bkey_i *u = packed_to_bkey(k);

				swap(u->k.p.inode, u->k.p.offset);
			} else if (f->bits_per_field[BKEY_FIELD_INODE] &&
				   f->bits_per_field[BKEY_FIELD_OFFSET]) {
				struct bkey_format tmp = *f, *in = f, *out = &tmp;

				swap(tmp.bits_per_field[BKEY_FIELD_INODE],
				     tmp.bits_per_field[BKEY_FIELD_OFFSET]);
				swap(tmp.field_offset[BKEY_FIELD_INODE],
				     tmp.field_offset[BKEY_FIELD_OFFSET]);

				if (!write)
					swap(in, out);

				uk = __bch2_bkey_unpack_key(in, k);
				swap(uk.p.inode, uk.p.offset);
				BUG_ON(!bch2_bkey_pack_key(k, &uk, out));
			}
		}
		break;
	case 3:
		if (version < bcachefs_metadata_version_snapshot &&
		    (level || btree_type_has_snapshots(btree_id))) {
			struct bkey_i *u = packed_to_bkey(k);

			if (u) {
				u->k.p.snapshot = write
					? 0 : U32_MAX;
			} else {
				u64 min_packed = le64_to_cpu(f->field_offset[BKEY_FIELD_SNAPSHOT]);
				u64 max_packed = min_packed +
					~(~0ULL << f->bits_per_field[BKEY_FIELD_SNAPSHOT]);

				uk = __bch2_bkey_unpack_key(f, k);
				uk.p.snapshot = write
					? min_packed : min_t(u64, U32_MAX, max_packed);

				BUG_ON(!bch2_bkey_pack_key(k, &uk, f));
			}
		}

		break;
	case 4: {
		struct bkey_s u;

		if (!bkey_packed(k)) {
			u = bkey_i_to_s(packed_to_bkey(k));
		} else {
			uk = __bch2_bkey_unpack_key(f, k);
			u.k = &uk;
			u.v = bkeyp_val(f, k);
		}

		if (big_endian != CPU_BIG_ENDIAN)
			bch2_bkey_swab_val(u);

		ops = bch2_bkey_type_ops(k->type);

		if (ops->compat)
			ops->compat(btree_id, version, big_endian, write, u);
		break;
	}
	default:
		BUG();
	}
}
