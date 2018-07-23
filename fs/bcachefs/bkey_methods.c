// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_types.h"
#include "alloc.h"
#include "dirent.h"
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

		bch2_bkey_val_to_text(c, type, buf, sizeof(buf), k);
		bch2_fs_bug(c, "invalid bkey %s: %s", buf, invalid);
		return;
	}

	if (k.k->type >= KEY_TYPE_GENERIC_NR &&
	    ops->key_debugcheck)
		ops->key_debugcheck(c, b, k);
}

#define p(...)	(out += scnprintf(out, end - out, __VA_ARGS__))

int bch2_bpos_to_text(char *buf, size_t size, struct bpos pos)
{
	char *out = buf, *end = buf + size;

	if (!bkey_cmp(pos, POS_MIN))
		p("POS_MIN");
	else if (!bkey_cmp(pos, POS_MAX))
		p("POS_MAX");
	else
		p("%llu:%llu", pos.inode, pos.offset);

	return out - buf;
}

int bch2_bkey_to_text(char *buf, size_t size, const struct bkey *k)
{
	char *out = buf, *end = buf + size;

	p("u64s %u type %u ", k->u64s, k->type);

	out += bch2_bpos_to_text(out, end - out, k->p);

	p(" snap %u len %u ver %llu", k->p.snapshot, k->size, k->version.lo);

	return out - buf;
}

int bch2_val_to_text(struct bch_fs *c, enum bkey_type type,
		     char *buf, size_t size, struct bkey_s_c k)
{
	const struct bkey_ops *ops = &bch2_bkey_ops[type];
	char *out = buf, *end = buf + size;

	switch (k.k->type) {
	case KEY_TYPE_DELETED:
		p(" deleted");
		break;
	case KEY_TYPE_DISCARD:
		p(" discard");
		break;
	case KEY_TYPE_ERROR:
		p(" error");
		break;
	case KEY_TYPE_COOKIE:
		p(" cookie");
		break;
	default:
		if (k.k->type >= KEY_TYPE_GENERIC_NR && ops->val_to_text)
			out += ops->val_to_text(c, out, end - out, k);
		break;
	}

	return out - buf;
}

int bch2_bkey_val_to_text(struct bch_fs *c, enum bkey_type type,
			  char *buf, size_t size, struct bkey_s_c k)
{
	char *out = buf, *end = buf + size;

	out += bch2_bkey_to_text(out, end - out, k.k);
	out += scnprintf(out, end - out, ": ");
	out += bch2_val_to_text(c, type, out, end - out, k);

	return out - buf;
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
