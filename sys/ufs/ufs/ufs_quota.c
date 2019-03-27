/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_quota.c	8.5 (Berkeley) 5/20/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ffs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

CTASSERT(sizeof(struct dqblk64) == sizeof(struct dqhdr64));

static int unprivileged_get_quota = 0;
SYSCTL_INT(_security_bsd, OID_AUTO, unprivileged_get_quota, CTLFLAG_RW,
    &unprivileged_get_quota, 0,
    "Unprivileged processes may retrieve quotas for other uids and gids");

static MALLOC_DEFINE(M_DQUOT, "ufs_quota", "UFS quota entries");

/*
 * Quota name to error message mapping.
 */
static char *quotatypes[] = INITQFNAMES;

static int chkdqchg(struct inode *, ufs2_daddr_t, struct ucred *, int, int *);
static int chkiqchg(struct inode *, int, struct ucred *, int, int *);
static int dqopen(struct vnode *, struct ufsmount *, int);
static int dqget(struct vnode *,
	u_long, struct ufsmount *, int, struct dquot **);
static int dqsync(struct vnode *, struct dquot *);
static int dqflush(struct vnode *);
static int quotaoff1(struct thread *td, struct mount *mp, int type);
static int quotaoff_inchange(struct thread *td, struct mount *mp, int type);

/* conversion functions - from_to() */
static void dqb32_dq(const struct dqblk32 *, struct dquot *);
static void dqb64_dq(const struct dqblk64 *, struct dquot *);
static void dq_dqb32(const struct dquot *, struct dqblk32 *);
static void dq_dqb64(const struct dquot *, struct dqblk64 *);
static void dqb32_dqb64(const struct dqblk32 *, struct dqblk64 *);
static void dqb64_dqb32(const struct dqblk64 *, struct dqblk32 *);

#ifdef DIAGNOSTIC
static void dqref(struct dquot *);
static void chkdquot(struct inode *);
#endif

/*
 * Set up the quotas for an inode.
 *
 * This routine completely defines the semantics of quotas.
 * If other criterion want to be used to establish quotas, the
 * MAXQUOTAS value in quota.h should be increased, and the
 * additional dquots set up here.
 */
int
getinoquota(struct inode *ip)
{
	struct ufsmount *ump;
	struct vnode *vp;
	int error;

	vp = ITOV(ip);

	/*
	 * Disk quotas must be turned off for system files.  Currently
	 * snapshot and quota files.
	 */
	if ((vp->v_vflag & VV_SYSTEM) != 0)
		return (0);
	/*
	 * XXX: Turn off quotas for files with a negative UID or GID.
	 * This prevents the creation of 100GB+ quota files.
	 */
	if ((int)ip->i_uid < 0 || (int)ip->i_gid < 0)
		return (0);
	ump = VFSTOUFS(vp->v_mount);
	/*
	 * Set up the user quota based on file uid.
	 * EINVAL means that quotas are not enabled.
	 */
	if ((error =
		dqget(vp, ip->i_uid, ump, USRQUOTA, &ip->i_dquot[USRQUOTA])) &&
	    error != EINVAL)
		return (error);
	/*
	 * Set up the group quota based on file gid.
	 * EINVAL means that quotas are not enabled.
	 */
	if ((error =
		dqget(vp, ip->i_gid, ump, GRPQUOTA, &ip->i_dquot[GRPQUOTA])) &&
	    error != EINVAL)
		return (error);
	return (0);
}

/*
 * Update disk usage, and take corrective action.
 */
int
chkdq(struct inode *ip, ufs2_daddr_t change, struct ucred *cred, int flags)
{
	struct dquot *dq;
	ufs2_daddr_t ncurblocks;
	struct vnode *vp = ITOV(ip);
	int i, error, warn, do_check;

	/*
	 * Disk quotas must be turned off for system files.  Currently
	 * snapshot and quota files.
	 */
	if ((vp->v_vflag & VV_SYSTEM) != 0)
		return (0);
	/*
	 * XXX: Turn off quotas for files with a negative UID or GID.
	 * This prevents the creation of 100GB+ quota files.
	 */
	if ((int)ip->i_uid < 0 || (int)ip->i_gid < 0)
		return (0);
#ifdef DIAGNOSTIC
	if ((flags & CHOWN) == 0)
		chkdquot(ip);
#endif
	if (change == 0)
		return (0);
	if (change < 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			DQI_LOCK(dq);
			DQI_WAIT(dq, PINOD+1, "chkdq1");
			ncurblocks = dq->dq_curblocks + change;
			if (ncurblocks >= 0)
				dq->dq_curblocks = ncurblocks;
			else
				dq->dq_curblocks = 0;
			dq->dq_flags &= ~DQ_BLKS;
			dq->dq_flags |= DQ_MOD;
			DQI_UNLOCK(dq);
		}
		return (0);
	}
	if ((flags & FORCE) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
		do_check = 1;
	else
		do_check = 0;
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		warn = 0;
		DQI_LOCK(dq);
		DQI_WAIT(dq, PINOD+1, "chkdq2");
		if (do_check) {
			error = chkdqchg(ip, change, cred, i, &warn);
			if (error) {
				/*
				 * Roll back user quota changes when
				 * group quota failed.
				 */
				while (i > 0) {
					--i;
					dq = ip->i_dquot[i];
					if (dq == NODQUOT)
						continue;
					DQI_LOCK(dq);
					DQI_WAIT(dq, PINOD+1, "chkdq3");
					ncurblocks = dq->dq_curblocks - change;
					if (ncurblocks >= 0)
						dq->dq_curblocks = ncurblocks;
					else
						dq->dq_curblocks = 0;
					dq->dq_flags &= ~DQ_BLKS;
					dq->dq_flags |= DQ_MOD;
					DQI_UNLOCK(dq);
				}
				return (error);
			}
		}
		/* Reset timer when crossing soft limit */
		if (dq->dq_curblocks + change >= dq->dq_bsoftlimit &&
		    dq->dq_curblocks < dq->dq_bsoftlimit)
			dq->dq_btime = time_second + ITOUMP(ip)->um_btime[i];
		dq->dq_curblocks += change;
		dq->dq_flags |= DQ_MOD;
		DQI_UNLOCK(dq);
		if (warn)
			uprintf("\n%s: warning, %s disk quota exceeded\n",
			    ITOVFS(ip)->mnt_stat.f_mntonname,
			    quotatypes[i]);
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
static int
chkdqchg(struct inode *ip, ufs2_daddr_t change, struct ucred *cred,
    int type, int *warn)
{
	struct dquot *dq = ip->i_dquot[type];
	ufs2_daddr_t ncurblocks = dq->dq_curblocks + change;

	/*
	 * If user would exceed their hard limit, disallow space allocation.
	 */
	if (ncurblocks >= dq->dq_bhardlimit && dq->dq_bhardlimit) {
		if ((dq->dq_flags & DQ_BLKS) == 0 &&
		    ip->i_uid == cred->cr_uid) {
			dq->dq_flags |= DQ_BLKS;
			DQI_UNLOCK(dq);
			uprintf("\n%s: write failed, %s disk limit reached\n",
			    ITOVFS(ip)->mnt_stat.f_mntonname,
			    quotatypes[type]);
			return (EDQUOT);
		}
		DQI_UNLOCK(dq);
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow space
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurblocks >= dq->dq_bsoftlimit && dq->dq_bsoftlimit) {
		if (dq->dq_curblocks < dq->dq_bsoftlimit) {
			dq->dq_btime = time_second + ITOUMP(ip)->um_btime[type];
			if (ip->i_uid == cred->cr_uid)
				*warn = 1;
			return (0);
		}
		if (time_second > dq->dq_btime) {
			if ((dq->dq_flags & DQ_BLKS) == 0 &&
			    ip->i_uid == cred->cr_uid) {
				dq->dq_flags |= DQ_BLKS;
				DQI_UNLOCK(dq);
				uprintf("\n%s: write failed, %s "
				    "disk quota exceeded for too long\n",
				    ITOVFS(ip)->mnt_stat.f_mntonname,
				    quotatypes[type]);
				return (EDQUOT);
			}
			DQI_UNLOCK(dq);
			return (EDQUOT);
		}
	}
	return (0);
}

/*
 * Check the inode limit, applying corrective action.
 */
int
chkiq(struct inode *ip, int change, struct ucred *cred, int flags)
{
	struct dquot *dq;
	int i, error, warn, do_check;

#ifdef DIAGNOSTIC
	if ((flags & CHOWN) == 0)
		chkdquot(ip);
#endif
	if (change == 0)
		return (0);
	if (change < 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			DQI_LOCK(dq);
			DQI_WAIT(dq, PINOD+1, "chkiq1");
			if (dq->dq_curinodes >= -change)
				dq->dq_curinodes += change;
			else
				dq->dq_curinodes = 0;
			dq->dq_flags &= ~DQ_INODS;
			dq->dq_flags |= DQ_MOD;
			DQI_UNLOCK(dq);
		}
		return (0);
	}
	if ((flags & FORCE) == 0 &&
	    priv_check_cred(cred, PRIV_VFS_EXCEEDQUOTA))
		do_check = 1;
	else
		do_check = 0;
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		warn = 0;
		DQI_LOCK(dq);
		DQI_WAIT(dq, PINOD+1, "chkiq2");
		if (do_check) {
			error = chkiqchg(ip, change, cred, i, &warn);
			if (error) {
				/*
				 * Roll back user quota changes when
				 * group quota failed.
				 */
				while (i > 0) {
					--i;
					dq = ip->i_dquot[i];
					if (dq == NODQUOT)
						continue;
					DQI_LOCK(dq);
					DQI_WAIT(dq, PINOD+1, "chkiq3");
					if (dq->dq_curinodes >= change)
						dq->dq_curinodes -= change;
					else
						dq->dq_curinodes = 0;
					dq->dq_flags &= ~DQ_INODS;
					dq->dq_flags |= DQ_MOD;
					DQI_UNLOCK(dq);
				}
				return (error);
			}
		}
		/* Reset timer when crossing soft limit */
		if (dq->dq_curinodes + change >= dq->dq_isoftlimit &&
		    dq->dq_curinodes < dq->dq_isoftlimit)
			dq->dq_itime = time_second + ITOUMP(ip)->um_itime[i];
		dq->dq_curinodes += change;
		dq->dq_flags |= DQ_MOD;
		DQI_UNLOCK(dq);
		if (warn)
			uprintf("\n%s: warning, %s inode quota exceeded\n",
			    ITOVFS(ip)->mnt_stat.f_mntonname,
			    quotatypes[i]);
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
static int
chkiqchg(struct inode *ip, int change, struct ucred *cred, int type, int *warn)
{
	struct dquot *dq = ip->i_dquot[type];
	ino_t ncurinodes = dq->dq_curinodes + change;

	/*
	 * If user would exceed their hard limit, disallow inode allocation.
	 */
	if (ncurinodes >= dq->dq_ihardlimit && dq->dq_ihardlimit) {
		if ((dq->dq_flags & DQ_INODS) == 0 &&
		    ip->i_uid == cred->cr_uid) {
			dq->dq_flags |= DQ_INODS;
			DQI_UNLOCK(dq);
			uprintf("\n%s: write failed, %s inode limit reached\n",
			    ITOVFS(ip)->mnt_stat.f_mntonname,
			    quotatypes[type]);
			return (EDQUOT);
		}
		DQI_UNLOCK(dq);
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow inode
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurinodes >= dq->dq_isoftlimit && dq->dq_isoftlimit) {
		if (dq->dq_curinodes < dq->dq_isoftlimit) {
			dq->dq_itime = time_second + ITOUMP(ip)->um_itime[type];
			if (ip->i_uid == cred->cr_uid)
				*warn = 1;
			return (0);
		}
		if (time_second > dq->dq_itime) {
			if ((dq->dq_flags & DQ_INODS) == 0 &&
			    ip->i_uid == cred->cr_uid) {
				dq->dq_flags |= DQ_INODS;
				DQI_UNLOCK(dq);
				uprintf("\n%s: write failed, %s "
				    "inode quota exceeded for too long\n",
				    ITOVFS(ip)->mnt_stat.f_mntonname,
				    quotatypes[type]);
				return (EDQUOT);
			}
			DQI_UNLOCK(dq);
			return (EDQUOT);
		}
	}
	return (0);
}

#ifdef DIAGNOSTIC
/*
 * On filesystems with quotas enabled, it is an error for a file to change
 * size and not to have a dquot structure associated with it.
 */
static void
chkdquot(struct inode *ip)
{
	struct ufsmount *ump;
	struct vnode *vp;
	int i;

	ump = ITOUMP(ip);
	vp = ITOV(ip);

	/*
	 * Disk quotas must be turned off for system files.  Currently
	 * these are snapshots and quota files.
	 */
	if ((vp->v_vflag & VV_SYSTEM) != 0)
		return;
	/*
	 * XXX: Turn off quotas for files with a negative UID or GID.
	 * This prevents the creation of 100GB+ quota files.
	 */
	if ((int)ip->i_uid < 0 || (int)ip->i_gid < 0)
		return;

	UFS_LOCK(ump);
	for (i = 0; i < MAXQUOTAS; i++) {
		if (ump->um_quotas[i] == NULLVP ||
		    (ump->um_qflags[i] & (QTF_OPENING|QTF_CLOSING)))
			continue;
		if (ip->i_dquot[i] == NODQUOT) {
			UFS_UNLOCK(ump);
			vn_printf(ITOV(ip), "chkdquot: missing dquot ");
			panic("chkdquot: missing dquot");
		}
	}
	UFS_UNLOCK(ump);
}
#endif

/*
 * Code to process quotactl commands.
 */

/*
 * Q_QUOTAON - set up a quota file for a particular filesystem.
 */
int
quotaon(struct thread *td, struct mount *mp, int type, void *fname)
{
	struct ufsmount *ump;
	struct vnode *vp, **vpp;
	struct vnode *mvp;
	struct dquot *dq;
	int error, flags;
	struct nameidata nd;

	error = priv_check(td, PRIV_UFS_QUOTAON);
	if (error != 0) {
		vfs_unbusy(mp);
		return (error);
	}

	if ((mp->mnt_flag & MNT_RDONLY) != 0) {
		vfs_unbusy(mp);
		return (EROFS);
	}

	ump = VFSTOUFS(mp);
	dq = NODQUOT;

	NDINIT(&nd, LOOKUP, FOLLOW, UIO_USERSPACE, fname, td);
	flags = FREAD | FWRITE;
	vfs_ref(mp);
	vfs_unbusy(mp);
	error = vn_open(&nd, &flags, 0, NULL);
	if (error != 0) {
		vfs_rel(mp);
		return (error);
	}
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vp = nd.ni_vp;
	error = vfs_busy(mp, MBF_NOWAIT);
	vfs_rel(mp);
	if (error == 0) {
		if (vp->v_type != VREG) {
			error = EACCES;
			vfs_unbusy(mp);
		}
	}
	if (error != 0) {
		VOP_UNLOCK(vp, 0);
		(void) vn_close(vp, FREAD|FWRITE, td->td_ucred, td);
		return (error);
	}

	UFS_LOCK(ump);
	if ((ump->um_qflags[type] & (QTF_OPENING|QTF_CLOSING)) != 0) {
		UFS_UNLOCK(ump);
		VOP_UNLOCK(vp, 0);
		(void) vn_close(vp, FREAD|FWRITE, td->td_ucred, td);
		vfs_unbusy(mp);
		return (EALREADY);
	}
	ump->um_qflags[type] |= QTF_OPENING|QTF_CLOSING;
	UFS_UNLOCK(ump);
	if ((error = dqopen(vp, ump, type)) != 0) {
		VOP_UNLOCK(vp, 0);
		UFS_LOCK(ump);
		ump->um_qflags[type] &= ~(QTF_OPENING|QTF_CLOSING);
		UFS_UNLOCK(ump);
		(void) vn_close(vp, FREAD|FWRITE, td->td_ucred, td);
		vfs_unbusy(mp);
		return (error);
	}
	VOP_UNLOCK(vp, 0);
	MNT_ILOCK(mp);
	mp->mnt_flag |= MNT_QUOTA;
	MNT_IUNLOCK(mp);

	vpp = &ump->um_quotas[type];
	if (*vpp != vp)
		quotaoff1(td, mp, type);

	/*
	 * When the directory vnode containing the quota file is
	 * inactivated, due to the shared lookup of the quota file
	 * vput()ing the dvp, the qsyncvp() call for the containing
	 * directory would try to acquire the quota lock exclusive.
	 * At the same time, lookup already locked the quota vnode
	 * shared.  Mark the quota vnode lock as allowing recursion
	 * and automatically converting shared locks to exclusive.
	 *
	 * Also mark quota vnode as system.
	 */
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vp->v_vflag |= VV_SYSTEM;
	VN_LOCK_AREC(vp);
	VN_LOCK_DSHARE(vp);
	VOP_UNLOCK(vp, 0);
	*vpp = vp;
	/*
	 * Save the credential of the process that turned on quotas.
	 * Set up the time limits for this quota.
	 */
	ump->um_cred[type] = crhold(td->td_ucred);
	ump->um_btime[type] = MAX_DQ_TIME;
	ump->um_itime[type] = MAX_IQ_TIME;
	if (dqget(NULLVP, 0, ump, type, &dq) == 0) {
		if (dq->dq_btime > 0)
			ump->um_btime[type] = dq->dq_btime;
		if (dq->dq_itime > 0)
			ump->um_itime[type] = dq->dq_itime;
		dqrele(NULLVP, dq);
	}
	/*
	 * Allow the getdq from getinoquota below to read the quota
	 * from file.
	 */
	UFS_LOCK(ump);
	ump->um_qflags[type] &= ~QTF_CLOSING;
	UFS_UNLOCK(ump);
	/*
	 * Search vnodes associated with this mount point,
	 * adding references to quota file being opened.
	 * NB: only need to add dquot's for inodes being modified.
	 */
again:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto again;
		}
		if (vp->v_type == VNON || vp->v_writecount == 0) {
			VOP_UNLOCK(vp, 0);
			vrele(vp);
			continue;
		}
		error = getinoquota(VTOI(vp));
		VOP_UNLOCK(vp, 0);
		vrele(vp);
		if (error) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			break;
		}
	}

        if (error)
		quotaoff_inchange(td, mp, type);
	UFS_LOCK(ump);
	ump->um_qflags[type] &= ~QTF_OPENING;
	KASSERT((ump->um_qflags[type] & QTF_CLOSING) == 0,
		("quotaon: leaking flags"));
	UFS_UNLOCK(ump);

	vfs_unbusy(mp);
	return (error);
}

/*
 * Main code to turn off disk quotas for a filesystem. Does not change
 * flags.
 */
static int
quotaoff1(struct thread *td, struct mount *mp, int type)
{
	struct vnode *vp;
	struct vnode *qvp, *mvp;
	struct ufsmount *ump;
	struct dquot *dq;
	struct inode *ip;
	struct ucred *cr;
	int error;

	ump = VFSTOUFS(mp);

	UFS_LOCK(ump);
	KASSERT((ump->um_qflags[type] & QTF_CLOSING) != 0,
		("quotaoff1: flags are invalid"));
	if ((qvp = ump->um_quotas[type]) == NULLVP) {
		UFS_UNLOCK(ump);
		return (0);
	}
	cr = ump->um_cred[type];
	UFS_UNLOCK(ump);

	/*
	 * Search vnodes associated with this mount point,
	 * deleting any references to quota file being closed.
	 */
again:
	MNT_VNODE_FOREACH_ALL(vp, mp, mvp) {
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td)) {
			MNT_VNODE_FOREACH_ALL_ABORT(mp, mvp);
			goto again;
		}
		ip = VTOI(vp);
		dq = ip->i_dquot[type];
		ip->i_dquot[type] = NODQUOT;
		dqrele(vp, dq);
		VOP_UNLOCK(vp, 0);
		vrele(vp);
	}

	error = dqflush(qvp);
	if (error != 0)
		return (error);

	/*
	 * Clear um_quotas before closing the quota vnode to prevent
	 * access to the closed vnode from dqget/dqsync
	 */
	UFS_LOCK(ump);
	ump->um_quotas[type] = NULLVP;
	ump->um_cred[type] = NOCRED;
	UFS_UNLOCK(ump);

	vn_lock(qvp, LK_EXCLUSIVE | LK_RETRY);
	qvp->v_vflag &= ~VV_SYSTEM;
	VOP_UNLOCK(qvp, 0);
	error = vn_close(qvp, FREAD|FWRITE, td->td_ucred, td);
	crfree(cr);

	return (error);
}

static int
quotaoff_inchange1(struct thread *td, struct mount *mp, int type)
{
	int error;
	bool need_resume;

	/*
	 * mp is already suspended on unmount.  If not, suspend it, to
	 * avoid the situation where quotaoff operation eventually
	 * failing due to SU structures still keeping references on
	 * dquots, but vnode's references are already clean.  This
	 * would cause quota accounting leak and asserts otherwise.
	 * Note that the thread has already called vn_start_write().
	 */
	if (mp->mnt_susp_owner == td) {
		need_resume = false;
	} else {
		error = vfs_write_suspend_umnt(mp);
		if (error != 0)
			return (error);
		need_resume = true;
	}
	error = quotaoff1(td, mp, type);
	if (need_resume)
		vfs_write_resume(mp, VR_START_WRITE);
	return (error);
}

/*
 * Turns off quotas, assumes that ump->um_qflags are already checked
 * and QTF_CLOSING is set to indicate operation in progress. Fixes
 * ump->um_qflags and mp->mnt_flag after.
 */
int
quotaoff_inchange(struct thread *td, struct mount *mp, int type)
{
	struct ufsmount *ump;
	int error, i;

	error = quotaoff_inchange1(td, mp, type);

	ump = VFSTOUFS(mp);
	UFS_LOCK(ump);
	ump->um_qflags[type] &= ~QTF_CLOSING;
	for (i = 0; i < MAXQUOTAS; i++)
		if (ump->um_quotas[i] != NULLVP)
			break;
	if (i == MAXQUOTAS) {
		MNT_ILOCK(mp);
		mp->mnt_flag &= ~MNT_QUOTA;
		MNT_IUNLOCK(mp);
	}
	UFS_UNLOCK(ump);
	return (error);
}

/*
 * Q_QUOTAOFF - turn off disk quotas for a filesystem.
 */
int
quotaoff(struct thread *td, struct mount *mp, int type)
{
	struct ufsmount *ump;
	int error;

	error = priv_check(td, PRIV_UFS_QUOTAOFF);
	if (error)
		return (error);

	ump = VFSTOUFS(mp);
	UFS_LOCK(ump);
	if ((ump->um_qflags[type] & (QTF_OPENING|QTF_CLOSING)) != 0) {
		UFS_UNLOCK(ump);
		return (EALREADY);
	}
	ump->um_qflags[type] |= QTF_CLOSING;
	UFS_UNLOCK(ump);

	return (quotaoff_inchange(td, mp, type));
}

/*
 * Q_GETQUOTA - return current values in a dqblk structure.
 */
static int
_getquota(struct thread *td, struct mount *mp, u_long id, int type,
    struct dqblk64 *dqb)
{
	struct dquot *dq;
	int error;

	switch (type) {
	case USRQUOTA:
		if ((td->td_ucred->cr_uid != id) && !unprivileged_get_quota) {
			error = priv_check(td, PRIV_VFS_GETQUOTA);
			if (error)
				return (error);
		}
		break;

	case GRPQUOTA:
		if (!groupmember(id, td->td_ucred) &&
		    !unprivileged_get_quota) {
			error = priv_check(td, PRIV_VFS_GETQUOTA);
			if (error)
				return (error);
		}
		break;

	default:
		return (EINVAL);
	}

	dq = NODQUOT;
	error = dqget(NULLVP, id, VFSTOUFS(mp), type, &dq);
	if (error)
		return (error);
	*dqb = dq->dq_dqb;
	dqrele(NULLVP, dq);
	return (error);
}

/*
 * Q_SETQUOTA - assign an entire dqblk structure.
 */
static int
_setquota(struct thread *td, struct mount *mp, u_long id, int type,
    struct dqblk64 *dqb)
{
	struct dquot *dq;
	struct dquot *ndq;
	struct ufsmount *ump;
	struct dqblk64 newlim;
	int error;

	error = priv_check(td, PRIV_VFS_SETQUOTA);
	if (error)
		return (error);

	newlim = *dqb;

	ndq = NODQUOT;
	ump = VFSTOUFS(mp);

	error = dqget(NULLVP, id, ump, type, &ndq);
	if (error)
		return (error);
	dq = ndq;
	DQI_LOCK(dq);
	DQI_WAIT(dq, PINOD+1, "setqta");
	/*
	 * Copy all but the current values.
	 * Reset time limit if previously had no soft limit or were
	 * under it, but now have a soft limit and are over it.
	 */
	newlim.dqb_curblocks = dq->dq_curblocks;
	newlim.dqb_curinodes = dq->dq_curinodes;
	if (dq->dq_id != 0) {
		newlim.dqb_btime = dq->dq_btime;
		newlim.dqb_itime = dq->dq_itime;
	}
	if (newlim.dqb_bsoftlimit &&
	    dq->dq_curblocks >= newlim.dqb_bsoftlimit &&
	    (dq->dq_bsoftlimit == 0 || dq->dq_curblocks < dq->dq_bsoftlimit))
		newlim.dqb_btime = time_second + ump->um_btime[type];
	if (newlim.dqb_isoftlimit &&
	    dq->dq_curinodes >= newlim.dqb_isoftlimit &&
	    (dq->dq_isoftlimit == 0 || dq->dq_curinodes < dq->dq_isoftlimit))
		newlim.dqb_itime = time_second + ump->um_itime[type];
	dq->dq_dqb = newlim;
	if (dq->dq_curblocks < dq->dq_bsoftlimit)
		dq->dq_flags &= ~DQ_BLKS;
	if (dq->dq_curinodes < dq->dq_isoftlimit)
		dq->dq_flags &= ~DQ_INODS;
	if (dq->dq_isoftlimit == 0 && dq->dq_bsoftlimit == 0 &&
	    dq->dq_ihardlimit == 0 && dq->dq_bhardlimit == 0)
		dq->dq_flags |= DQ_FAKE;
	else
		dq->dq_flags &= ~DQ_FAKE;
	dq->dq_flags |= DQ_MOD;
	DQI_UNLOCK(dq);
	dqrele(NULLVP, dq);
	return (0);
}

/*
 * Q_SETUSE - set current inode and block usage.
 */
static int
_setuse(struct thread *td, struct mount *mp, u_long id, int type,
    struct dqblk64 *dqb)
{
	struct dquot *dq;
	struct ufsmount *ump;
	struct dquot *ndq;
	struct dqblk64 usage;
	int error;

	error = priv_check(td, PRIV_UFS_SETUSE);
	if (error)
		return (error);

	usage = *dqb;

	ump = VFSTOUFS(mp);
	ndq = NODQUOT;

	error = dqget(NULLVP, id, ump, type, &ndq);
	if (error)
		return (error);
	dq = ndq;
	DQI_LOCK(dq);
	DQI_WAIT(dq, PINOD+1, "setuse");
	/*
	 * Reset time limit if have a soft limit and were
	 * previously under it, but are now over it.
	 */
	if (dq->dq_bsoftlimit && dq->dq_curblocks < dq->dq_bsoftlimit &&
	    usage.dqb_curblocks >= dq->dq_bsoftlimit)
		dq->dq_btime = time_second + ump->um_btime[type];
	if (dq->dq_isoftlimit && dq->dq_curinodes < dq->dq_isoftlimit &&
	    usage.dqb_curinodes >= dq->dq_isoftlimit)
		dq->dq_itime = time_second + ump->um_itime[type];
	dq->dq_curblocks = usage.dqb_curblocks;
	dq->dq_curinodes = usage.dqb_curinodes;
	if (dq->dq_curblocks < dq->dq_bsoftlimit)
		dq->dq_flags &= ~DQ_BLKS;
	if (dq->dq_curinodes < dq->dq_isoftlimit)
		dq->dq_flags &= ~DQ_INODS;
	dq->dq_flags |= DQ_MOD;
	DQI_UNLOCK(dq);
	dqrele(NULLVP, dq);
	return (0);
}

int
getquota32(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk32 dqb32;
	struct dqblk64 dqb64;
	int error;

	error = _getquota(td, mp, id, type, &dqb64);
	if (error)
		return (error);
	dqb64_dqb32(&dqb64, &dqb32);
	error = copyout(&dqb32, addr, sizeof(dqb32));
	return (error);
}

int
setquota32(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk32 dqb32;
	struct dqblk64 dqb64;
	int error;

	error = copyin(addr, &dqb32, sizeof(dqb32));
	if (error)
		return (error);
	dqb32_dqb64(&dqb32, &dqb64);
	error = _setquota(td, mp, id, type, &dqb64);
	return (error);
}

int
setuse32(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk32 dqb32;
	struct dqblk64 dqb64;
	int error;

	error = copyin(addr, &dqb32, sizeof(dqb32));
	if (error)
		return (error);
	dqb32_dqb64(&dqb32, &dqb64);
	error = _setuse(td, mp, id, type, &dqb64);
	return (error);
}

int
getquota(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk64 dqb64;
	int error;

	error = _getquota(td, mp, id, type, &dqb64);
	if (error)
		return (error);
	error = copyout(&dqb64, addr, sizeof(dqb64));
	return (error);
}

int
setquota(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk64 dqb64;
	int error;

	error = copyin(addr, &dqb64, sizeof(dqb64));
	if (error)
		return (error);
	error = _setquota(td, mp, id, type, &dqb64);
	return (error);
}

int
setuse(struct thread *td, struct mount *mp, u_long id, int type, void *addr)
{
	struct dqblk64 dqb64;
	int error;

	error = copyin(addr, &dqb64, sizeof(dqb64));
	if (error)
		return (error);
	error = _setuse(td, mp, id, type, &dqb64);
	return (error);
}

/*
 * Q_GETQUOTASIZE - get bit-size of quota file fields
 */
int
getquotasize(struct thread *td, struct mount *mp, u_long id, int type,
    void *sizep)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	int bitsize;

	UFS_LOCK(ump);
	if (ump->um_quotas[type] == NULLVP ||
	    (ump->um_qflags[type] & QTF_CLOSING)) {
		UFS_UNLOCK(ump);
		return (EINVAL);
	}
	if ((ump->um_qflags[type] & QTF_64BIT) != 0)
		bitsize = 64;
	else
		bitsize = 32;
	UFS_UNLOCK(ump);
	return (copyout(&bitsize, sizep, sizeof(int)));
}

/*
 * Q_SYNC - sync quota files to disk.
 */
int
qsync(struct mount *mp)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct thread *td = curthread;		/* XXX */
	struct vnode *vp, *mvp;
	struct dquot *dq;
	int i, error;

	/*
	 * Check if the mount point has any quotas.
	 * If not, simply return.
	 */
	for (i = 0; i < MAXQUOTAS; i++)
		if (ump->um_quotas[i] != NULLVP)
			break;
	if (i == MAXQUOTAS)
		return (0);
	/*
	 * Search vnodes associated with this mount point,
	 * synchronizing any modified dquot structures.
	 */
again:
	MNT_VNODE_FOREACH_ACTIVE(vp, mp, mvp) {
		if (vp->v_type == VNON) {
			VI_UNLOCK(vp);
			continue;
		}
		error = vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td);
		if (error) {
			if (error == ENOENT) {
				MNT_VNODE_FOREACH_ACTIVE_ABORT(mp, mvp);
				goto again;
			}
			continue;
		}
		for (i = 0; i < MAXQUOTAS; i++) {
			dq = VTOI(vp)->i_dquot[i];
			if (dq != NODQUOT)
				dqsync(vp, dq);
		}
		vput(vp);
	}
	return (0);
}

/*
 * Sync quota file for given vnode to disk.
 */
int
qsyncvp(struct vnode *vp)
{
	struct ufsmount *ump = VFSTOUFS(vp->v_mount);
	struct dquot *dq;
	int i;

	/*
	 * Check if the mount point has any quotas.
	 * If not, simply return.
	 */
	for (i = 0; i < MAXQUOTAS; i++)
		if (ump->um_quotas[i] != NULLVP)
			break;
	if (i == MAXQUOTAS)
		return (0);
	/*
	 * Search quotas associated with this vnode
	 * synchronizing any modified dquot structures.
	 */
	for (i = 0; i < MAXQUOTAS; i++) {
		dq = VTOI(vp)->i_dquot[i];
		if (dq != NODQUOT)
			dqsync(vp, dq);
	}
	return (0);
}

/*
 * Code pertaining to management of the in-core dquot data structures.
 */
#define DQHASH(dqvp, id) \
	(&dqhashtbl[((((intptr_t)(dqvp)) >> 8) + id) & dqhash])
static LIST_HEAD(dqhash, dquot) *dqhashtbl;
static u_long dqhash;

/*
 * Dquot free list.
 */
#define	DQUOTINC	5	/* minimum free dquots desired */
static TAILQ_HEAD(dqfreelist, dquot) dqfreelist;
static long numdquot, desireddquot = DQUOTINC;

/*
 * Lock to protect quota hash, dq free list and dq_cnt ref counters of
 * _all_ dqs.
 */
struct mtx dqhlock;

#define	DQH_LOCK()	mtx_lock(&dqhlock)
#define	DQH_UNLOCK()	mtx_unlock(&dqhlock)

static struct dquot *dqhashfind(struct dqhash *dqh, u_long id,
	struct vnode *dqvp);

/*
 * Initialize the quota system.
 */
void
dqinit(void)
{

	mtx_init(&dqhlock, "dqhlock", NULL, MTX_DEF);
	dqhashtbl = hashinit(desiredvnodes, M_DQUOT, &dqhash);
	TAILQ_INIT(&dqfreelist);
}

/*
 * Shut down the quota system.
 */
void
dquninit(void)
{
	struct dquot *dq;

	hashdestroy(dqhashtbl, M_DQUOT, dqhash);
	while ((dq = TAILQ_FIRST(&dqfreelist)) != NULL) {
		TAILQ_REMOVE(&dqfreelist, dq, dq_freelist);
		mtx_destroy(&dq->dq_lock);
		free(dq, M_DQUOT);
	}
	mtx_destroy(&dqhlock);
}

static struct dquot *
dqhashfind(struct dqhash *dqh, u_long id, struct vnode *dqvp)
{
	struct dquot *dq;

	mtx_assert(&dqhlock, MA_OWNED);
	LIST_FOREACH(dq, dqh, dq_hash) {
		if (dq->dq_id != id ||
		    dq->dq_ump->um_quotas[dq->dq_type] != dqvp)
			continue;
		/*
		 * Cache hit with no references.  Take
		 * the structure off the free list.
		 */
		if (dq->dq_cnt == 0)
			TAILQ_REMOVE(&dqfreelist, dq, dq_freelist);
		DQREF(dq);
		return (dq);
	}
	return (NODQUOT);
}

/*
 * Determine the quota file type.
 *
 * A 32-bit quota file is simply an array of struct dqblk32.
 *
 * A 64-bit quota file is a struct dqhdr64 followed by an array of struct
 * dqblk64.  The header contains various magic bits which allow us to be
 * reasonably confident that it is indeeda 64-bit quota file and not just
 * a 32-bit quota file that just happens to "look right".
 *
 */
static int
dqopen(struct vnode *vp, struct ufsmount *ump, int type)
{
	struct dqhdr64 dqh;
	struct iovec aiov;
	struct uio auio;
	int error;

	ASSERT_VOP_LOCKED(vp, "dqopen");
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = &dqh;
	aiov.iov_len = sizeof(dqh);
	auio.uio_resid = sizeof(dqh);
	auio.uio_offset = 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = (struct thread *)0;
	error = VOP_READ(vp, &auio, 0, ump->um_cred[type]);

	if (error != 0)
		return (error);
	if (auio.uio_resid > 0) {
		/* assume 32 bits */
		return (0);
	}

	UFS_LOCK(ump);
	if (strcmp(dqh.dqh_magic, Q_DQHDR64_MAGIC) == 0 &&
	    be32toh(dqh.dqh_version) == Q_DQHDR64_VERSION &&
	    be32toh(dqh.dqh_hdrlen) == (uint32_t)sizeof(struct dqhdr64) &&
	    be32toh(dqh.dqh_reclen) == (uint32_t)sizeof(struct dqblk64)) {
		/* XXX: what if the magic matches, but the sizes are wrong? */
		ump->um_qflags[type] |= QTF_64BIT;
	} else {
		ump->um_qflags[type] &= ~QTF_64BIT;
	}
	UFS_UNLOCK(ump);

	return (0);
}

/*
 * Obtain a dquot structure for the specified identifier and quota file
 * reading the information from the file if necessary.
 */
static int
dqget(struct vnode *vp, u_long id, struct ufsmount *ump, int type,
    struct dquot **dqp)
{
	uint8_t buf[sizeof(struct dqblk64)];
	off_t base, recsize;
	struct dquot *dq, *dq1;
	struct dqhash *dqh;
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int dqvplocked, error;

#ifdef DEBUG_VFS_LOCKS
	if (vp != NULLVP)
		ASSERT_VOP_ELOCKED(vp, "dqget");
#endif

	if (vp != NULLVP && *dqp != NODQUOT) {
		return (0);
	}

	/* XXX: Disallow negative id values to prevent the
	* creation of 100GB+ quota data files.
	*/
	if ((int)id < 0)
		return (EINVAL);

	UFS_LOCK(ump);
	dqvp = ump->um_quotas[type];
	if (dqvp == NULLVP || (ump->um_qflags[type] & QTF_CLOSING)) {
		*dqp = NODQUOT;
		UFS_UNLOCK(ump);
		return (EINVAL);
	}
	vref(dqvp);
	UFS_UNLOCK(ump);
	error = 0;
	dqvplocked = 0;

	/*
	 * Check the cache first.
	 */
	dqh = DQHASH(dqvp, id);
	DQH_LOCK();
	dq = dqhashfind(dqh, id, dqvp);
	if (dq != NULL) {
		DQH_UNLOCK();
hfound:		DQI_LOCK(dq);
		DQI_WAIT(dq, PINOD+1, "dqget");
		DQI_UNLOCK(dq);
		if (dq->dq_ump == NULL) {
			dqrele(vp, dq);
			dq = NODQUOT;
			error = EIO;
		}
		*dqp = dq;
		if (dqvplocked)
			vput(dqvp);
		else
			vrele(dqvp);
		return (error);
	}

	/*
	 * Quota vnode lock is before DQ_LOCK. Acquire dqvp lock there
	 * since new dq will appear on the hash chain DQ_LOCKed.
	 */
	if (vp != dqvp) {
		DQH_UNLOCK();
		vn_lock(dqvp, LK_SHARED | LK_RETRY);
		dqvplocked = 1;
		DQH_LOCK();
		/*
		 * Recheck the cache after sleep for quota vnode lock.
		 */
		dq = dqhashfind(dqh, id, dqvp);
		if (dq != NULL) {
			DQH_UNLOCK();
			goto hfound;
		}
	}

	/*
	 * Not in cache, allocate a new one or take it from the
	 * free list.
	 */
	if (TAILQ_FIRST(&dqfreelist) == NODQUOT &&
	    numdquot < MAXQUOTAS * desiredvnodes)
		desireddquot += DQUOTINC;
	if (numdquot < desireddquot) {
		numdquot++;
		DQH_UNLOCK();
		dq1 = malloc(sizeof *dq1, M_DQUOT, M_WAITOK | M_ZERO);
		mtx_init(&dq1->dq_lock, "dqlock", NULL, MTX_DEF);
		DQH_LOCK();
		/*
		 * Recheck the cache after sleep for memory.
		 */
		dq = dqhashfind(dqh, id, dqvp);
		if (dq != NULL) {
			numdquot--;
			DQH_UNLOCK();
			mtx_destroy(&dq1->dq_lock);
			free(dq1, M_DQUOT);
			goto hfound;
		}
		dq = dq1;
	} else {
		if ((dq = TAILQ_FIRST(&dqfreelist)) == NULL) {
			DQH_UNLOCK();
			tablefull("dquot");
			*dqp = NODQUOT;
			if (dqvplocked)
				vput(dqvp);
			else
				vrele(dqvp);
			return (EUSERS);
		}
		if (dq->dq_cnt || (dq->dq_flags & DQ_MOD))
			panic("dqget: free dquot isn't %p", dq);
		TAILQ_REMOVE(&dqfreelist, dq, dq_freelist);
		if (dq->dq_ump != NULL)
			LIST_REMOVE(dq, dq_hash);
	}

	/*
	 * Dq is put into hash already locked to prevent parallel
	 * usage while it is being read from file.
	 */
	dq->dq_flags = DQ_LOCK;
	dq->dq_id = id;
	dq->dq_type = type;
	dq->dq_ump = ump;
	LIST_INSERT_HEAD(dqh, dq, dq_hash);
	DQREF(dq);
	DQH_UNLOCK();

	/*
	 * Read the requested quota record from the quota file, performing
	 * any necessary conversions.
	 */
	if (ump->um_qflags[type] & QTF_64BIT) {
		recsize = sizeof(struct dqblk64);
		base = sizeof(struct dqhdr64);
	} else {
		recsize = sizeof(struct dqblk32);
		base = 0;
	}
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = buf;
	aiov.iov_len = recsize;
	auio.uio_resid = recsize;
	auio.uio_offset = base + id * recsize;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = (struct thread *)0;

	error = VOP_READ(dqvp, &auio, 0, ump->um_cred[type]);
	if (auio.uio_resid == recsize && error == 0) {
		bzero(&dq->dq_dqb, sizeof(dq->dq_dqb));
	} else {
		if (ump->um_qflags[type] & QTF_64BIT)
			dqb64_dq((struct dqblk64 *)buf, dq);
		else
			dqb32_dq((struct dqblk32 *)buf, dq);
	}
	if (dqvplocked)
		vput(dqvp);
	else
		vrele(dqvp);
	/*
	 * I/O error in reading quota file, release
	 * quota structure and reflect problem to caller.
	 */
	if (error) {
		DQH_LOCK();
		dq->dq_ump = NULL;
		LIST_REMOVE(dq, dq_hash);
		DQH_UNLOCK();
		DQI_LOCK(dq);
		if (dq->dq_flags & DQ_WANT)
			wakeup(dq);
		dq->dq_flags = 0;
		DQI_UNLOCK(dq);
		dqrele(vp, dq);
		*dqp = NODQUOT;
		return (error);
	}
	DQI_LOCK(dq);
	/*
	 * Check for no limit to enforce.
	 * Initialize time values if necessary.
	 */
	if (dq->dq_isoftlimit == 0 && dq->dq_bsoftlimit == 0 &&
	    dq->dq_ihardlimit == 0 && dq->dq_bhardlimit == 0)
		dq->dq_flags |= DQ_FAKE;
	if (dq->dq_id != 0) {
		if (dq->dq_btime == 0) {
			dq->dq_btime = time_second + ump->um_btime[type];
			if (dq->dq_bsoftlimit &&
			    dq->dq_curblocks >= dq->dq_bsoftlimit)
				dq->dq_flags |= DQ_MOD;
		}
		if (dq->dq_itime == 0) {
			dq->dq_itime = time_second + ump->um_itime[type];
			if (dq->dq_isoftlimit &&
			    dq->dq_curinodes >= dq->dq_isoftlimit)
				dq->dq_flags |= DQ_MOD;
		}
	}
	DQI_WAKEUP(dq);
	DQI_UNLOCK(dq);
	*dqp = dq;
	return (0);
}

#ifdef DIAGNOSTIC
/*
 * Obtain a reference to a dquot.
 */
static void
dqref(struct dquot *dq)
{

	dq->dq_cnt++;
}
#endif

/*
 * Release a reference to a dquot.
 */
void
dqrele(struct vnode *vp, struct dquot *dq)
{

	if (dq == NODQUOT)
		return;
	DQH_LOCK();
	KASSERT(dq->dq_cnt > 0, ("Lost dq %p reference 1", dq));
	if (dq->dq_cnt > 1) {
		dq->dq_cnt--;
		DQH_UNLOCK();
		return;
	}
	DQH_UNLOCK();
sync:
	(void) dqsync(vp, dq);

	DQH_LOCK();
	KASSERT(dq->dq_cnt > 0, ("Lost dq %p reference 2", dq));
	if (--dq->dq_cnt > 0)
	{
		DQH_UNLOCK();
		return;
	}

	/*
	 * The dq may become dirty after it is synced but before it is
	 * put to the free list. Checking the DQ_MOD there without
	 * locking dq should be safe since no other references to the
	 * dq exist.
	 */
	if ((dq->dq_flags & DQ_MOD) != 0) {
		dq->dq_cnt++;
		DQH_UNLOCK();
		goto sync;
	}
	TAILQ_INSERT_TAIL(&dqfreelist, dq, dq_freelist);
	DQH_UNLOCK();
}

/*
 * Update the disk quota in the quota file.
 */
static int
dqsync(struct vnode *vp, struct dquot *dq)
{
	uint8_t buf[sizeof(struct dqblk64)];
	off_t base, recsize;
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int error;
	struct mount *mp;
	struct ufsmount *ump;

#ifdef DEBUG_VFS_LOCKS
	if (vp != NULL)
		ASSERT_VOP_ELOCKED(vp, "dqsync");
#endif

	mp = NULL;
	error = 0;
	if (dq == NODQUOT)
		panic("dqsync: dquot");
	if ((ump = dq->dq_ump) == NULL)
		return (0);
	UFS_LOCK(ump);
	if ((dqvp = ump->um_quotas[dq->dq_type]) == NULLVP) {
		if (vp == NULL) {
			UFS_UNLOCK(ump);
			return (0);
		} else
			panic("dqsync: file");
	}
	vref(dqvp);
	UFS_UNLOCK(ump);

	DQI_LOCK(dq);
	if ((dq->dq_flags & DQ_MOD) == 0) {
		DQI_UNLOCK(dq);
		vrele(dqvp);
		return (0);
	}
	DQI_UNLOCK(dq);

	(void) vn_start_secondary_write(dqvp, &mp, V_WAIT);
	if (vp != dqvp)
		vn_lock(dqvp, LK_EXCLUSIVE | LK_RETRY);

	DQI_LOCK(dq);
	DQI_WAIT(dq, PINOD+2, "dqsync");
	if ((dq->dq_flags & DQ_MOD) == 0)
		goto out;
	dq->dq_flags |= DQ_LOCK;
	DQI_UNLOCK(dq);

	/*
	 * Write the quota record to the quota file, performing any
	 * necessary conversions.  See dqget() for additional details.
	 */
	if (ump->um_qflags[dq->dq_type] & QTF_64BIT) {
		dq_dqb64(dq, (struct dqblk64 *)buf);
		recsize = sizeof(struct dqblk64);
		base = sizeof(struct dqhdr64);
	} else {
		dq_dqb32(dq, (struct dqblk32 *)buf);
		recsize = sizeof(struct dqblk32);
		base = 0;
	}

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = buf;
	aiov.iov_len = recsize;
	auio.uio_resid = recsize;
	auio.uio_offset = base + dq->dq_id * recsize;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = (struct thread *)0;
	error = VOP_WRITE(dqvp, &auio, 0, dq->dq_ump->um_cred[dq->dq_type]);
	if (auio.uio_resid && error == 0)
		error = EIO;

	DQI_LOCK(dq);
	DQI_WAKEUP(dq);
	dq->dq_flags &= ~DQ_MOD;
out:
	DQI_UNLOCK(dq);
	if (vp != dqvp)
		vput(dqvp);
	else
		vrele(dqvp);
	vn_finished_secondary_write(mp);
	return (error);
}

/*
 * Flush all entries from the cache for a particular vnode.
 */
static int
dqflush(struct vnode *vp)
{
	struct dquot *dq, *nextdq;
	struct dqhash *dqh;
	int error;

	/*
	 * Move all dquot's that used to refer to this quota
	 * file off their hash chains (they will eventually
	 * fall off the head of the free list and be re-used).
	 */
	error = 0;
	DQH_LOCK();
	for (dqh = &dqhashtbl[dqhash]; dqh >= dqhashtbl; dqh--) {
		for (dq = LIST_FIRST(dqh); dq; dq = nextdq) {
			nextdq = LIST_NEXT(dq, dq_hash);
			if (dq->dq_ump->um_quotas[dq->dq_type] != vp)
				continue;
			if (dq->dq_cnt)
				error = EBUSY;
			else {
				LIST_REMOVE(dq, dq_hash);
				dq->dq_ump = NULL;
			}
		}
	}
	DQH_UNLOCK();
	return (error);
}

/*
 * The following three functions are provided for the adjustment of
 * quotas by the soft updates code.
 */
#ifdef SOFTUPDATES
/*
 * Acquire a reference to the quota structures associated with a vnode.
 * Return count of number of quota structures found.
 */
int
quotaref(vp, qrp)
	struct vnode *vp;
	struct dquot **qrp;
{
	struct inode *ip;
	struct dquot *dq;
	int i, found;

	for (i = 0; i < MAXQUOTAS; i++)
		qrp[i] = NODQUOT;
	/*
	 * Disk quotas must be turned off for system files.  Currently
	 * snapshot and quota files.
	 */
	if ((vp->v_vflag & VV_SYSTEM) != 0)
		return (0);
	/*
	 * Iterate through and copy active quotas.
	 */
	found = 0;
	ip = VTOI(vp);
	mtx_lock(&dqhlock);
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		DQREF(dq);
		qrp[i] = dq;
		found++;
	}
	mtx_unlock(&dqhlock);
	return (found);
}

/*
 * Release a set of quota structures obtained from a vnode.
 */
void
quotarele(qrp)
	struct dquot **qrp;
{
	struct dquot *dq;
	int i;

	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = qrp[i]) == NODQUOT)
			continue;
		dqrele(NULL, dq);
	}
}

/*
 * Adjust the number of blocks associated with a quota.
 * Positive numbers when adding blocks; negative numbers when freeing blocks.
 */
void
quotaadj(qrp, ump, blkcount)
	struct dquot **qrp;
	struct ufsmount *ump;
	int64_t blkcount;
{
	struct dquot *dq;
	ufs2_daddr_t ncurblocks;
	int i;

	if (blkcount == 0)
		return;
	for (i = 0; i < MAXQUOTAS; i++) {
		if ((dq = qrp[i]) == NODQUOT)
			continue;
		DQI_LOCK(dq);
		DQI_WAIT(dq, PINOD+1, "adjqta");
		ncurblocks = dq->dq_curblocks + blkcount;
		if (ncurblocks >= 0)
			dq->dq_curblocks = ncurblocks;
		else
			dq->dq_curblocks = 0;
		if (blkcount < 0)
			dq->dq_flags &= ~DQ_BLKS;
		else if (dq->dq_curblocks + blkcount >= dq->dq_bsoftlimit &&
			 dq->dq_curblocks < dq->dq_bsoftlimit)
			dq->dq_btime = time_second + ump->um_btime[i];
		dq->dq_flags |= DQ_MOD;
		DQI_UNLOCK(dq);
	}
}
#endif /* SOFTUPDATES */

/*
 * 32-bit / 64-bit conversion functions.
 *
 * 32-bit quota records are stored in native byte order.  Attention must
 * be paid to overflow issues.
 *
 * 64-bit quota records are stored in network byte order.
 */

#define CLIP32(u64) (u64 > UINT32_MAX ? UINT32_MAX : (uint32_t)u64)

/*
 * Convert 32-bit host-order structure to dquot.
 */
static void
dqb32_dq(const struct dqblk32 *dqb32, struct dquot *dq)
{

	dq->dq_bhardlimit = dqb32->dqb_bhardlimit;
	dq->dq_bsoftlimit = dqb32->dqb_bsoftlimit;
	dq->dq_curblocks = dqb32->dqb_curblocks;
	dq->dq_ihardlimit = dqb32->dqb_ihardlimit;
	dq->dq_isoftlimit = dqb32->dqb_isoftlimit;
	dq->dq_curinodes = dqb32->dqb_curinodes;
	dq->dq_btime = dqb32->dqb_btime;
	dq->dq_itime = dqb32->dqb_itime;
}

/*
 * Convert 64-bit network-order structure to dquot.
 */
static void
dqb64_dq(const struct dqblk64 *dqb64, struct dquot *dq)
{

	dq->dq_bhardlimit = be64toh(dqb64->dqb_bhardlimit);
	dq->dq_bsoftlimit = be64toh(dqb64->dqb_bsoftlimit);
	dq->dq_curblocks = be64toh(dqb64->dqb_curblocks);
	dq->dq_ihardlimit = be64toh(dqb64->dqb_ihardlimit);
	dq->dq_isoftlimit = be64toh(dqb64->dqb_isoftlimit);
	dq->dq_curinodes = be64toh(dqb64->dqb_curinodes);
	dq->dq_btime = be64toh(dqb64->dqb_btime);
	dq->dq_itime = be64toh(dqb64->dqb_itime);
}

/*
 * Convert dquot to 32-bit host-order structure.
 */
static void
dq_dqb32(const struct dquot *dq, struct dqblk32 *dqb32)
{

	dqb32->dqb_bhardlimit = CLIP32(dq->dq_bhardlimit);
	dqb32->dqb_bsoftlimit = CLIP32(dq->dq_bsoftlimit);
	dqb32->dqb_curblocks = CLIP32(dq->dq_curblocks);
	dqb32->dqb_ihardlimit = CLIP32(dq->dq_ihardlimit);
	dqb32->dqb_isoftlimit = CLIP32(dq->dq_isoftlimit);
	dqb32->dqb_curinodes = CLIP32(dq->dq_curinodes);
	dqb32->dqb_btime = CLIP32(dq->dq_btime);
	dqb32->dqb_itime = CLIP32(dq->dq_itime);
}

/*
 * Convert dquot to 64-bit network-order structure.
 */
static void
dq_dqb64(const struct dquot *dq, struct dqblk64 *dqb64)
{

	dqb64->dqb_bhardlimit = htobe64(dq->dq_bhardlimit);
	dqb64->dqb_bsoftlimit = htobe64(dq->dq_bsoftlimit);
	dqb64->dqb_curblocks = htobe64(dq->dq_curblocks);
	dqb64->dqb_ihardlimit = htobe64(dq->dq_ihardlimit);
	dqb64->dqb_isoftlimit = htobe64(dq->dq_isoftlimit);
	dqb64->dqb_curinodes = htobe64(dq->dq_curinodes);
	dqb64->dqb_btime = htobe64(dq->dq_btime);
	dqb64->dqb_itime = htobe64(dq->dq_itime);
}

/*
 * Convert 64-bit host-order structure to 32-bit host-order structure.
 */
static void
dqb64_dqb32(const struct dqblk64 *dqb64, struct dqblk32 *dqb32)
{

	dqb32->dqb_bhardlimit = CLIP32(dqb64->dqb_bhardlimit);
	dqb32->dqb_bsoftlimit = CLIP32(dqb64->dqb_bsoftlimit);
	dqb32->dqb_curblocks = CLIP32(dqb64->dqb_curblocks);
	dqb32->dqb_ihardlimit = CLIP32(dqb64->dqb_ihardlimit);
	dqb32->dqb_isoftlimit = CLIP32(dqb64->dqb_isoftlimit);
	dqb32->dqb_curinodes = CLIP32(dqb64->dqb_curinodes);
	dqb32->dqb_btime = CLIP32(dqb64->dqb_btime);
	dqb32->dqb_itime = CLIP32(dqb64->dqb_itime);
}

/*
 * Convert 32-bit host-order structure to 64-bit host-order structure.
 */
static void
dqb32_dqb64(const struct dqblk32 *dqb32, struct dqblk64 *dqb64)
{

	dqb64->dqb_bhardlimit = dqb32->dqb_bhardlimit;
	dqb64->dqb_bsoftlimit = dqb32->dqb_bsoftlimit;
	dqb64->dqb_curblocks = dqb32->dqb_curblocks;
	dqb64->dqb_ihardlimit = dqb32->dqb_ihardlimit;
	dqb64->dqb_isoftlimit = dqb32->dqb_isoftlimit;
	dqb64->dqb_curinodes = dqb32->dqb_curinodes;
	dqb64->dqb_btime = dqb32->dqb_btime;
	dqb64->dqb_itime = dqb32->dqb_itime;
}
