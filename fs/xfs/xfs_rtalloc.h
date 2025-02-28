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

void xfs_rt_resv_free(struct xfs_mount *mp);
int xfs_rt_resv_init(struct xfs_mount *mp);

/*
 * Grow the realtime area of the filesystem.
 */
int
xfs_growfs_rt(
	struct xfs_mount	*mp,	/* file system mount structure */
	xfs_growfs_rt_t		*in);	/* user supplied growfs struct */

int xfs_rtalloc_reinit_frextents(struct xfs_mount *mp);
int xfs_growfs_check_rtgeom(const struct xfs_mount *mp, xfs_rfsblock_t dblocks,
		xfs_rfsblock_t rblocks, xfs_agblock_t rextsize);
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
# define xfs_rt_resv_free(mp)				((void)0)
# define xfs_rt_resv_init(mp)				(0)

static inline int
xfs_growfs_check_rtgeom(const struct xfs_mount *mp,
		xfs_rfsblock_t dblocks, xfs_rfsblock_t rblocks,
		xfs_extlen_t rextsize)
{
	return 0;
}
#endif	/* CONFIG_XFS_RT */

int xfs_rtallocate_rtgs(struct xfs_trans *tp, xfs_fsblock_t bno_hint,
		xfs_rtxlen_t minlen, xfs_rtxlen_t maxlen, xfs_rtxlen_t prod,
		bool wasdel, bool initial_user_data, xfs_rtblock_t *bno,
		xfs_extlen_t *blen);

#endif	/* __XFS_RTALLOC_H__ */
