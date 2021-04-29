// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_types.h"
#include "alloc_background.h"
#include "dirent.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "inode.h"
#include "quota.h"
#include "reflink.h"
#include "xattr.h"

const char * const bch2_bkey_types[] = {
#define x(name, nr) #name,
	BCH_BKEY_TYPES()
#undef x
	NULL
};

static const char *deleted_key_invalid(const struct bch_fs *c,
					struct bkey_s_c k)
{
	return NULL;
}

#define bch2_bkey_ops_deleted (struct bkey_ops) {	\
	.key_invalid = deleted_key_invalid,		\
}

#define bch2_bkey_ops_discard (struct bkey_ops) {	\
	.key_invalid = deleted_key_invalid,		\
}

static const char *empty_val_key_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	if (bkey_val_bytes(k.k))
		return "value size should be zero";

	return NULL;
}

#define bch2_bkey_ops_error (struct bkey_ops) {		\
	.key_invalid = empty_val_key_invalid,		\
}

static const char *key_type_cookie_invalid(const struct bch_fs *c,
					   struct bkey_s_c k)
{
	if (bkey_val_bytes(k.k) != sizeof(struct bch_cookie))
		return "incorrect value size";

	return NULL;
}

#define bch2_bkey_ops_cookie (struct bkey_ops) {	\
	.key_invalid = key_type_cookie_invalid,		\
}

#define bch2_bkey_ops_hash_whiteout (struct bkey_ops) {	\
	.key_invalid = empty_val_key_invalid,		\
}

static const char *key_type_inline_data_invalid(const struct bch_fs *c,
					   struct bkey_s_c k)
{
	return NULL;
}

static void key_type_inline_data_to_text(struct printbuf *out, struct bch_fs *c,
					 struct bkey_s_c k)
{
	struct bkey_s_c_inline_data d = bkey_s_c_to_inline_data(k);
	unsigned datalen = bkey_inline_data_bytes(k.k);

	pr_buf(out, "datalen %u: %*phN",
	       datalen, min(datalen, 32U), d.v->data);
}

#define bch2_bkey_ops_inline_data (struct bkey_ops) {	\
	.key_invalid	= key_type_inline_data_invalid,	\
	.val_to_text	= key_type_inline_data_to_text,	\
}

const struct bkey_ops bch2_bkey_ops[] = {
#define x(name, nr) [KEY_TYPE_##name]	= bch2_bkey_ops_##name,
	BCH_BKEY_TYPES()
#undef x
};

const char *bch2_bkey_val_invalid(struct bch_fs *c, struct bkey_s_c k)
{
	if (k.k->type >= KEY_TYPE_MAX)
		return "invalid type";

	return bch2_bkey_ops[k.k->type].key_invalid(c, k);
}

static unsigned bch2_key_types_allowed[] = {
	[BKEY_TYPE_extents] =
		(1U << KEY_TYPE_error)|
		(1U << KEY_TYPE_cookie)|
		(1U << KEY_TYPE_extent)|
		(1U << KEY_TYPE_reservation)|
		(1U << KEY_TYPE_reflink_p)|
		(1U << KEY_TYPE_inline_data),
	[BKEY_TYPE_inodes] =
		(1U << KEY_TYPE_inode)|
		(1U << KEY_TYPE_inode_generation),
	[BKEY_TYPE_dirents] =
		(1U << KEY_TYPE_hash_whiteout)|
		(1U << KEY_TYPE_dirent),
	[BKEY_TYPE_xattrs] =
		(1U << KEY_TYPE_cookie)|
		(1U << KEY_TYPE_hash_whiteout)|
		(1U << KEY_TYPE_xattr),
	[BKEY_TYPE_alloc] =
		(1U << KEY_TYPE_alloc)|
		(1U << KEY_TYPE_alloc_v2),
	[BKEY_TYPE_quotas] =
		(1U << KEY_TYPE_quota),
	[BKEY_TYPE_stripes] =
		(1U << KEY_TYPE_stripe),
	[BKEY_TYPE_reflink] =
		(1U << KEY_TYPE_reflink_v)|
		(1U << KEY_TYPE_indirect_inline_data),
	[BKEY_TYPE_btree] =
		(1U << KEY_TYPE_btree_ptr)|
		(1U << KEY_TYPE_btree_ptr_v2),
};

const char *__bch2_bkey_invalid(struct bch_fs *c, struct bkey_s_c k,
				enum btree_node_type type)
{
	unsigned key_types_allowed = (1U << KEY_TYPE_deleted)|
		bch2_key_types_allowed[type] ;

	if (k.k->u64s < BKEY_U64s)
		return "u64s too small";

	if (!(key_types_allowed & (1U << k.k->type)))
		return "invalid key type for this btree";

	if (type == BKEY_TYPE_btree &&
	    bkey_val_u64s(k.k) > BKEY_BTREE_PTR_VAL_U64s_MAX)
		return "value too big";

	if (btree_node_type_is_extents(type)) {
		if ((k.k->size == 0) != bkey_deleted(k.k))
			return "bad size field";

		if (k.k->size > k.k->p.offset)
			return "size greater than offset";
	} else {
		if (k.k->size)
			return "nonzero size field";
	}

	if (type != BKEY_TYPE_btree &&
	    !btree_type_has_snapshots(type) &&
	    k.k->p.snapshot)
		return "nonzero snapshot";

	if (type != BKEY_TYPE_btree &&
	    btree_type_has_snapshots(type) &&
	    k.k->p.snapshot != U32_MAX)
		return "invalid snapshot field";

	if (type != BKEY_TYPE_btree &&
	    !bkey_cmp(k.k->p, POS_MAX))
		return "POS_MAX key";

	return NULL;
}

const char *bch2_bkey_invalid(struct bch_fs *c, struct bkey_s_c k,
			      enum btree_node_type type)
{
	return __bch2_bkey_invalid(c, k, type) ?:
		bch2_bkey_val_invalid(c, k);
}

const char *bch2_bkey_in_btree_node(struct btree *b, struct bkey_s_c k)
{
	if (bpos_cmp(k.k->p, b->data->min_key) < 0)
		return "key before start of btree node";

	if (bpos_cmp(k.k->p, b->data->max_key) > 0)
		return "key past end of btree node";

	return NULL;
}

void bch2_bkey_debugcheck(struct bch_fs *c, struct btree *b, struct bkey_s_c k)
{
	const char *invalid;

	BUG_ON(!k.k->u64s);

	invalid = bch2_bkey_invalid(c, k, btree_node_type(b)) ?:
		bch2_bkey_in_btree_node(b, k);
	if (invalid) {
		char buf[160];

		bch2_bkey_val_to_text(&PBUF(buf), c, k);
		bch2_fs_inconsistent(c, "invalid bkey %s: %s", buf, invalid);
	}
}

void bch2_bpos_to_text(struct printbuf *out, struct bpos pos)
{
	if (!bpos_cmp(pos, POS_MIN))
		pr_buf(out, "POS_MIN");
	else if (!bpos_cmp(pos, POS_MAX))
		pr_buf(out, "POS_MAX");
	else {
		if (pos.inode == U64_MAX)
			pr_buf(out, "U64_MAX");
		else
			pr_buf(out, "%llu", pos.inode);
		pr_buf(out, ":");
		if (pos.offset == U64_MAX)
			pr_buf(out, "U64_MAX");
		else
			pr_buf(out, "%llu", pos.offset);
		pr_buf(out, ":");
		if (pos.snapshot == U32_MAX)
			pr_buf(out, "U32_MAX");
		else
			pr_buf(out, "%u", pos.snapshot);
	}
}

void bch2_bkey_to_text(struct printbuf *out, const struct bkey *k)
{
	if (k) {
		pr_buf(out, "u64s %u type ", k->u64s);

		if (k->type < KEY_TYPE_MAX)
			pr_buf(out, "%s ", bch2_bkey_types[k->type]);
		else
			pr_buf(out, "%u ", k->type);

		bch2_bpos_to_text(out, k->p);

		pr_buf(out, " len %u ver %llu", k->size, k->version.lo);
	} else {
		pr_buf(out, "(null)");
	}
}

void bch2_val_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	if (k.k->type < KEY_TYPE_MAX) {
		const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];

		if (likely(ops->val_to_text))
			ops->val_to_text(out, c, k);
	} else {
		pr_buf(out, "(invalid type %u)", k.k->type);
	}
}

void bch2_bkey_val_to_text(struct printbuf *out, struct bch_fs *c,
			   struct bkey_s_c k)
{
	bch2_bkey_to_text(out, k.k);

	if (k.k) {
		pr_buf(out, ": ");
		bch2_val_to_text(out, c, k);
	}
}

void bch2_bkey_swab_val(struct bkey_s k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];

	if (ops->swab)
		ops->swab(k);
}

bool bch2_bkey_normalize(struct bch_fs *c, struct bkey_s k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];

	return ops->key_normalize
		? ops->key_normalize(c, k)
		: false;
}

bool bch2_bkey_merge(struct bch_fs *c, struct bkey_s l, struct bkey_s_c r)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[l.k->type];

	return bch2_bkey_maybe_mergable(l.k, r.k) && ops->key_merge(c, l, r);
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
	struct bkey_s u;
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
				u64 min_packed = f->field_offset[BKEY_FIELD_SNAPSHOT];
				u64 max_packed = min_packed +
					~(~0ULL << f->bits_per_field[BKEY_FIELD_SNAPSHOT]);

				uk = __bch2_bkey_unpack_key(f, k);
				uk.p.snapshot = write
					? min_packed : min_t(u64, U32_MAX, max_packed);

				BUG_ON(!bch2_bkey_pack_key(k, &uk, f));
			}
		}

		break;
	case 4:
		if (!bkey_packed(k)) {
			u = bkey_i_to_s(packed_to_bkey(k));
		} else {
			uk = __bch2_bkey_unpack_key(f, k);
			u.k = &uk;
			u.v = bkeyp_val(f, k);
		}

		if (big_endian != CPU_BIG_ENDIAN)
			bch2_bkey_swab_val(u);

		ops = &bch2_bkey_ops[k->type];

		if (ops->compat)
			ops->compat(btree_id, version, big_endian, write, u);
		break;
	default:
		BUG();
	}
}
