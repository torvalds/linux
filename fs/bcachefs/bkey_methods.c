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
#include "xattr.h"

const char * const bch_bkey_types[] = {
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

const struct bkey_ops bch2_bkey_ops_deleted = {
	.key_invalid = deleted_key_invalid,
};

const struct bkey_ops bch2_bkey_ops_discard = {
	.key_invalid = deleted_key_invalid,
};

static const char *empty_val_key_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	if (bkey_val_bytes(k.k))
		return "value size should be zero";

	return NULL;
}

const struct bkey_ops bch2_bkey_ops_error = {
	.key_invalid = empty_val_key_invalid,
};

static const char *key_type_cookie_invalid(const struct bch_fs *c,
					   struct bkey_s_c k)
{
	if (bkey_val_bytes(k.k) != sizeof(struct bch_cookie))
		return "incorrect value size";

	return NULL;
}

const struct bkey_ops bch2_bkey_ops_cookie = {
	.key_invalid = key_type_cookie_invalid,
};

const struct bkey_ops bch2_bkey_ops_whiteout = {
	.key_invalid = empty_val_key_invalid,
};

static const struct bkey_ops bch2_bkey_ops[] = {
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

const char *__bch2_bkey_invalid(struct bch_fs *c, struct bkey_s_c k,
				enum btree_node_type type)
{
	if (k.k->u64s < BKEY_U64s)
		return "u64s too small";

	if (btree_node_type_is_extents(type)) {
		if ((k.k->size == 0) != bkey_deleted(k.k))
			return "bad size field";
	} else {
		if (k.k->size)
			return "nonzero size field";
	}

	if (k.k->p.snapshot)
		return "nonzero snapshot";

	if (type != BKEY_TYPE_BTREE &&
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
	if (bkey_cmp(bkey_start_pos(k.k), b->data->min_key) < 0)
		return "key before start of btree node";

	if (bkey_cmp(k.k->p, b->data->max_key) > 0)
		return "key past end of btree node";

	return NULL;
}

void bch2_bkey_debugcheck(struct bch_fs *c, struct btree *b, struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];
	const char *invalid;

	BUG_ON(!k.k->u64s);

	invalid = bch2_bkey_invalid(c, k, btree_node_type(b)) ?:
		bch2_bkey_in_btree_node(b, k);
	if (invalid) {
		char buf[160];

		bch2_bkey_val_to_text(&PBUF(buf), c, k);
		bch2_fs_bug(c, "invalid bkey %s: %s", buf, invalid);
		return;
	}

	if (ops->key_debugcheck)
		ops->key_debugcheck(c, b, k);
}

void bch2_bpos_to_text(struct printbuf *out, struct bpos pos)
{
	if (!bkey_cmp(pos, POS_MIN))
		pr_buf(out, "POS_MIN");
	else if (!bkey_cmp(pos, POS_MAX))
		pr_buf(out, "POS_MAX");
	else
		pr_buf(out, "%llu:%llu", pos.inode, pos.offset);
}

void bch2_bkey_to_text(struct printbuf *out, const struct bkey *k)
{
	pr_buf(out, "u64s %u type %u ", k->u64s, k->type);

	bch2_bpos_to_text(out, k->p);

	pr_buf(out, " snap %u len %u ver %llu",
	       k->p.snapshot, k->size, k->version.lo);
}

void bch2_val_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];

	if (likely(ops->val_to_text))
		ops->val_to_text(out, c, k);
	else
		pr_buf(out, " %s", bch_bkey_types[k.k->type]);
}

void bch2_bkey_val_to_text(struct printbuf *out, struct bch_fs *c,
			   struct bkey_s_c k)
{
	bch2_bkey_to_text(out, k.k);
	pr_buf(out, ": ");
	bch2_val_to_text(out, c, k);
}

void bch2_bkey_swab(const struct bkey_format *f,
		    struct bkey_packed *k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k->type];

	bch2_bkey_swab_key(f, k);

	if (ops->swab)
		ops->swab(f, k);
}

bool bch2_bkey_normalize(struct bch_fs *c, struct bkey_s k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[k.k->type];

	return ops->key_normalize
		? ops->key_normalize(c, k)
		: false;
}

enum merge_result bch2_bkey_merge(struct bch_fs *c,
				  struct bkey_i *l, struct bkey_i *r)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[l->k.type];

	if (!key_merging_disabled(c) &&
	    ops->key_merge &&
	    l->k.type == r->k.type &&
	    !bversion_cmp(l->k.version, r->k.version) &&
	    !bkey_cmp(l->k.p, bkey_start_pos(&r->k)))
		return ops->key_merge(c, l, r);

	return BCH_MERGE_NOMERGE;
}

static const struct old_bkey_type {
	u8		btree_node_type;
	u8		old;
	u8		new;
} bkey_renumber_table[] = {
	{BKEY_TYPE_BTREE,	128, KEY_TYPE_btree_ptr		},
	{BKEY_TYPE_EXTENTS,	128, KEY_TYPE_extent		},
	{BKEY_TYPE_EXTENTS,	129, KEY_TYPE_extent		},
	{BKEY_TYPE_EXTENTS,	130, KEY_TYPE_reservation	},
	{BKEY_TYPE_INODES,	128, KEY_TYPE_inode		},
	{BKEY_TYPE_INODES,	130, KEY_TYPE_inode_generation	},
	{BKEY_TYPE_DIRENTS,	128, KEY_TYPE_dirent		},
	{BKEY_TYPE_DIRENTS,	129, KEY_TYPE_whiteout		},
	{BKEY_TYPE_XATTRS,	128, KEY_TYPE_xattr		},
	{BKEY_TYPE_XATTRS,	129, KEY_TYPE_whiteout		},
	{BKEY_TYPE_ALLOC,	128, KEY_TYPE_alloc		},
	{BKEY_TYPE_QUOTAS,	128, KEY_TYPE_quota		},
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
