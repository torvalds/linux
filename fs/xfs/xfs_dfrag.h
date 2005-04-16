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
#ifndef __XFS_DFRAG_H__
#define	__XFS_DFRAG_H__

/*
 * Structure passed to xfs_swapext
 */

typedef struct xfs_swapext
{
	__int64_t	sx_version;	/* version */
	__int64_t	sx_fdtarget;	/* fd of target file */
	__int64_t	sx_fdtmp;	/* fd of tmp file */
	xfs_off_t	sx_offset;	/* offset into file */
	xfs_off_t	sx_length;	/* leng from offset */
	char		sx_pad[16];	/* pad space, unused */
	xfs_bstat_t	sx_stat;	/* stat of target b4 copy */
} xfs_swapext_t;

/*
 * Version flag
 */
#define XFS_SX_VERSION		0

#ifdef __KERNEL__
/*
 * Prototypes for visible xfs_dfrag.c routines.
 */

/*
 * Syscall interface for xfs_swapext
 */
int	xfs_swapext(struct xfs_swapext __user *sx);

#endif	/* __KERNEL__ */

#endif	/* __XFS_DFRAG_H__ */
