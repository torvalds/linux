/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FSCK_H
#define _BCACHEFS_FSCK_H

int bch2_fsck_full(struct bch_fs *);
int bch2_fsck_walk_inodes_only(struct bch_fs *);

#endif /* _BCACHEFS_FSCK_H */
