// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 */
#ifndef __XFS_ITABLE_H__
#define	__XFS_ITABLE_H__

/*
 * xfs_bulkstat() is used to fill in xfs_bstat structures as well as dm_stat
 * structures (by the dmi library). This is a pointer to a formatter function
 * that will iget the inode and fill in the appropriate structure.
 * see xfs_bulkstat_one() and xfs_dm_bulkstat_one() in dmapi_xfs.c
 */
typedef int (*bulkstat_one_pf)(struct xfs_mount	*mp,
			       xfs_ino_t	ino,
			       void		__user *buffer,
			       int		ubsize,
			       int		*ubused,
			       int		*stat);

/*
 * Values for stat return value.
 */
#define BULKSTAT_RV_NOTHING	0
#define BULKSTAT_RV_DIDONE	1
#define BULKSTAT_RV_GIVEUP	2

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 */
int					/* error status */
xfs_bulkstat(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	bulkstat_one_pf formatter,	/* func that'd fill a single buf */
	size_t		statstruct_size,/* sizeof struct that we're filling */
	char		__user *ubuffer,/* buffer with inode stats */
	int		*done);		/* 1 if there are more stats to get */

typedef int (*bulkstat_one_fmt_pf)(  /* used size in bytes or negative error */
	void			__user *ubuffer, /* buffer to write to */
	int			ubsize,		 /* remaining user buffer sz */
	int			*ubused,	 /* bytes used by formatter */
	const xfs_bstat_t	*buffer);        /* buffer to read from */

int
xfs_bulkstat_one_int(
	xfs_mount_t		*mp,
	xfs_ino_t		ino,
	void			__user *buffer,
	int			ubsize,
	bulkstat_one_fmt_pf	formatter,
	int			*ubused,
	int			*stat);

int
xfs_bulkstat_one(
	xfs_mount_t		*mp,
	xfs_ino_t		ino,
	void			__user *buffer,
	int			ubsize,
	int			*ubused,
	int			*stat);

typedef int (*inumbers_fmt_pf)(
	void			__user *ubuffer, /* buffer to write to */
	const xfs_inogrp_t	*buffer,	/* buffer to read from */
	long			count,		/* # of elements to read */
	long			*written);	/* # of bytes written */

int
xfs_inumbers_fmt(
	void			__user *ubuffer, /* buffer to write to */
	const xfs_inogrp_t	*buffer,	/* buffer to read from */
	long			count,		/* # of elements to read */
	long			*written);	/* # of bytes written */

int					/* error status */
xfs_inumbers(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_ino_t		*last,	/* last inode returned */
	int			*count,	/* size of buffer/count returned */
	void			__user *buffer, /* buffer with inode info */
	inumbers_fmt_pf		formatter);

/* Temporarily needed while we refactor functions. */
struct xfs_btree_cur;
struct xfs_inobt_rec_incore;
void xfs_bulkstat_ichunk_ra(struct xfs_mount *mp, xfs_agnumber_t agno,
		struct xfs_inobt_rec_incore *irec);
int xfs_bulkstat_grab_ichunk(struct xfs_btree_cur *cur, xfs_agino_t agino,
		int *icount, struct xfs_inobt_rec_incore *irec);

#endif	/* __XFS_ITABLE_H__ */
