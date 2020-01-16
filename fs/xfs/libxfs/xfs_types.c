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
	xfs_agnumber_t		agyes)
{
	ASSERT(agyes < mp->m_sb.sb_agcount);

	if (agyes < mp->m_sb.sb_agcount - 1)
		return mp->m_sb.sb_agblocks;
	return mp->m_sb.sb_dblocks - (agyes * mp->m_sb.sb_agblocks);
}

/*
 * Verify that an AG block number pointer neither points outside the AG
 * yesr points at static metadata.
 */
bool
xfs_verify_agbyes(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agyes,
	xfs_agblock_t		agbyes)
{
	xfs_agblock_t		eoag;

	eoag = xfs_ag_block_count(mp, agyes);
	if (agbyes >= eoag)
		return false;
	if (agbyes <= XFS_AGFL_BLOCK(mp))
		return false;
	return true;
}

/*
 * Verify that an FS block number pointer neither points outside the
 * filesystem yesr points at static AG metadata.
 */
bool
xfs_verify_fsbyes(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbyes)
{
	xfs_agnumber_t		agyes = XFS_FSB_TO_AGNO(mp, fsbyes);

	if (agyes >= mp->m_sb.sb_agcount)
		return false;
	return xfs_verify_agbyes(mp, agyes, XFS_FSB_TO_AGBNO(mp, fsbyes));
}

/* Calculate the first and last possible iyesde number in an AG. */
void
xfs_agiyes_range(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agyes,
	xfs_agiyes_t		*first,
	xfs_agiyes_t		*last)
{
	xfs_agblock_t		byes;
	xfs_agblock_t		eoag;

	eoag = xfs_ag_block_count(mp, agyes);

	/*
	 * Calculate the first iyesde, which will be in the first
	 * cluster-aligned block after the AGFL.
	 */
	byes = round_up(XFS_AGFL_BLOCK(mp) + 1, M_IGEO(mp)->cluster_align);
	*first = XFS_AGB_TO_AGINO(mp, byes);

	/*
	 * Calculate the last iyesde, which will be at the end of the
	 * last (aligned) cluster that can be allocated in the AG.
	 */
	byes = round_down(eoag, M_IGEO(mp)->cluster_align);
	*last = XFS_AGB_TO_AGINO(mp, byes) - 1;
}

/*
 * Verify that an AG iyesde number pointer neither points outside the AG
 * yesr points at static metadata.
 */
bool
xfs_verify_agiyes(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agyes,
	xfs_agiyes_t		agiyes)
{
	xfs_agiyes_t		first;
	xfs_agiyes_t		last;

	xfs_agiyes_range(mp, agyes, &first, &last);
	return agiyes >= first && agiyes <= last;
}

/*
 * Verify that an AG iyesde number pointer neither points outside the AG
 * yesr points at static metadata, or is NULLAGINO.
 */
bool
xfs_verify_agiyes_or_null(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agyes,
	xfs_agiyes_t		agiyes)
{
	return agiyes == NULLAGINO || xfs_verify_agiyes(mp, agyes, agiyes);
}

/*
 * Verify that an FS iyesde number pointer neither points outside the
 * filesystem yesr points at static AG metadata.
 */
bool
xfs_verify_iyes(
	struct xfs_mount	*mp,
	xfs_iyes_t		iyes)
{
	xfs_agnumber_t		agyes = XFS_INO_TO_AGNO(mp, iyes);
	xfs_agiyes_t		agiyes = XFS_INO_TO_AGINO(mp, iyes);

	if (agyes >= mp->m_sb.sb_agcount)
		return false;
	if (XFS_AGINO_TO_INO(mp, agyes, agiyes) != iyes)
		return false;
	return xfs_verify_agiyes(mp, agyes, agiyes);
}

/* Is this an internal iyesde number? */
bool
xfs_internal_inum(
	struct xfs_mount	*mp,
	xfs_iyes_t		iyes)
{
	return iyes == mp->m_sb.sb_rbmiyes || iyes == mp->m_sb.sb_rsumiyes ||
		(xfs_sb_version_hasquota(&mp->m_sb) &&
		 xfs_is_quota_iyesde(&mp->m_sb, iyes));
}

/*
 * Verify that a directory entry's iyesde number doesn't point at an internal
 * iyesde, empty space, or static AG metadata.
 */
bool
xfs_verify_dir_iyes(
	struct xfs_mount	*mp,
	xfs_iyes_t		iyes)
{
	if (xfs_internal_inum(mp, iyes))
		return false;
	return xfs_verify_iyes(mp, iyes);
}

/*
 * Verify that an realtime block number pointer doesn't point off the
 * end of the realtime device.
 */
bool
xfs_verify_rtbyes(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbyes)
{
	return rtbyes < mp->m_sb.sb_rblocks;
}

/* Calculate the range of valid icount values. */
void
xfs_icount_range(
	struct xfs_mount	*mp,
	unsigned long long	*min,
	unsigned long long	*max)
{
	unsigned long long	nr_iyess = 0;
	xfs_agnumber_t		agyes;

	/* root, rtbitmap, rtsum all live in the first chunk */
	*min = XFS_INODES_PER_CHUNK;

	for (agyes = 0; agyes < mp->m_sb.sb_agcount; agyes++) {
		xfs_agiyes_t	first, last;

		xfs_agiyes_range(mp, agyes, &first, &last);
		nr_iyess += last - first + 1;
	}
	*max = nr_iyess;
}

/* Sanity-checking of iyesde counts. */
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
	xfs_fileoff_t		dabyes)
{
	xfs_dablk_t		max_dablk = -1U;

	return dabyes <= max_dablk;
}
