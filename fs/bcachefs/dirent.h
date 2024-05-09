/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DIRENT_H
#define _BCACHEFS_DIRENT_H

#include "str_hash.h"

enum bkey_invalid_flags;
extern const struct bch_hash_desc bch2_dirent_hash_desc;

int bch2_dirent_invalid(const struct bch_fs *, struct bkey_s_c,
			enum bkey_invalid_flags, struct printbuf *);
void bch2_dirent_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_dirent ((struct bkey_ops) {	\
	.key_invalid	= bch2_dirent_invalid,		\
	.val_to_text	= bch2_dirent_to_text,		\
	.min_val_size	= 16,				\
})

struct qstr;
struct file;
struct dir_context;
struct bch_fs;
struct bch_hash_info;
struct bch_inode_info;

struct qstr bch2_dirent_get_name(struct bkey_s_c_dirent d);

static inline unsigned dirent_val_u64s(unsigned len)
{
	return DIV_ROUND_UP(offsetof(struct bch_dirent, d_name) + len,
			    sizeof(u64));
}

int bch2_dirent_read_target(struct btree_trans *, subvol_inum,
			    struct bkey_s_c_dirent, subvol_inum *);

int bch2_dirent_create(struct btree_trans *, subvol_inum,
		       const struct bch_hash_info *, u8,
		       const struct qstr *, u64, u64 *, int);

static inline unsigned vfs_d_type(unsigned type)
{
	return type == DT_SUBVOL ? DT_DIR : type;
}

enum bch_rename_mode {
	BCH_RENAME,
	BCH_RENAME_OVERWRITE,
	BCH_RENAME_EXCHANGE,
};

int bch2_dirent_rename(struct btree_trans *,
		       subvol_inum, struct bch_hash_info *,
		       subvol_inum, struct bch_hash_info *,
		       const struct qstr *, subvol_inum *, u64 *,
		       const struct qstr *, subvol_inum *, u64 *,
		       enum bch_rename_mode);

int __bch2_dirent_lookup_trans(struct btree_trans *, struct btree_iter *,
			       subvol_inum, const struct bch_hash_info *,
			       const struct qstr *, subvol_inum *, unsigned);
u64 bch2_dirent_lookup(struct bch_fs *, subvol_inum,
		       const struct bch_hash_info *,
		       const struct qstr *, subvol_inum *);

int bch2_empty_dir_trans(struct btree_trans *, subvol_inum);
int bch2_readdir(struct bch_fs *, subvol_inum, struct dir_context *);

#endif /* _BCACHEFS_DIRENT_H */
