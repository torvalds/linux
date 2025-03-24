// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_QM_H__
#define __XFS_QM_H__

#include "xfs_dquot_item.h"
#include "xfs_dquot.h"

struct xfs_inode;

extern struct kmem_cache	*xfs_dqtrx_cache;

/*
 * Number of bmaps that we ask from bmapi when doing a quotacheck.
 * We make this restriction to keep the memory usage to a minimum.
 */
#define XFS_DQITER_MAP_SIZE	10

#define XFS_IS_DQUOT_UNINITIALIZED(dqp) ( \
	!dqp->q_blk.hardlimit && \
	!dqp->q_blk.softlimit && \
	!dqp->q_rtb.hardlimit && \
	!dqp->q_rtb.softlimit && \
	!dqp->q_ino.hardlimit && \
	!dqp->q_ino.softlimit && \
	!dqp->q_blk.count && \
	!dqp->q_rtb.count && \
	!dqp->q_ino.count)

struct xfs_quota_limits {
	xfs_qcnt_t		hard;	/* default hard limit */
	xfs_qcnt_t		soft;	/* default soft limit */
	time64_t		time;	/* limit for timers */
};

/* Defaults for each quota type: time limits, warn limits, usage limits */
struct xfs_def_quota {
	struct xfs_quota_limits	blk;
	struct xfs_quota_limits	ino;
	struct xfs_quota_limits	rtb;
};

/*
 * Various quota information for individual filesystems.
 * The mount structure keeps a pointer to this.
 */
struct xfs_quotainfo {
	struct radix_tree_root	qi_uquota_tree;
	struct radix_tree_root	qi_gquota_tree;
	struct radix_tree_root	qi_pquota_tree;
	struct mutex		qi_tree_lock;
	struct xfs_inode	*qi_uquotaip;	/* user quota inode */
	struct xfs_inode	*qi_gquotaip;	/* group quota inode */
	struct xfs_inode	*qi_pquotaip;	/* project quota inode */
	struct xfs_inode	*qi_dirip;	/* quota metadir */
	struct list_lru		qi_lru;
	int			qi_dquots;
	struct mutex		qi_quotaofflock;/* to serialize quotaoff */
	xfs_filblks_t		qi_dqchunklen;	/* # BBs in a chunk of dqs */
	uint			qi_dqperchunk;	/* # ondisk dq in above chunk */
	struct xfs_def_quota	qi_usr_default;
	struct xfs_def_quota	qi_grp_default;
	struct xfs_def_quota	qi_prj_default;
	struct shrinker		*qi_shrinker;

	/* Minimum and maximum quota expiration timestamp values. */
	time64_t		qi_expiry_min;
	time64_t		qi_expiry_max;

	/* Hook to feed quota counter updates to an active online repair. */
	struct xfs_hooks	qi_mod_ino_dqtrx_hooks;
	struct xfs_hooks	qi_apply_dqtrx_hooks;
};

static inline struct radix_tree_root *
xfs_dquot_tree(
	struct xfs_quotainfo	*qi,
	xfs_dqtype_t		type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return &qi->qi_uquota_tree;
	case XFS_DQTYPE_GROUP:
		return &qi->qi_gquota_tree;
	case XFS_DQTYPE_PROJ:
		return &qi->qi_pquota_tree;
	default:
		ASSERT(0);
	}
	return NULL;
}

static inline struct xfs_inode *
xfs_quota_inode(struct xfs_mount *mp, xfs_dqtype_t type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return mp->m_quotainfo->qi_uquotaip;
	case XFS_DQTYPE_GROUP:
		return mp->m_quotainfo->qi_gquotaip;
	case XFS_DQTYPE_PROJ:
		return mp->m_quotainfo->qi_pquotaip;
	default:
		ASSERT(0);
	}
	return NULL;
}

/*
 * Parameters for tracking dqtrx changes on behalf of an inode.  The hook
 * function arg parameter is the field being updated.
 */
struct xfs_mod_ino_dqtrx_params {
	uintptr_t		tx_id;
	xfs_ino_t		ino;
	xfs_dqtype_t		q_type;
	xfs_dqid_t		q_id;
	int64_t			delta;
};

extern void	xfs_trans_mod_dquot(struct xfs_trans *tp, struct xfs_dquot *dqp,
				    uint field, int64_t delta);
extern void	xfs_trans_dqjoin(struct xfs_trans *, struct xfs_dquot *);
extern void	xfs_trans_log_dquot(struct xfs_trans *, struct xfs_dquot *);

/*
 * We keep the usr, grp, and prj dquots separately so that locking will be
 * easier to do at commit time. All transactions that we know of at this point
 * affect no more than two dquots of one type. Hence, the TRANS_MAXDQS value.
 */
enum {
	XFS_QM_TRANS_USR = 0,
	XFS_QM_TRANS_GRP,
	XFS_QM_TRANS_PRJ,
	XFS_QM_TRANS_DQTYPES
};
#define XFS_QM_TRANS_MAXDQS		5
struct xfs_dquot_acct {
	struct xfs_dqtrx	dqs[XFS_QM_TRANS_DQTYPES][XFS_QM_TRANS_MAXDQS];
};

/*
 * Users are allowed to have a usage exceeding their softlimit for
 * a period this long.
 */
#define XFS_QM_BTIMELIMIT	(7 * 24*60*60)          /* 1 week */
#define XFS_QM_RTBTIMELIMIT	(7 * 24*60*60)          /* 1 week */
#define XFS_QM_ITIMELIMIT	(7 * 24*60*60)          /* 1 week */

extern void		xfs_qm_destroy_quotainfo(struct xfs_mount *);

/* quota ops */
extern int		xfs_qm_scall_trunc_qfiles(struct xfs_mount *, uint);
extern int		xfs_qm_scall_getquota(struct xfs_mount *mp,
					xfs_dqid_t id,
					xfs_dqtype_t type,
					struct qc_dqblk *dst);
extern int		xfs_qm_scall_getquota_next(struct xfs_mount *mp,
					xfs_dqid_t *id,
					xfs_dqtype_t type,
					struct qc_dqblk *dst);
extern int		xfs_qm_scall_setqlim(struct xfs_mount *mp,
					xfs_dqid_t id,
					xfs_dqtype_t type,
					struct qc_dqblk *newlim);
extern int		xfs_qm_scall_quotaon(struct xfs_mount *, uint);
extern int		xfs_qm_scall_quotaoff(struct xfs_mount *, uint);

static inline struct xfs_def_quota *
xfs_get_defquota(struct xfs_quotainfo *qi, xfs_dqtype_t type)
{
	switch (type) {
	case XFS_DQTYPE_USER:
		return &qi->qi_usr_default;
	case XFS_DQTYPE_GROUP:
		return &qi->qi_grp_default;
	case XFS_DQTYPE_PROJ:
		return &qi->qi_prj_default;
	default:
		ASSERT(0);
		return NULL;
	}
}

int xfs_qm_qino_load(struct xfs_mount *mp, xfs_dqtype_t type,
		struct xfs_inode **ipp);

#endif /* __XFS_QM_H__ */
