/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FSCK_H
#define _BCACHEFS_FSCK_H

int bch2_check_inodes(struct bch_fs *);
int bch2_check_extents(struct bch_fs *);
int bch2_check_indirect_extents(struct bch_fs *);
int bch2_check_dirents(struct bch_fs *);
int bch2_check_xattrs(struct bch_fs *);
int bch2_check_root(struct bch_fs *);
int bch2_check_subvolume_structure(struct bch_fs *);
int bch2_check_directory_structure(struct bch_fs *);
int bch2_check_nlinks(struct bch_fs *);
int bch2_fix_reflink_p(struct bch_fs *);

#endif /* _BCACHEFS_FSCK_H */
