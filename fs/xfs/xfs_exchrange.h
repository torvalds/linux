/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_EXCHRANGE_H__
#define __XFS_EXCHRANGE_H__

/* Update the mtime/cmtime of file1 and file2 */
#define __XFS_EXCHANGE_RANGE_UPD_CMTIME1	(1ULL << 63)
#define __XFS_EXCHANGE_RANGE_UPD_CMTIME2	(1ULL << 62)

#define XFS_EXCHANGE_RANGE_PRIV_FLAGS	(__XFS_EXCHANGE_RANGE_UPD_CMTIME1 | \
					 __XFS_EXCHANGE_RANGE_UPD_CMTIME2)

struct xfs_exchrange {
	struct file		*file1;
	struct file		*file2;

	loff_t			file1_offset;
	loff_t			file2_offset;
	u64			length;

	u64			flags;	/* XFS_EXCHANGE_RANGE flags */
};

long xfs_ioc_exchange_range(struct file *file,
		struct xfs_exchange_range __user *argp);

struct xfs_exchmaps_req;

void xfs_exchrange_ilock(struct xfs_trans *tp, struct xfs_inode *ip1,
		struct xfs_inode *ip2);
void xfs_exchrange_iunlock(struct xfs_inode *ip1, struct xfs_inode *ip2);

int xfs_exchrange_estimate(struct xfs_exchmaps_req *req);

#endif /* __XFS_EXCHRANGE_H__ */
