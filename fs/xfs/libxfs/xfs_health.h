/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_HEALTH_H__
#define __XFS_HEALTH_H__

struct xfs_group;

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
 *  - checked && sick   => metadata needs repair
 *  - checked && !sick  => metadata is ok
 *  - !checked && sick  => errors have been observed during normal operation,
 *                         but the metadata has not been checked thoroughly
 *  - !checked && !sick => has not been examined since mount
 *
 * Evidence of health problems can be sorted into three basic categories:
 *
 * a) Primary evidence, which signals that something is defective within the
 *    general grouping of metadata.
 *
 * b) Secondary evidence, which are side effects of primary problem but are
 *    not themselves problems.  These can be forgotten when the primary
 *    health problems are addressed.
 *
 * c) Indirect evidence, which points to something being wrong in another
 *    group, but we had to release resources and this is all that's left of
 *    that state.
 */

struct xfs_mount;
struct xfs_perag;
struct xfs_inode;
struct xfs_fsop_geom;
struct xfs_btree_cur;
struct xfs_da_args;
struct xfs_rtgroup;

/* Observable health issues for metadata spanning the entire filesystem. */
#define XFS_SICK_FS_COUNTERS	(1 << 0)  /* summary counters */
#define XFS_SICK_FS_UQUOTA	(1 << 1)  /* user quota */
#define XFS_SICK_FS_GQUOTA	(1 << 2)  /* group quota */
#define XFS_SICK_FS_PQUOTA	(1 << 3)  /* project quota */
#define XFS_SICK_FS_QUOTACHECK	(1 << 4)  /* quota counts */
#define XFS_SICK_FS_NLINKS	(1 << 5)  /* inode link counts */
#define XFS_SICK_FS_METADIR	(1 << 6)  /* metadata directory tree */
#define XFS_SICK_FS_METAPATH	(1 << 7)  /* metadata directory tree path */

/* Observable health issues for realtime group metadata. */
#define XFS_SICK_RG_SUPER	(1 << 0)  /* rt group superblock */
#define XFS_SICK_RG_BITMAP	(1 << 1)  /* rt group bitmap */
#define XFS_SICK_RG_SUMMARY	(1 << 2)  /* rt groups summary */

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
#define XFS_SICK_AG_INODES	(1 << 10) /* inactivated bad inodes */

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

/* Don't propagate sick status to ag health summary during inactivation */
#define XFS_SICK_INO_FORGET	(1 << 12)
#define XFS_SICK_INO_DIRTREE	(1 << 13)  /* directory tree structure */

/* Primary evidence of health problems in a given group. */
#define XFS_SICK_FS_PRIMARY	(XFS_SICK_FS_COUNTERS | \
				 XFS_SICK_FS_UQUOTA | \
				 XFS_SICK_FS_GQUOTA | \
				 XFS_SICK_FS_PQUOTA | \
				 XFS_SICK_FS_QUOTACHECK | \
				 XFS_SICK_FS_NLINKS | \
				 XFS_SICK_FS_METADIR | \
				 XFS_SICK_FS_METAPATH)

#define XFS_SICK_RG_PRIMARY	(XFS_SICK_RG_SUPER | \
				 XFS_SICK_RG_BITMAP | \
				 XFS_SICK_RG_SUMMARY)

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
				 XFS_SICK_INO_PARENT | \
				 XFS_SICK_INO_DIRTREE)

#define XFS_SICK_INO_ZAPPED	(XFS_SICK_INO_BMBTD_ZAPPED | \
				 XFS_SICK_INO_BMBTA_ZAPPED | \
				 XFS_SICK_INO_DIR_ZAPPED | \
				 XFS_SICK_INO_SYMLINK_ZAPPED)

/* Secondary state related to (but not primary evidence of) health problems. */
#define XFS_SICK_FS_SECONDARY	(0)
#define XFS_SICK_RG_SECONDARY	(0)
#define XFS_SICK_AG_SECONDARY	(0)
#define XFS_SICK_INO_SECONDARY	(XFS_SICK_INO_FORGET)

/* Evidence of health problems elsewhere. */
#define XFS_SICK_FS_INDIRECT	(0)
#define XFS_SICK_RG_INDIRECT	(0)
#define XFS_SICK_AG_INDIRECT	(XFS_SICK_AG_INODES)
#define XFS_SICK_INO_INDIRECT	(0)

/* All health masks. */
#define XFS_SICK_FS_ALL		(XFS_SICK_FS_PRIMARY | \
				 XFS_SICK_FS_SECONDARY | \
				 XFS_SICK_FS_INDIRECT)

#define XFS_SICK_RG_ALL		(XFS_SICK_RG_PRIMARY | \
				 XFS_SICK_RG_SECONDARY | \
				 XFS_SICK_RG_INDIRECT)

#define XFS_SICK_AG_ALL		(XFS_SICK_AG_PRIMARY | \
				 XFS_SICK_AG_SECONDARY | \
				 XFS_SICK_AG_INDIRECT)

#define XFS_SICK_INO_ALL	(XFS_SICK_INO_PRIMARY | \
				 XFS_SICK_INO_SECONDARY | \
				 XFS_SICK_INO_INDIRECT | \
				 XFS_SICK_INO_ZAPPED)

/*
 * These functions must be provided by the xfs implementation.  Function
 * behavior with respect to the first argument should be as follows:
 *
 * xfs_*_mark_sick:        Set the sick flags and do not set checked flags.
 *                         Runtime code should call this upon encountering
 *                         a corruption.
 *
 * xfs_*_mark_corrupt:     Set the sick and checked flags simultaneously.
 *                         Fsck tools should call this when corruption is
 *                         found.
 *
 * xfs_*_mark_healthy:     Clear the sick flags and set the checked flags.
 *                         Fsck tools should call this after correcting errors.
 *
 * xfs_*_measure_sickness: Return the sick and check status in the provided
 *                         out parameters.
 */

void xfs_fs_mark_sick(struct xfs_mount *mp, unsigned int mask);
void xfs_fs_mark_corrupt(struct xfs_mount *mp, unsigned int mask);
void xfs_fs_mark_healthy(struct xfs_mount *mp, unsigned int mask);
void xfs_fs_measure_sickness(struct xfs_mount *mp, unsigned int *sick,
		unsigned int *checked);

void xfs_rgno_mark_sick(struct xfs_mount *mp, xfs_rgnumber_t rgno,
		unsigned int mask);

void xfs_agno_mark_sick(struct xfs_mount *mp, xfs_agnumber_t agno,
		unsigned int mask);
void xfs_group_mark_sick(struct xfs_group *xg, unsigned int mask);
#define xfs_ag_mark_sick(pag, mask) \
	xfs_group_mark_sick(pag_group(pag), (mask))
void xfs_group_mark_corrupt(struct xfs_group *xg, unsigned int mask);
void xfs_group_mark_healthy(struct xfs_group *xg, unsigned int mask);
void xfs_group_measure_sickness(struct xfs_group *xg, unsigned int *sick,
		unsigned int *checked);

void xfs_inode_mark_sick(struct xfs_inode *ip, unsigned int mask);
void xfs_inode_mark_corrupt(struct xfs_inode *ip, unsigned int mask);
void xfs_inode_mark_healthy(struct xfs_inode *ip, unsigned int mask);
void xfs_inode_measure_sickness(struct xfs_inode *ip, unsigned int *sick,
		unsigned int *checked);

void xfs_health_unmount(struct xfs_mount *mp);
void xfs_bmap_mark_sick(struct xfs_inode *ip, int whichfork);
void xfs_btree_mark_sick(struct xfs_btree_cur *cur);
void xfs_dirattr_mark_sick(struct xfs_inode *ip, int whichfork);
void xfs_da_mark_sick(struct xfs_da_args *args);

/* Now some helpers. */

static inline bool
xfs_fs_has_sickness(struct xfs_mount *mp, unsigned int mask)
{
	unsigned int	sick, checked;

	xfs_fs_measure_sickness(mp, &sick, &checked);
	return sick & mask;
}

static inline bool
xfs_group_has_sickness(
	struct xfs_group	*xg,
	unsigned int		mask)
{
	unsigned int		sick, checked;

	xfs_group_measure_sickness(xg, &sick, &checked);
	return sick & mask;
}

#define xfs_ag_has_sickness(pag, mask) \
	xfs_group_has_sickness(pag_group(pag), (mask))
#define xfs_ag_is_healthy(pag) \
	(!xfs_ag_has_sickness((pag), UINT_MAX))

#define xfs_rtgroup_has_sickness(rtg, mask) \
	xfs_group_has_sickness(rtg_group(rtg), (mask))
#define xfs_rtgroup_is_healthy(rtg) \
	(!xfs_rtgroup_has_sickness((rtg), UINT_MAX))

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
xfs_inode_is_healthy(struct xfs_inode *ip)
{
	return !xfs_inode_has_sickness(ip, -1U);
}

void xfs_fsop_geom_health(struct xfs_mount *mp, struct xfs_fsop_geom *geo);
void xfs_ag_geom_health(struct xfs_perag *pag, struct xfs_ag_geometry *ageo);
void xfs_rtgroup_geom_health(struct xfs_rtgroup *rtg,
		struct xfs_rtgroup_geometry *rgeo);
void xfs_bulkstat_health(struct xfs_inode *ip, struct xfs_bulkstat *bs);

#define xfs_metadata_is_sick(error) \
	(unlikely((error) == -EFSCORRUPTED || (error) == -EFSBADCRC))

#endif	/* __XFS_HEALTH_H__ */
