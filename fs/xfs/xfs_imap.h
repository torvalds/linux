/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_IMAP_H__
#define	__XFS_IMAP_H__

/*
 * This is the structure passed to xfs_imap() to map
 * an inode number to its on disk location.
 */
typedef struct xfs_imap {
	xfs_daddr_t	im_blkno;	/* starting BB of inode chunk */
	uint		im_len;		/* length in BBs of inode chunk */
	xfs_agblock_t	im_agblkno;	/* logical block of inode chunk in ag */
	ushort		im_ioffset;	/* inode offset in block in "inodes" */
	ushort		im_boffset;	/* inode offset in block in bytes */
} xfs_imap_t;

#ifdef __KERNEL__
struct xfs_mount;
struct xfs_trans;
int	xfs_imap(struct xfs_mount *, struct xfs_trans *, xfs_ino_t,
		 xfs_imap_t *, uint);
#endif

#endif	/* __XFS_IMAP_H__ */
