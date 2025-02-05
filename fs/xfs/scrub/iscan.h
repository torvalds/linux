/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_ISCAN_H__
#define __XFS_SCRUB_ISCAN_H__

struct xchk_iscan {
	struct xfs_scrub	*sc;

	/* Lock to protect the scan cursor. */
	struct mutex		lock;

	/*
	 * This is the first inode in the inumber address space that we
	 * examined.  When the scan wraps around back to here, the scan is
	 * finished.
	 */
	xfs_ino_t		scan_start_ino;

	/* This is the inode that will be examined next. */
	xfs_ino_t		cursor_ino;

	/* If nonzero and non-NULL, skip this inode when scanning. */
	xfs_ino_t		skip_ino;

	/*
	 * This is the last inode that we've successfully scanned, either
	 * because the caller scanned it, or we moved the cursor past an empty
	 * part of the inode address space.  Scan callers should only use the
	 * xchk_iscan_visit function to modify this.
	 */
	xfs_ino_t		__visited_ino;

	/* Operational state of the livescan. */
	unsigned long		__opstate;

	/* Give up on iterating @cursor_ino if we can't iget it by this time. */
	unsigned long		__iget_deadline;

	/* Amount of time (in ms) that we will try to iget an inode. */
	unsigned int		iget_timeout;

	/* Wait this many ms to retry an iget. */
	unsigned int		iget_retry_delay;

	/*
	 * The scan grabs batches of inodes and stashes them here before
	 * handing them out with _iter.  Unallocated inodes are set in the
	 * mask so that all updates to that inode are selected for live
	 * update propagation.
	 */
	xfs_ino_t		__batch_ino;
	xfs_inofree_t		__skipped_inomask;
	struct xfs_inode	*__inodes[XFS_INODES_PER_CHUNK];
};

/* Set if the scan has been aborted due to some event in the fs. */
#define XCHK_ISCAN_OPSTATE_ABORTED	(1)

/* Use trylock to acquire the AGI */
#define XCHK_ISCAN_OPSTATE_TRYLOCK_AGI	(2)

static inline bool
xchk_iscan_aborted(const struct xchk_iscan *iscan)
{
	return test_bit(XCHK_ISCAN_OPSTATE_ABORTED, &iscan->__opstate);
}

static inline void
xchk_iscan_abort(struct xchk_iscan *iscan)
{
	set_bit(XCHK_ISCAN_OPSTATE_ABORTED, &iscan->__opstate);
}

static inline bool
xchk_iscan_agi_needs_trylock(const struct xchk_iscan *iscan)
{
	return test_bit(XCHK_ISCAN_OPSTATE_TRYLOCK_AGI, &iscan->__opstate);
}

static inline void
xchk_iscan_set_agi_trylock(struct xchk_iscan *iscan)
{
	set_bit(XCHK_ISCAN_OPSTATE_TRYLOCK_AGI, &iscan->__opstate);
}

void xchk_iscan_start(struct xfs_scrub *sc, unsigned int iget_timeout,
		unsigned int iget_retry_delay, struct xchk_iscan *iscan);
void xchk_iscan_finish_early(struct xchk_iscan *iscan);
void xchk_iscan_teardown(struct xchk_iscan *iscan);

int xchk_iscan_iter(struct xchk_iscan *iscan, struct xfs_inode **ipp);
void xchk_iscan_iter_finish(struct xchk_iscan *iscan);

void xchk_iscan_mark_visited(struct xchk_iscan *iscan, struct xfs_inode *ip);
bool xchk_iscan_want_live_update(struct xchk_iscan *iscan, xfs_ino_t ino);

#endif /* __XFS_SCRUB_ISCAN_H__ */
