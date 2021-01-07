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

/* Find the size of the AG, in blocks. */
xfs_agblock_t
xfs_ag_block_count(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	ASSERT(agno < mp->m_sb.sb_agcount);

	if (agno < mp->m_sb.sb_agcount - 1)
		return mp->m_sb.sb_agblocks;
	return mp->m_sb.sb_dblocks - (agno * mp->m_sb.sb_agblocks);
}

/*
 * Verify that an AG block number pointer neither points outside the AG
 * nor points at static metadata.
 */
bool
xfs_verify_agbno(
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
bool
xfs_verify_fsbno(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno)
{
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(mp, fsbno);

	if (agno >= mp->m_sb.sb_agcount)
		return false;
	return xfs_verify_agbno(mp, agno, XFS_FSB_TO_AGBNO(mp, fsbno));
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

/* Calculate the first and last possible inode number in an AG. */
void
xfs_agino_range(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agino_t		*first,
	xfs_agino_t		*last)
{
	xfs_agblock_t		bno;
	xfs_agblock_t		eoag;

	eoag = xfs_ag_block_count(mp, agno);

	/*
	 * Calculate the first inode, which will be in the first
	 * cluster-aligned block after the AGFL.
	 */
	bno = round_up(XFS_AGFL_BLOCK(mp) + 1, M_IGEO(mp)->cluster_align);
	*first = XFS_AGB_TO_AGINO(mp, bno);

	/*
	 * Calculate the last inode, which will be at the end of the
	 * last (aligned) cluster that can be allocated in the AG.
	 */
	bno = round_down(eoag, M_IGEO(mp)->cluster_align);
	*last = XFS_AGB_TO_AGINO(mp, bno) - 1;
}

/*
 * Verify that an AG inode number pointer neither points outside the AG
 * nor points at static metadata.
 */
bool
xfs_verify_agino(
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
 * Verify that an AG inode number pointer neither points outside the AG
 * nor points at static metadata, or is NULLAGINO.
 */
bool
xfs_verify_agino_or_null(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agino_t		agino)
{
	return agino == NULLAGINO || xfs_verify_agino(mp, agno, agino);
}

/*
 * Verify that an FS inode number pointer neither points outside the
 * filesystem nor points at static AG metadata.
 */
bool
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
	return xfs_verify_agino(mp, agno, agino);
}

/* Is this an internal inode number? */
bool
xfs_internal_inum(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	return ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino ||
		(xfs_sb_version_hasquota(&mp->m_sb) &&
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
	if (xfs_internal_inum(mp, ino))
		return false;
	return xfs_verify_ino(mp, ino);
}

/*
 * Verify that an realtime block number pointer doesn't point off the
 * end of the realtime device.
 */
bool
xfs_verify_rtbno(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno)
{
	return rtbno < mp->m_sb.sb_rblocks;
}

/* Verify that a realtime device extent is fully contained inside the volume. */
bool
xfs_verify_rtext(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbno,
	xfs_rtblock_t		len)
{
	if (rtbno + len <= rtbno)
		return false;

	if (!xfs_verify_rtbno(mp, rtbno))
		return false;

	return xfs_verify_rtbno(mp, rtbno + len - 1);
}

/* Calculate the range of valid icount values. */
void
xfs_icount_range(
	struct xfs_mount	*mp,
	unsigned long long	*min,
	unsigned long long	*max)
{
	unsigned long long	nr_inos = 0;
	xfs_agnumber_t		agno;

	/* root, rtbitmap, rtsum all live in the first chunk */
	*min = XFS_INODES_PER_CHUNK;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		xfs_agino_t	first, last;

		xfs_agino_range(mp, agno, &first, &last);
		nr_inos += last - first + 1;
	}
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
