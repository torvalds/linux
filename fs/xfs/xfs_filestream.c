// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2006-2007 Silicon Graphics, Inc.
 * Copyright (c) 2014 Christoph Hellwig.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_alloc.h"
#include "xfs_mru_cache.h"
#include "xfs_trace.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_trans.h"
#include "xfs_filestream.h"

struct xfs_fstrm_item {
	struct xfs_mru_cache_elem	mru;
	xfs_agnumber_t			ag; /* AG in use for this directory */
};

enum xfs_fstrm_alloc {
	XFS_PICK_USERDATA = 1,
	XFS_PICK_LOWSPACE = 2,
};

/*
 * Allocation group filestream associations are tracked with per-ag atomic
 * counters.  These counters allow xfs_filestream_pick_ag() to tell whether a
 * particular AG already has active filestreams associated with it.
 */
int
xfs_filestream_peek_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_read(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static int
xfs_filestream_get_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;
	int		ret;

	pag = xfs_perag_get(mp, agno);
	ret = atomic_inc_return(&pag->pagf_fstrms);
	xfs_perag_put(pag);
	return ret;
}

static void
xfs_filestream_put_ag(
	xfs_mount_t	*mp,
	xfs_agnumber_t	agno)
{
	struct xfs_perag *pag;

	pag = xfs_perag_get(mp, agno);
	atomic_dec(&pag->pagf_fstrms);
	xfs_perag_put(pag);
}

static void
xfs_fstrm_free_func(
	void			*data,
	struct xfs_mru_cache_elem *mru)
{
	struct xfs_mount	*mp = data;
	struct xfs_fstrm_item	*item =
		container_of(mru, struct xfs_fstrm_item, mru);

	xfs_filestream_put_ag(mp, item->ag);
	trace_xfs_filestream_free(mp, mru->key, item->ag);

	kmem_free(item);
}

/*
 * Scan the AGs starting at startag looking for an AG that isn't in use and has
 * at least minlen blocks free.
 */
static int
xfs_filestream_pick_ag(
	struct xfs_inode	*ip,
	xfs_agnumber_t		*agp,
	int			flags,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_fstrm_item	*item;
	struct xfs_perag	*pag;
	xfs_extlen_t		minlen = *longest;
	xfs_extlen_t		free = 0, minfree, maxfree = 0;
	xfs_agnumber_t		startag = *agp;
	xfs_agnumber_t		ag = startag;
	xfs_agnumber_t		max_ag = NULLAGNUMBER;
	int			err, trylock, nscan;

	ASSERT(S_ISDIR(VFS_I(ip)->i_mode));

	/* 2% of an AG's blocks must be free for it to be chosen. */
	minfree = mp->m_sb.sb_agblocks / 50;

	*agp = NULLAGNUMBER;

	/* For the first pass, don't sleep trying to init the per-AG. */
	trylock = XFS_ALLOC_FLAG_TRYLOCK;

	for (nscan = 0; 1; nscan++) {
		trace_xfs_filestream_scan(mp, ip->i_ino, ag);

		pag = xfs_perag_get(mp, ag);
		*longest = 0;
		err = xfs_bmap_longest_free_extent(pag, NULL, longest);
		if (err) {
			xfs_perag_put(pag);
			if (err != -EAGAIN)
				return err;
			/* Couldn't lock the AGF, skip this AG. */
			goto next_ag;
		}

		/* Keep track of the AG with the most free blocks. */
		if (pag->pagf_freeblks > maxfree) {
			maxfree = pag->pagf_freeblks;
			max_ag = ag;
		}

		/*
		 * The AG reference count does two things: it enforces mutual
		 * exclusion when examining the suitability of an AG in this
		 * loop, and it guards against two filestreams being established
		 * in the same AG as each other.
		 */
		if (xfs_filestream_get_ag(mp, ag) > 1) {
			xfs_filestream_put_ag(mp, ag);
			goto next_ag;
		}

		if (((minlen && *longest >= minlen) ||
		     (!minlen && pag->pagf_freeblks >= minfree)) &&
		    (!xfs_perag_prefers_metadata(pag) ||
		     !(flags & XFS_PICK_USERDATA) ||
		     (flags & XFS_PICK_LOWSPACE))) {

			/* Break out, retaining the reference on the AG. */
			free = pag->pagf_freeblks;
			xfs_perag_put(pag);
			*agp = ag;
			break;
		}

		/* Drop the reference on this AG, it's not usable. */
		xfs_filestream_put_ag(mp, ag);
next_ag:
		xfs_perag_put(pag);
		/* Move to the next AG, wrapping to AG 0 if necessary. */
		if (++ag >= mp->m_sb.sb_agcount)
			ag = 0;

		/* If a full pass of the AGs hasn't been done yet, continue. */
		if (ag != startag)
			continue;

		/* Allow sleeping in xfs_alloc_read_agf() on the 2nd pass. */
		if (trylock != 0) {
			trylock = 0;
			continue;
		}

		/* Finally, if lowspace wasn't set, set it for the 3rd pass. */
		if (!(flags & XFS_PICK_LOWSPACE)) {
			flags |= XFS_PICK_LOWSPACE;
			continue;
		}

		/*
		 * Take the AG with the most free space, regardless of whether
		 * it's already in use by another filestream.
		 */
		if (max_ag != NULLAGNUMBER) {
			xfs_filestream_get_ag(mp, max_ag);
			free = maxfree;
			*agp = max_ag;
			break;
		}

		/* take AG 0 if none matched */
		trace_xfs_filestream_pick(ip, *agp, free, nscan);
		*agp = 0;
		return 0;
	}

	trace_xfs_filestream_pick(ip, *agp, free, nscan);

	if (*agp == NULLAGNUMBER)
		return 0;

	err = -ENOMEM;
	item = kmem_alloc(sizeof(*item), KM_MAYFAIL);
	if (!item)
		goto out_put_ag;

	item->ag = *agp;

	err = xfs_mru_cache_insert(mp->m_filestream, ip->i_ino, &item->mru);
	if (err) {
		if (err == -EEXIST)
			err = 0;
		goto out_free_item;
	}

	return 0;

out_free_item:
	kmem_free(item);
out_put_ag:
	xfs_filestream_put_ag(mp, *agp);
	return err;
}

static struct xfs_inode *
xfs_filestream_get_parent(
	struct xfs_inode	*ip)
{
	struct inode		*inode = VFS_I(ip), *dir = NULL;
	struct dentry		*dentry, *parent;

	dentry = d_find_alias(inode);
	if (!dentry)
		goto out;

	parent = dget_parent(dentry);
	if (!parent)
		goto out_dput;

	dir = igrab(d_inode(parent));
	dput(parent);

out_dput:
	dput(dentry);
out:
	return dir ? XFS_I(dir) : NULL;
}

/*
 * Search for an allocation group with a single extent large enough for
 * the request.  If one isn't found, then the largest available free extent is
 * returned as the best length possible.
 */
int
xfs_filestream_select_ag(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	xfs_extlen_t		*blen)
{
	struct xfs_mount	*mp = ap->ip->i_mount;
	struct xfs_perag	*pag;
	struct xfs_inode	*pip = NULL;
	xfs_agnumber_t		agno = NULLAGNUMBER;
	struct xfs_mru_cache_elem *mru;
	int			flags = 0;
	int			error;

	args->total = ap->total;
	*blen = 0;

	pip = xfs_filestream_get_parent(ap->ip);
	if (!pip) {
		agno = 0;
		goto out_select;
	}

	mru = xfs_mru_cache_lookup(mp->m_filestream, pip->i_ino);
	if (mru) {
		agno = container_of(mru, struct xfs_fstrm_item, mru)->ag;
		xfs_mru_cache_done(mp->m_filestream);
		mru = NULL;

		trace_xfs_filestream_lookup(mp, ap->ip->i_ino, agno);
		xfs_irele(pip);

		ap->blkno = XFS_AGB_TO_FSB(args->mp, agno, 0);
		xfs_bmap_adjacent(ap);

		pag = xfs_perag_grab(mp, agno);
		if (pag) {
			error = xfs_bmap_longest_free_extent(pag, args->tp, blen);
			xfs_perag_rele(pag);
			if (error) {
				if (error != -EAGAIN)
					goto out_error;
				*blen = 0;
			}
		}
		if (*blen >= args->maxlen)
			goto out_select;
	} else if (xfs_is_inode32(mp)) {
		xfs_agnumber_t	 rotorstep = xfs_rotorstep;
		agno = (mp->m_agfrotor / rotorstep) %
				mp->m_sb.sb_agcount;
		mp->m_agfrotor = (mp->m_agfrotor + 1) %
				 (mp->m_sb.sb_agcount * rotorstep);
	} else {
		agno = XFS_INO_TO_AGNO(mp, pip->i_ino);
	}

	/* Changing parent AG association now, so remove the existing one. */
	mru = xfs_mru_cache_remove(mp->m_filestream, pip->i_ino);
	if (mru) {
		struct xfs_fstrm_item *item =
			container_of(mru, struct xfs_fstrm_item, mru);
		agno = (item->ag + 1) % mp->m_sb.sb_agcount;
		xfs_fstrm_free_func(mp, mru);
	}
	ap->blkno = XFS_AGB_TO_FSB(args->mp, agno, 0);
	xfs_bmap_adjacent(ap);

	/*
	 * If there is very little free space before we start a filestreams
	 * allocation, we're almost guaranteed to fail to find a better AG with
	 * larger free space available so we don't even try.
	 */
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
		goto out_select;

	if (ap->datatype & XFS_ALLOC_USERDATA)
		flags |= XFS_PICK_USERDATA;
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
		flags |= XFS_PICK_LOWSPACE;

	*blen = ap->length;
	error = xfs_filestream_pick_ag(pip, &agno, flags, blen);
	if (error)
		goto out_error;
	if (agno == NULLAGNUMBER) {
		agno = 0;
		goto out_irele;
	}

	pag = xfs_perag_grab(mp, agno);
	if (!pag)
		goto out_irele;

	error = xfs_bmap_longest_free_extent(pag, args->tp, blen);
	xfs_perag_rele(pag);
	if (error) {
		if (error != -EAGAIN)
			goto out_error;
		*blen = 0;
	}

out_irele:
	xfs_irele(pip);
out_select:
	ap->blkno = XFS_AGB_TO_FSB(mp, agno, 0);
	return 0;

out_error:
	xfs_irele(pip);
	return error;

}

void
xfs_filestream_deassociate(
	struct xfs_inode	*ip)
{
	xfs_mru_cache_delete(ip->i_mount->m_filestream, ip->i_ino);
}

int
xfs_filestream_mount(
	xfs_mount_t	*mp)
{
	/*
	 * The filestream timer tunable is currently fixed within the range of
	 * one second to four minutes, with five seconds being the default.  The
	 * group count is somewhat arbitrary, but it'd be nice to adhere to the
	 * timer tunable to within about 10 percent.  This requires at least 10
	 * groups.
	 */
	return xfs_mru_cache_create(&mp->m_filestream, mp,
			xfs_fstrm_centisecs * 10, 10, xfs_fstrm_free_func);
}

void
xfs_filestream_unmount(
	xfs_mount_t	*mp)
{
	xfs_mru_cache_destroy(mp->m_filestream);
}
