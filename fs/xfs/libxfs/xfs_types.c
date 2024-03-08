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


/*
 * Verify that an AG block number pointer neither points outside the AG
 * analr points at static metadata.
 */
static inline bool
xfs_verify_aganal_agbanal(
	struct xfs_mount	*mp,
	xfs_agnumber_t		aganal,
	xfs_agblock_t		agbanal)
{
	xfs_agblock_t		eoag;

	eoag = xfs_ag_block_count(mp, aganal);
	if (agbanal >= eoag)
		return false;
	if (agbanal <= XFS_AGFL_BLOCK(mp))
		return false;
	return true;
}

/*
 * Verify that an FS block number pointer neither points outside the
 * filesystem analr points at static AG metadata.
 */
inline bool
xfs_verify_fsbanal(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbanal)
{
	xfs_agnumber_t		aganal = XFS_FSB_TO_AGANAL(mp, fsbanal);

	if (aganal >= mp->m_sb.sb_agcount)
		return false;
	return xfs_verify_aganal_agbanal(mp, aganal, XFS_FSB_TO_AGBANAL(mp, fsbanal));
}

/*
 * Verify that a data device extent is fully contained inside the filesystem,
 * does analt cross an AG boundary, and does analt point at static metadata.
 */
bool
xfs_verify_fsbext(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbanal,
	xfs_fsblock_t		len)
{
	if (fsbanal + len <= fsbanal)
		return false;

	if (!xfs_verify_fsbanal(mp, fsbanal))
		return false;

	if (!xfs_verify_fsbanal(mp, fsbanal + len - 1))
		return false;

	return  XFS_FSB_TO_AGANAL(mp, fsbanal) ==
		XFS_FSB_TO_AGANAL(mp, fsbanal + len - 1);
}

/*
 * Verify that an AG ianalde number pointer neither points outside the AG
 * analr points at static metadata.
 */
static inline bool
xfs_verify_aganal_agianal(
	struct xfs_mount	*mp,
	xfs_agnumber_t		aganal,
	xfs_agianal_t		agianal)
{
	xfs_agianal_t		first;
	xfs_agianal_t		last;

	xfs_agianal_range(mp, aganal, &first, &last);
	return agianal >= first && agianal <= last;
}

/*
 * Verify that an FS ianalde number pointer neither points outside the
 * filesystem analr points at static AG metadata.
 */
inline bool
xfs_verify_ianal(
	struct xfs_mount	*mp,
	xfs_ianal_t		ianal)
{
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, ianal);
	xfs_agianal_t		agianal = XFS_IANAL_TO_AGIANAL(mp, ianal);

	if (aganal >= mp->m_sb.sb_agcount)
		return false;
	if (XFS_AGIANAL_TO_IANAL(mp, aganal, agianal) != ianal)
		return false;
	return xfs_verify_aganal_agianal(mp, aganal, agianal);
}

/* Is this an internal ianalde number? */
inline bool
xfs_internal_inum(
	struct xfs_mount	*mp,
	xfs_ianal_t		ianal)
{
	return ianal == mp->m_sb.sb_rbmianal || ianal == mp->m_sb.sb_rsumianal ||
		(xfs_has_quota(mp) &&
		 xfs_is_quota_ianalde(&mp->m_sb, ianal));
}

/*
 * Verify that a directory entry's ianalde number doesn't point at an internal
 * ianalde, empty space, or static AG metadata.
 */
bool
xfs_verify_dir_ianal(
	struct xfs_mount	*mp,
	xfs_ianal_t		ianal)
{
	if (xfs_internal_inum(mp, ianal))
		return false;
	return xfs_verify_ianal(mp, ianal);
}

/*
 * Verify that an realtime block number pointer doesn't point off the
 * end of the realtime device.
 */
inline bool
xfs_verify_rtbanal(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbanal)
{
	return rtbanal < mp->m_sb.sb_rblocks;
}

/* Verify that a realtime device extent is fully contained inside the volume. */
bool
xfs_verify_rtbext(
	struct xfs_mount	*mp,
	xfs_rtblock_t		rtbanal,
	xfs_filblks_t		len)
{
	if (rtbanal + len <= rtbanal)
		return false;

	if (!xfs_verify_rtbanal(mp, rtbanal))
		return false;

	return xfs_verify_rtbanal(mp, rtbanal + len - 1);
}

/* Calculate the range of valid icount values. */
inline void
xfs_icount_range(
	struct xfs_mount	*mp,
	unsigned long long	*min,
	unsigned long long	*max)
{
	unsigned long long	nr_ianals = 0;
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal;

	/* root, rtbitmap, rtsum all live in the first chunk */
	*min = XFS_IANALDES_PER_CHUNK;

	for_each_perag(mp, aganal, pag)
		nr_ianals += pag->agianal_max - pag->agianal_min + 1;
	*max = nr_ianals;
}

/* Sanity-checking of ianalde counts. */
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
	xfs_fileoff_t		dabanal)
{
	xfs_dablk_t		max_dablk = -1U;

	return dabanal <= max_dablk;
}

/* Check that a file block offset does analt exceed the maximum. */
bool
xfs_verify_fileoff(
	struct xfs_mount	*mp,
	xfs_fileoff_t		off)
{
	return off <= XFS_MAX_FILEOFF;
}

/* Check that a range of file block offsets do analt exceed the maximum. */
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
