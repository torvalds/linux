/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_trans_priv.h"
#include "xfs_extfree_item.h"

/*
 * This routine is called to allocate an "extent free intention"
 * log item that will hold nextents worth of extents.  The
 * caller must use all nextents extents, because we are not
 * flexible about this at all.
 */
xfs_efi_log_item_t *
xfs_trans_get_efi(xfs_trans_t	*tp,
		  uint		nextents)
{
	xfs_efi_log_item_t	*efip;

	ASSERT(tp != NULL);
	ASSERT(nextents > 0);

	efip = xfs_efi_init(tp->t_mountp, nextents);
	ASSERT(efip != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)efip);

	return (efip);
}

/*
 * This routine is called to indicate that the described
 * extent is to be logged as needing to be freed.  It should
 * be called once for each extent to be freed.
 */
void
xfs_trans_log_efi_extent(xfs_trans_t		*tp,
			 xfs_efi_log_item_t	*efip,
			 xfs_fsblock_t		start_block,
			 xfs_extlen_t		ext_len)
{
	xfs_log_item_desc_t	*lidp;
	uint			next_extent;
	xfs_extent_t		*extp;

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)efip);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;

	next_extent = efip->efi_next_extent;
	ASSERT(next_extent < efip->efi_format.efi_nextents);
	extp = &(efip->efi_format.efi_extents[next_extent]);
	extp->ext_start = start_block;
	extp->ext_len = ext_len;
	efip->efi_next_extent++;
}


/*
 * This routine is called to allocate an "extent free done"
 * log item that will hold nextents worth of extents.  The
 * caller must use all nextents extents, because we are not
 * flexible about this at all.
 */
xfs_efd_log_item_t *
xfs_trans_get_efd(xfs_trans_t		*tp,
		  xfs_efi_log_item_t	*efip,
		  uint			nextents)
{
	xfs_efd_log_item_t	*efdp;

	ASSERT(tp != NULL);
	ASSERT(nextents > 0);

	efdp = xfs_efd_init(tp->t_mountp, efip, nextents);
	ASSERT(efdp != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)efdp);

	return (efdp);
}

/*
 * This routine is called to indicate that the described
 * extent is to be logged as having been freed.  It should
 * be called once for each extent freed.
 */
void
xfs_trans_log_efd_extent(xfs_trans_t		*tp,
			 xfs_efd_log_item_t	*efdp,
			 xfs_fsblock_t		start_block,
			 xfs_extlen_t		ext_len)
{
	xfs_log_item_desc_t	*lidp;
	uint			next_extent;
	xfs_extent_t		*extp;

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)efdp);
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;

	next_extent = efdp->efd_next_extent;
	ASSERT(next_extent < efdp->efd_format.efd_nextents);
	extp = &(efdp->efd_format.efd_extents[next_extent]);
	extp->ext_start = start_block;
	extp->ext_len = ext_len;
	efdp->efd_next_extent++;
}
