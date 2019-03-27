/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	from nfs_subs.c  8.8 (Berkeley) 5/22/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * These functions support the macros and help fiddle mbuf chains for
 * the nfs op functions. They do things like create the rpc header and
 * copy data between mbuf chains and uio lists.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/sysent.h>
#include <sys/syscall.h>
#include <sys/sysproto.h>
#include <sys/taskqueue.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/uma.h>

#include <fs/nfs/nfsport.h>
#include <fs/nfsclient/nfsnode.h>
#include <fs/nfsclient/nfsmount.h>
#include <fs/nfsclient/nfs.h>
#include <fs/nfsclient/nfs_kdtrace.h>

#include <netinet/in.h>

/*
 * Note that stdarg.h and the ANSI style va_start macro is used for both
 * ANSI and traditional C compilers.
 */
#include <machine/stdarg.h>

extern struct mtx ncl_iod_mutex;
extern enum nfsiod_state ncl_iodwant[NFS_MAXASYNCDAEMON];
extern struct nfsmount *ncl_iodmount[NFS_MAXASYNCDAEMON];
extern int ncl_numasync;
extern unsigned int ncl_iodmax;
extern struct nfsstatsv1 nfsstatsv1;

struct task	ncl_nfsiodnew_task;

int
ncl_uninit(struct vfsconf *vfsp)
{
	/*
	 * XXX: Unloading of nfscl module is unsupported.
	 */
#if 0
	int i;

	/*
	 * Tell all nfsiod processes to exit. Clear ncl_iodmax, and wakeup
	 * any sleeping nfsiods so they check ncl_iodmax and exit.
	 */
	mtx_lock(&ncl_iod_mutex);
	ncl_iodmax = 0;
	for (i = 0; i < ncl_numasync; i++)
		if (ncl_iodwant[i] == NFSIOD_AVAILABLE)
			wakeup(&ncl_iodwant[i]);
	/* The last nfsiod to exit will wake us up when ncl_numasync hits 0 */
	while (ncl_numasync)
		msleep(&ncl_numasync, &ncl_iod_mutex, PWAIT, "ioddie", 0);
	mtx_unlock(&ncl_iod_mutex);
	ncl_nhuninit();
	return (0);
#else
	return (EOPNOTSUPP);
#endif
}

void 
ncl_dircookie_lock(struct nfsnode *np)
{
	mtx_lock(&np->n_mtx);
	while (np->n_flag & NDIRCOOKIELK)
		(void) msleep(&np->n_flag, &np->n_mtx, PZERO, "nfsdirlk", 0);
	np->n_flag |= NDIRCOOKIELK;
	mtx_unlock(&np->n_mtx);
}

void 
ncl_dircookie_unlock(struct nfsnode *np)
{
	mtx_lock(&np->n_mtx);
	np->n_flag &= ~NDIRCOOKIELK;
	wakeup(&np->n_flag);
	mtx_unlock(&np->n_mtx);
}

bool
ncl_excl_start(struct vnode *vp)
{
	struct nfsnode *np;
	int vn_lk;

	ASSERT_VOP_LOCKED(vp, "ncl_excl_start");
	vn_lk = NFSVOPISLOCKED(vp);
	if (vn_lk == LK_EXCLUSIVE)
		return (false);
	KASSERT(vn_lk == LK_SHARED,
	    ("ncl_excl_start: wrong vnode lock %d", vn_lk));
	/* Ensure exclusive access, this might block */
	np = VTONFS(vp);
	lockmgr(&np->n_excl, LK_EXCLUSIVE, NULL);
	return (true);
}

void
ncl_excl_finish(struct vnode *vp, bool old_lock)
{
	struct nfsnode *np;

	if (!old_lock)
		return;
	np = VTONFS(vp);
	lockmgr(&np->n_excl, LK_RELEASE, NULL);
}

#ifdef NFS_ACDEBUG
#include <sys/sysctl.h>
SYSCTL_DECL(_vfs_nfs);
static int nfs_acdebug;
SYSCTL_INT(_vfs_nfs, OID_AUTO, acdebug, CTLFLAG_RW, &nfs_acdebug, 0, "");
#endif

/*
 * Check the time stamp
 * If the cache is valid, copy contents to *vap and return 0
 * otherwise return an error
 */
int
ncl_getattrcache(struct vnode *vp, struct vattr *vaper)
{
	struct nfsnode *np;
	struct vattr *vap;
	struct nfsmount *nmp;
	int timeo, mustflush;
	
	np = VTONFS(vp);
	vap = &np->n_vattr.na_vattr;
	nmp = VFSTONFS(vp->v_mount);
	mustflush = nfscl_mustflush(vp);	/* must be before mtx_lock() */
	mtx_lock(&np->n_mtx);
	/* XXX n_mtime doesn't seem to be updated on a miss-and-reload */
	timeo = (time_second - np->n_mtime.tv_sec) / 10;

#ifdef NFS_ACDEBUG
	if (nfs_acdebug>1)
		printf("ncl_getattrcache: initial timeo = %d\n", timeo);
#endif

	if (vap->va_type == VDIR) {
		if ((np->n_flag & NMODIFIED) || timeo < nmp->nm_acdirmin)
			timeo = nmp->nm_acdirmin;
		else if (timeo > nmp->nm_acdirmax)
			timeo = nmp->nm_acdirmax;
	} else {
		if ((np->n_flag & NMODIFIED) || timeo < nmp->nm_acregmin)
			timeo = nmp->nm_acregmin;
		else if (timeo > nmp->nm_acregmax)
			timeo = nmp->nm_acregmax;
	}

#ifdef NFS_ACDEBUG
	if (nfs_acdebug > 2)
		printf("acregmin %d; acregmax %d; acdirmin %d; acdirmax %d\n",
		    nmp->nm_acregmin, nmp->nm_acregmax,
		    nmp->nm_acdirmin, nmp->nm_acdirmax);

	if (nfs_acdebug)
		printf("ncl_getattrcache: age = %d; final timeo = %d\n",
		    (time_second - np->n_attrstamp), timeo);
#endif

	if ((time_second - np->n_attrstamp) >= timeo &&
	    (mustflush != 0 || np->n_attrstamp == 0)) {
		nfsstatsv1.attrcache_misses++;
		mtx_unlock(&np->n_mtx);
		KDTRACE_NFS_ATTRCACHE_GET_MISS(vp);
		return( ENOENT);
	}
	nfsstatsv1.attrcache_hits++;
	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			if (np->n_flag & NMODIFIED) {
				if (vap->va_size < np->n_size)
					vap->va_size = np->n_size;
				else
					np->n_size = vap->va_size;
			} else {
				np->n_size = vap->va_size;
			}
			vnode_pager_setsize(vp, np->n_size);
		} else {
			np->n_size = vap->va_size;
		}
	}
	bcopy((caddr_t)vap, (caddr_t)vaper, sizeof(struct vattr));
	if (np->n_flag & NCHG) {
		if (np->n_flag & NACC)
			vaper->va_atime = np->n_atim;
		if (np->n_flag & NUPD)
			vaper->va_mtime = np->n_mtim;
	}
	mtx_unlock(&np->n_mtx);
	KDTRACE_NFS_ATTRCACHE_GET_HIT(vp, vap);
	return (0);
}

static nfsuint64 nfs_nullcookie = { { 0, 0 } };
/*
 * This function finds the directory cookie that corresponds to the
 * logical byte offset given.
 */
nfsuint64 *
ncl_getcookie(struct nfsnode *np, off_t off, int add)
{
	struct nfsdmap *dp, *dp2;
	int pos;
	nfsuint64 *retval = NULL;
	
	pos = (uoff_t)off / NFS_DIRBLKSIZ;
	if (pos == 0 || off < 0) {
		KASSERT(!add, ("nfs getcookie add at <= 0"));
		return (&nfs_nullcookie);
	}
	pos--;
	dp = LIST_FIRST(&np->n_cookies);
	if (!dp) {
		if (add) {
			dp = malloc(sizeof (struct nfsdmap),
				M_NFSDIROFF, M_WAITOK);
			dp->ndm_eocookie = 0;
			LIST_INSERT_HEAD(&np->n_cookies, dp, ndm_list);
		} else
			goto out;
	}
	while (pos >= NFSNUMCOOKIES) {
		pos -= NFSNUMCOOKIES;
		if (LIST_NEXT(dp, ndm_list)) {
			if (!add && dp->ndm_eocookie < NFSNUMCOOKIES &&
			    pos >= dp->ndm_eocookie)
				goto out;
			dp = LIST_NEXT(dp, ndm_list);
		} else if (add) {
			dp2 = malloc(sizeof (struct nfsdmap),
				M_NFSDIROFF, M_WAITOK);
			dp2->ndm_eocookie = 0;
			LIST_INSERT_AFTER(dp, dp2, ndm_list);
			dp = dp2;
		} else
			goto out;
	}
	if (pos >= dp->ndm_eocookie) {
		if (add)
			dp->ndm_eocookie = pos + 1;
		else
			goto out;
	}
	retval = &dp->ndm_cookies[pos];
out:
	return (retval);
}

/*
 * Invalidate cached directory information, except for the actual directory
 * blocks (which are invalidated separately).
 * Done mainly to avoid the use of stale offset cookies.
 */
void
ncl_invaldir(struct vnode *vp)
{
	struct nfsnode *np = VTONFS(vp);

	KASSERT(vp->v_type == VDIR, ("nfs: invaldir not dir"));
	ncl_dircookie_lock(np);
	np->n_direofoffset = 0;
	np->n_cookieverf.nfsuquad[0] = 0;
	np->n_cookieverf.nfsuquad[1] = 0;
	if (LIST_FIRST(&np->n_cookies))
		LIST_FIRST(&np->n_cookies)->ndm_eocookie = 0;
	ncl_dircookie_unlock(np);
}

/*
 * The write verifier has changed (probably due to a server reboot), so all
 * B_NEEDCOMMIT blocks will have to be written again. Since they are on the
 * dirty block list as B_DELWRI, all this takes is clearing the B_NEEDCOMMIT
 * and B_CLUSTEROK flags.  Once done the new write verifier can be set for the
 * mount point.
 *
 * B_CLUSTEROK must be cleared along with B_NEEDCOMMIT because stage 1 data
 * writes are not clusterable.
 */
void
ncl_clearcommit(struct mount *mp)
{
	struct vnode *vp, *nvp;
	struct buf *bp, *nbp;
	struct bufobj *bo;

	MNT_VNODE_FOREACH_ALL(vp, mp, nvp) {
		bo = &vp->v_bufobj;
		vholdl(vp);
		VI_UNLOCK(vp);
		BO_LOCK(bo);
		TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
			if (!BUF_ISLOCKED(bp) &&
			    (bp->b_flags & (B_DELWRI | B_NEEDCOMMIT))
				== (B_DELWRI | B_NEEDCOMMIT))
				bp->b_flags &= ~(B_NEEDCOMMIT | B_CLUSTEROK);
		}
		BO_UNLOCK(bo);
		vdrop(vp);
	}
}

/*
 * Called once to initialize data structures...
 */
int
ncl_init(struct vfsconf *vfsp)
{
	int i;

	/* Ensure async daemons disabled */
	for (i = 0; i < NFS_MAXASYNCDAEMON; i++) {
		ncl_iodwant[i] = NFSIOD_NOT_AVAILABLE;
		ncl_iodmount[i] = NULL;
	}
	TASK_INIT(&ncl_nfsiodnew_task, 0, ncl_nfsiodnew_tq, NULL);
	ncl_nhinit();			/* Init the nfsnode table */

	return (0);
}

