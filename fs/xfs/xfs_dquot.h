// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DQUOT_H__
#define __XFS_DQUOT_H__

/*
 * Dquots are structures that hold quota information about a user or a group,
 * much like inodes are for files. In fact, dquots share many characteristics
 * with inodes. However, dquots can also be a centralized resource, relative
 * to a collection of inodes. In this respect, dquots share some characteristics
 * of the superblock.
 * XFS dquots exploit both those in its algorithms. They make every attempt
 * to not be a bottleneck when quotas are on and have minimal impact, if any,
 * when quotas are off.
 */

struct xfs_mount;
struct xfs_trans;

enum {
	XFS_QLOWSP_1_PCNT = 0,
	XFS_QLOWSP_3_PCNT,
	XFS_QLOWSP_5_PCNT,
	XFS_QLOWSP_MAX
};

/*
 * The incore dquot structure
 */
struct xfs_dquot {
	uint			dq_flags;
	struct list_head	q_lru;
	struct xfs_mount	*q_mount;
	uint			q_nrefs;
	xfs_daddr_t		q_blkno;
	int			q_bufoffset;
	xfs_fileoff_t		q_fileoffset;

	struct xfs_disk_dquot	q_core;
	struct xfs_dq_logitem	q_logitem;
	/* total regular nblks used+reserved */
	xfs_qcnt_t		q_res_bcount;
	/* total inos allocd+reserved */
	xfs_qcnt_t		q_res_icount;
	/* total realtime blks used+reserved */
	xfs_qcnt_t		q_res_rtbcount;
	xfs_qcnt_t		q_prealloc_lo_wmark;
	xfs_qcnt_t		q_prealloc_hi_wmark;
	int64_t			q_low_space[XFS_QLOWSP_MAX];
	struct mutex		q_qlock;
	struct completion	q_flush;
	atomic_t		q_pincount;
	struct wait_queue_head	q_pinwait;
};

/*
 * Lock hierarchy for q_qlock:
 *	XFS_QLOCK_NORMAL is the implicit default,
 *	XFS_QLOCK_NESTED is the dquot with the higher id in xfs_dqlock2
 */
enum {
	XFS_QLOCK_NORMAL = 0,
	XFS_QLOCK_NESTED,
};

/*
 * Manage the q_flush completion queue embedded in the dquot. This completion
 * queue synchronizes processes attempting to flush the in-core dquot back to
 * disk.
 */
static inline void xfs_dqflock(struct xfs_dquot *dqp)
{
	wait_for_completion(&dqp->q_flush);
}

static inline bool xfs_dqflock_nowait(struct xfs_dquot *dqp)
{
	return try_wait_for_completion(&dqp->q_flush);
}

static inline void xfs_dqfunlock(struct xfs_dquot *dqp)
{
	complete(&dqp->q_flush);
}

static inline int xfs_dqlock_nowait(struct xfs_dquot *dqp)
{
	return mutex_trylock(&dqp->q_qlock);
}

static inline void xfs_dqlock(struct xfs_dquot *dqp)
{
	mutex_lock(&dqp->q_qlock);
}

static inline void xfs_dqunlock(struct xfs_dquot *dqp)
{
	mutex_unlock(&dqp->q_qlock);
}

static inline int xfs_this_quota_on(struct xfs_mount *mp, int type)
{
	switch (type & XFS_DQ_ALLTYPES) {
	case XFS_DQ_USER:
		return XFS_IS_UQUOTA_ON(mp);
	case XFS_DQ_GROUP:
		return XFS_IS_GQUOTA_ON(mp);
	case XFS_DQ_PROJ:
		return XFS_IS_PQUOTA_ON(mp);
	default:
		return 0;
	}
}

static inline struct xfs_dquot *xfs_inode_dquot(struct xfs_inode *ip, int type)
{
	switch (type & XFS_DQ_ALLTYPES) {
	case XFS_DQ_USER:
		return ip->i_udquot;
	case XFS_DQ_GROUP:
		return ip->i_gdquot;
	case XFS_DQ_PROJ:
		return ip->i_pdquot;
	default:
		return NULL;
	}
}

/*
 * Check whether a dquot is under low free space conditions. We assume the quota
 * is enabled and enforced.
 */
static inline bool xfs_dquot_lowsp(struct xfs_dquot *dqp)
{
	int64_t freesp;

	freesp = be64_to_cpu(dqp->q_core.d_blk_hardlimit) - dqp->q_res_bcount;
	if (freesp < dqp->q_low_space[XFS_QLOWSP_1_PCNT])
		return true;

	return false;
}

#define XFS_DQ_IS_LOCKED(dqp)	(mutex_is_locked(&((dqp)->q_qlock)))
#define XFS_DQ_IS_DIRTY(dqp)	((dqp)->dq_flags & XFS_DQ_DIRTY)
#define XFS_QM_ISUDQ(dqp)	((dqp)->dq_flags & XFS_DQ_USER)
#define XFS_QM_ISPDQ(dqp)	((dqp)->dq_flags & XFS_DQ_PROJ)
#define XFS_QM_ISGDQ(dqp)	((dqp)->dq_flags & XFS_DQ_GROUP)

void		xfs_qm_dqdestroy(struct xfs_dquot *dqp);
int		xfs_qm_dqflush(struct xfs_dquot *dqp, struct xfs_buf **bpp);
void		xfs_qm_dqunpin_wait(struct xfs_dquot *dqp);
void		xfs_qm_adjust_dqtimers(struct xfs_mount *mp,
						struct xfs_disk_dquot *d);
void		xfs_qm_adjust_dqlimits(struct xfs_mount *mp,
						struct xfs_dquot *d);
xfs_dqid_t	xfs_qm_id_for_quotatype(struct xfs_inode *ip, uint type);
int		xfs_qm_dqget(struct xfs_mount *mp, xfs_dqid_t id,
					uint type, bool can_alloc,
					struct xfs_dquot **dqpp);
int		xfs_qm_dqget_inode(struct xfs_inode *ip, uint type,
						bool can_alloc,
						struct xfs_dquot **dqpp);
int		xfs_qm_dqget_next(struct xfs_mount *mp, xfs_dqid_t id,
					uint type, struct xfs_dquot **dqpp);
int		xfs_qm_dqget_uncached(struct xfs_mount *mp,
						xfs_dqid_t id, uint type,
						struct xfs_dquot **dqpp);
void		xfs_qm_dqput(struct xfs_dquot *dqp);

void		xfs_dqlock2(struct xfs_dquot *, struct xfs_dquot *);

void		xfs_dquot_set_prealloc_limits(struct xfs_dquot *);

static inline struct xfs_dquot *xfs_qm_dqhold(struct xfs_dquot *dqp)
{
	xfs_dqlock(dqp);
	dqp->q_nrefs++;
	xfs_dqunlock(dqp);
	return dqp;
}

typedef int (*xfs_qm_dqiterate_fn)(struct xfs_dquot *dq, uint dqtype,
		void *priv);
int xfs_qm_dqiterate(struct xfs_mount *mp, uint dqtype,
		xfs_qm_dqiterate_fn iter_fn, void *priv);

#endif /* __XFS_DQUOT_H__ */
