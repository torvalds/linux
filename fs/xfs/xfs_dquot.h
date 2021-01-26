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

struct xfs_dquot_res {
	/* Total resources allocated and reserved. */
	xfs_qcnt_t		reserved;

	/* Total resources allocated. */
	xfs_qcnt_t		count;

	/* Absolute and preferred limits. */
	xfs_qcnt_t		hardlimit;
	xfs_qcnt_t		softlimit;

	/*
	 * For root dquots, this is the default grace period, in seconds.
	 * Otherwise, this is when the quota grace period expires,
	 * in seconds since the Unix epoch.
	 */
	time64_t		timer;

	/*
	 * For root dquots, this is the maximum number of warnings that will
	 * be issued for this quota type.  Otherwise, this is the number of
	 * warnings issued against this quota.  Note that none of this is
	 * implemented.
	 */
	xfs_qwarncnt_t		warnings;
};

/*
 * The incore dquot structure
 */
struct xfs_dquot {
	struct list_head	q_lru;
	struct xfs_mount	*q_mount;
	xfs_dqtype_t		q_type;
	uint16_t		q_flags;
	xfs_dqid_t		q_id;
	uint			q_nrefs;
	int			q_bufoffset;
	xfs_daddr_t		q_blkno;
	xfs_fileoff_t		q_fileoffset;

	struct xfs_dquot_res	q_blk;	/* regular blocks */
	struct xfs_dquot_res	q_ino;	/* inodes */
	struct xfs_dquot_res	q_rtb;	/* realtime blocks */

	struct xfs_dq_logitem	q_logitem;

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

static inline int
xfs_dquot_type(const struct xfs_dquot *dqp)
{
	return dqp->q_type & XFS_DQTYPE_REC_MASK;
}

static inline int xfs_this_quota_on(struct xfs_mount *mp, xfs_dqtype_t type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return XFS_IS_UQUOTA_ON(mp);
	case XFS_DQTYPE_GROUP:
		return XFS_IS_GQUOTA_ON(mp);
	case XFS_DQTYPE_PROJ:
		return XFS_IS_PQUOTA_ON(mp);
	default:
		return 0;
	}
}

static inline struct xfs_dquot *xfs_inode_dquot(
	struct xfs_inode	*ip,
	xfs_dqtype_t		type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return ip->i_udquot;
	case XFS_DQTYPE_GROUP:
		return ip->i_gdquot;
	case XFS_DQTYPE_PROJ:
		return ip->i_pdquot;
	default:
		return NULL;
	}
}

/* Decide if the dquot's limits are actually being enforced. */
static inline bool
xfs_dquot_is_enforced(
	const struct xfs_dquot	*dqp)
{
	switch (xfs_dquot_type(dqp)) {
	case XFS_DQTYPE_USER:
		return XFS_IS_UQUOTA_ENFORCED(dqp->q_mount);
	case XFS_DQTYPE_GROUP:
		return XFS_IS_GQUOTA_ENFORCED(dqp->q_mount);
	case XFS_DQTYPE_PROJ:
		return XFS_IS_PQUOTA_ENFORCED(dqp->q_mount);
	}
	ASSERT(0);
	return false;
}

/*
 * Check whether a dquot is under low free space conditions. We assume the quota
 * is enabled and enforced.
 */
static inline bool xfs_dquot_lowsp(struct xfs_dquot *dqp)
{
	int64_t freesp;

	freesp = dqp->q_blk.hardlimit - dqp->q_blk.reserved;
	if (freesp < dqp->q_low_space[XFS_QLOWSP_1_PCNT])
		return true;

	return false;
}

void xfs_dquot_to_disk(struct xfs_disk_dquot *ddqp, struct xfs_dquot *dqp);

#define XFS_DQ_IS_LOCKED(dqp)	(mutex_is_locked(&((dqp)->q_qlock)))
#define XFS_DQ_IS_DIRTY(dqp)	((dqp)->q_flags & XFS_DQFLAG_DIRTY)

void		xfs_qm_dqdestroy(struct xfs_dquot *dqp);
int		xfs_qm_dqflush(struct xfs_dquot *dqp, struct xfs_buf **bpp);
void		xfs_qm_dqunpin_wait(struct xfs_dquot *dqp);
void		xfs_qm_adjust_dqtimers(struct xfs_dquot *d);
void		xfs_qm_adjust_dqlimits(struct xfs_dquot *d);
xfs_dqid_t	xfs_qm_id_for_quotatype(struct xfs_inode *ip,
				xfs_dqtype_t type);
int		xfs_qm_dqget(struct xfs_mount *mp, xfs_dqid_t id,
				xfs_dqtype_t type, bool can_alloc,
				struct xfs_dquot **dqpp);
int		xfs_qm_dqget_inode(struct xfs_inode *ip, xfs_dqtype_t type,
				bool can_alloc, struct xfs_dquot **dqpp);
int		xfs_qm_dqget_next(struct xfs_mount *mp, xfs_dqid_t id,
				xfs_dqtype_t type, struct xfs_dquot **dqpp);
int		xfs_qm_dqget_uncached(struct xfs_mount *mp,
				xfs_dqid_t id, xfs_dqtype_t type,
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

typedef int (*xfs_qm_dqiterate_fn)(struct xfs_dquot *dq,
		xfs_dqtype_t type, void *priv);
int xfs_qm_dqiterate(struct xfs_mount *mp, xfs_dqtype_t type,
		xfs_qm_dqiterate_fn iter_fn, void *priv);

time64_t xfs_dquot_set_timeout(struct xfs_mount *mp, time64_t timeout);
time64_t xfs_dquot_set_grace_period(time64_t grace);

#endif /* __XFS_DQUOT_H__ */
