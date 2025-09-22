/*	$OpenBSD: ufs_quota.c,v 1.48 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: ufs_quota.c,v 1.8 1996/02/09 22:36:09 christos Exp $	*/

/*
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
 *	@(#)ufs_quota.c	8.5 (Berkeley) 8/19/94
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/ktrace.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#include <sys/queue.h>

#include <crypto/siphash.h>

/*
 * The following structure records disk usage for a user or group on a
 * filesystem. There is one allocated for each quota that exists on any
 * filesystem for the current user or group. A cache is kept of recently
 * used entries.
 */
struct dquot {
	LIST_ENTRY(dquot) dq_hash;	/* hash list */
	TAILQ_ENTRY(dquot) dq_freelist;	/* free list */
	u_int16_t dq_flags;		/* flags, see below */
	u_int16_t dq_type;		/* quota type of this dquot */
	u_int32_t dq_cnt;		/* count of active references */
	u_int32_t dq_id;		/* identifier this applies to */
	struct  vnode *dq_vp;           /* file backing this quota */
	struct  ucred  *dq_cred;        /* credentials for writing file */
	struct	dqblk dq_dqb;		/* actual usage & quotas */
};

/*
 * Flag values.
 */
#define	DQ_LOCK		0x01		/* this quota locked (no MODS) */
#define	DQ_WANT		0x02		/* wakeup on unlock */
#define	DQ_MOD		0x04		/* this quota modified since read */
#define	DQ_FAKE		0x08		/* no limits here, just usage */
#define	DQ_BLKS		0x10		/* has been warned about blk limit */
#define	DQ_INODS	0x20		/* has been warned about inode limit */

/*
 * Shorthand notation.
 */
#define	dq_bhardlimit	dq_dqb.dqb_bhardlimit
#define	dq_bsoftlimit	dq_dqb.dqb_bsoftlimit
#define	dq_curblocks	dq_dqb.dqb_curblocks
#define	dq_ihardlimit	dq_dqb.dqb_ihardlimit
#define	dq_isoftlimit	dq_dqb.dqb_isoftlimit
#define	dq_curinodes	dq_dqb.dqb_curinodes
#define	dq_btime	dq_dqb.dqb_btime
#define	dq_itime	dq_dqb.dqb_itime

/*
 * If the system has never checked for a quota for this file, then it is
 * set to NODQUOT.  Once a write attempt is made the inode pointer is set
 * to reference a dquot structure.
 */
#define	NODQUOT		NULL

void	dqref(struct dquot *);
void	dqrele(struct vnode *, struct dquot *);
int	dqsync(struct vnode *, struct dquot *);

#ifdef DIAGNOSTIC
void	chkdquot(struct inode *);
#endif

int	getquota(struct mount *, u_long, int, caddr_t);
int	quotaon(struct proc *, struct mount *, int, caddr_t);
int	setquota(struct mount *, u_long, int, caddr_t);
int	setuse(struct mount *, u_long, int, caddr_t);

int	chkdqchg(struct inode *, long, struct ucred *, int);
int	chkiqchg(struct inode *, long, struct ucred *, int);

int dqget(struct vnode *, u_long, struct ufsmount *, int,
	       struct dquot **);

int     quotaon_vnode(struct vnode *, void *);
int     quotaoff_vnode(struct vnode *, void *);
int     qsync_vnode(struct vnode *, void *);

/*
 * Quota name to error message mapping.
 */
static char *quotatypes[] = INITQFNAMES;

/*
 * Obtain a reference to a dquot.
 */
void
dqref(struct dquot *dq)
{
	dq->dq_cnt++;
}

/*
 * Set up the quotas for an inode.
 *
 * This routine completely defines the semantics of quotas.
 * If other criterion want to be used to establish quotas, the
 * MAXQUOTAS value in quotas.h should be increased, and the
 * additional dquots set up here.
 */
int
getinoquota(struct inode *ip)
{
	struct ufsmount *ump;
	struct vnode *vp = ITOV(ip);
	int error;

	ump = ip->i_ump;
	/*
	 * Set up the user quota based on file uid.
	 * EINVAL means that quotas are not enabled.
	 */
	if (ip->i_dquot[USRQUOTA] == NODQUOT &&
	    (error =
		dqget(vp, DIP(ip, uid), ump, USRQUOTA, &ip->i_dquot[USRQUOTA])) &&
	    error != EINVAL)
		return (error);
	/*
	 * Set up the group quota based on file gid.
	 * EINVAL means that quotas are not enabled.
	 */
	if (ip->i_dquot[GRPQUOTA] == NODQUOT &&
	    (error =
		dqget(vp, DIP(ip, gid), ump, GRPQUOTA, &ip->i_dquot[GRPQUOTA])) &&
	    error != EINVAL)
		return (error);
	return (0);
}

/*
 * Update disk usage, and take corrective action.
 */
int 
ufs_quota_alloc_blocks2(struct inode *ip, daddr_t change,
    struct ucred *cred, enum ufs_quota_flags flags)
{
	struct dquot *dq;
	int i;
	int error;

#ifdef DIAGNOSTIC
	chkdquot(ip);
#endif

	if (change == 0)
		return (0);

	if ((flags & UFS_QUOTA_FORCE) == 0 && 
	    (cred != NOCRED && cred->cr_uid != 0)) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if (flags & (1 << i)) 
				continue;
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			if ((error = chkdqchg(ip, change, cred, i)) != 0)
				return (error);
		}
	}
	for (i = 0; i < MAXQUOTAS; i++) {
		if (flags & (1 << i))
			continue;
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			tsleep_nsec(dq, PINOD+1, "chkdq", INFSLP);
		}
		dq->dq_curblocks += change;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

int
ufs_quota_free_blocks2(struct inode *ip, daddr_t change,
    struct ucred *cred, enum ufs_quota_flags flags)
{
	struct dquot *dq;
	int i;

#ifdef DIAGNOSTIC
	if (!VOP_ISLOCKED(ITOV(ip))) 
		panic ("ufs_quota_free_blocks2: vnode is not locked");
#endif

	if (change == 0) 
		return (0);

	for (i = 0; i < MAXQUOTAS; i++) {
		if (flags & (1 << i))
			continue;
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			tsleep_nsec(dq, PINOD+1, "chkdq", INFSLP);
		}
		if (dq->dq_curblocks >= change)
			dq->dq_curblocks -= change;
		else
			dq->dq_curblocks = 0;
		dq->dq_flags &= ~DQ_BLKS;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
int
chkdqchg(struct inode *ip, long change, struct ucred *cred, int type)
{
	struct dquot *dq = ip->i_dquot[type];
	long ncurblocks = dq->dq_curblocks + change;

	/*
	 * If user would exceed their hard limit, disallow space allocation.
	 */
	if (ncurblocks >= dq->dq_bhardlimit && dq->dq_bhardlimit) {
		if ((dq->dq_flags & DQ_BLKS) == 0 &&
		    DIP(ip, uid) == cred->cr_uid) {
			uprintf("\n%s: write failed, %s disk limit reached\n",
			    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
			    quotatypes[type]);
			dq->dq_flags |= DQ_BLKS;
		}
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow space
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurblocks >= dq->dq_bsoftlimit && dq->dq_bsoftlimit) {
		if (dq->dq_curblocks < dq->dq_bsoftlimit) {
			dq->dq_btime = gettime() + ip->i_ump->um_btime[type];
			if (DIP(ip, uid) == cred->cr_uid)
				uprintf("\n%s: warning, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
				    quotatypes[type], "disk quota exceeded");
			return (0);
		}
		if (gettime() > dq->dq_btime) {
			if ((dq->dq_flags & DQ_BLKS) == 0 &&
			    DIP(ip, uid) == cred->cr_uid) {
				uprintf("\n%s: write failed, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
				    quotatypes[type],
				    "disk quota exceeded for too long");
				dq->dq_flags |= DQ_BLKS;
			}
			return (EDQUOT);
		}
	}
	return (0);
}

/*
 * Check the inode limit, applying corrective action.
 */
int
ufs_quota_alloc_inode2(struct inode *ip, struct ucred *cred,
    enum ufs_quota_flags flags)
{
	struct dquot *dq;
	int i;
	int error;

#ifdef DIAGNOSTIC
	chkdquot(ip);
#endif

	if ((flags & UFS_QUOTA_FORCE) == 0 && cred->cr_uid != 0) {
		for (i = 0; i < MAXQUOTAS; i++) {
			if (flags & (1 << i)) 
				continue;
			if ((dq = ip->i_dquot[i]) == NODQUOT)
				continue;
			if ((error = chkiqchg(ip, 1, cred, i)) != 0)
				return (error);
		}
	}
	for (i = 0; i < MAXQUOTAS; i++) {
		if (flags & (1 << i)) 
			continue;
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			tsleep_nsec(dq, PINOD+1, "chkiq", INFSLP);
		}
		dq->dq_curinodes++;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

int
ufs_quota_free_inode2(struct inode *ip, struct ucred *cred,
    enum ufs_quota_flags flags)
{
	struct dquot *dq;
	int i;

#ifdef DIAGNOSTIC
	if (!VOP_ISLOCKED(ITOV(ip))) 
		panic ("ufs_quota_free_blocks2: vnode is not locked");
#endif

	for (i = 0; i < MAXQUOTAS; i++) {
		if (flags & (1 << i)) 
			continue;
		if ((dq = ip->i_dquot[i]) == NODQUOT)
			continue;
		while (dq->dq_flags & DQ_LOCK) {
			dq->dq_flags |= DQ_WANT;
			tsleep_nsec(dq, PINOD+1, "chkiq", INFSLP);
		}
		if (dq->dq_curinodes > 0)
			dq->dq_curinodes--;
		dq->dq_flags &= ~DQ_INODS;
		dq->dq_flags |= DQ_MOD;
	}
	return (0);
}

/*
 * Check for a valid change to a users allocation.
 * Issue an error message if appropriate.
 */
int
chkiqchg(struct inode *ip, long change, struct ucred *cred, int type)
{
	struct dquot *dq = ip->i_dquot[type];
	long ncurinodes = dq->dq_curinodes + change;

	/*
	 * If user would exceed their hard limit, disallow inode allocation.
	 */
	if (ncurinodes >= dq->dq_ihardlimit && dq->dq_ihardlimit) {
		if ((dq->dq_flags & DQ_INODS) == 0 &&
		    DIP(ip, uid) == cred->cr_uid) {
			uprintf("\n%s: write failed, %s inode limit reached\n",
			    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
			    quotatypes[type]);
			dq->dq_flags |= DQ_INODS;
		}
		return (EDQUOT);
	}
	/*
	 * If user is over their soft limit for too long, disallow inode
	 * allocation. Reset time limit as they cross their soft limit.
	 */
	if (ncurinodes >= dq->dq_isoftlimit && dq->dq_isoftlimit) {
		if (dq->dq_curinodes < dq->dq_isoftlimit) {
			dq->dq_itime = gettime() + ip->i_ump->um_itime[type];
			if (DIP(ip, uid) == cred->cr_uid)
				uprintf("\n%s: warning, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
				    quotatypes[type], "inode quota exceeded");
			return (0);
		}
		if (gettime() > dq->dq_itime) {
			if ((dq->dq_flags & DQ_INODS) == 0 &&
			    DIP(ip, uid) == cred->cr_uid) {
				uprintf("\n%s: write failed, %s %s\n",
				    ITOV(ip)->v_mount->mnt_stat.f_mntonname,
				    quotatypes[type],
				    "inode quota exceeded for too long");
				dq->dq_flags |= DQ_INODS;
			}
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
void
chkdquot(struct inode *ip)
{
	struct ufsmount *ump = ip->i_ump;
	int i;
	struct vnode *vp = ITOV(ip);

	if (!VOP_ISLOCKED(vp)) 
		panic ("chkdquot: vnode is not locked");
		
	for (i = 0; i < MAXQUOTAS; i++) {
		if (ump->um_quotas[i] == NULL ||
		    (ump->um_qflags[i] & (QTF_OPENING|QTF_CLOSING)))
			continue;
		if (ip->i_dquot[i] == NODQUOT) {
			vprint("chkdquot: missing dquot", ITOV(ip));
			panic("missing dquot");
		}
	}
}
#endif

/*
 * Code to process quotactl commands.
 */

int
quotaon_vnode(struct vnode *vp, void *arg) 
{
	int error;

	if (vp->v_type == VNON || vp->v_writecount == 0)
		return (0);

	if (vget(vp, LK_EXCLUSIVE)) {
		return (0);
	}

	error = getinoquota(VTOI(vp));
	vput(vp);
	
	return (error);
}

/*
 * Q_QUOTAON - set up a quota file for a particular file system.
 */
int
quotaon(struct proc *p, struct mount *mp, int type, caddr_t fname)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct vnode *vp, **vpp;
	struct dquot *dq;
	int error;
	struct nameidata nd;

#ifdef DIAGNOSTIC
	if (!vfs_isbusy(mp))
		panic ("quotaon: mount point not busy");
#endif

	vpp = &ump->um_quotas[type];
	NDINIT(&nd, 0, 0, UIO_USERSPACE, fname, p);
	if ((error = vn_open(&nd, FREAD|FWRITE, 0)) != 0)
		return (error);
	vp = nd.ni_vp;
	VOP_UNLOCK(vp);
	if (vp->v_type != VREG) {
		(void) vn_close(vp, FREAD|FWRITE, p->p_ucred, p);
		return (EACCES);
	}

	/*
	 * Update the vnode and ucred for quota file updates
	 */
	if (*vpp != vp) {
		quotaoff(p, mp, type);
		*vpp = vp;
		crhold(p->p_ucred);
		ump->um_cred[type] = p->p_ucred;
	} else {
		struct ucred *ocred = ump->um_cred[type];

		(void) vn_close(vp, FREAD|FWRITE, ocred, p);
		if (ocred != p->p_ucred) {
			crhold(p->p_ucred);
			ump->um_cred[type] = p->p_ucred;
			crfree(ocred);
		}
	}

	ump->um_qflags[type] |= QTF_OPENING;
	mp->mnt_flag |= MNT_QUOTA;
	vp->v_flag |= VSYSTEM;
	/*
	 * Set up the time limits for this quota.
	 */
	ump->um_btime[type] = MAX_DQ_TIME;
	ump->um_itime[type] = MAX_IQ_TIME;
	if (dqget(NULL, 0, ump, type, &dq) == 0) {
		if (dq->dq_btime > 0)
			ump->um_btime[type] = dq->dq_btime;
		if (dq->dq_itime > 0)
			ump->um_itime[type] = dq->dq_itime;
		dqrele(NULL, dq);
	}
	/*
	 * Search vnodes associated with this mount point,
	 * adding references to quota file being opened.
	 * NB: only need to add dquot's for inodes being modified.
	 */
	error = vfs_mount_foreach_vnode(mp, quotaon_vnode, NULL);

	ump->um_qflags[type] &= ~QTF_OPENING;
	if (error)
		quotaoff(p, mp, type);
	return (error);
}

struct quotaoff_arg {
	struct proc *p;
	int type;
};

int
quotaoff_vnode(struct vnode *vp, void *arg) 
{
	struct quotaoff_arg *qa = (struct quotaoff_arg *)arg;
	struct inode *ip;
	struct dquot *dq;

	if (vp->v_type == VNON)
		return (0);

	if (vget(vp, LK_EXCLUSIVE))
		return (0);
	ip = VTOI(vp);
	dq = ip->i_dquot[qa->type];
	ip->i_dquot[qa->type] = NODQUOT;
	dqrele(vp, dq);
	vput(vp);
	return (0);
}

/*
 * Q_QUOTAOFF - turn off disk quotas for a filesystem.
 */
int
quotaoff(struct proc *p, struct mount *mp, int type)
{
	struct vnode *qvp;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct quotaoff_arg qa;
	int error;
	
#ifdef DIAGNOSTIC
	if (!vfs_isbusy(mp))
		panic ("quotaoff: mount point not busy");
#endif
	if ((qvp = ump->um_quotas[type]) == NULL)
		return (0);
	ump->um_qflags[type] |= QTF_CLOSING;
	/*
	 * Search vnodes associated with this mount point,
	 * deleting any references to quota file being closed.
	 */
	qa.p = p;
	qa.type = type;
	vfs_mount_foreach_vnode(mp, quotaoff_vnode, &qa);

	error = vn_close(qvp, FREAD|FWRITE, p->p_ucred, p);
	ump->um_quotas[type] = NULL;
	crfree(ump->um_cred[type]);
	ump->um_cred[type] = NOCRED;
	ump->um_qflags[type] &= ~QTF_CLOSING;
	for (type = 0; type < MAXQUOTAS; type++)
		if (ump->um_quotas[type] != NULL)
			break;
	if (type == MAXQUOTAS)
		mp->mnt_flag &= ~MNT_QUOTA;
	return (error);
}

/*
 * Q_GETQUOTA - return current values in a dqblk structure.
 */
int
getquota(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct dquot *dq;
	int error;

	if ((error = dqget(NULL, id, VFSTOUFS(mp), type, &dq)) != 0)
		return (error);
	error = copyout((caddr_t)&dq->dq_dqb, addr, sizeof (struct dqblk));
#ifdef KTRACE
	if (error == 0) {
		struct proc *p = curproc;
		if (KTRPOINT(p, KTR_STRUCT))
			ktrquota(p, &dq->dq_dqb);
	}
#endif

	dqrele(NULL, dq);
	return (error);
}

/*
 * Q_SETQUOTA - assign an entire dqblk structure.
 */
int
setquota(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct dquot *dq;
	struct dquot *ndq;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct dqblk newlim;
	int error;

	error = copyin(addr, (caddr_t)&newlim, sizeof (struct dqblk));
	if (error)
		return (error);
#ifdef KTRACE
	{
		struct proc *p = curproc;
		if (KTRPOINT(p, KTR_STRUCT))
			ktrquota(p, &newlim);
	}
#endif

	if ((error = dqget(NULL, id, ump, type, &ndq)) != 0)
		return (error);
	dq = ndq;
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		tsleep_nsec(dq, PINOD+1, "setquota", INFSLP);
	}
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
		newlim.dqb_btime = gettime() + ump->um_btime[type];
	if (newlim.dqb_isoftlimit &&
	    dq->dq_curinodes >= newlim.dqb_isoftlimit &&
	    (dq->dq_isoftlimit == 0 || dq->dq_curinodes < dq->dq_isoftlimit))
		newlim.dqb_itime = gettime() + ump->um_itime[type];
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
	dqrele(NULL, dq);
	return (0);
}

/*
 * Q_SETUSE - set current inode and block usage.
 */
int
setuse(struct mount *mp, u_long id, int type, caddr_t addr)
{
	struct dquot *dq;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct dquot *ndq;
	struct dqblk usage;
	int error;

	error = copyin(addr, (caddr_t)&usage, sizeof (struct dqblk));
	if (error)
		return (error);
#ifdef KTRACE
	{
		struct proc *p = curproc;
		if (KTRPOINT(p, KTR_STRUCT))
			ktrquota(p, &usage);
	}
#endif

	if ((error = dqget(NULL, id, ump, type, &ndq)) != 0)
		return (error);
	dq = ndq;
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		tsleep_nsec(dq, PINOD+1, "setuse", INFSLP);
	}
	/*
	 * Reset time limit if have a soft limit and were
	 * previously under it, but are now over it.
	 */
	if (dq->dq_bsoftlimit && dq->dq_curblocks < dq->dq_bsoftlimit &&
	    usage.dqb_curblocks >= dq->dq_bsoftlimit)
		dq->dq_btime = gettime() + ump->um_btime[type];
	if (dq->dq_isoftlimit && dq->dq_curinodes < dq->dq_isoftlimit &&
	    usage.dqb_curinodes >= dq->dq_isoftlimit)
		dq->dq_itime = gettime() + ump->um_itime[type];
	dq->dq_curblocks = usage.dqb_curblocks;
	dq->dq_curinodes = usage.dqb_curinodes;
	if (dq->dq_curblocks < dq->dq_bsoftlimit)
		dq->dq_flags &= ~DQ_BLKS;
	if (dq->dq_curinodes < dq->dq_isoftlimit)
		dq->dq_flags &= ~DQ_INODS;
	dq->dq_flags |= DQ_MOD;
	dqrele(NULL, dq);
	return (0);
}

int
qsync_vnode(struct vnode *vp, void *arg)
{
	int i;
	struct dquot *dq;
	    
	if (vp->v_type == VNON)
		return (0);

	if (vget(vp, LK_EXCLUSIVE | LK_NOWAIT))
		return (0);

	for (i = 0; i < MAXQUOTAS; i++) {
		dq = VTOI(vp)->i_dquot[i];
		if (dq != NODQUOT && (dq->dq_flags & DQ_MOD))
			dqsync(vp, dq);
	}
	vput(vp);
	return (0);
}

/*
 * Q_SYNC - sync quota files to disk.
 */
int
qsync(struct mount *mp)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	int i;

	/*
	 * Check if the mount point has any quotas.
	 * If not, simply return.
	 */
	for (i = 0; i < MAXQUOTAS; i++)
		if (ump->um_quotas[i] != NULL)
			break;
	if (i == MAXQUOTAS)
		return (0);
	/*
	 * Search vnodes associated with this mount point,
	 * synchronizing any modified dquot structures.
	 */
	vfs_mount_foreach_vnode(mp, qsync_vnode, NULL);
	return (0);
}

/*
 * Code pertaining to management of the in-core dquot data structures.
 */
LIST_HEAD(dqhash, dquot) *dqhashtbl;
SIPHASH_KEY dqhashkey;
u_long dqhash;

/*
 * Dquot free list.
 */
#define	DQUOTINC	5	/* minimum free dquots desired */
TAILQ_HEAD(dqfreelist, dquot) dqfreelist;
long numdquot, desireddquot = DQUOTINC;

/*
 * Initialize the quota system.
 */
void
ufs_quota_init(void)
{
	dqhashtbl = hashinit(initialvnodes, M_DQUOT, M_WAITOK, &dqhash);
	arc4random_buf(&dqhashkey, sizeof(dqhashkey));
	TAILQ_INIT(&dqfreelist);
}

/*
 * Obtain a dquot structure for the specified identifier and quota file
 * reading the information from the file if necessary.
 */
int
dqget(struct vnode *vp, u_long id, struct ufsmount *ump, int type,
    struct dquot **dqp)
{
	SIPHASH_CTX ctx;
	struct dquot *dq;
	struct dqhash *dqh;
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int error;

	dqvp = ump->um_quotas[type];
	if (dqvp == NULL || (ump->um_qflags[type] & QTF_CLOSING)) {
		*dqp = NODQUOT;
		return (EINVAL);
	}
	/*
	 * Check the cache first.
	 */
	SipHash24_Init(&ctx, &dqhashkey);
	SipHash24_Update(&ctx, &dqvp, sizeof(dqvp));
	SipHash24_Update(&ctx, &id, sizeof(id));
	dqh = &dqhashtbl[SipHash24_End(&ctx) & dqhash];

	LIST_FOREACH(dq, dqh, dq_hash) {
		if (dq->dq_id != id ||
		    dq->dq_vp != dqvp)
			continue;
		/*
		 * Cache hit with no references.  Take
		 * the structure off the free list.
		 */
		if (dq->dq_cnt == 0)
			TAILQ_REMOVE(&dqfreelist, dq, dq_freelist);
		dqref(dq);
		*dqp = dq;
		return (0);
	}
	/*
	 * Not in cache, allocate a new one.
	 */
	if (TAILQ_FIRST(&dqfreelist) == NODQUOT &&
	    numdquot < MAXQUOTAS * initialvnodes)
		desireddquot += DQUOTINC;
	if (numdquot < desireddquot) {
		dq = malloc(sizeof *dq, M_DQUOT, M_WAITOK | M_ZERO);
		numdquot++;
	} else {
		if ((dq = TAILQ_FIRST(&dqfreelist)) == NULL) {
			tablefull("dquot");
			*dqp = NODQUOT;
			return (EUSERS);
		}
		if (dq->dq_cnt || (dq->dq_flags & DQ_MOD))
			panic("free dquot isn't");
		TAILQ_REMOVE(&dqfreelist, dq, dq_freelist);
		LIST_REMOVE(dq, dq_hash);
		crfree(dq->dq_cred);
		dq->dq_cred = NOCRED;
	}
	/*
	 * Initialize the contents of the dquot structure.
	 */
	if (vp != dqvp)
		vn_lock(dqvp, LK_EXCLUSIVE | LK_RETRY);
	LIST_INSERT_HEAD(dqh, dq, dq_hash);
	dqref(dq);
	dq->dq_flags = DQ_LOCK;
	dq->dq_id = id;
	dq->dq_vp = dqvp;
	dq->dq_type = type;
	crhold(ump->um_cred[type]);
	dq->dq_cred = ump->um_cred[type];
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t)&dq->dq_dqb;
	aiov.iov_len = sizeof (struct dqblk);
	auio.uio_resid = sizeof (struct dqblk);
	auio.uio_offset = (off_t)(id * sizeof (struct dqblk));
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_procp = NULL;
	error = VOP_READ(dqvp, &auio, 0, dq->dq_cred);
	if (auio.uio_resid == sizeof(struct dqblk) && error == 0)
		memset(&dq->dq_dqb, 0, sizeof(struct dqblk));
	if (vp != dqvp)
		VOP_UNLOCK(dqvp);
	if (dq->dq_flags & DQ_WANT)
		wakeup(dq);
	dq->dq_flags = 0;
	/*
	 * I/O error in reading quota file, release
	 * quota structure and reflect problem to caller.
	 */
	if (error) {
		LIST_REMOVE(dq, dq_hash);
		dqrele(vp, dq);
		*dqp = NODQUOT;
		return (error);
	}
	/*
	 * Check for no limit to enforce.
	 * Initialize time values if necessary.
	 */
	if (dq->dq_isoftlimit == 0 && dq->dq_bsoftlimit == 0 &&
	    dq->dq_ihardlimit == 0 && dq->dq_bhardlimit == 0)
		dq->dq_flags |= DQ_FAKE;
	if (dq->dq_id != 0) {
		if (dq->dq_btime == 0)
			dq->dq_btime = gettime() + ump->um_btime[type];
		if (dq->dq_itime == 0)
			dq->dq_itime = gettime() + ump->um_itime[type];
	}
	*dqp = dq;
	return (0);
}

/*
 * Release a reference to a dquot.
 */
void
dqrele(struct vnode *vp, struct dquot *dq)
{

	if (dq == NODQUOT)
		return;
	if (dq->dq_cnt > 1) {
		dq->dq_cnt--;
		return;
	}
	if (dq->dq_flags & DQ_MOD)
		(void) dqsync(vp, dq);
	if (--dq->dq_cnt > 0)
		return;
	TAILQ_INSERT_TAIL(&dqfreelist, dq, dq_freelist);
}

/*
 * Update the disk quota in the quota file.
 */
int
dqsync(struct vnode *vp, struct dquot *dq)
{
	struct vnode *dqvp;
	struct iovec aiov;
	struct uio auio;
	int error;

	if (dq == NODQUOT)
		panic("dqsync: dquot");
	if ((dq->dq_flags & DQ_MOD) == 0)
		return (0);
	if ((dqvp = dq->dq_vp) == NULL)
		panic("dqsync: file");

	if (vp != dqvp)
		vn_lock(dqvp, LK_EXCLUSIVE | LK_RETRY);
	while (dq->dq_flags & DQ_LOCK) {
		dq->dq_flags |= DQ_WANT;
		tsleep_nsec(dq, PINOD+2, "dqsync", INFSLP);
		if ((dq->dq_flags & DQ_MOD) == 0) {
			if (vp != dqvp)
				VOP_UNLOCK(dqvp);
			return (0);
		}
	}
	dq->dq_flags |= DQ_LOCK;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t)&dq->dq_dqb;
	aiov.iov_len = sizeof (struct dqblk);
	auio.uio_resid = sizeof (struct dqblk);
	auio.uio_offset = (off_t)(dq->dq_id * sizeof (struct dqblk));
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_procp = NULL;
	error = VOP_WRITE(dqvp, &auio, 0, dq->dq_cred);
	if (auio.uio_resid && error == 0)
		error = EIO;
	if (dq->dq_flags & DQ_WANT)
		wakeup(dq);
	dq->dq_flags &= ~(DQ_MOD|DQ_LOCK|DQ_WANT);
	if (vp != dqvp)
		VOP_UNLOCK(dqvp);
	return (error);
}

int
ufs_quota_delete(struct inode *ip)
{
	struct vnode *vp = ITOV(ip);
	int i;
	for (i = 0; i < MAXQUOTAS; i++) {
		if (ip->i_dquot[i] != NODQUOT) {
			dqrele(vp, ip->i_dquot[i]);
			ip->i_dquot[i] = NODQUOT;
		}
	}

	return (0);
}

/*
 * Do operations associated with quotas
 */
int
ufs_quotactl(struct mount *mp, int cmds, uid_t uid, caddr_t arg,
    struct proc *p)
{
	int cmd, type, error;

	if (uid == -1)
		uid = p->p_ucred->cr_ruid;
	cmd = cmds >> SUBCMDSHIFT;

	switch (cmd) {
	case Q_SYNC:
		break;
	case Q_GETQUOTA:
		if (uid == p->p_ucred->cr_ruid)
			break;
		/* FALLTHROUGH */
	default:
		if ((error = suser(p)) != 0)
			return (error);
	}

	type = cmds & SUBCMDMASK;
	if ((u_int)type >= MAXQUOTAS)
		return (EINVAL);

	if (vfs_busy(mp, VB_READ|VB_NOWAIT))
		return (0);
 

	switch (cmd) {

	case Q_QUOTAON:
		error = quotaon(p, mp, type, arg);
		break;

	case Q_QUOTAOFF:
		error = quotaoff(p, mp, type);
		break;

	case Q_SETQUOTA:
		error = setquota(mp, uid, type, arg) ;
		break;

	case Q_SETUSE:
		error = setuse(mp, uid, type, arg);
		break;

	case Q_GETQUOTA:
		error = getquota(mp, uid, type, arg);
		break;

	case Q_SYNC:
		error = qsync(mp);
		break;

	default:
		error = EINVAL;
		break;
	}

	vfs_unbusy(mp);
	return (error);
}
