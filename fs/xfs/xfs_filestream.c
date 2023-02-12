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
 * Scan the AGs starting at startag looking for an AG that isn't in use and has
 * at least minlen blocks free.
 */
static int
xfs_filestream_pick_ag(
	struct xfs_alloc_arg	*args,
	struct xfs_inode	*ip,
	xfs_agnumber_t		start_agno,
	int			flags,
	xfs_extlen_t		*longest)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_fstrm_item	*item;
	struct xfs_perag	*pag;
	struct xfs_perag	*max_pag = NULL;
	xfs_extlen_t		minlen = *longest;
	xfs_extlen_t		free = 0, minfree, maxfree = 0;
	xfs_agnumber_t		agno;
	int			err, trylock;

	ASSERT(S_ISDIR(VFS_I(ip)->i_mode));

	/* 2% of an AG's blocks must be free for it to be chosen. */
	minfree = mp->m_sb.sb_agblocks / 50;

	/* For the first pass, don't sleep trying to init the per-AG. */
	trylock = XFS_ALLOC_FLAG_TRYLOCK;

restart:
	for_each_perag_wrap(mp, start_agno, agno, pag) {
		trace_xfs_filestream_scan(pag, ip->i_ino);
		*longest = 0;
		err = xfs_bmap_longest_free_extent(pag, NULL, longest);
		if (err) {
			xfs_perag_rele(pag);
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
		/* Allow sleeping in xfs_alloc_read_agf() on the 2nd pass. */
		if (trylock) {
			trylock = 0;
			goto restart;
		}

		/* Finally, if lowspace wasn't set, set it for the 3rd pass. */
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
			for_each_perag_wrap(mp, start_agno, agno, pag)
				break;
			atomic_inc(&pag->pagf_fstrms);
			*longest = 0;
		} else {
			pag = max_pag;
			free = maxfree;
			atomic_inc(&pag->pagf_fstrms);
		}
	} else if (max_pag) {
		xfs_perag_rele(max_pag);
	}

	trace_xfs_filestream_pick(ip, pag, free);

	err = -ENOMEM;
	item = kmem_alloc(sizeof(*item), KM_MAYFAIL);
	if (!item)
		goto out_put_ag;


	/*
	 * We are going to use this perag now, so take another ref to it for the
	 * allocation context returned to the caller. If we raced to create and
	 * insert the filestreams item into the MRU (-EEXIST), then we still
	 * keep this reference but free the item reference we gained above. On
	 * any other failure, we have to drop both.
	 */
	atomic_inc(&pag->pag_active_ref);
	item->pag = pag;
	args->pag = pag;

	err = xfs_mru_cache_insert(mp->m_filestream, ip->i_ino, &item->mru);
	if (err) {
		if (err == -EEXIST) {
			err = 0;
		} else {
			xfs_perag_rele(args->pag);
			args->pag = NULL;
		}
		goto out_free_item;
	}

	return 0;

out_free_item:
	kmem_free(item);
out_put_ag:
	atomic_dec(&pag->pagf_fstrms);
	xfs_perag_rele(pag);
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
 * Lookup the mru cache for an existing association. If one exists and we can
 * use it, return with the agno and blen indicating that the allocation will
 * proceed with that association.
 *
 * If we have no association, or we cannot use the current one and have to
 * destroy it, return with blen = 0 and agno pointing at the next agno to try.
 */
int
xfs_filestream_select_ag_mru(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	struct xfs_inode	*pip,
	xfs_agnumber_t		*agno,
	xfs_extlen_t		*blen)
{
	struct xfs_mount	*mp = ap->ip->i_mount;
	struct xfs_perag	*pag;
	struct xfs_mru_cache_elem *mru;
	int			error;

	mru = xfs_mru_cache_lookup(mp->m_filestream, pip->i_ino);
	if (!mru)
		goto out_default_agno;

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

	error = xfs_bmap_longest_free_extent(pag, args->tp, blen);
	if (error) {
		/* We aren't going to use this perag */
		xfs_perag_rele(pag);
		if (error != -EAGAIN)
			return error;
		*blen = 0;
	}

	/*
	 * We are done if there's still enough contiguous free space to succeed.
	 * If there is very little free space before we start a filestreams
	 * allocation, we're almost guaranteed to fail to find a better AG with
	 * larger free space available so we don't even try.
	 */
	*agno = pag->pag_agno;
	if (*blen >= args->maxlen || (ap->tp->t_flags & XFS_TRANS_LOWMODE)) {
		args->pag = pag;
		return 0;
	}

	/* Changing parent AG association now, so remove the existing one. */
	xfs_perag_rele(pag);
	mru = xfs_mru_cache_remove(mp->m_filestream, pip->i_ino);
	if (mru) {
		struct xfs_fstrm_item *item =
			container_of(mru, struct xfs_fstrm_item, mru);
		*agno = (item->pag->pag_agno + 1) % mp->m_sb.sb_agcount;
		xfs_fstrm_free_func(mp, mru);
		return 0;
	}

out_default_agno:
	if (xfs_is_inode32(mp)) {
		xfs_agnumber_t	 rotorstep = xfs_rotorstep;
		*agno = (mp->m_agfrotor / rotorstep) %
				mp->m_sb.sb_agcount;
		mp->m_agfrotor = (mp->m_agfrotor + 1) %
				 (mp->m_sb.sb_agcount * rotorstep);
		return 0;
	}
	*agno = XFS_INO_TO_AGNO(mp, pip->i_ino);
	return 0;

}

/*
 * Search for an allocation group with a single extent large enough for
 * the request.  If one isn't found, then adjust the minimum allocation
 * size to the largest space found.
 */
int
xfs_filestream_select_ag(
	struct xfs_bmalloca	*ap,
	struct xfs_alloc_arg	*args,
	xfs_extlen_t		*blen)
{
	struct xfs_mount	*mp = ap->ip->i_mount;
	struct xfs_inode	*pip = NULL;
	xfs_agnumber_t		agno;
	int			flags = 0;
	int			error = 0;

	args->total = ap->total;
	*blen = 0;

	pip = xfs_filestream_get_parent(ap->ip);
	if (!pip) {
		ap->blkno = XFS_AGB_TO_FSB(mp, 0, 0);
		return 0;
	}

	error = xfs_filestream_select_ag_mru(ap, args, pip, &agno, blen);
	if (error)
		goto out_rele;
	if (*blen >= args->maxlen)
		goto out_select;
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
		goto out_select;

	ap->blkno = XFS_AGB_TO_FSB(args->mp, agno, 0);
	xfs_bmap_adjacent(ap);
	*blen = ap->length;
	if (ap->datatype & XFS_ALLOC_USERDATA)
		flags |= XFS_PICK_USERDATA;
	if (ap->tp->t_flags & XFS_TRANS_LOWMODE)
		flags |= XFS_PICK_LOWSPACE;

	error = xfs_filestream_pick_ag(args, pip, agno, flags, blen);
	if (error)
		goto out_rele;
out_select:
	ap->blkno = XFS_AGB_TO_FSB(mp, args->pag->pag_agno, 0);
out_rele:
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
