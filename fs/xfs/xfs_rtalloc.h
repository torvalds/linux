// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_RTALLOC_H__
#define	__XFS_RTALLOC_H__

/* kernel only definitions and functions */

struct xfs_mount;
struct xfs_trans;

#ifdef CONFIG_XFS_RT
/* rtgroup superblock initialization */
int xfs_rtmount_readsb(struct xfs_mount *mp);
void xfs_rtmount_freesb(struct xfs_mount *mp);

/*
 * Initialize realtime fields in the mount structure.
 */
int					/* error */
xfs_rtmount_init(
	struct xfs_mount	*mp);	/* file system mount structure */
void
xfs_rtunmount_inodes(
	struct xfs_mount	*mp);

/*
 * Get the bitmap and summary inodes into the mount structure
 * at mount time.
 */
int					/* error */
xfs_rtmount_inodes(
	struct xfs_mount	*mp);	/* file system mount structure */

/*
 * Grow the realtime area of the filesystem.
 */
int
xfs_growfs_rt(
	struct xfs_mount	*mp,	/* file system mount structure */
	xfs_growfs_rt_t		*in);	/* user supplied growfs struct */

int xfs_rtalloc_reinit_frextents(struct xfs_mount *mp);
#else
# define xfs_growfs_rt(mp,in)				(-ENOSYS)
# define xfs_rtalloc_reinit_frextents(m)		(0)
# define xfs_rtmount_readsb(mp)				(0)
# define xfs_rtmount_freesb(mp)				((void)0)
static inline int		/* error */
xfs_rtmount_init(
	xfs_mount_t	*mp)	/* file system mount structure */
{
	if (mp->m_sb.sb_rblocks == 0)
		return 0;

	xfs_warn(mp, "Not built with CONFIG_XFS_RT");
	return -ENOSYS;
}
# define xfs_rtmount_inodes(m)  (((mp)->m_sb.sb_rblocks == 0)? 0 : (-ENOSYS))
# define xfs_rtunmount_inodes(m)
#endif	/* CONFIG_XFS_RT */

#endif	/* __XFS_RTALLOC_H__ */
