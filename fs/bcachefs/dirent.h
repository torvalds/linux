/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DIRENT_H
#define _BCACHEFS_DIRENT_H

#include "str_hash.h"

extern const struct bch_hash_desc bch2_dirent_hash_desc;

const char *bch2_dirent_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_dirent_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_dirent (struct bkey_ops) {	\
	.key_invalid	= bch2_dirent_invalid,		\
	.val_to_text	= bch2_dirent_to_text,		\
}

struct qstr;
struct file;
struct dir_context;
struct bch_fs;
struct bch_hash_info;
struct bch_inode_info;

unsigned bch2_dirent_name_bytes(struct bkey_s_c_dirent);

static inline unsigned dirent_val_u64s(unsigned len)
{
	return DIV_ROUND_UP(offsetof(struct bch_dirent, d_name) + len,
			    sizeof(u64));
}

int __bch2_dirent_create(struct btree_trans *, u64,
			 const struct bch_hash_info *, u8,
			 const struct qstr *, u64, int);
int bch2_dirent_create(struct bch_fs *c, u64, const struct bch_hash_info *,
		       u8, const struct qstr *, u64, u64 *, int);

int __bch2_dirent_delete(struct btree_trans *, u64,
			 const struct bch_hash_info *,
			 const struct qstr *);
int bch2_dirent_delete(struct bch_fs *, u64, const struct bch_hash_info *,
		       const struct qstr *, u64 *);

enum bch_rename_mode {
	BCH_RENAME,
	BCH_RENAME_OVERWRITE,
	BCH_RENAME_EXCHANGE,
};

int bch2_dirent_rename(struct btree_trans *,
		       struct bch_inode_info *, const struct qstr *,
		       struct bch_inode_info *, const struct qstr *,
		       enum bch_rename_mode);

u64 bch2_dirent_lookup(struct bch_fs *, u64, const struct bch_hash_info *,
		       const struct qstr *);

int bch2_empty_dir_trans(struct btree_trans *, u64);
int bch2_empty_dir(struct bch_fs *, u64);
int bch2_readdir(struct bch_fs *, struct file *, struct dir_context *);

#endif /* _BCACHEFS_DIRENT_H */
