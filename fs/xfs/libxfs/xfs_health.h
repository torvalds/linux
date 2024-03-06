/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_HEALTH_H__
#define __XFS_HEALTH_H__

/*
 * In-Core Filesystem Health Assessments
 * =====================================
 *
 * We'd like to be able to summarize the current health status of the
 * filesystem so that the administrator knows when it's necessary to schedule
 * some downtime for repairs.  Until then, we would also like to avoid abrupt
 * shutdowns due to corrupt metadata.
 *
 * The online scrub feature evaluates the health of all filesystem metadata.
 * When scrub detects corruption in a piece of metadata it will set the
 * corresponding sickness flag, and repair will clear it if successful.  If
 * problems remain at unmount time, we can also request manual intervention by
 * logging a notice to run xfs_repair.
 *
 * Each health tracking group uses a pair of fields for reporting.  The
 * "checked" field tell us if a given piece of metadata has ever been examined,
 * and the "sick" field tells us if that piece was found to need repairs.
 * Therefore we can conclude that for a given sick flag value:
 *
 *  - checked && sick  => metadata needs repair
 *  - checked && !sick => metadata is ok
 *  - !checked         => has not been examined since mount
 */

struct xfs_mount;
struct xfs_perag;
struct xfs_inode;
struct xfs_fsop_geom;

/* Observable health issues for metadata spanning the entire filesystem. */
#define XFS_SICK_FS_COUNTERS	(1 << 0)  /* summary counters */
#define XFS_SICK_FS_UQUOTA	(1 << 1)  /* user quota */
#define XFS_SICK_FS_GQUOTA	(1 << 2)  /* group quota */
#define XFS_SICK_FS_PQUOTA	(1 << 3)  /* project quota */

/* Observable health issues for realtime volume metadata. */
#define XFS_SICK_RT_BITMAP	(1 << 0)  /* realtime bitmap */
#define XFS_SICK_RT_SUMMARY	(1 << 1)  /* realtime summary */

/* Observable health issues for AG metadata. */
#define XFS_SICK_AG_SB		(1 << 0)  /* superblock */
#define XFS_SICK_AG_AGF		(1 << 1)  /* AGF header */
#define XFS_SICK_AG_AGFL	(1 << 2)  /* AGFL header */
#define XFS_SICK_AG_AGI		(1 << 3)  /* AGI header */
#define XFS_SICK_AG_BNOBT	(1 << 4)  /* free space by block */
#define XFS_SICK_AG_CNTBT	(1 << 5)  /* free space by length */
#define XFS_SICK_AG_INOBT	(1 << 6)  /* inode index */
#define XFS_SICK_AG_FINOBT	(1 << 7)  /* free inode index */
#define XFS_SICK_AG_RMAPBT	(1 << 8)  /* reverse mappings */
#define XFS_SICK_AG_REFCNTBT	(1 << 9)  /* reference counts */

/* Observable health issues for inode metadata. */
#define XFS_SICK_INO_CORE	(1 << 0)  /* inode core */
#define XFS_SICK_INO_BMBTD	(1 << 1)  /* data fork */
#define XFS_SICK_INO_BMBTA	(1 << 2)  /* attr fork */
#define XFS_SICK_INO_BMBTC	(1 << 3)  /* cow fork */
#define XFS_SICK_INO_DIR	(1 << 4)  /* directory */
#define XFS_SICK_INO_XATTR	(1 << 5)  /* extended attributes */
#define XFS_SICK_INO_SYMLINK	(1 << 6)  /* symbolic link remote target */
#define XFS_SICK_INO_PARENT	(1 << 7)  /* parent pointers */

#define XFS_SICK_INO_BMBTD_ZAPPED	(1 << 8)  /* data fork erased */
#define XFS_SICK_INO_BMBTA_ZAPPED	(1 << 9)  /* attr fork erased */
#define XFS_SICK_INO_DIR_ZAPPED		(1 << 10) /* directory erased */
#define XFS_SICK_INO_SYMLINK_ZAPPED	(1 << 11) /* symlink erased */

/* Primary evidence of health problems in a given group. */
#define XFS_SICK_FS_PRIMARY	(XFS_SICK_FS_COUNTERS | \
				 XFS_SICK_FS_UQUOTA | \
				 XFS_SICK_FS_GQUOTA | \
				 XFS_SICK_FS_PQUOTA)

#define XFS_SICK_RT_PRIMARY	(XFS_SICK_RT_BITMAP | \
				 XFS_SICK_RT_SUMMARY)

#define XFS_SICK_AG_PRIMARY	(XFS_SICK_AG_SB | \
				 XFS_SICK_AG_AGF | \
				 XFS_SICK_AG_AGFL | \
				 XFS_SICK_AG_AGI | \
				 XFS_SICK_AG_BNOBT | \
				 XFS_SICK_AG_CNTBT | \
				 XFS_SICK_AG_INOBT | \
				 XFS_SICK_AG_FINOBT | \
				 XFS_SICK_AG_RMAPBT | \
				 XFS_SICK_AG_REFCNTBT)

#define XFS_SICK_INO_PRIMARY	(XFS_SICK_INO_CORE | \
				 XFS_SICK_INO_BMBTD | \
				 XFS_SICK_INO_BMBTA | \
				 XFS_SICK_INO_BMBTC | \
				 XFS_SICK_INO_DIR | \
				 XFS_SICK_INO_XATTR | \
				 XFS_SICK_INO_SYMLINK | \
				 XFS_SICK_INO_PARENT)

#define XFS_SICK_INO_ZAPPED	(XFS_SICK_INO_BMBTD_ZAPPED | \
				 XFS_SICK_INO_BMBTA_ZAPPED | \
				 XFS_SICK_INO_DIR_ZAPPED | \
				 XFS_SICK_INO_SYMLINK_ZAPPED)

/* These functions must be provided by the xfs implementation. */

void xfs_fs_mark_sick(struct xfs_mount *mp, unsigned int mask);
void xfs_fs_mark_healthy(struct xfs_mount *mp, unsigned int mask);
void xfs_fs_measure_sickness(struct xfs_mount *mp, unsigned int *sick,
		unsigned int *checked);

void xfs_rt_mark_sick(struct xfs_mount *mp, unsigned int mask);
void xfs_rt_mark_healthy(struct xfs_mount *mp, unsigned int mask);
void xfs_rt_measure_sickness(struct xfs_mount *mp, unsigned int *sick,
		unsigned int *checked);

void xfs_ag_mark_sick(struct xfs_perag *pag, unsigned int mask);
void xfs_ag_mark_healthy(struct xfs_perag *pag, unsigned int mask);
void xfs_ag_measure_sickness(struct xfs_perag *pag, unsigned int *sick,
		unsigned int *checked);

void xfs_inode_mark_sick(struct xfs_inode *ip, unsigned int mask);
void xfs_inode_mark_healthy(struct xfs_inode *ip, unsigned int mask);
void xfs_inode_measure_sickness(struct xfs_inode *ip, unsigned int *sick,
		unsigned int *checked);

void xfs_health_unmount(struct xfs_mount *mp);

/* Now some helpers. */

static inline bool
xfs_fs_has_sickness(struct xfs_mount *mp, unsigned int mask)
{
	unsigned int	sick, checked;

	xfs_fs_measure_sickness(mp, &sick, &checked);
	return sick & mask;
}

static inline bool
xfs_rt_has_sickness(struct xfs_mount *mp, unsigned int mask)
{
	unsigned int	sick, checked;

	xfs_rt_measure_sickness(mp, &sick, &checked);
	return sick & mask;
}

static inline bool
xfs_ag_has_sickness(struct xfs_perag *pag, unsigned int mask)
{
	unsigned int	sick, checked;

	xfs_ag_measure_sickness(pag, &sick, &checked);
	return sick & mask;
}

static inline bool
xfs_inode_has_sickness(struct xfs_inode *ip, unsigned int mask)
{
	unsigned int	sick, checked;

	xfs_inode_measure_sickness(ip, &sick, &checked);
	return sick & mask;
}

static inline bool
xfs_fs_is_healthy(struct xfs_mount *mp)
{
	return !xfs_fs_has_sickness(mp, -1U);
}

static inline bool
xfs_rt_is_healthy(struct xfs_mount *mp)
{
	return !xfs_rt_has_sickness(mp, -1U);
}

static inline bool
xfs_ag_is_healthy(struct xfs_perag *pag)
{
	return !xfs_ag_has_sickness(pag, -1U);
}

static inline bool
xfs_inode_is_healthy(struct xfs_inode *ip)
{
	return !xfs_inode_has_sickness(ip, -1U);
}

void xfs_fsop_geom_health(struct xfs_mount *mp, struct xfs_fsop_geom *geo);
void xfs_ag_geom_health(struct xfs_perag *pag, struct xfs_ag_geometry *ageo);
void xfs_bulkstat_health(struct xfs_inode *ip, struct xfs_bulkstat *bs);

#endif	/* __XFS_HEALTH_H__ */
