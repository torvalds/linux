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
#ifndef __XFS_MACROS_H__
#define	__XFS_MACROS_H__

/*
 * Set for debug kernels and simulation
 * These replacements save space.
 * Used in xfs_macros.c.
 */
#define	XFS_WANT_SPACE_C	\
	(!defined(_STANDALONE) && defined(DEBUG))

/*
 * Set for debug simulation and kernel builds, but not for standalone.
 * These replacements do not save space.
 * Used in xfs_macros.c.
 */
#define	XFS_WANT_FUNCS_C	\
	(!defined(_STANDALONE) && defined(DEBUG))

/*
 * Corresponding names used in .h files.
 */
#define	XFS_WANT_SPACE	(XFS_WANT_SPACE_C && !defined(XFS_MACRO_C))
#define	XFS_WANT_FUNCS	(XFS_WANT_FUNCS_C && !defined(XFS_MACRO_C))

/*
 * These are the macros that get turned into functions to save space.
 */
#define	XFSSO_NULLSTARTBLOCK 1
#define	XFSSO_XFS_AGB_TO_DADDR 1
#define XFSSO_XFS_AGB_TO_FSB 1
#define	XFSSO_XFS_AGINO_TO_INO 1
#define	XFSSO_XFS_ALLOC_BLOCK_MINRECS 1
#define	XFSSO_XFS_ATTR_SF_NEXTENTRY 1
#define	XFSSO_XFS_BMAP_BLOCK_DMAXRECS 1
#define	XFSSO_XFS_BMAP_BLOCK_IMAXRECS 1
#define	XFSSO_XFS_BMAP_BLOCK_IMINRECS 1
#define	XFSSO_XFS_BMAP_INIT 1
#define	XFSSO_XFS_BMAP_PTR_IADDR 1
#define	XFSSO_XFS_BMAP_SANITY_CHECK 1
#define	XFSSO_XFS_BMAPI_AFLAG 1
#define	XFSSO_XFS_CFORK_SIZE 1
#define	XFSSO_XFS_DA_COOKIE_BNO 1
#define	XFSSO_XFS_DA_COOKIE_ENTRY 1
#define	XFSSO_XFS_DADDR_TO_AGBNO 1
#define	XFSSO_XFS_DADDR_TO_FSB 1
#define	XFSSO_XFS_DFORK_PTR 1
#define	XFSSO_XFS_DIR_SF_GET_DIRINO 1
#define	XFSSO_XFS_DIR_SF_NEXTENTRY 1
#define	XFSSO_XFS_DIR_SF_PUT_DIRINO 1
#define	XFSSO_XFS_FILBLKS_MIN 1
#define	XFSSO_XFS_FSB_SANITY_CHECK 1
#define	XFSSO_XFS_FSB_TO_DADDR 1
#define	XFSSO_XFS_FSB_TO_DB 1
#define	XFSSO_XFS_IALLOC_INODES 1
#define	XFSSO_XFS_IFORK_ASIZE 1
#define	XFSSO_XFS_IFORK_DSIZE 1
#define	XFSSO_XFS_IFORK_FORMAT 1
#define	XFSSO_XFS_IFORK_NEXT_SET 1
#define	XFSSO_XFS_IFORK_NEXTENTS 1
#define	XFSSO_XFS_IFORK_PTR 1
#define	XFSSO_XFS_ILOG_FBROOT 1
#define	XFSSO_XFS_ILOG_FEXT 1
#define	XFSSO_XFS_INO_MASK 1
#define	XFSSO_XFS_INO_TO_FSB 1
#define	XFSSO_XFS_INODE_CLEAR_READ_AHEAD 1
#define	XFSSO_XFS_MIN_FREELIST 1
#define XFSSO_XFS_SB_GOOD_VERSION 1
#define XFSSO_XFS_SB_VERSION_HASNLINK 1
#define	XFSSO_XLOG_GRANT_ADD_SPACE 1
#define	XFSSO_XLOG_GRANT_SUB_SPACE 1

#endif	/* __XFS_MACROS_H__ */
