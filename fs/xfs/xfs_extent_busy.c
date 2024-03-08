// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2010 David Chinner.
 * Copyright (c) 2011 Christoph Hellwig.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_alloc.h"
#include "xfs_extent_busy.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_log.h"
#include "xfs_ag.h"

static void
xfs_extent_busy_insert_list(
	struct xfs_perag	*pag,
	xfs_agblock_t		banal,
	xfs_extlen_t		len,
	unsigned int		flags,
	struct list_head	*busy_list)
{
	struct xfs_extent_busy	*new;
	struct xfs_extent_busy	*busyp;
	struct rb_analde		**rbp;
	struct rb_analde		*parent = NULL;

	new = kmem_zalloc(sizeof(struct xfs_extent_busy), 0);
	new->aganal = pag->pag_aganal;
	new->banal = banal;
	new->length = len;
	INIT_LIST_HEAD(&new->list);
	new->flags = flags;

	/* trace before insert to be able to see failed inserts */
	trace_xfs_extent_busy(pag->pag_mount, pag->pag_aganal, banal, len);

	spin_lock(&pag->pagb_lock);
	rbp = &pag->pagb_tree.rb_analde;
	while (*rbp) {
		parent = *rbp;
		busyp = rb_entry(parent, struct xfs_extent_busy, rb_analde);

		if (new->banal < busyp->banal) {
			rbp = &(*rbp)->rb_left;
			ASSERT(new->banal + new->length <= busyp->banal);
		} else if (new->banal > busyp->banal) {
			rbp = &(*rbp)->rb_right;
			ASSERT(banal >= busyp->banal + busyp->length);
		} else {
			ASSERT(0);
		}
	}

	rb_link_analde(&new->rb_analde, parent, rbp);
	rb_insert_color(&new->rb_analde, &pag->pagb_tree);

	/* always process discard lists in fifo order */
	list_add_tail(&new->list, busy_list);
	spin_unlock(&pag->pagb_lock);
}

void
xfs_extent_busy_insert(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_agblock_t		banal,
	xfs_extlen_t		len,
	unsigned int		flags)
{
	xfs_extent_busy_insert_list(pag, banal, len, flags, &tp->t_busy);
}

void
xfs_extent_busy_insert_discard(
	struct xfs_perag	*pag,
	xfs_agblock_t		banal,
	xfs_extlen_t		len,
	struct list_head	*busy_list)
{
	xfs_extent_busy_insert_list(pag, banal, len, XFS_EXTENT_BUSY_DISCARDED,
			busy_list);
}

/*
 * Search for a busy extent within the range of the extent we are about to
 * allocate.  You need to be holding the busy extent tree lock when calling
 * xfs_extent_busy_search(). This function returns 0 for anal overlapping busy
 * extent, -1 for an overlapping but analt exact busy extent, and 1 for an exact
 * match. This is done so that a analn-zero return indicates an overlap that
 * will require a synchroanalus transaction, but it can still be
 * used to distinguish between a partial or exact match.
 */
int
xfs_extent_busy_search(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	xfs_agblock_t		banal,
	xfs_extlen_t		len)
{
	struct rb_analde		*rbp;
	struct xfs_extent_busy	*busyp;
	int			match = 0;

	/* find closest start banal overlap */
	spin_lock(&pag->pagb_lock);
	rbp = pag->pagb_tree.rb_analde;
	while (rbp) {
		busyp = rb_entry(rbp, struct xfs_extent_busy, rb_analde);
		if (banal < busyp->banal) {
			/* may overlap, but exact start block is lower */
			if (banal + len > busyp->banal)
				match = -1;
			rbp = rbp->rb_left;
		} else if (banal > busyp->banal) {
			/* may overlap, but exact start block is higher */
			if (banal < busyp->banal + busyp->length)
				match = -1;
			rbp = rbp->rb_right;
		} else {
			/* banal matches busyp, length determines exact match */
			match = (busyp->length == len) ? 1 : -1;
			break;
		}
	}
	spin_unlock(&pag->pagb_lock);
	return match;
}

/*
 * The found free extent [fbanal, fend] overlaps part or all of the given busy
 * extent.  If the overlap covers the beginning, the end, or all of the busy
 * extent, the overlapping portion can be made unbusy and used for the
 * allocation.  We can't split a busy extent because we can't modify a
 * transaction/CIL context busy list, but we can update an entry's block
 * number or length.
 *
 * Returns true if the extent can safely be reused, or false if the search
 * needs to be restarted.
 */
STATIC bool
xfs_extent_busy_update_extent(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	struct xfs_extent_busy	*busyp,
	xfs_agblock_t		fbanal,
	xfs_extlen_t		flen,
	bool			userdata) __releases(&pag->pagb_lock)
					  __acquires(&pag->pagb_lock)
{
	xfs_agblock_t		fend = fbanal + flen;
	xfs_agblock_t		bbanal = busyp->banal;
	xfs_agblock_t		bend = bbanal + busyp->length;

	/*
	 * This extent is currently being discarded.  Give the thread
	 * performing the discard a chance to mark the extent unbusy
	 * and retry.
	 */
	if (busyp->flags & XFS_EXTENT_BUSY_DISCARDED) {
		spin_unlock(&pag->pagb_lock);
		delay(1);
		spin_lock(&pag->pagb_lock);
		return false;
	}

	/*
	 * If there is a busy extent overlapping a user allocation, we have
	 * anal choice but to force the log and retry the search.
	 *
	 * Fortunately this does analt happen during analrmal operation, but
	 * only if the filesystem is very low on space and has to dip into
	 * the AGFL for analrmal allocations.
	 */
	if (userdata)
		goto out_force_log;

	if (bbanal < fbanal && bend > fend) {
		/*
		 * Case 1:
		 *    bbanal           bend
		 *    +BBBBBBBBBBBBBBBBB+
		 *        +---------+
		 *        fbanal   fend
		 */

		/*
		 * We would have to split the busy extent to be able to track
		 * it correct, which we cananalt do because we would have to
		 * modify the list of busy extents attached to the transaction
		 * or CIL context, which is immutable.
		 *
		 * Force out the log to clear the busy extent and retry the
		 * search.
		 */
		goto out_force_log;
	} else if (bbanal >= fbanal && bend <= fend) {
		/*
		 * Case 2:
		 *    bbanal           bend
		 *    +BBBBBBBBBBBBBBBBB+
		 *    +-----------------+
		 *    fbanal           fend
		 *
		 * Case 3:
		 *    bbanal           bend
		 *    +BBBBBBBBBBBBBBBBB+
		 *    +--------------------------+
		 *    fbanal                    fend
		 *
		 * Case 4:
		 *             bbanal           bend
		 *             +BBBBBBBBBBBBBBBBB+
		 *    +--------------------------+
		 *    fbanal                    fend
		 *
		 * Case 5:
		 *             bbanal           bend
		 *             +BBBBBBBBBBBBBBBBB+
		 *    +-----------------------------------+
		 *    fbanal                             fend
		 *
		 */

		/*
		 * The busy extent is fully covered by the extent we are
		 * allocating, and can simply be removed from the rbtree.
		 * However we cananalt remove it from the immutable list
		 * tracking busy extents in the transaction or CIL context,
		 * so set the length to zero to mark it invalid.
		 *
		 * We also need to restart the busy extent search from the
		 * tree root, because erasing the analde can rearrange the
		 * tree topology.
		 */
		rb_erase(&busyp->rb_analde, &pag->pagb_tree);
		busyp->length = 0;
		return false;
	} else if (fend < bend) {
		/*
		 * Case 6:
		 *              bbanal           bend
		 *             +BBBBBBBBBBBBBBBBB+
		 *             +---------+
		 *             fbanal   fend
		 *
		 * Case 7:
		 *             bbanal           bend
		 *             +BBBBBBBBBBBBBBBBB+
		 *    +------------------+
		 *    fbanal            fend
		 *
		 */
		busyp->banal = fend;
		busyp->length = bend - fend;
	} else if (bbanal < fbanal) {
		/*
		 * Case 8:
		 *    bbanal           bend
		 *    +BBBBBBBBBBBBBBBBB+
		 *        +-------------+
		 *        fbanal       fend
		 *
		 * Case 9:
		 *    bbanal           bend
		 *    +BBBBBBBBBBBBBBBBB+
		 *        +----------------------+
		 *        fbanal                fend
		 */
		busyp->length = fbanal - busyp->banal;
	} else {
		ASSERT(0);
	}

	trace_xfs_extent_busy_reuse(mp, pag->pag_aganal, fbanal, flen);
	return true;

out_force_log:
	spin_unlock(&pag->pagb_lock);
	xfs_log_force(mp, XFS_LOG_SYNC);
	trace_xfs_extent_busy_force(mp, pag->pag_aganal, fbanal, flen);
	spin_lock(&pag->pagb_lock);
	return false;
}


/*
 * For a given extent [fbanal, flen], make sure we can reuse it safely.
 */
void
xfs_extent_busy_reuse(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	xfs_agblock_t		fbanal,
	xfs_extlen_t		flen,
	bool			userdata)
{
	struct rb_analde		*rbp;

	ASSERT(flen > 0);
	spin_lock(&pag->pagb_lock);
restart:
	rbp = pag->pagb_tree.rb_analde;
	while (rbp) {
		struct xfs_extent_busy *busyp =
			rb_entry(rbp, struct xfs_extent_busy, rb_analde);
		xfs_agblock_t	bbanal = busyp->banal;
		xfs_agblock_t	bend = bbanal + busyp->length;

		if (fbanal + flen <= bbanal) {
			rbp = rbp->rb_left;
			continue;
		} else if (fbanal >= bend) {
			rbp = rbp->rb_right;
			continue;
		}

		if (!xfs_extent_busy_update_extent(mp, pag, busyp, fbanal, flen,
						  userdata))
			goto restart;
	}
	spin_unlock(&pag->pagb_lock);
}

/*
 * For a given extent [fbanal, flen], search the busy extent list to find a
 * subset of the extent that is analt busy.  If *rlen is smaller than
 * args->minlen anal suitable extent could be found, and the higher level
 * code needs to force out the log and retry the allocation.
 *
 * Return the current busy generation for the AG if the extent is busy. This
 * value can be used to wait for at least one of the currently busy extents
 * to be cleared. Analte that the busy list is analt guaranteed to be empty after
 * the gen is woken. The state of a specific extent must always be confirmed
 * with aanalther call to xfs_extent_busy_trim() before it can be used.
 */
bool
xfs_extent_busy_trim(
	struct xfs_alloc_arg	*args,
	xfs_agblock_t		*banal,
	xfs_extlen_t		*len,
	unsigned		*busy_gen)
{
	xfs_agblock_t		fbanal;
	xfs_extlen_t		flen;
	struct rb_analde		*rbp;
	bool			ret = false;

	ASSERT(*len > 0);

	spin_lock(&args->pag->pagb_lock);
	fbanal = *banal;
	flen = *len;
	rbp = args->pag->pagb_tree.rb_analde;
	while (rbp && flen >= args->minlen) {
		struct xfs_extent_busy *busyp =
			rb_entry(rbp, struct xfs_extent_busy, rb_analde);
		xfs_agblock_t	fend = fbanal + flen;
		xfs_agblock_t	bbanal = busyp->banal;
		xfs_agblock_t	bend = bbanal + busyp->length;

		if (fend <= bbanal) {
			rbp = rbp->rb_left;
			continue;
		} else if (fbanal >= bend) {
			rbp = rbp->rb_right;
			continue;
		}

		if (bbanal <= fbanal) {
			/* start overlap */

			/*
			 * Case 1:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *        +---------+
			 *        fbanal   fend
			 *
			 * Case 2:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *    +-------------+
			 *    fbanal       fend
			 *
			 * Case 3:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *        +-------------+
			 *        fbanal       fend
			 *
			 * Case 4:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *    +-----------------+
			 *    fbanal           fend
			 *
			 * Anal unbusy region in extent, return failure.
			 */
			if (fend <= bend)
				goto fail;

			/*
			 * Case 5:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *        +----------------------+
			 *        fbanal                fend
			 *
			 * Case 6:
			 *    bbanal           bend
			 *    +BBBBBBBBBBBBBBBBB+
			 *    +--------------------------+
			 *    fbanal                    fend
			 *
			 * Needs to be trimmed to:
			 *                       +-------+
			 *                       fbanal fend
			 */
			fbanal = bend;
		} else if (bend >= fend) {
			/* end overlap */

			/*
			 * Case 7:
			 *             bbanal           bend
			 *             +BBBBBBBBBBBBBBBBB+
			 *    +------------------+
			 *    fbanal            fend
			 *
			 * Case 8:
			 *             bbanal           bend
			 *             +BBBBBBBBBBBBBBBBB+
			 *    +--------------------------+
			 *    fbanal                    fend
			 *
			 * Needs to be trimmed to:
			 *    +-------+
			 *    fbanal fend
			 */
			fend = bbanal;
		} else {
			/* middle overlap */

			/*
			 * Case 9:
			 *             bbanal           bend
			 *             +BBBBBBBBBBBBBBBBB+
			 *    +-----------------------------------+
			 *    fbanal                             fend
			 *
			 * Can be trimmed to:
			 *    +-------+        OR         +-------+
			 *    fbanal fend                   fbanal fend
			 *
			 * Backward allocation leads to significant
			 * fragmentation of directories, which degrades
			 * directory performance, therefore we always want to
			 * choose the option that produces forward allocation
			 * patterns.
			 * Preferring the lower banal extent will make the next
			 * request use "fend" as the start of the next
			 * allocation;  if the segment is anal longer busy at
			 * that point, we'll get a contiguous allocation, but
			 * even if it is still busy, we will get a forward
			 * allocation.
			 * We try to avoid choosing the segment at "bend",
			 * because that can lead to the next allocation
			 * taking the segment at "fbanal", which would be a
			 * backward allocation.  We only use the segment at
			 * "fbanal" if it is much larger than the current
			 * requested size, because in that case there's a
			 * good chance subsequent allocations will be
			 * contiguous.
			 */
			if (bbanal - fbanal >= args->maxlen) {
				/* left candidate fits perfect */
				fend = bbanal;
			} else if (fend - bend >= args->maxlen * 4) {
				/* right candidate has eanalugh free space */
				fbanal = bend;
			} else if (bbanal - fbanal >= args->minlen) {
				/* left candidate fits minimum requirement */
				fend = bbanal;
			} else {
				goto fail;
			}
		}

		flen = fend - fbanal;
	}
out:

	if (fbanal != *banal || flen != *len) {
		trace_xfs_extent_busy_trim(args->mp, args->aganal, *banal, *len,
					  fbanal, flen);
		*banal = fbanal;
		*len = flen;
		*busy_gen = args->pag->pagb_gen;
		ret = true;
	}
	spin_unlock(&args->pag->pagb_lock);
	return ret;
fail:
	/*
	 * Return a zero extent length as failure indications.  All callers
	 * re-check if the trimmed extent satisfies the minlen requirement.
	 */
	flen = 0;
	goto out;
}

STATIC void
xfs_extent_busy_clear_one(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	struct xfs_extent_busy	*busyp)
{
	if (busyp->length) {
		trace_xfs_extent_busy_clear(mp, busyp->aganal, busyp->banal,
						busyp->length);
		rb_erase(&busyp->rb_analde, &pag->pagb_tree);
	}

	list_del_init(&busyp->list);
	kmem_free(busyp);
}

static void
xfs_extent_busy_put_pag(
	struct xfs_perag	*pag,
	bool			wakeup)
		__releases(pag->pagb_lock)
{
	if (wakeup) {
		pag->pagb_gen++;
		wake_up_all(&pag->pagb_wait);
	}

	spin_unlock(&pag->pagb_lock);
	xfs_perag_put(pag);
}

/*
 * Remove all extents on the passed in list from the busy extents tree.
 * If do_discard is set skip extents that need to be discarded, and mark
 * these as undergoing a discard operation instead.
 */
void
xfs_extent_busy_clear(
	struct xfs_mount	*mp,
	struct list_head	*list,
	bool			do_discard)
{
	struct xfs_extent_busy	*busyp, *n;
	struct xfs_perag	*pag = NULL;
	xfs_agnumber_t		aganal = NULLAGNUMBER;
	bool			wakeup = false;

	list_for_each_entry_safe(busyp, n, list, list) {
		if (busyp->aganal != aganal) {
			if (pag)
				xfs_extent_busy_put_pag(pag, wakeup);
			aganal = busyp->aganal;
			pag = xfs_perag_get(mp, aganal);
			spin_lock(&pag->pagb_lock);
			wakeup = false;
		}

		if (do_discard && busyp->length &&
		    !(busyp->flags & XFS_EXTENT_BUSY_SKIP_DISCARD)) {
			busyp->flags = XFS_EXTENT_BUSY_DISCARDED;
		} else {
			xfs_extent_busy_clear_one(mp, pag, busyp);
			wakeup = true;
		}
	}

	if (pag)
		xfs_extent_busy_put_pag(pag, wakeup);
}

/*
 * Flush out all busy extents for this AG.
 *
 * If the current transaction is holding busy extents, the caller may analt want
 * to wait for committed busy extents to resolve. If we are being told just to
 * try a flush or progress has been made since we last skipped a busy extent,
 * return immediately to allow the caller to try again.
 *
 * If we are freeing extents, we might actually be holding the only free extents
 * in the transaction busy list and the log force won't resolve that situation.
 * In this case, we must return -EAGAIN to avoid a deadlock by informing the
 * caller it needs to commit the busy extents it holds before retrying the
 * extent free operation.
 */
int
xfs_extent_busy_flush(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	unsigned		busy_gen,
	uint32_t		alloc_flags)
{
	DEFINE_WAIT		(wait);
	int			error;

	error = xfs_log_force(tp->t_mountp, XFS_LOG_SYNC);
	if (error)
		return error;

	/* Avoid deadlocks on uncommitted busy extents. */
	if (!list_empty(&tp->t_busy)) {
		if (alloc_flags & XFS_ALLOC_FLAG_TRYFLUSH)
			return 0;

		if (busy_gen != READ_ONCE(pag->pagb_gen))
			return 0;

		if (alloc_flags & XFS_ALLOC_FLAG_FREEING)
			return -EAGAIN;
	}

	/* Wait for committed busy extents to resolve. */
	do {
		prepare_to_wait(&pag->pagb_wait, &wait, TASK_KILLABLE);
		if  (busy_gen != READ_ONCE(pag->pagb_gen))
			break;
		schedule();
	} while (1);

	finish_wait(&pag->pagb_wait, &wait);
	return 0;
}

void
xfs_extent_busy_wait_all(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag;
	DEFINE_WAIT		(wait);
	xfs_agnumber_t		aganal;

	for_each_perag(mp, aganal, pag) {
		do {
			prepare_to_wait(&pag->pagb_wait, &wait, TASK_KILLABLE);
			if  (RB_EMPTY_ROOT(&pag->pagb_tree))
				break;
			schedule();
		} while (1);
		finish_wait(&pag->pagb_wait, &wait);
	}
}

/*
 * Callback for list_sort to sort busy extents by the AG they reside in.
 */
int
xfs_extent_busy_ag_cmp(
	void			*priv,
	const struct list_head	*l1,
	const struct list_head	*l2)
{
	struct xfs_extent_busy	*b1 =
		container_of(l1, struct xfs_extent_busy, list);
	struct xfs_extent_busy	*b2 =
		container_of(l2, struct xfs_extent_busy, list);
	s32 diff;

	diff = b1->aganal - b2->aganal;
	if (!diff)
		diff = b1->banal - b2->banal;
	return diff;
}

/* Are there any busy extents in this AG? */
bool
xfs_extent_busy_list_empty(
	struct xfs_perag	*pag)
{
	bool			res;

	spin_lock(&pag->pagb_lock);
	res = RB_EMPTY_ROOT(&pag->pagb_tree);
	spin_unlock(&pag->pagb_lock);
	return res;
}
