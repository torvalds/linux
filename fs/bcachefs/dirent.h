/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DIRENT_H
#define _BCACHEFS_DIRENT_H

#include "str_hash.h"

extern const struct bch_hash_desc bch2_dirent_hash_desc;

int bch2_dirent_validate(struct bch_fs *, struct bkey_s_c,
			 struct bkey_validate_context);
void bch2_dirent_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_dirent ((struct bkey_ops) {	\
	.key_validate	= bch2_dirent_validate,		\
	.val_to_text	= bch2_dirent_to_text,		\
	.min_val_size	= 16,				\
})

struct qstr;
struct file;
struct dir_context;
struct bch_fs;
struct bch_hash_info;
struct bch_inode_info;

int bch2_casefold(struct btree_trans *, const struct bch_hash_info *,
		  const struct qstr *, struct qstr *);

static inline int bch2_maybe_casefold(struct btree_trans *trans,
				      const struct bch_hash_info *info,
				      const struct qstr *str, struct qstr *out_cf)
{
	if (likely(!info->cf_encoding)) {
		*out_cf = *str;
		return 0;
	} else {
		return bch2_casefold(trans, info, str, out_cf);
	}
}

struct qstr bch2_dirent_get_name(struct bkey_s_c_dirent);

static inline unsigned dirent_val_u64s(unsigned len, unsigned cf_len)
{
	unsigned bytes = cf_len
		? offsetof(struct bch_dirent, d_cf_name_block.d_names) + len + cf_len
		: offsetof(struct bch_dirent, d_name) + len;

	return DIV_ROUND_UP(bytes, sizeof(u64));
}

int bch2_dirent_read_target(struct btree_trans *, subvol_inum,
			    struct bkey_s_c_dirent, subvol_inum *);

static inline void dirent_copy_target(struct bkey_i_dirent *dst,
				      struct bkey_s_c_dirent src)
{
	dst->v.d_inum = src.v->d_inum;
	dst->v.d_type = src.v->d_type;
}

int bch2_dirent_init_name(struct bkey_i_dirent *,
			  const struct bch_hash_info *,
			  const struct qstr *,
			  const struct qstr *);
struct bkey_i_dirent *bch2_dirent_create_key(struct btree_trans *,
				const struct bch_hash_info *, subvol_inum, u8,
				const struct qstr *, const struct qstr *, u64);

int bch2_dirent_create_snapshot(struct btree_trans *, u32, u64, u32,
			const struct bch_hash_info *, u8,
			const struct qstr *, u64, u64 *,
			enum btree_iter_update_trigger_flags);
int bch2_dirent_create(struct btree_trans *, subvol_inum,
		       const struct bch_hash_info *, u8,
		       const struct qstr *, u64, u64 *,
		       enum btree_iter_update_trigger_flags);

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

int bch2_dirent_lookup_trans(struct btree_trans *, struct btree_iter *,
			       subvol_inum, const struct bch_hash_info *,
			       const struct qstr *, subvol_inum *, unsigned);
u64 bch2_dirent_lookup(struct bch_fs *, subvol_inum,
		       const struct bch_hash_info *,
		       const struct qstr *, subvol_inum *);

int bch2_empty_dir_snapshot(struct btree_trans *, u64, u32, u32);
int bch2_empty_dir_trans(struct btree_trans *, subvol_inum);
int bch2_readdir(struct bch_fs *, subvol_inum, struct bch_hash_info *, struct dir_context *);

int bch2_fsck_remove_dirent(struct btree_trans *, struct bpos);

#endif /* _BCACHEFS_DIRENT_H */
