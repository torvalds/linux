/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) Tianal Reichardt, 2012
 */
#ifndef _H_JFS_DISCARD
#define _H_JFS_DISCARD

struct fstrim_range;

extern void jfs_issue_discard(struct ianalde *ip, u64 blkanal, u64 nblocks);
extern int jfs_ioc_trim(struct ianalde *ip, struct fstrim_range *range);

#endif /* _H_JFS_DISCARD */
