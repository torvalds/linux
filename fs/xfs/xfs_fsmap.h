// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_FSMAP_H__
#define __XFS_FSMAP_H__

struct fsmap;
struct fsmap_head;

/* internal fsmap representation */
struct xfs_fsmap {
	dev_t		fmr_device;	/* device id */
	uint32_t	fmr_flags;	/* mapping flags */
	uint64_t	fmr_physical;	/* device offset of segment */
	uint64_t	fmr_owner;	/* owner id */
	xfs_fileoff_t	fmr_offset;	/* file offset of segment */
	xfs_filblks_t	fmr_length;	/* length of segment, blocks */
};

struct xfs_fsmap_head {
	uint32_t	fmh_iflags;	/* control flags */
	uint32_t	fmh_oflags;	/* output flags */
	unsigned int	fmh_count;	/* # of entries in array incl. input */
	unsigned int	fmh_entries;	/* # of entries filled in (output). */

	struct xfs_fsmap fmh_keys[2];	/* low and high keys */
};

/* internal fsmap record format */
struct xfs_fsmap_irec {
	xfs_daddr_t	start_daddr;
	xfs_daddr_t	len_daddr;
	uint64_t	owner;		/* extent owner */
	uint64_t	offset;		/* offset within the owner */
	unsigned int	rm_flags;	/* rmap state flags */

	/*
	 * rmapbt startblock corresponding to start_daddr, if the record came
	 * from an rmap btree.
	 */
	xfs_agblock_t	rec_key;
};

int xfs_ioc_getfsmap(struct xfs_inode *ip, struct fsmap_head __user *arg);

#endif /* __XFS_FSMAP_H__ */
