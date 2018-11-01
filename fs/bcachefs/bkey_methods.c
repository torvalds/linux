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

const struct bkey_ops bch2_bkey_ops[] = {
	[BKEY_TYPE_EXTENTS]	= bch2_bkey_extent_ops,
	[BKEY_TYPE_INODES]	= bch2_bkey_inode_ops,
	[BKEY_TYPE_DIRENTS]	= bch2_bkey_dirent_ops,
	[BKEY_TYPE_XATTRS]	= bch2_bkey_xattr_ops,
	[BKEY_TYPE_ALLOC]	= bch2_bkey_alloc_ops,
	[BKEY_TYPE_QUOTAS]	= bch2_bkey_quota_ops,
	[BKEY_TYPE_EC]		= bch2_bkey_ec_ops,
	[BKEY_TYPE_BTREE]	= bch2_bkey_btree_ops,
};

const char *bch2_bkey_val_invalid(struct bch_fs *c, enum bkey_type type,
				  struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[type];

	switch (k.k->type) {
	case KEY_TYPE_DELETED:
	case KEY_TYPE_DISCARD:
		return NULL;

	case KEY_TYPE_ERROR:
		return bkey_val_bytes(k.k) != 0
			? "value size should be zero"
			: NULL;

	case KEY_TYPE_COOKIE:
		return bkey_val_bytes(k.k) != sizeof(struct bch_cookie)
			? "incorrect value size"
			: NULL;

	default:
		if (k.k->type < KEY_TYPE_GENERIC_NR)
			return "invalid type";

		return ops->key_invalid(c, k);
	}
}

const char *__bch2_bkey_invalid(struct bch_fs *c, enum bkey_type type,
			      struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[type];

	if (k.k->u64s < BKEY_U64s)
		return "u64s too small";

	if (!ops->is_extents) {
		if (k.k->size)
			return "nonzero size field";
	} else {
		if ((k.k->size == 0) != bkey_deleted(k.k))
			return "bad size field";
	}

	if (ops->is_extents &&
	    !k.k->size &&
	    !bkey_deleted(k.k))
		return "zero size field";

	if (k.k->p.snapshot)
		return "nonzero snapshot";

	if (type != BKEY_TYPE_BTREE &&
	    !bkey_cmp(k.k->p, POS_MAX))
		return "POS_MAX key";

	return NULL;
}

const char *bch2_bkey_invalid(struct bch_fs *c, enum bkey_type type,
			      struct bkey_s_c k)
{
	return __bch2_bkey_invalid(c, type, k) ?:
		bch2_bkey_val_invalid(c, type, k);
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
	enum bkey_type type = btree_node_type(b);
	const struct bkey_ops *ops = &bch2_bkey_ops[type];
	const char *invalid;

	BUG_ON(!k.k->u64s);

	invalid = bch2_bkey_invalid(c, type, k) ?:
		bch2_bkey_in_btree_node(b, k);
	if (invalid) {
		char buf[160];

		bch2_bkey_val_to_text(&PBUF(buf), c, type, k);
		bch2_fs_bug(c, "invalid bkey %s: %s", buf, invalid);
		return;
	}

	if (k.k->type >= KEY_TYPE_GENERIC_NR &&
	    ops->key_debugcheck)
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
		      enum bkey_type type, struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[type];

	switch (k.k->type) {
	case KEY_TYPE_DELETED:
		pr_buf(out, " deleted");
		break;
	case KEY_TYPE_DISCARD:
		pr_buf(out, " discard");
		break;
	case KEY_TYPE_ERROR:
		pr_buf(out, " error");
		break;
	case KEY_TYPE_COOKIE:
		pr_buf(out, " cookie");
		break;
	default:
		if (k.k->type >= KEY_TYPE_GENERIC_NR && ops->val_to_text)
			ops->val_to_text(out, c, k);
		break;
	}
}

void bch2_bkey_val_to_text(struct printbuf *out, struct bch_fs *c,
			   enum bkey_type type, struct bkey_s_c k)
{
	bch2_bkey_to_text(out, k.k);
	pr_buf(out, ": ");
	bch2_val_to_text(out, c, type, k);
}

void bch2_bkey_swab(enum bkey_type type,
		   const struct bkey_format *f,
		   struct bkey_packed *k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[type];

	bch2_bkey_swab_key(f, k);

	if (ops->swab)
		ops->swab(f, k);
}
