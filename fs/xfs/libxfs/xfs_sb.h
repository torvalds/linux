// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_SB_H__
#define	__XFS_SB_H__

struct xfs_mount;
struct xfs_sb;
struct xfs_dsb;
struct xfs_trans;
struct xfs_fsop_geom;
struct xfs_perag;

extern void	xfs_log_sb(struct xfs_trans *tp);
extern int	xfs_sync_sb(struct xfs_mount *mp, bool wait);
extern int	xfs_sync_sb_buf(struct xfs_mount *mp);
extern void	xfs_sb_mount_common(struct xfs_mount *mp, struct xfs_sb *sbp);
extern void	xfs_sb_from_disk(struct xfs_sb *to, struct xfs_dsb *from);
extern void	xfs_sb_to_disk(struct xfs_dsb *to, struct xfs_sb *from);
extern void	xfs_sb_quota_from_disk(struct xfs_sb *sbp);
extern bool	xfs_sb_good_version(struct xfs_sb *sbp);
extern uint64_t	xfs_sb_version_to_features(struct xfs_sb *sbp);

extern int	xfs_update_secondary_sbs(struct xfs_mount *mp);

#define XFS_FS_GEOM_MAX_STRUCT_VER	(5)
extern void	xfs_fs_geometry(struct xfs_mount *mp, struct xfs_fsop_geom *geo,
				int struct_version);
extern int	xfs_sb_read_secondary(struct xfs_mount *mp,
				struct xfs_trans *tp, xfs_agnumber_t agno,
				struct xfs_buf **bpp);
extern int	xfs_sb_get_secondary(struct xfs_mount *mp,
				struct xfs_trans *tp, xfs_agnumber_t agno,
				struct xfs_buf **bpp);

bool	xfs_validate_stripe_geometry(struct xfs_mount *mp,
		__s64 sunit, __s64 swidth, int sectorsize, bool may_repair,
		bool silent);

uint8_t xfs_compute_rextslog(xfs_rtbxlen_t rtextents);

#endif	/* __XFS_SB_H__ */
