// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_REAP_H__
#define __XFS_SCRUB_REAP_H__

struct xagb_bitmap;
struct xfsb_bitmap;

int xrep_reap_agblocks(struct xfs_scrub *sc, struct xagb_bitmap *bitmap,
		const struct xfs_owner_info *oinfo, enum xfs_ag_resv_type type);
int xrep_reap_fsblocks(struct xfs_scrub *sc, struct xfsb_bitmap *bitmap,
		const struct xfs_owner_info *oinfo);
int xrep_reap_ifork(struct xfs_scrub *sc, struct xfs_inode *ip, int whichfork);
int xrep_reap_metadir_fsblocks(struct xfs_scrub *sc,
		struct xfsb_bitmap *bitmap);

#ifdef CONFIG_XFS_RT
int xrep_reap_rtblocks(struct xfs_scrub *sc, struct xrtb_bitmap *bitmap,
		const struct xfs_owner_info *oinfo);
#else
# define xrep_reap_rtblocks(...)	(-EOPNOTSUPP)
#endif /* CONFIG_XFS_RT */

/* Buffer cache scan context. */
struct xrep_bufscan {
	/* Disk address for the buffers we want to scan. */
	xfs_daddr_t		daddr;

	/* Maximum number of sectors to scan. */
	xfs_daddr_t		max_sectors;

	/* Each round, increment the search length by this number of sectors. */
	xfs_daddr_t		daddr_step;

	/* Internal scan state; initialize to zero. */
	xfs_daddr_t		__sector_count;
};

xfs_daddr_t xrep_bufscan_max_sectors(struct xfs_mount *mp,
		xfs_extlen_t fsblocks);
struct xfs_buf *xrep_bufscan_advance(struct xfs_mount *mp,
		struct xrep_bufscan *scan);

#endif /* __XFS_SCRUB_REAP_H__ */
