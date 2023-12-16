// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_bit.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "bitmap.h"
#include "scrub/agb_bitmap.h"

/*
 * Record all btree blocks seen while iterating all records of a btree.
 *
 * We know that the btree query_all function starts at the left edge and walks
 * towards the right edge of the tree.  Therefore, we know that we can walk up
 * the btree cursor towards the root; if the pointer for a given level points
 * to the first record/key in that block, we haven't seen this block before;
 * and therefore we need to remember that we saw this block in the btree.
 *
 * So if our btree is:
 *
 *    4
 *  / | \
 * 1  2  3
 *
 * Pretend for this example that each leaf block has 100 btree records.  For
 * the first btree record, we'll observe that bc_levels[0].ptr == 1, so we
 * record that we saw block 1.  Then we observe that bc_levels[1].ptr == 1, so
 * we record block 4.  The list is [1, 4].
 *
 * For the second btree record, we see that bc_levels[0].ptr == 2, so we exit
 * the loop.  The list remains [1, 4].
 *
 * For the 101st btree record, we've moved onto leaf block 2.  Now
 * bc_levels[0].ptr == 1 again, so we record that we saw block 2.  We see that
 * bc_levels[1].ptr == 2, so we exit the loop.  The list is now [1, 4, 2].
 *
 * For the 102nd record, bc_levels[0].ptr == 2, so we continue.
 *
 * For the 201st record, we've moved on to leaf block 3.
 * bc_levels[0].ptr == 1, so we add 3 to the list.  Now it is [1, 4, 2, 3].
 *
 * For the 300th record we just exit, with the list being [1, 4, 2, 3].
 */

/* Mark a btree block to the agblock bitmap. */
STATIC int
xagb_bitmap_visit_btblock(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*priv)
{
	struct xagb_bitmap	*bitmap = priv;
	struct xfs_buf		*bp;
	xfs_fsblock_t		fsbno;
	xfs_agblock_t		agbno;

	xfs_btree_get_block(cur, level, &bp);
	if (!bp)
		return 0;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, xfs_buf_daddr(bp));
	agbno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);

	return xagb_bitmap_set(bitmap, agbno, 1);
}

/* Mark all (per-AG) btree blocks in the agblock bitmap. */
int
xagb_bitmap_set_btblocks(
	struct xagb_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	return xfs_btree_visit_blocks(cur, xagb_bitmap_visit_btblock,
			XFS_BTREE_VISIT_ALL, bitmap);
}

/*
 * Record all the buffers pointed to by the btree cursor.  Callers already
 * engaged in a btree walk should call this function to capture the list of
 * blocks going from the leaf towards the root.
 */
int
xagb_bitmap_set_btcur_path(
	struct xagb_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	int			i;
	int			error;

	for (i = 0; i < cur->bc_nlevels && cur->bc_levels[i].ptr == 1; i++) {
		error = xagb_bitmap_visit_btblock(cur, i, bitmap);
		if (error)
			return error;
	}

	return 0;
}
