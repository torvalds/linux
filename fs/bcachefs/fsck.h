/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FSCK_H
#define _BCACHEFS_FSCK_H

#include "str_hash.h"

int bch2_fsck_update_backpointers(struct btree_trans *,
				  struct snapshots_seen *,
				  const struct bch_hash_desc,
				  struct bch_hash_info *,
				  struct bkey_i *);

int bch2_check_inodes(struct bch_fs *);
int bch2_check_extents(struct bch_fs *);
int bch2_check_indirect_extents(struct bch_fs *);
int bch2_check_dirents(struct bch_fs *);
int bch2_check_xattrs(struct bch_fs *);
int bch2_check_root(struct bch_fs *);
int bch2_check_subvolume_structure(struct bch_fs *);
int bch2_check_unreachable_inodes(struct bch_fs *);
int bch2_check_directory_structure(struct bch_fs *);
int bch2_check_nlinks(struct bch_fs *);
int bch2_fix_reflink_p(struct bch_fs *);

long bch2_ioctl_fsck_offline(struct bch_ioctl_fsck_offline __user *);
long bch2_ioctl_fsck_online(struct bch_fs *, struct bch_ioctl_fsck_online);

#endif /* _BCACHEFS_FSCK_H */
