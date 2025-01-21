// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (C) 2017 Oracle.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtgroup.h"


/*
 * Verify that an AG block number pointer neither points outside the AG
 * nor points at static metadata.
 */
static inline bool
xfs_verify_agno_agbno(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno)
{
	xfs_agblock_t		eoag;

	eoag = xfs_ag_block_count(mp, agno);
	if (agbno >= eoag)
		return false;
	if (agbno <= XFS_AGFL_BLOCK(mp))
		return false;
	return true;
}

/*
 * Verify that an FS block number pointer neither points outside the
 * filesystem nor points at static AG metadata.
 */
inline bool
xfs_verify_fsbno(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno)
{
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(mp, fsbno);

	if (agno >= mp->m_sb.sb_agcount)
		return false;
	return xfs_verify_agno_agbno(mp, agno, XFS_FSB_TO_AGBNO(mp, fsbno));
}

/*
 * Verify that a data device extent is fully contained inside the filesystem,
 * does not cross an AG boundary, and does not point at static metadata.
 */
bool
xfs_verify_fsbext(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	xfs_fsblock_t		len)
{
	if (fsbno + len <= fsbno)
		return false;

	if (!xfs_verify_fsbno(mp, fsbno))
		return false;

	if (!xfs_verify_fsbno(mp, fsbno + len - 1))
		return false;

	return  XFS_FSB_TO_AGNO(mp, fsbno) ==
		XFS_FSB_TO_AGNO(mp, fsbno + len - 1);
}

/*
 * Verify that an AG inode number pointer neither points outside the AG
 * nor points at static metadata.
 */
static inline bool
xfs_verify_agno_agino(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agino_t		agino)
{
	xfs_agino_t		first;
	xfs_agino_t		last;

	xfs_agino_range(mp, agno, &first, &last);
	return agino >= first && agino <= last;
}

/*
 * Verify that an FS inode number pointer neither points outside the
 * filesystem nor points at static AG metadata.
 */
inline bool
xfs_verify_ino(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, ino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ino);

	if (agno >= mp->m_sb.sb_agcount)
		return false;
	if (XFS_AGINO_TO_INO(mp, agno, agino) != ino)
		return false;
	return xfs_verify_agno_agino(mp, agno, agino);
}

/* Is this an internal inode number? */
inline bool
xfs_is_sb_inum(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	return ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino ||
		(xfs_has_quota(mp) &&
		 xfs_is_quota_inode(&mp->m_sb, ino));
}

/*
 * Verify that a directory entry's inode number doesn't point at an internal
 * inode, empty space, or static AG metadata.
 */
bool
xfs_verify_dir_ino(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	if (xfs_is_sb_inum(mp, ino))
		return false;
	return xfs_verify_ino(mp, ino);
}

/*
 * Verify that a realtime block number pointer neither points outside the
 * allocatable areas of the rtgroup nor off the end of the realtime
 * device.
 */
inline bool
xfs_verify_rtbno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	if (xfs_has_rtgroups(mp)) {
		xfs_rgnumber_t	rgno = xfs_rtb_to_rgno(mp, rtbno);
		xfs_rtxnum_t	rtx = xfs_rtb_to_rtx(mp, rtbno);

		if (rgno >= mp->m_sb.sb_rgcount)
			return false;
		if (rtx >= xfs_rtgroup_extents(mp, rgno))
			return false;
		if (xfs_has_rtsb(mp) && rgno == 0 && rtx == 0)
			return false;
		return true;
	}

	return rtbno < mp->m_sb.sb_rblocks;
}

/*
 * Verify that an allocated realtime device extent neither points outside
 * allocatable areas of the rtgroup, across an rtgroup boundary, nor off the
 * end of the realtime device.
 */
bool
xfs_verify_rtbext(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno,
	xfs_filblks_t		len)
{
	if (rtbno + len <= rtbno)
		return false;

	if (!xfs_verify_rtbno(mp, rtbno))
		return false;

	if (!xfs_verify_rtbno(mp, rtbno + len - 1))
		return false;

	if (xfs_has_rtgroups(mp) &&
	    xfs_rtb_to_rgno(mp, rtbno) != xfs_rtb_to_rgno(mp, rtbno + len - 1))
		return false;

	return true;
}

/* Calculate the range of valid icount values. */
inline void
xfs_icount_range(
	struct xfs_mount	*mp,
	unsigned long long	*min,
	unsigned long long	*max)
{
	unsigned long long	nr_inos = 0;
	struct xfs_perag	*pag = NULL;

	/* root, rtbitmap, rtsum all live in the first chunk */
	*min = XFS_INODES_PER_CHUNK;

	while ((pag = xfs_perag_next(mp, pag)))
		nr_inos += pag->agino_max - pag->agino_min + 1;
	*max = nr_inos;
}

/* Sanity-checking of inode counts. */
bool
xfs_verify_icount(
	struct xfs_mount	*mp,
	unsigned long long	icount)
{
	unsigned long long	min, max;

	xfs_icount_range(mp, &min, &max);
	return icount >= min && icount <= max;
}

/* Sanity-checking of dir/attr block offsets. */
bool
xfs_verify_dablk(
	struct xfs_mount	*mp,
	xfs_fileoff_t		dabno)
{
	xfs_dablk_t		max_dablk = -1U;

	return dabno <= max_dablk;
}

/* Check that a file block offset does not exceed the maximum. */
bool
xfs_verify_fileoff(
	struct xfs_mount	*mp,
	xfs_fileoff_t		off)
{
	return off <= XFS_MAX_FILEOFF;
}

/* Check that a range of file block offsets do not exceed the maximum. */
bool
xfs_verify_fileext(
	struct xfs_mount	*mp,
	xfs_fileoff_t		off,
	xfs_fileoff_t		len)
{
	if (off + len <= off)
		return false;

	if (!xfs_verify_fileoff(mp, off))
		return false;

	return xfs_verify_fileoff(mp, off + len - 1);
}
