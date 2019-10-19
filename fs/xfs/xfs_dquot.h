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
typedef struct xfs_dquot {
	uint		 dq_flags;	/* various flags (XFS_DQ_*) */
	struct list_head q_lru;		/* global free list of dquots */
	struct xfs_mount*q_mount;	/* filesystem this relates to */
	uint		 q_nrefs;	/* # active refs from inodes */
	xfs_daddr_t	 q_blkno;	/* blkno of dquot buffer */
	int		 q_bufoffset;	/* off of dq in buffer (# dquots) */
	xfs_fileoff_t	 q_fileoffset;	/* offset in quotas file */

	xfs_disk_dquot_t q_core;	/* actual usage & quotas */
	xfs_dq_logitem_t q_logitem;	/* dquot log item */
	xfs_qcnt_t	 q_res_bcount;	/* total regular nblks used+reserved */
	xfs_qcnt_t	 q_res_icount;	/* total inos allocd+reserved */
	xfs_qcnt_t	 q_res_rtbcount;/* total realtime blks used+reserved */
	xfs_qcnt_t	 q_prealloc_lo_wmark;/* prealloc throttle wmark */
	xfs_qcnt_t	 q_prealloc_hi_wmark;/* prealloc disabled wmark */
	int64_t		 q_low_space[XFS_QLOWSP_MAX];
	struct mutex	 q_qlock;	/* quota lock */
	struct completion q_flush;	/* flush completion queue */
	atomic_t          q_pincount;	/* dquot pin count */
	wait_queue_head_t q_pinwait;	/* dquot pinning wait queue */
} xfs_dquot_t;

/*
 * Lock hierarchy for q_qlock:
 *	XFS_QLOCK_NORMAL is the implicit default,
 * 	XFS_QLOCK_NESTED is the dquot with the higher id in xfs_dqlock2
 */
enum {
	XFS_QLOCK_NORMAL = 0,
	XFS_QLOCK_NESTED,
};

/*
 * Manage the q_flush completion queue embedded in the dquot.  This completion
 * queue synchronizes processes attempting to flush the in-core dquot back to
 * disk.
 */
static inline void xfs_dqflock(xfs_dquot_t *dqp)
{
	wait_for_completion(&dqp->q_flush);
}

static inline bool xfs_dqflock_nowait(xfs_dquot_t *dqp)
{
	return try_wait_for_completion(&dqp->q_flush);
}

static inline void xfs_dqfunlock(xfs_dquot_t *dqp)
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

static inline xfs_dquot_t *xfs_inode_dquot(struct xfs_inode *ip, int type)
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

extern void		xfs_qm_dqdestroy(xfs_dquot_t *);
extern int		xfs_qm_dqflush(struct xfs_dquot *, struct xfs_buf **);
extern void		xfs_qm_dqunpin_wait(xfs_dquot_t *);
extern void		xfs_qm_adjust_dqtimers(xfs_mount_t *,
					xfs_disk_dquot_t *);
extern void		xfs_qm_adjust_dqlimits(struct xfs_mount *,
					       struct xfs_dquot *);
extern xfs_dqid_t	xfs_qm_id_for_quotatype(struct xfs_inode *ip,
					uint type);
extern int		xfs_qm_dqget(struct xfs_mount *mp, xfs_dqid_t id,
					uint type, bool can_alloc,
					struct xfs_dquot **dqpp);
extern int		xfs_qm_dqget_inode(struct xfs_inode *ip, uint type,
					bool can_alloc,
					struct xfs_dquot **dqpp);
extern int		xfs_qm_dqget_next(struct xfs_mount *mp, xfs_dqid_t id,
					uint type, struct xfs_dquot **dqpp);
extern int		xfs_qm_dqget_uncached(struct xfs_mount *mp,
					xfs_dqid_t id, uint type,
					struct xfs_dquot **dqpp);
extern void		xfs_qm_dqput(xfs_dquot_t *);

extern void		xfs_dqlock2(struct xfs_dquot *, struct xfs_dquot *);

extern void		xfs_dquot_set_prealloc_limits(struct xfs_dquot *);

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
