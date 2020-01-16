/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) Tiyes Reichardt, 2012
 */
#ifndef _H_JFS_DISCARD
#define _H_JFS_DISCARD

struct fstrim_range;

extern void jfs_issue_discard(struct iyesde *ip, u64 blkyes, u64 nblocks);
extern int jfs_ioc_trim(struct iyesde *ip, struct fstrim_range *range);

#endif /* _H_JFS_DISCARD */
