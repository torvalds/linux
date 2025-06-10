// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Christoph Hellwig.
 * Copyright (c) 2024-2025, Western Digital Corporation or its affiliates.
 */
#include "xfs.h"
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
xfs_zone_validate_empty(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		*write_pointer)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	if (rtg_rmap(rtg)->i_used_blocks > 0) {
		xfs_warn(mp, "empty zone %u has non-zero used counter (0x%x).",
			 rtg_rgno(rtg), rtg_rmap(rtg)->i_used_blocks);
		return false;
	}

	*write_pointer = 0;
	return true;
}

static bool
xfs_zone_validate_wp(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		*write_pointer)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	xfs_rtblock_t		wp_fsb = xfs_daddr_to_rtb(mp, zone->wp);

	if (rtg_rmap(rtg)->i_used_blocks > rtg->rtg_extents) {
		xfs_warn(mp, "zone %u has too large used counter (0x%x).",
			 rtg_rgno(rtg), rtg_rmap(rtg)->i_used_blocks);
		return false;
	}

	if (xfs_rtb_to_rgno(mp, wp_fsb) != rtg_rgno(rtg)) {
		xfs_warn(mp, "zone %u write pointer (0x%llx) outside of zone.",
			 rtg_rgno(rtg), wp_fsb);
		return false;
	}

	*write_pointer = xfs_rtb_to_rgbno(mp, wp_fsb);
	if (*write_pointer >= rtg->rtg_extents) {
		xfs_warn(mp, "zone %u has invalid write pointer (0x%x).",
			 rtg_rgno(rtg), *write_pointer);
		return false;
	}

	return true;
}

static bool
xfs_zone_validate_full(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		*write_pointer)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	if (rtg_rmap(rtg)->i_used_blocks > rtg->rtg_extents) {
		xfs_warn(mp, "zone %u has too large used counter (0x%x).",
			 rtg_rgno(rtg), rtg_rmap(rtg)->i_used_blocks);
		return false;
	}

	*write_pointer = rtg->rtg_extents;
	return true;
}

static bool
xfs_zone_validate_seq(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		*write_pointer)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	switch (zone->cond) {
	case BLK_ZONE_COND_EMPTY:
		return xfs_zone_validate_empty(zone, rtg, write_pointer);
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
		return xfs_zone_validate_wp(zone, rtg, write_pointer);
	case BLK_ZONE_COND_FULL:
		return xfs_zone_validate_full(zone, rtg, write_pointer);
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
		xfs_warn(mp, "zone %u has unsupported zone condition 0x%x.",
			rtg_rgno(rtg), zone->cond);
		return false;
	default:
		xfs_warn(mp, "zone %u has unknown zone condition 0x%x.",
			rtg_rgno(rtg), zone->cond);
		return false;
	}
}

static bool
xfs_zone_validate_conv(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg)
{
	struct xfs_mount	*mp = rtg_mount(rtg);

	switch (zone->cond) {
	case BLK_ZONE_COND_NOT_WP:
		return true;
	default:
		xfs_warn(mp,
"conventional zone %u has unsupported zone condition 0x%x.",
			 rtg_rgno(rtg), zone->cond);
		return false;
	}
}

bool
xfs_zone_validate(
	struct blk_zone		*zone,
	struct xfs_rtgroup	*rtg,
	xfs_rgblock_t		*write_pointer)
{
	struct xfs_mount	*mp = rtg_mount(rtg);
	struct xfs_groups	*g = &mp->m_groups[XG_TYPE_RTG];
	uint32_t		expected_size;

	/*
	 * Check that the zone capacity matches the rtgroup size stored in the
	 * superblock.  Note that all zones including the last one must have a
	 * uniform capacity.
	 */
	if (XFS_BB_TO_FSB(mp, zone->capacity) != g->blocks) {
		xfs_warn(mp,
"zone %u capacity (0x%llx) does not match RT group size (0x%x).",
			rtg_rgno(rtg), XFS_BB_TO_FSB(mp, zone->capacity),
			g->blocks);
		return false;
	}

	if (g->has_daddr_gaps) {
		expected_size = 1 << g->blklog;
	} else {
		if (zone->len != zone->capacity) {
			xfs_warn(mp,
"zone %u has capacity != size ((0x%llx vs 0x%llx)",
				rtg_rgno(rtg),
				XFS_BB_TO_FSB(mp, zone->len),
				XFS_BB_TO_FSB(mp, zone->capacity));
			return false;
		}
		expected_size = g->blocks;
	}

	if (XFS_BB_TO_FSB(mp, zone->len) != expected_size) {
		xfs_warn(mp,
"zone %u length (0x%llx) does match geometry (0x%x).",
			rtg_rgno(rtg), XFS_BB_TO_FSB(mp, zone->len),
			expected_size);
	}

	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		return xfs_zone_validate_conv(zone, rtg);
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		return xfs_zone_validate_seq(zone, rtg, write_pointer);
	default:
		xfs_warn(mp, "zoned %u has unsupported type 0x%x.",
			rtg_rgno(rtg), zone->type);
		return false;
	}
}
