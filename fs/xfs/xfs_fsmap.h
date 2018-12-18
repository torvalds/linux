// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_FSMAP_H__
#define __XFS_FSMAP_H__

struct fsmap;

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

void xfs_fsmap_from_internal(struct fsmap *dest, struct xfs_fsmap *src);
void xfs_fsmap_to_internal(struct xfs_fsmap *dest, struct fsmap *src);

/* fsmap to userspace formatter - copy to user & advance pointer */
typedef int (*xfs_fsmap_format_t)(struct xfs_fsmap *, void *);

int xfs_getfsmap(struct xfs_mount *mp, struct xfs_fsmap_head *head,
		xfs_fsmap_format_t formatter, void *arg);

#endif /* __XFS_FSMAP_H__ */
