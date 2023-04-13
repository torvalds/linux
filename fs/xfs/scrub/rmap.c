// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_ag.h"

/*
 * Set us up to scrub reverse mapping btrees.
 */
int
xchk_setup_ag_rmapbt(
	struct xfs_scrub	*sc)
{
	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);

	return xchk_setup_ag_btree(sc, false);
}

/* Reverse-mapping scrubber. */

/* Cross-reference a rmap against the refcount btree. */
STATIC void
xchk_rmapbt_xref_refc(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	bool			non_inode;
	bool			is_bmbt;
	bool			is_attr;
	bool			is_unwritten;
	int			error;

	if (!sc->sa.refc_cur || xchk_skip_xref(sc->sm))
		return;

	non_inode = XFS_RMAP_NON_INODE_OWNER(irec->rm_owner);
	is_bmbt = irec->rm_flags & XFS_RMAP_BMBT_BLOCK;
	is_attr = irec->rm_flags & XFS_RMAP_ATTR_FORK;
	is_unwritten = irec->rm_flags & XFS_RMAP_UNWRITTEN;

	/* If this is shared, must be a data fork extent. */
	error = xfs_refcount_find_shared(sc->sa.refc_cur, irec->rm_startblock,
			irec->rm_blockcount, &fbno, &flen, false);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.refc_cur))
		return;
	if (flen != 0 && (non_inode || is_attr || is_bmbt || is_unwritten))
		xchk_btree_xref_set_corrupt(sc, sc->sa.refc_cur, 0);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_rmapbt_xref(
	struct xfs_scrub	*sc,
	struct xfs_rmap_irec	*irec)
{
	xfs_agblock_t		agbno = irec->rm_startblock;
	xfs_extlen_t		len = irec->rm_blockcount;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_space(sc, agbno, len);
	if (irec->rm_owner == XFS_RMAP_OWN_INODES)
		xchk_xref_is_inode_chunk(sc, agbno, len);
	else
		xchk_xref_is_not_inode_chunk(sc, agbno, len);
	if (irec->rm_owner == XFS_RMAP_OWN_COW)
		xchk_xref_is_cow_staging(sc, irec->rm_startblock,
				irec->rm_blockcount);
	else
		xchk_rmapbt_xref_refc(sc, irec);
}

/*
 * Check for bogus UNWRITTEN flags in the rmapbt node block keys.
 *
 * In reverse mapping records, the file mapping extent state
 * (XFS_RMAP_OFF_UNWRITTEN) is a record attribute, not a key field.  It is not
 * involved in lookups in any way.  In older kernels, the functions that
 * convert rmapbt records to keys forgot to filter out the extent state bit,
 * even though the key comparison functions have filtered the flag correctly.
 * If we spot an rmap key with the unwritten bit set in rm_offset, we should
 * mark the btree as needing optimization to rebuild the btree without those
 * flags.
 */
STATIC void
xchk_rmapbt_check_unwritten_in_keyflags(
	struct xchk_btree	*bs)
{
	struct xfs_scrub	*sc = bs->sc;
	struct xfs_btree_cur	*cur = bs->cur;
	struct xfs_btree_block	*keyblock;
	union xfs_btree_key	*lkey, *hkey;
	__be64			badflag = cpu_to_be64(XFS_RMAP_OFF_UNWRITTEN);
	unsigned int		level;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_PREEN)
		return;

	for (level = 1; level < cur->bc_nlevels; level++) {
		struct xfs_buf	*bp;
		unsigned int	ptr;

		/* Only check the first time we've seen this node block. */
		if (cur->bc_levels[level].ptr > 1)
			continue;

		keyblock = xfs_btree_get_block(cur, level, &bp);
		for (ptr = 1; ptr <= be16_to_cpu(keyblock->bb_numrecs); ptr++) {
			lkey = xfs_btree_key_addr(cur, ptr, keyblock);

			if (lkey->rmap.rm_offset & badflag) {
				xchk_btree_set_preen(sc, cur, level);
				break;
			}

			hkey = xfs_btree_high_key_addr(cur, ptr, keyblock);
			if (hkey->rmap.rm_offset & badflag) {
				xchk_btree_set_preen(sc, cur, level);
				break;
			}
		}
	}
}

/* Scrub an rmapbt record. */
STATIC int
xchk_rmapbt_rec(
	struct xchk_btree	*bs,
	const union xfs_btree_rec *rec)
{
	struct xfs_rmap_irec	irec;

	if (xfs_rmap_btrec_to_irec(rec, &irec) != NULL ||
	    xfs_rmap_check_irec(bs->cur, &irec) != NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	xchk_rmapbt_check_unwritten_in_keyflags(bs);
	xchk_rmapbt_xref(bs->sc, &irec);
	return 0;
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
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	bool				should_have_rmap)
{
	bool				has_rmap;
	int				error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_record_exists(sc->sa.rmap_cur, bno, len, oinfo,
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
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	xchk_xref_check_owner(sc, bno, len, oinfo, true);
}

/* xref check that the extent is not owned by a given owner */
void
xchk_xref_is_not_owned_by(
	struct xfs_scrub		*sc,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	xchk_xref_check_owner(sc, bno, len, oinfo, false);
}

/* xref check that the extent has no reverse mapping at all */
void
xchk_xref_has_no_owner(
	struct xfs_scrub	*sc,
	xfs_agblock_t		bno,
	xfs_extlen_t		len)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	error = xfs_rmap_has_records(sc->sa.rmap_cur, bno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}
