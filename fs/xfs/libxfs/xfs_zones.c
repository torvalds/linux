// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Christoph Hellwig.
 * Copyright (c) 2024-2025, Western Digital Corporation or its affiliates.
 */
#include "xfs_platform.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_rtgroup.h"
#include "xfs_zones.h"

static bool
xfs_validate_blk_zone_seq(
	struct xfs_mount	*mp,
	struct blk_zone		*zone,
	unsigned int		zone_no,
	xfs_rgblock_t		*write_pointer)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_EMPTY:
		*write_pointer = 0;
		return true;
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
	case BLK_ZONE_COND_ACTIVE:
		if (zone->wp < zone->start ||
		    zone->wp >= zone->start + zone->capacity) {
			xfs_warn(mp,
	"zone %u write pointer (%llu) outside of zone.",
				zone_no, zone->wp);
			return false;
		}

		*write_pointer = XFS_BB_TO_FSB(mp, zone->wp - zone->start);
		return true;
	case BLK_ZONE_COND_FULL:
		*write_pointer = XFS_BB_TO_FSB(mp, zone->capacity);
		return true;
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
		xfs_warn(mp, "zone %u has unsupported zone condition 0x%x.",
			zone_no, zone->cond);
		return false;
	default:
		xfs_warn(mp, "zone %u has unknown zone condition 0x%x.",
			zone_no, zone->cond);
		return false;
	}
}

static bool
xfs_validate_blk_zone_conv(
	struct xfs_mount	*mp,
	struct blk_zone		*zone,
	unsigned int		zone_no)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_NOT_WP:
		return true;
	default:
		xfs_warn(mp,
"conventional zone %u has unsupported zone condition 0x%x.",
			 zone_no, zone->cond);
		return false;
	}
}

bool
xfs_validate_blk_zone(
	struct xfs_mount	*mp,
	struct blk_zone		*zone,
	unsigned int		zone_no,
	uint32_t		expected_size,
	uint32_t		expected_capacity,
	xfs_rgblock_t		*write_pointer)
{
	/*
	 * Check that the zone capacity matches the rtgroup size stored in the
	 * superblock.  Note that all zones including the last one must have a
	 * uniform capacity.
	 */
	if (XFS_BB_TO_FSB(mp, zone->capacity) != expected_capacity) {
		xfs_warn(mp,
"zone %u capacity (%llu) does not match RT group size (%u).",
			zone_no, XFS_BB_TO_FSB(mp, zone->capacity),
			expected_capacity);
		return false;
	}

	if (XFS_BB_TO_FSB(mp, zone->len) != expected_size) {
		xfs_warn(mp,
"zone %u length (%llu) does not match geometry (%u).",
			zone_no, XFS_BB_TO_FSB(mp, zone->len),
			expected_size);
		return false;
	}

	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		return xfs_validate_blk_zone_conv(mp, zone, zone_no);
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		return xfs_validate_blk_zone_seq(mp, zone, zone_no,
				write_pointer);
	default:
		xfs_warn(mp, "zoned %u has unsupported type 0x%x.",
			zone_no, zone->type);
		return false;
	}
}
