/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) Tino Reichardt, 2012
 */
#ifndef _H_JFS_DISCARD
#define _H_JFS_DISCARD

struct fstrim_range;

extern void jfs_issue_discard(struct inode *ip, u64 blkno, u64 nblocks);
extern int jfs_ioc_trim(struct inode *ip, struct fstrim_range *range);

#endif /* _H_JFS_DISCARD */
