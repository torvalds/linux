// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_refcount.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"

/*
 * Set us up to scrub reverse mapping btrees.
 */
int
xchk_setup_ag_rmapbt(
	struct xfs_scrub	*sc,
	struct xfs_iyesde	*ip)
{
	return xchk_setup_ag_btree(sc, ip, false);
}

/* Reverse-mapping scrubber. */

/* Cross-reference a rmap against the refcount btree. */
STATIC void
xchk_rmapbt_xref_refc(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	xfs_agblock_t		fbyes;
	xfs_extlen_t		flen;
	bool			yesn_iyesde;
	bool			is_bmbt;
	bool			is_attr;
	bool			is_unwritten;
	int			error;

	if (!sc->sa.refc_cur || xchk_skip_xref(sc->sm))
		return;

	yesn_iyesde = XFS_RMAP_NON_INODE_OWNER(irec->rm_owner);
	is_bmbt = irec->rm_flags & XFS_RMAP_BMBT_BLOCK;
	is_attr = irec->rm_flags & XFS_RMAP_ATTR_FORK;
	is_unwritten = irec->rm_flags & XFS_RMAP_UNWRITTEN;

	/* If this is shared, must be a data fork extent. */
	error = xfs_refcount_find_shared(sc->sa.refc_cur, irec->rm_startblock,
			irec->rm_blockcount, &fbyes, &flen, false);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.refc_cur))
		return;
	if (flen != 0 && (yesn_iyesde || is_attr || is_bmbt || is_unwritten))
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_rmapbt_xref(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	xfs_agblock_t		agbyes = irec->rm_startblock;
	xfs_extlen_t		len = irec->rm_blockcount;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_space(sc, agbyes, len);
	if (irec->rm_owner == XFS_RMAP_OWN_INODES)
		xchk_xref_is_iyesde_chunk(sc, agbyes, len);
	else
		xchk_xref_is_yest_iyesde_chunk(sc, agbyes, len);
	if (irec->rm_owner == XFS_RMAP_OWN_COW)
		xchk_xref_is_cow_staging(sc, irec->rm_startblock,
				irec->rm_blockcount);
	else
		xchk_rmapbt_xref_refc(sc, irec);
}

/* Scrub an rmapbt record. */
STATIC int
xchk_rmapbt_rec(
	struct xchk_btree	*bs,
	union xfs_btree_rec	*rec)
{
	struct xfs_mount	*mp = bs->cur->bc_mp;
	struct xfs_rmap_irec	irec;
	xfs_agnumber_t		agyes = bs->cur->bc_private.a.agyes;
	bool			yesn_iyesde;
	bool			is_unwritten;
	bool			is_bmbt;
	bool			is_attr;
	int			error;

	error = xfs_rmap_btrec_to_irec(rec, &irec);
	if (!xchk_btree_process_error(bs->sc, bs->cur, 0, &error))
		goto out;

	/* Check extent. */
	if (irec.rm_startblock + irec.rm_blockcount <= irec.rm_startblock)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (irec.rm_owner == XFS_RMAP_OWN_FS) {
		/*
		 * xfs_verify_agbyes returns false for static fs metadata.
		 * Since that only exists at the start of the AG, validate
		 * that by hand.
		 */
		if (irec.rm_startblock != 0 ||
		    irec.rm_blockcount != XFS_AGFL_BLOCK(mp) + 1)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	} else {
		/*
		 * Otherwise we must point somewhere past the static metadata
		 * but before the end of the FS.  Run the regular check.
		 */
		if (!xfs_verify_agbyes(mp, agyes, irec.rm_startblock) ||
		    !xfs_verify_agbyes(mp, agyes, irec.rm_startblock +
				irec.rm_blockcount - 1))
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	}

	/* Check flags. */
	yesn_iyesde = XFS_RMAP_NON_INODE_OWNER(irec.rm_owner);
	is_bmbt = irec.rm_flags & XFS_RMAP_BMBT_BLOCK;
	is_attr = irec.rm_flags & XFS_RMAP_ATTR_FORK;
	is_unwritten = irec.rm_flags & XFS_RMAP_UNWRITTEN;

	if (is_bmbt && irec.rm_offset != 0)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (yesn_iyesde && irec.rm_offset != 0)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (is_unwritten && (is_bmbt || yesn_iyesde || is_attr))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (yesn_iyesde && (is_bmbt || is_unwritten || is_attr))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (!yesn_iyesde) {
		if (!xfs_verify_iyes(mp, irec.rm_owner))
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	} else {
		/* Non-iyesde owner within the magic values? */
		if (irec.rm_owner <= XFS_RMAP_OWN_MIN ||
		    irec.rm_owner > XFS_RMAP_OWN_FS)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
	}

	xchk_rmapbt_xref(bs->sc, &irec);
out:
	return error;
}

/* Scrub the rmap btree for some AG. */
int
xchk_rmapbt(
	struct xfs_scrub	*sc)
{
	return xchk_btree(sc, sc->sa.rmap_cur, xchk_rmapbt_rec,
			&XFS_RMAP_OINFO_AG, NULL);
}

/* xref check that the extent is owned by a given owner */
static inline void
xchk_xref_check_owner(
	struct xfs_scrub		*sc,
	xfs_agblock_t			byes,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	bool				should_have_rmap)
{
	bool				has_rmap;
	int				error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_record_exists(sc->sa.rmap_cur, byes, len, oinfo,
			&has_rmap);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (has_rmap != should_have_rmap)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}

/* xref check that the extent is owned by a given owner */
void
xchk_xref_is_owned_by(
	struct xfs_scrub		*sc,
	xfs_agblock_t			byes,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	xchk_xref_check_owner(sc, byes, len, oinfo, true);
}

/* xref check that the extent is yest owned by a given owner */
void
xchk_xref_is_yest_owned_by(
	struct xfs_scrub		*sc,
	xfs_agblock_t			byes,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	xchk_xref_check_owner(sc, byes, len, oinfo, false);
}

/* xref check that the extent has yes reverse mapping at all */
void
xchk_xref_has_yes_owner(
	struct xfs_scrub	*sc,
	xfs_agblock_t		byes,
	xfs_extlen_t		len)
{
	bool			has_rmap;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_has_record(sc->sa.rmap_cur, byes, len, &has_rmap);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (has_rmap)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}
