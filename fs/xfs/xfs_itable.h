/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
			       void		*private_data,
			       xfs_daddr_t	bno,
			       int		*ubused,
			       void		*dip,
			       int		*stat);

/*
 * Values for stat return value.
 */
#define BULKSTAT_RV_NOTHING	0
#define BULKSTAT_RV_DIDONE	1
#define BULKSTAT_RV_GIVEUP	2

/*
 * Values for bulkstat flag argument.
 */
#define BULKSTAT_FG_IGET	0x1	/* Go through the buffer cache */
#define BULKSTAT_FG_QUICK	0x2	/* No iget, walk the dinode cluster */
#define BULKSTAT_FG_INLINE	0x4	/* No iget if inline attrs */

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 */
int					/* error status */
xfs_bulkstat(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	*lastino,	/* last inode returned */
	int		*count,		/* size of buffer/count returned */
	bulkstat_one_pf formatter,	/* func that'd fill a single buf */
	void		*private_data,	/* private data for formatter */
	size_t		statstruct_size,/* sizeof struct that we're filling */
	char		__user *ubuffer,/* buffer with inode stats */
	int		flags,		/* flag to control access method */
	int		*done);		/* 1 if there are more stats to get */

int
xfs_bulkstat_single(
	xfs_mount_t		*mp,
	xfs_ino_t		*lastinop,
	char			__user *buffer,
	int			*done);

int
xfs_bulkstat_one(
	xfs_mount_t		*mp,
	xfs_ino_t		ino,
	void			__user *buffer,
	int			ubsize,
	void			*private_data,
	xfs_daddr_t		bno,
	int			*ubused,
	void			*dibuff,
	int			*stat);

int
xfs_internal_inum(
	xfs_mount_t		*mp,
	xfs_ino_t		ino);

int					/* error status */
xfs_inumbers(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_ino_t		*last,	/* last inode returned */
	int			*count,	/* size of buffer/count returned */
	xfs_inogrp_t		__user *buffer);/* buffer with inode info */

#endif	/* __XFS_ITABLE_H__ */
