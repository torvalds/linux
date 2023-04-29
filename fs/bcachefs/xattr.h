/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_XATTR_H
#define _BCACHEFS_XATTR_H

#include "str_hash.h"

extern const struct bch_hash_desc bch2_xattr_hash_desc;

int bch2_xattr_invalid(const struct bch_fs *, struct bkey_s_c, unsigned, struct printbuf *);
void bch2_xattr_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_xattr ((struct bkey_ops) {	\
	.key_invalid	= bch2_xattr_invalid,		\
	.val_to_text	= bch2_xattr_to_text,		\
	.min_val_size	= 8,				\
})

static inline unsigned xattr_val_u64s(unsigned name_len, unsigned val_len)
{
	return DIV_ROUND_UP(offsetof(struct bch_xattr, x_name) +
			    name_len + val_len, sizeof(u64));
}

#define xattr_val(_xattr)					\
	((void *) (_xattr)->x_name + (_xattr)->x_name_len)

struct xattr_search_key {
	u8		type;
	struct qstr	name;
};

#define X_SEARCH(_type, _name, _len) ((struct xattr_search_key)	\
	{ .type = _type, .name = QSTR_INIT(_name, _len) })

struct dentry;
struct xattr_handler;
struct bch_hash_info;
struct bch_inode_info;

int bch2_xattr_get(struct bch_fs *, struct bch_inode_info *,
		  const char *, void *, size_t, int);

int bch2_xattr_set(struct btree_trans *, subvol_inum,
		   const struct bch_hash_info *,
		   const char *, const void *, size_t, int, int);

ssize_t bch2_xattr_list(struct dentry *, char *, size_t);

extern const struct xattr_handler *bch2_xattr_handlers[];

#endif /* _BCACHEFS_XATTR_H */
