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
	struct xfs_perag		*pag; /* AG in use for this directory */
};

enum xfs_fstrm_alloc {
	XFS_PICK_USERDATA = 1,
	XFS_PICK_LOWSPACE = 2,
};

static void
xfs_fstrm_free_func(
	void			*data,
	struct xfs_mru_cache_elem *mru)
{
	struct xfs_fstrm_item	*item =
		container_of(mru, struct xfs_fstrm_item, mru);
	struct xfs_perag	*pag = item->pag;

	trace_xfs_filestream_free(pag, mru->key);
	atomic_dec(&pag->pagf_fstrms);
	xfs_perag_rele(pag);

	kmem_free(item);
}

/*
 * Scan the AGs starting at start_agno looking for an AG that isn't in use and
 * has at least minlen blocks free. If no AG is found to match the allocation
 * requirements, pick the AG with the most free space in it.
 */
static int
xfs_filestream_pick_ag(
	struct xfs_alloc_arg	*args,
	xfs_ino_t		pino,
	xfs_agnumber_t		start_agno,
	int			flags,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_perag	*pag;
	struct xfs_perag	*max_pag = NULL;
	xfs_extlen_t		minlen = *longest;
	xfs_extlen_t		free = 0, minfree, maxfree = 0;
	xfs_agnumber_t		agno;
	bool			first_pass = true;
	int			err;

	/* 2% of an AG's blocks must be free for it to be chosen. */
	minfree = mp->m_sb.sb_agblocks / 50;

restart:
	for_each_perag_wrap(mp, start_agno, agno, pag) {
		trace_xfs_filestream_scan(pag, pino);
		*longest = 0;
		err = xfs_bmap_longest_free_extent(pag, NULL, longest);
		if (err) {
			if (err != -EAGAIN)
				break;
			/* Couldn't lock the AGF, skip this AG. */
			err = 0;
			continue;
		}

		/* Keep track of the AG with the most free blocks. */
		if (pag->pagf_freeblks > maxfree) {
			maxfree = pag->pagf_freeblks;
			if (max_pag)
				xfs_perag_rele(max_pag);
			atomic_inc(&pag->pag_active_ref);
			max_pag = pag;
		}

		/*
		 * The AG reference count does two things: it enforces mutual
		 * exclusion when examining the suitability of an AG in this
		 * loop, and it guards against two filestreams being established
		 * in the same AG as each other.
		 */
		if (atomic_inc_return(&pag->pagf_fstrms) <= 1) {
			if (((minlen && *longest >= minlen) ||
			     (!minlen && pag->pagf_freeblks >= minfree)) &&
			    (!xfs_perag_prefers_metadata(pag) ||
			     !(flags & XFS_PICK_USERDATA) ||
			     (flags & XFS_PICK_LOWSPACE))) {
				/* Break out, retaining the reference on the AG. */
				free = pag->pagf_freeblks;
				break;
			}
		}

		/* Drop the reference on this AG, it's not usable. */
		atomic_dec(&pag->pagf_fstrms);
	}

	if (err) {
		xfs_perag_rele(pag);
		if (max_pag)
			xfs_perag_rele(max_pag);
		return err;
	}

	if (!pag) {
		/*
		 * Allow a second pass to give xfs_bmap_longest_free_extent()
		 * another attempt at locking AGFs that it might have skipped
		 * over before we fail.
		 */
		if (first_pass) {
			first_pass = false;
			goto restart;
		}

		/*
		 * We must be low on data space, so run a final lowspace
		 * optimised selection pass if we haven't already.
		 */
		if (!(flags & XFS_PICK_LOWSPACE)) {
			flags |= XFS_PICK_LOWSPACE;
			goto restart;
		}

		/*
		 * No unassociated AGs are available, so select the AG with the
		 * most free space, regardless of whether it's already in use by
		 * another filestream. It none suit, just use whatever AG we can
		 * grab.
		 */
		if (!max_pag) {
			for_each_perag_wrap(args->mp, 0, start_agno, args->pag)
				break;
			atomic_inc(&args->pag->pagf_fstrms);
			*longest = 0;
		} else {
			pag = max_pag;
			free = maxfree;
			atomic_inc(&pag->pagf_fstrms);
		}
	} else if (max_pag) {
		xfs_perag_rele(max_pag);
	}

	trace_xfs_filestream_pick(pag, pino, free);
	args->pag = pag;
	return 0;

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
 * Lookup the mru cache for an existing association. If one exists and we can
 * use it, return with an active perag reference indicating that the allocation
 * will proceed with that association.
 *
 * If we have no association, or we cannot use the current one and have to
 * destroy it, return with longest = 0 to tell the caller to create a new
 * association.
 */
static int
xfs_filestream_lookup_association(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	xfs_ino_t		pino,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_perag	*pag;
	struct xfs_mru_cache_elem *mru;
	int			error = 0;

	*longest = 0;
	mru = xfs_mru_cache_lookup(mp->m_filestream, pino);
	if (!mru)
		return 0;
	/*
	 * Grab the pag and take an extra active reference for the caller whilst
	 * the mru item cannot go away. This means we'll pin the perag with
	 * the reference we get here even if the filestreams association is torn
	 * down immediately after we mark the lookup as done.
	 */
	pag = container_of(mru, struct xfs_fstrm_item, mru)->pag;
	atomic_inc(&pag->pag_active_ref);
	xfs_mru_cache_done(mp->m_filestream);

	trace_xfs_filestream_lookup(pag, ap->ip->i_ino);

	ap->blkno = XFS_AGB_TO_FSB(args->mp, pag->pag_agno, 0);
	xfs_bmap_adjacent(ap);

	/*
	 * If there is very little free space before we start a filestreams
	 * allocation, we're almost guaranteed to fail to find a large enough
	 * free space available so just use the cached AG.
	 */
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE) {
		*longest = 1;
		goto out_done;
	}

	error = xfs_bmap_longest_free_extent(pag, args->tp, longest);
	if (error == -EAGAIN)
		error = 0;
	if (error || *longest < args->maxlen) {
		/* We aren't going to use this perag */
		*longest = 0;
		xfs_perag_rele(pag);
		return error;
	}

out_done:
	args->pag = pag;
	return 0;
}

static int
xfs_filestream_create_association(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	xfs_ino_t		pino,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_mru_cache_elem *mru;
	struct xfs_fstrm_item	*item;
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, pino);
	int			flags = 0;
	int			error;

	/* Changing parent AG association now, so remove the existing one. */
	mru = xfs_mru_cache_remove(mp->m_filestream, pino);
	if (mru) {
		struct xfs_fstrm_item *item =
			container_of(mru, struct xfs_fstrm_item, mru);

		agno = (item->pag->pag_agno + 1) % mp->m_sb.sb_agcount;
		xfs_fstrm_free_func(mp, mru);
	} else if (xfs_is_inode32(mp)) {
		xfs_agnumber_t	 rotorstep = xfs_rotorstep;

		agno = (mp->m_agfrotor / rotorstep) % mp->m_sb.sb_agcount;
		mp->m_agfrotor = (mp->m_agfrotor + 1) %
				 (mp->m_sb.sb_agcount * rotorstep);
	}

	ap->blkno = XFS_AGB_TO_FSB(args->mp, agno, 0);
	xfs_bmap_adjacent(ap);

	if (ap->datatype & XFS_ALLOC_USERDATA)
		flags |= XFS_PICK_USERDATA;
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
		flags |= XFS_PICK_LOWSPACE;

	*longest = ap->length;
	error = xfs_filestream_pick_ag(args, pino, agno, flags, longest);
	if (error)
		return error;

	/*
	 * We are going to use this perag now, so create an assoication for it.
	 * xfs_filestream_pick_ag() has already bumped the perag fstrms counter
	 * for us, so all we need to do here is take another active reference to
	 * the perag for the cached association.
	 *
	 * If we fail to store the association, we need to drop the fstrms
	 * counter as well as drop the perag reference we take here for the
	 * item. We do not need to return an error for this failure - as long as
	 * we return a referenced AG, the allocation can still go ahead just
	 * fine.
	 */
	item = kmem_alloc(sizeof(*item), KM_MAYFAIL);
	if (!item)
		goto out_put_fstrms;

	atomic_inc(&args->pag->pag_active_ref);
	item->pag = args->pag;
	error = xfs_mru_cache_insert(mp->m_filestream, pino, &item->mru);
	if (error)
		goto out_free_item;
	return 0;

out_free_item:
	xfs_perag_rele(item->pag);
	kmem_free(item);
out_put_fstrms:
	atomic_dec(&args->pag->pagf_fstrms);
	return 0;
}

/*
 * Search for an allocation group with a single extent large enough for
 * the request. First we look for an existing association and use that if it
 * is found. Otherwise, we create a new association by selecting an AG that fits
 * the allocation criteria.
 *
 * We return with a referenced perag in args->pag to indicate which AG we are
 * allocating into or an error with no references held.
 */
int
xfs_filestream_select_ag(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = args->mp;
	struct xfs_inode	*pip;
	xfs_ino_t		ino = 0;
	int			error = 0;

	*longest = 0;
	args->total = ap->total;
	pip = xfs_filestream_get_parent(ap->ip);
	if (pip) {
		ino = pip->i_ino;
		error = xfs_filestream_lookup_association(ap, args, ino,
				longest);
		xfs_irele(pip);
		if (error)
			return error;
		if (*longest >= args->maxlen)
			goto out_select;
		if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
			goto out_select;
	}

	error = xfs_filestream_create_association(ap, args, ino, longest);
	if (error)
		return error;

out_select:
	ap->blkno = XFS_AGB_TO_FSB(mp, args->pag->pag_agno, 0);
	return 0;
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
