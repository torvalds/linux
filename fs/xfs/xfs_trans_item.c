/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"

STATIC int	xfs_trans_unlock_chunk(xfs_log_item_chunk_t *,
					int, int, xfs_lsn_t);

/*
 * This is called to add the given log item to the transaction's
 * list of log items.  It must find a free log item descriptor
 * or allocate a new one and add the item to that descriptor.
 * The function returns a pointer to item descriptor used to point
 * to the new item.  The log item will now point to its new descriptor
 * with its li_desc field.
 */
xfs_log_item_desc_t *
xfs_trans_add_item(xfs_trans_t *tp, xfs_log_item_t *lip)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_chunk_t	*licp;
	int			i=0;

	/*
	 * If there are no free descriptors, allocate a new chunk
	 * of them and put it at the front of the chunk list.
	 */
	if (tp->t_items_free == 0) {
		licp = (xfs_log_item_chunk_t*)
		       kmem_alloc(sizeof(xfs_log_item_chunk_t), KM_SLEEP);
		ASSERT(licp != NULL);
		/*
		 * Initialize the chunk, and then
		 * claim the first slot in the newly allocated chunk.
		 */
		XFS_LIC_INIT(licp);
		XFS_LIC_CLAIM(licp, 0);
		licp->lic_unused = 1;
		XFS_LIC_INIT_SLOT(licp, 0);
		lidp = XFS_LIC_SLOT(licp, 0);

		/*
		 * Link in the new chunk and update the free count.
		 */
		licp->lic_next = tp->t_items.lic_next;
		tp->t_items.lic_next = licp;
		tp->t_items_free = XFS_LIC_NUM_SLOTS - 1;

		/*
		 * Initialize the descriptor and the generic portion
		 * of the log item.
		 *
		 * Point the new slot at this item and return it.
		 * Also point the log item at its currently active
		 * descriptor and set the item's mount pointer.
		 */
		lidp->lid_item = lip;
		lidp->lid_flags = 0;
		lidp->lid_size = 0;
		lip->li_desc = lidp;
		lip->li_mountp = tp->t_mountp;
		return lidp;
	}

	/*
	 * Find the free descriptor. It is somewhere in the chunklist
	 * of descriptors.
	 */
	licp = &tp->t_items;
	while (licp != NULL) {
		if (XFS_LIC_VACANCY(licp)) {
			if (licp->lic_unused <= XFS_LIC_MAX_SLOT) {
				i = licp->lic_unused;
				ASSERT(XFS_LIC_ISFREE(licp, i));
				break;
			}
			for (i = 0; i <= XFS_LIC_MAX_SLOT; i++) {
				if (XFS_LIC_ISFREE(licp, i))
					break;
			}
			ASSERT(i <= XFS_LIC_MAX_SLOT);
			break;
		}
		licp = licp->lic_next;
	}
	ASSERT(licp != NULL);
	/*
	 * If we find a free descriptor, claim it,
	 * initialize it, and return it.
	 */
	XFS_LIC_CLAIM(licp, i);
	if (licp->lic_unused <= i) {
		licp->lic_unused = i + 1;
		XFS_LIC_INIT_SLOT(licp, i);
	}
	lidp = XFS_LIC_SLOT(licp, i);
	tp->t_items_free--;
	lidp->lid_item = lip;
	lidp->lid_flags = 0;
	lidp->lid_size = 0;
	lip->li_desc = lidp;
	lip->li_mountp = tp->t_mountp;
	return lidp;
}

/*
 * Free the given descriptor.
 *
 * This requires setting the bit in the chunk's free mask corresponding
 * to the given slot.
 */
void
xfs_trans_free_item(xfs_trans_t	*tp, xfs_log_item_desc_t *lidp)
{
	uint			slot;
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	**licpp;

	slot = XFS_LIC_DESC_TO_SLOT(lidp);
	licp = XFS_LIC_DESC_TO_CHUNK(lidp);
	XFS_LIC_RELSE(licp, slot);
	lidp->lid_item->li_desc = NULL;
	tp->t_items_free++;

	/*
	 * If there are no more used items in the chunk and this is not
	 * the chunk embedded in the transaction structure, then free
	 * the chunk. First pull it from the chunk list and then
	 * free it back to the heap.  We didn't bother with a doubly
	 * linked list here because the lists should be very short
	 * and this is not a performance path.  It's better to save
	 * the memory of the extra pointer.
	 *
	 * Also decrement the transaction structure's count of free items
	 * by the number in a chunk since we are freeing an empty chunk.
	 */
	if (XFS_LIC_ARE_ALL_FREE(licp) && (licp != &(tp->t_items))) {
		licpp = &(tp->t_items.lic_next);
		while (*licpp != licp) {
			ASSERT(*licpp != NULL);
			licpp = &((*licpp)->lic_next);
		}
		*licpp = licp->lic_next;
		kmem_free(licp, sizeof(xfs_log_item_chunk_t));
		tp->t_items_free -= XFS_LIC_NUM_SLOTS;
	}
}

/*
 * This is called to find the descriptor corresponding to the given
 * log item.  It returns a pointer to the descriptor.
 * The log item MUST have a corresponding descriptor in the given
 * transaction.  This routine does not return NULL, it panics.
 *
 * The descriptor pointer is kept in the log item's li_desc field.
 * Just return it.
 */
/*ARGSUSED*/
xfs_log_item_desc_t *
xfs_trans_find_item(xfs_trans_t	*tp, xfs_log_item_t *lip)
{
	ASSERT(lip->li_desc != NULL);

	return lip->li_desc;
}


/*
 * Return a pointer to the first descriptor in the chunk list.
 * This does not return NULL if there are none, it panics.
 *
 * The first descriptor must be in either the first or second chunk.
 * This is because the only chunk allowed to be empty is the first.
 * All others are freed when they become empty.
 *
 * At some point this and xfs_trans_next_item() should be optimized
 * to quickly look at the mask to determine if there is anything to
 * look at.
 */
xfs_log_item_desc_t *
xfs_trans_first_item(xfs_trans_t *tp)
{
	xfs_log_item_chunk_t	*licp;
	int			i;

	licp = &tp->t_items;
	/*
	 * If it's not in the first chunk, skip to the second.
	 */
	if (XFS_LIC_ARE_ALL_FREE(licp)) {
		licp = licp->lic_next;
	}

	/*
	 * Return the first non-free descriptor in the chunk.
	 */
	ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
	for (i = 0; i < licp->lic_unused; i++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}

		return XFS_LIC_SLOT(licp, i);
	}
	cmn_err(CE_WARN, "xfs_trans_first_item() -- no first item");
	return NULL;
}


/*
 * Given a descriptor, return the next descriptor in the chunk list.
 * This returns NULL if there are no more used descriptors in the list.
 *
 * We do this by first locating the chunk in which the descriptor resides,
 * and then scanning forward in the chunk and the list for the next
 * used descriptor.
 */
/*ARGSUSED*/
xfs_log_item_desc_t *
xfs_trans_next_item(xfs_trans_t *tp, xfs_log_item_desc_t *lidp)
{
	xfs_log_item_chunk_t	*licp;
	int			i;

	licp = XFS_LIC_DESC_TO_CHUNK(lidp);

	/*
	 * First search the rest of the chunk. The for loop keeps us
	 * from referencing things beyond the end of the chunk.
	 */
	for (i = (int)XFS_LIC_DESC_TO_SLOT(lidp) + 1; i < licp->lic_unused; i++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}

		return XFS_LIC_SLOT(licp, i);
	}

	/*
	 * Now search the next chunk.  It must be there, because the
	 * next chunk would have been freed if it were empty.
	 * If there is no next chunk, return NULL.
	 */
	if (licp->lic_next == NULL) {
		return NULL;
	}

	licp = licp->lic_next;
	ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
	for (i = 0; i < licp->lic_unused; i++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}

		return XFS_LIC_SLOT(licp, i);
	}
	ASSERT(0);
	/* NOTREACHED */
	return NULL; /* keep gcc quite */
}

/*
 * This is called to unlock all of the items of a transaction and to free
 * all the descriptors of that transaction.
 *
 * It walks the list of descriptors and unlocks each item.  It frees
 * each chunk except that embedded in the transaction as it goes along.
 */
void
xfs_trans_free_items(
	xfs_trans_t	*tp,
	int		flags)
{
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	*next_licp;
	int			abort;

	abort = flags & XFS_TRANS_ABORT;
	licp = &tp->t_items;
	/*
	 * Special case the embedded chunk so we don't free it below.
	 */
	if (!XFS_LIC_ARE_ALL_FREE(licp)) {
		(void) xfs_trans_unlock_chunk(licp, 1, abort, NULLCOMMITLSN);
		XFS_LIC_ALL_FREE(licp);
		licp->lic_unused = 0;
	}
	licp = licp->lic_next;

	/*
	 * Unlock each item in each chunk and free the chunks.
	 */
	while (licp != NULL) {
		ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
		(void) xfs_trans_unlock_chunk(licp, 1, abort, NULLCOMMITLSN);
		next_licp = licp->lic_next;
		kmem_free(licp, sizeof(xfs_log_item_chunk_t));
		licp = next_licp;
	}

	/*
	 * Reset the transaction structure's free item count.
	 */
	tp->t_items_free = XFS_LIC_NUM_SLOTS;
	tp->t_items.lic_next = NULL;
}



/*
 * This is called to unlock the items associated with a transaction.
 * Items which were not logged should be freed.
 * Those which were logged must still be tracked so they can be unpinned
 * when the transaction commits.
 */
void
xfs_trans_unlock_items(xfs_trans_t *tp, xfs_lsn_t commit_lsn)
{
	xfs_log_item_chunk_t	*licp;
	xfs_log_item_chunk_t	*next_licp;
	xfs_log_item_chunk_t	**licpp;
	int			freed;

	freed = 0;
	licp = &tp->t_items;

	/*
	 * Special case the embedded chunk so we don't free.
	 */
	if (!XFS_LIC_ARE_ALL_FREE(licp)) {
		freed = xfs_trans_unlock_chunk(licp, 0, 0, commit_lsn);
	}
	licpp = &(tp->t_items.lic_next);
	licp = licp->lic_next;

	/*
	 * Unlock each item in each chunk, free non-dirty descriptors,
	 * and free empty chunks.
	 */
	while (licp != NULL) {
		ASSERT(!XFS_LIC_ARE_ALL_FREE(licp));
		freed += xfs_trans_unlock_chunk(licp, 0, 0, commit_lsn);
		next_licp = licp->lic_next;
		if (XFS_LIC_ARE_ALL_FREE(licp)) {
			*licpp = next_licp;
			kmem_free(licp, sizeof(xfs_log_item_chunk_t));
			freed -= XFS_LIC_NUM_SLOTS;
		} else {
			licpp = &(licp->lic_next);
		}
		ASSERT(*licpp == next_licp);
		licp = next_licp;
	}

	/*
	 * Fix the free descriptor count in the transaction.
	 */
	tp->t_items_free += freed;
}

/*
 * Unlock each item pointed to by a descriptor in the given chunk.
 * Stamp the commit lsn into each item if necessary.
 * Free descriptors pointing to items which are not dirty if freeing_chunk
 * is zero. If freeing_chunk is non-zero, then we need to unlock all
 * items in the chunk.
 * 
 * Return the number of descriptors freed.
 */
STATIC int
xfs_trans_unlock_chunk(
	xfs_log_item_chunk_t	*licp,
	int			freeing_chunk,
	int			abort,
	xfs_lsn_t		commit_lsn)
{
	xfs_log_item_desc_t	*lidp;
	xfs_log_item_t		*lip;
	int			i;
	int			freed;

	freed = 0;
	lidp = licp->lic_descs;
	for (i = 0; i < licp->lic_unused; i++, lidp++) {
		if (XFS_LIC_ISFREE(licp, i)) {
			continue;
		}
		lip = lidp->lid_item;
		lip->li_desc = NULL;

		if (commit_lsn != NULLCOMMITLSN)
			IOP_COMMITTING(lip, commit_lsn);
		if (abort)
			lip->li_flags |= XFS_LI_ABORTED;
		IOP_UNLOCK(lip);

		/*
		 * Free the descriptor if the item is not dirty
		 * within this transaction and the caller is not
		 * going to just free the entire thing regardless.
		 */
		if (!(freeing_chunk) &&
		    (!(lidp->lid_flags & XFS_LID_DIRTY) || abort)) {
			XFS_LIC_RELSE(licp, i);
			freed++;
		}
	}

	return freed;
}


/*
 * This is called to add the given busy item to the transaction's
 * list of busy items.  It must find a free busy item descriptor
 * or allocate a new one and add the item to that descriptor.
 * The function returns a pointer to busy descriptor used to point
 * to the new busy entry.  The log busy entry will now point to its new
 * descriptor with its ???? field.
 */
xfs_log_busy_slot_t *
xfs_trans_add_busy(xfs_trans_t *tp, xfs_agnumber_t ag, xfs_extlen_t idx)
{
	xfs_log_busy_chunk_t	*lbcp;
	xfs_log_busy_slot_t	*lbsp;
	int			i=0;

	/*
	 * If there are no free descriptors, allocate a new chunk
	 * of them and put it at the front of the chunk list.
	 */
	if (tp->t_busy_free == 0) {
		lbcp = (xfs_log_busy_chunk_t*)
		       kmem_alloc(sizeof(xfs_log_busy_chunk_t), KM_SLEEP);
		ASSERT(lbcp != NULL);
		/*
		 * Initialize the chunk, and then
		 * claim the first slot in the newly allocated chunk.
		 */
		XFS_LBC_INIT(lbcp);
		XFS_LBC_CLAIM(lbcp, 0);
		lbcp->lbc_unused = 1;
		lbsp = XFS_LBC_SLOT(lbcp, 0);

		/*
		 * Link in the new chunk and update the free count.
		 */
		lbcp->lbc_next = tp->t_busy.lbc_next;
		tp->t_busy.lbc_next = lbcp;
		tp->t_busy_free = XFS_LIC_NUM_SLOTS - 1;

		/*
		 * Initialize the descriptor and the generic portion
		 * of the log item.
		 *
		 * Point the new slot at this item and return it.
		 * Also point the log item at its currently active
		 * descriptor and set the item's mount pointer.
		 */
		lbsp->lbc_ag = ag;
		lbsp->lbc_idx = idx;
		return lbsp;
	}

	/*
	 * Find the free descriptor. It is somewhere in the chunklist
	 * of descriptors.
	 */
	lbcp = &tp->t_busy;
	while (lbcp != NULL) {
		if (XFS_LBC_VACANCY(lbcp)) {
			if (lbcp->lbc_unused <= XFS_LBC_MAX_SLOT) {
				i = lbcp->lbc_unused;
				break;
			} else {
				/* out-of-order vacancy */
				cmn_err(CE_DEBUG, "OOO vacancy lbcp 0x%p\n", lbcp);
				ASSERT(0);
			}
		}
		lbcp = lbcp->lbc_next;
	}
	ASSERT(lbcp != NULL);
	/*
	 * If we find a free descriptor, claim it,
	 * initialize it, and return it.
	 */
	XFS_LBC_CLAIM(lbcp, i);
	if (lbcp->lbc_unused <= i) {
		lbcp->lbc_unused = i + 1;
	}
	lbsp = XFS_LBC_SLOT(lbcp, i);
	tp->t_busy_free--;
	lbsp->lbc_ag = ag;
	lbsp->lbc_idx = idx;
	return lbsp;
}


/*
 * xfs_trans_free_busy
 * Free all of the busy lists from a transaction
 */
void
xfs_trans_free_busy(xfs_trans_t *tp)
{
	xfs_log_busy_chunk_t	*lbcp;
	xfs_log_busy_chunk_t	*lbcq;

	lbcp = tp->t_busy.lbc_next;
	while (lbcp != NULL) {
		lbcq = lbcp->lbc_next;
		kmem_free(lbcp, sizeof(xfs_log_busy_chunk_t));
		lbcp = lbcq;
	}

	XFS_LBC_INIT(&tp->t_busy);
	tp->t_busy.lbc_unused = 0;
}
