/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_IO_MISC_H
#define _BCACHEFS_IO_MISC_H

int bch2_extent_fallocate(struct btree_trans *, subvol_inum, struct btree_iter *,
			  u64, struct bch_io_opts, s64 *,
			  struct write_point_specifier);
int bch2_fpunch_at(struct btree_trans *, struct btree_iter *,
		   subvol_inum, u64, s64 *);
int bch2_fpunch(struct bch_fs *c, subvol_inum, u64, u64, s64 *);

void bch2_logged_op_truncate_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_logged_op_truncate ((struct bkey_ops) {	\
	.val_to_text	= bch2_logged_op_truncate_to_text,	\
	.min_val_size	= 24,					\
})

int bch2_resume_logged_op_truncate(struct btree_trans *, struct bkey_i *);

int bch2_truncate(struct bch_fs *, subvol_inum, u64, u64 *);

void bch2_logged_op_finsert_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_logged_op_finsert ((struct bkey_ops) {	\
	.val_to_text	= bch2_logged_op_finsert_to_text,	\
	.min_val_size	= 24,					\
})

int bch2_resume_logged_op_finsert(struct btree_trans *, struct bkey_i *);

int bch2_fcollapse_finsert(struct bch_fs *, subvol_inum, u64, u64, bool, s64 *);

#endif /* _BCACHEFS_IO_MISC_H */
