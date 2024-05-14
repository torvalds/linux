/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef __XFS_PWORK_H__
#define __XFS_PWORK_H__

struct xfs_pwork;
struct xfs_mount;

typedef int (*xfs_pwork_work_fn)(struct xfs_mount *mp, struct xfs_pwork *pwork);

/*
 * Parallel work coordination structure.
 */
struct xfs_pwork_ctl {
	struct workqueue_struct	*wq;
	struct xfs_mount	*mp;
	xfs_pwork_work_fn	work_fn;
	struct wait_queue_head	poll_wait;
	atomic_t		nr_work;
	int			error;
};

/*
 * Embed this parallel work control item inside your own work structure,
 * then queue work with it.
 */
struct xfs_pwork {
	struct work_struct	work;
	struct xfs_pwork_ctl	*pctl;
};

#define XFS_PWORK_SINGLE_THREADED	{ .pctl = NULL }

/* Have we been told to abort? */
static inline bool
xfs_pwork_ctl_want_abort(
	struct xfs_pwork_ctl	*pctl)
{
	return pctl && pctl->error;
}

/* Have we been told to abort? */
static inline bool
xfs_pwork_want_abort(
	struct xfs_pwork	*pwork)
{
	return xfs_pwork_ctl_want_abort(pwork->pctl);
}

int xfs_pwork_init(struct xfs_mount *mp, struct xfs_pwork_ctl *pctl,
		xfs_pwork_work_fn work_fn, const char *tag);
void xfs_pwork_queue(struct xfs_pwork_ctl *pctl, struct xfs_pwork *pwork);
int xfs_pwork_destroy(struct xfs_pwork_ctl *pctl);
void xfs_pwork_poll(struct xfs_pwork_ctl *pctl);

#endif /* __XFS_PWORK_H__ */
