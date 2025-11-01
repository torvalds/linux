/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBXFS_ZONES_H
#define _LIBXFS_ZONES_H

struct xfs_rtgroup;

/*
 * In order to guarantee forward progress for GC we need to reserve at least
 * two zones:  one that will be used for moving data into and one spare zone
 * making sure that we have enough space to relocate a nearly-full zone.
 * To allow for slightly sloppy accounting for when we need to reserve the
 * second zone, we actually reserve three as that is easier than doing fully
 * accurate bookkeeping.
 */
#define XFS_GC_ZONES		3U

/*
 * In addition we need two zones for user writes, one open zone for writing
 * and one to still have available blocks without resetting the open zone
 * when data in the open zone has been freed.
 */
#define XFS_RESERVED_ZONES	(XFS_GC_ZONES + 1)
#define XFS_MIN_ZONES		(XFS_RESERVED_ZONES + 1)

/*
 * Always keep one zone out of the general open zone pool to allow for GC to
 * happen while other writers are waiting for free space.
 */
#define XFS_OPEN_GC_ZONES	1U
#define XFS_MIN_OPEN_ZONES	(XFS_OPEN_GC_ZONES + 1U)

/*
 * For zoned devices that do not have a limit on the number of open zones, and
 * for regular devices using the zoned allocator, use the most common SMR disks
 * limit (128) as the default limit on the number of open zones.
 */
#define XFS_DEFAULT_MAX_OPEN_ZONES	128

bool xfs_zone_validate(struct blk_zone *zone, struct xfs_rtgroup *rtg,
	xfs_rgblock_t *write_pointer);

#endif /* _LIBXFS_ZONES_H */
