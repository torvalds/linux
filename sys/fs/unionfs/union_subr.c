/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994 Jan-Simon Pendry
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2005, 2006, 2012 Masanori Ozawa <ozawa@ongs.co.jp>, ONGS Inc.
 * Copyright (c) 2006, 2012 Daichi Goto <daichi@freebsd.org>
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)union_subr.c	8.20 (Berkeley) 5/20/95
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/filedesc.h>
#include <sys/stat.h>
#include <sys/resourcevar.h>

#include <security/mac/mac_framework.h>

#include <vm/uma.h>

#include <fs/unionfs/union.h>

#define NUNIONFSNODECACHE 16

static MALLOC_DEFINE(M_UNIONFSHASH, "UNIONFS hash", "UNIONFS hash table");
MALLOC_DEFINE(M_UNIONFSNODE, "UNIONFS node", "UNIONFS vnode private part");
MALLOC_DEFINE(M_UNIONFSPATH, "UNIONFS path", "UNIONFS path private part");

/*
 * Initialize
 */
int 
unionfs_init(struct vfsconf *vfsp)
{
	UNIONFSDEBUG("unionfs_init\n");	/* printed during system boot */
	return (0);
}

/*
 * Uninitialize
 */
int 
unionfs_uninit(struct vfsconf *vfsp)
{
	return (0);
}

static struct unionfs_node_hashhead *
unionfs_get_hashhead(struct vnode *dvp, char *path)
{
	int		count;
	char		hash;
	struct unionfs_node *unp;

	hash = 0;
	unp = VTOUNIONFS(dvp);
	if (path != NULL) {
		for (count = 0; path[count]; count++)
			hash += path[count];
	}

	return (&(unp->un_hashtbl[hash & (unp->un_hashmask)]));
}

/*
 * Get the cached vnode.
 */
static struct vnode *
unionfs_get_cached_vnode(struct vnode *uvp, struct vnode *lvp,
			struct vnode *dvp, char *path)
{
	struct unionfs_node_hashhead *hd;
	struct unionfs_node *unp;
	struct vnode   *vp;

	KASSERT((uvp == NULLVP || uvp->v_type == VDIR),
	    ("unionfs_get_cached_vnode: v_type != VDIR"));
	KASSERT((lvp == NULLVP || lvp->v_type == VDIR),
	    ("unionfs_get_cached_vnode: v_type != VDIR"));

	VI_LOCK(dvp);
	hd = unionfs_get_hashhead(dvp, path);
	LIST_FOREACH(unp, hd, un_hash) {
		if (!strcmp(unp->un_path, path)) {
			vp = UNIONFSTOV(unp);
			VI_LOCK_FLAGS(vp, MTX_DUPOK);
			VI_UNLOCK(dvp);
			vp->v_iflag &= ~VI_OWEINACT;
			if ((vp->v_iflag & (VI_DOOMED | VI_DOINGINACT)) != 0) {
				VI_UNLOCK(vp);
				vp = NULLVP;
			} else
				VI_UNLOCK(vp);
			return (vp);
		}
	}
	VI_UNLOCK(dvp);

	return (NULLVP);
}

/*
 * Add the new vnode into cache.
 */
static struct vnode *
unionfs_ins_cached_vnode(struct unionfs_node *uncp,
			struct vnode *dvp, char *path)
{
	struct unionfs_node_hashhead *hd;
	struct unionfs_node *unp;
	struct vnode   *vp;

	KASSERT((uncp->un_uppervp==NULLVP || uncp->un_uppervp->v_type==VDIR),
	    ("unionfs_ins_cached_vnode: v_type != VDIR"));
	KASSERT((uncp->un_lowervp==NULLVP || uncp->un_lowervp->v_type==VDIR),
	    ("unionfs_ins_cached_vnode: v_type != VDIR"));

	VI_LOCK(dvp);
	hd = unionfs_get_hashhead(dvp, path);
	LIST_FOREACH(unp, hd, un_hash) {
		if (!strcmp(unp->un_path, path)) {
			vp = UNIONFSTOV(unp);
			VI_LOCK_FLAGS(vp, MTX_DUPOK);
			vp->v_iflag &= ~VI_OWEINACT;
			if ((vp->v_iflag & (VI_DOOMED | VI_DOINGINACT)) != 0) {
				LIST_INSERT_HEAD(hd, uncp, un_hash);
				VI_UNLOCK(vp);
				vp = NULLVP;
			} else
				VI_UNLOCK(vp);
			VI_UNLOCK(dvp);
			return (vp);
		}
	}

	LIST_INSERT_HEAD(hd, uncp, un_hash);
	VI_UNLOCK(dvp);

	return (NULLVP);
}

/*
 * Remove the vnode.
 */
static void
unionfs_rem_cached_vnode(struct unionfs_node *unp, struct vnode *dvp)
{
	KASSERT((unp != NULL), ("unionfs_rem_cached_vnode: null node"));
	KASSERT((dvp != NULLVP),
	    ("unionfs_rem_cached_vnode: null parent vnode"));
	KASSERT((unp->un_hash.le_prev != NULL),
	    ("unionfs_rem_cached_vnode: null hash"));

	VI_LOCK(dvp);
	LIST_REMOVE(unp, un_hash);
	unp->un_hash.le_next = NULL;
	unp->un_hash.le_prev = NULL;
	VI_UNLOCK(dvp);
}

/*
 * Make a new or get existing unionfs node.
 * 
 * uppervp and lowervp should be unlocked. Because if new unionfs vnode is
 * locked, uppervp or lowervp is locked too. In order to prevent dead lock,
 * you should not lock plurality simultaneously.
 */
int
unionfs_nodeget(struct mount *mp, struct vnode *uppervp,
		struct vnode *lowervp, struct vnode *dvp,
		struct vnode **vpp, struct componentname *cnp,
		struct thread *td)
{
	struct unionfs_mount *ump;
	struct unionfs_node *unp;
	struct vnode   *vp;
	int		error;
	int		lkflags;
	enum vtype	vt;
	char	       *path;

	ump = MOUNTTOUNIONFSMOUNT(mp);
	lkflags = (cnp ? cnp->cn_lkflags : 0);
	path = (cnp ? cnp->cn_nameptr : NULL);
	*vpp = NULLVP;

	if (uppervp == NULLVP && lowervp == NULLVP)
		panic("unionfs_nodeget: upper and lower is null");

	vt = (uppervp != NULLVP ? uppervp->v_type : lowervp->v_type);

	/* If it has no ISLASTCN flag, path check is skipped. */
	if (cnp && !(cnp->cn_flags & ISLASTCN))
		path = NULL;

	/* check the cache */
	if (path != NULL && dvp != NULLVP && vt == VDIR) {
		vp = unionfs_get_cached_vnode(uppervp, lowervp, dvp, path);
		if (vp != NULLVP) {
			vref(vp);
			*vpp = vp;
			goto unionfs_nodeget_out;
		}
	}

	if ((uppervp == NULLVP || ump->um_uppervp != uppervp) ||
	    (lowervp == NULLVP || ump->um_lowervp != lowervp)) {
		/* dvp will be NULLVP only in case of root vnode. */
		if (dvp == NULLVP)
			return (EINVAL);
	}
	unp = malloc(sizeof(struct unionfs_node),
	    M_UNIONFSNODE, M_WAITOK | M_ZERO);

	error = getnewvnode("unionfs", mp, &unionfs_vnodeops, &vp);
	if (error != 0) {
		free(unp, M_UNIONFSNODE);
		return (error);
	}
	error = insmntque(vp, mp);	/* XXX: Too early for mpsafe fs */
	if (error != 0) {
		free(unp, M_UNIONFSNODE);
		return (error);
	}
	if (dvp != NULLVP)
		vref(dvp);
	if (uppervp != NULLVP)
		vref(uppervp);
	if (lowervp != NULLVP)
		vref(lowervp);

	if (vt == VDIR)
		unp->un_hashtbl = hashinit(NUNIONFSNODECACHE, M_UNIONFSHASH,
		    &(unp->un_hashmask));

	unp->un_vnode = vp;
	unp->un_uppervp = uppervp;
	unp->un_lowervp = lowervp;
	unp->un_dvp = dvp;
	if (uppervp != NULLVP)
		vp->v_vnlock = uppervp->v_vnlock;
	else
		vp->v_vnlock = lowervp->v_vnlock;

	if (path != NULL) {
		unp->un_path = (char *)
		    malloc(cnp->cn_namelen +1, M_UNIONFSPATH, M_WAITOK|M_ZERO);
		bcopy(cnp->cn_nameptr, unp->un_path, cnp->cn_namelen);
		unp->un_path[cnp->cn_namelen] = '\0';
	}
	vp->v_type = vt;
	vp->v_data = unp;

	if ((uppervp != NULLVP && ump->um_uppervp == uppervp) &&
	    (lowervp != NULLVP && ump->um_lowervp == lowervp))
		vp->v_vflag |= VV_ROOT;

	if (path != NULL && dvp != NULLVP && vt == VDIR)
		*vpp = unionfs_ins_cached_vnode(unp, dvp, path);
	if ((*vpp) != NULLVP) {
		if (dvp != NULLVP)
			vrele(dvp);
		if (uppervp != NULLVP)
			vrele(uppervp);
		if (lowervp != NULLVP)
			vrele(lowervp);

		unp->un_uppervp = NULLVP;
		unp->un_lowervp = NULLVP;
		unp->un_dvp = NULLVP;
		vrele(vp);
		vp = *vpp;
		vref(vp);
	} else
		*vpp = vp;

unionfs_nodeget_out:
	if (lkflags & LK_TYPE_MASK)
		vn_lock(vp, lkflags | LK_RETRY);

	return (0);
}

/*
 * Clean up the unionfs node.
 */
void
unionfs_noderem(struct vnode *vp, struct thread *td)
{
	int		count;
	struct unionfs_node *unp, *unp_t1, *unp_t2;
	struct unionfs_node_hashhead *hd;
	struct unionfs_node_status *unsp, *unsp_tmp;
	struct vnode   *lvp;
	struct vnode   *uvp;
	struct vnode   *dvp;

	/*
	 * Use the interlock to protect the clearing of v_data to
	 * prevent faults in unionfs_lock().
	 */
	VI_LOCK(vp);
	unp = VTOUNIONFS(vp);
	lvp = unp->un_lowervp;
	uvp = unp->un_uppervp;
	dvp = unp->un_dvp;
	unp->un_lowervp = unp->un_uppervp = NULLVP;
	vp->v_vnlock = &(vp->v_lock);
	vp->v_data = NULL;
	vp->v_object = NULL;
	VI_UNLOCK(vp);

	if (lvp != NULLVP)
		VOP_UNLOCK(lvp, LK_RELEASE);
	if (uvp != NULLVP)
		VOP_UNLOCK(uvp, LK_RELEASE);

	if (dvp != NULLVP && unp->un_hash.le_prev != NULL)
		unionfs_rem_cached_vnode(unp, dvp);

	if (lockmgr(vp->v_vnlock, LK_EXCLUSIVE, VI_MTX(vp)) != 0)
		panic("the lock for deletion is unacquirable.");

	if (lvp != NULLVP)
		vrele(lvp);
	if (uvp != NULLVP)
		vrele(uvp);
	if (dvp != NULLVP) {
		vrele(dvp);
		unp->un_dvp = NULLVP;
	}
	if (unp->un_path != NULL) {
		free(unp->un_path, M_UNIONFSPATH);
		unp->un_path = NULL;
	}

	if (unp->un_hashtbl != NULL) {
		for (count = 0; count <= unp->un_hashmask; count++) {
			hd = unp->un_hashtbl + count;
			LIST_FOREACH_SAFE(unp_t1, hd, un_hash, unp_t2) {
				LIST_REMOVE(unp_t1, un_hash);
				unp_t1->un_hash.le_next = NULL;
				unp_t1->un_hash.le_prev = NULL;
			}
		}
		hashdestroy(unp->un_hashtbl, M_UNIONFSHASH, unp->un_hashmask);
	}

	LIST_FOREACH_SAFE(unsp, &(unp->un_unshead), uns_list, unsp_tmp) {
		LIST_REMOVE(unsp, uns_list);
		free(unsp, M_TEMP);
	}
	free(unp, M_UNIONFSNODE);
}

/*
 * Get the unionfs node status.
 * You need exclusive lock this vnode.
 */
void
unionfs_get_node_status(struct unionfs_node *unp, struct thread *td,
			struct unionfs_node_status **unspp)
{
	struct unionfs_node_status *unsp;
	pid_t pid = td->td_proc->p_pid;

	KASSERT(NULL != unspp, ("null pointer"));
	ASSERT_VOP_ELOCKED(UNIONFSTOV(unp), "unionfs_get_node_status");

	LIST_FOREACH(unsp, &(unp->un_unshead), uns_list) {
		if (unsp->uns_pid == pid) {
			*unspp = unsp;
			return;
		}
	}

	/* create a new unionfs node status */
	unsp = malloc(sizeof(struct unionfs_node_status),
	    M_TEMP, M_WAITOK | M_ZERO);

	unsp->uns_pid = pid;
	LIST_INSERT_HEAD(&(unp->un_unshead), unsp, uns_list);

	*unspp = unsp;
}

/*
 * Remove the unionfs node status, if you can.
 * You need exclusive lock this vnode.
 */
void
unionfs_tryrem_node_status(struct unionfs_node *unp,
			   struct unionfs_node_status *unsp)
{
	KASSERT(NULL != unsp, ("null pointer"));
	ASSERT_VOP_ELOCKED(UNIONFSTOV(unp), "unionfs_get_node_status");

	if (0 < unsp->uns_lower_opencnt || 0 < unsp->uns_upper_opencnt)
		return;

	LIST_REMOVE(unsp, uns_list);
	free(unsp, M_TEMP);
}

/*
 * Create upper node attr.
 */
void
unionfs_create_uppervattr_core(struct unionfs_mount *ump,
			       struct vattr *lva,
			       struct vattr *uva,
			       struct thread *td)
{
	VATTR_NULL(uva);
	uva->va_type = lva->va_type;
	uva->va_atime = lva->va_atime;
	uva->va_mtime = lva->va_mtime;
	uva->va_ctime = lva->va_ctime;

	switch (ump->um_copymode) {
	case UNIONFS_TRANSPARENT:
		uva->va_mode = lva->va_mode;
		uva->va_uid = lva->va_uid;
		uva->va_gid = lva->va_gid;
		break;
	case UNIONFS_MASQUERADE:
		if (ump->um_uid == lva->va_uid) {
			uva->va_mode = lva->va_mode & 077077;
			uva->va_mode |= (lva->va_type == VDIR ? ump->um_udir : ump->um_ufile) & 0700;
			uva->va_uid = lva->va_uid;
			uva->va_gid = lva->va_gid;
		} else {
			uva->va_mode = (lva->va_type == VDIR ? ump->um_udir : ump->um_ufile);
			uva->va_uid = ump->um_uid;
			uva->va_gid = ump->um_gid;
		}
		break;
	default:		/* UNIONFS_TRADITIONAL */
		uva->va_mode = 0777 & ~td->td_proc->p_fd->fd_cmask;
		uva->va_uid = ump->um_uid;
		uva->va_gid = ump->um_gid;
		break;
	}
}

/*
 * Create upper node attr.
 */
int
unionfs_create_uppervattr(struct unionfs_mount *ump,
			  struct vnode *lvp,
			  struct vattr *uva,
			  struct ucred *cred,
			  struct thread *td)
{
	int		error;
	struct vattr	lva;

	if ((error = VOP_GETATTR(lvp, &lva, cred)))
		return (error);

	unionfs_create_uppervattr_core(ump, &lva, uva, td);

	return (error);
}

/*
 * relookup
 * 
 * dvp should be locked on entry and will be locked on return.
 * 
 * If an error is returned, *vpp will be invalid, otherwise it will hold a
 * locked, referenced vnode. If *vpp == dvp then remember that only one
 * LK_EXCLUSIVE lock is held.
 */
int
unionfs_relookup(struct vnode *dvp, struct vnode **vpp,
		 struct componentname *cnp, struct componentname *cn,
		 struct thread *td, char *path, int pathlen, u_long nameiop)
{
	int	error;

	cn->cn_namelen = pathlen;
	cn->cn_pnbuf = uma_zalloc(namei_zone, M_WAITOK);
	bcopy(path, cn->cn_pnbuf, pathlen);
	cn->cn_pnbuf[pathlen] = '\0';

	cn->cn_nameiop = nameiop;
	cn->cn_flags = (LOCKPARENT | LOCKLEAF | HASBUF | SAVENAME | ISLASTCN);
	cn->cn_lkflags = LK_EXCLUSIVE;
	cn->cn_thread = td;
	cn->cn_cred = cnp->cn_cred;

	cn->cn_nameptr = cn->cn_pnbuf;

	if (nameiop == DELETE)
		cn->cn_flags |= (cnp->cn_flags & (DOWHITEOUT | SAVESTART));
	else if (RENAME == nameiop)
		cn->cn_flags |= (cnp->cn_flags & SAVESTART);
	else if (nameiop == CREATE)
		cn->cn_flags |= NOCACHE;

	vref(dvp);
	VOP_UNLOCK(dvp, LK_RELEASE);

	if ((error = relookup(dvp, vpp, cn))) {
		uma_zfree(namei_zone, cn->cn_pnbuf);
		cn->cn_flags &= ~HASBUF;
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
	} else
		vrele(dvp);

	return (error);
}

/*
 * relookup for CREATE namei operation.
 *
 * dvp is unionfs vnode. dvp should be locked.
 *
 * If it called 'unionfs_copyfile' function by unionfs_link etc,
 * VOP_LOOKUP information is broken.
 * So it need relookup in order to create link etc.
 */
int
unionfs_relookup_for_create(struct vnode *dvp, struct componentname *cnp,
			    struct thread *td)
{
	int	error;
	struct vnode *udvp;
	struct vnode *vp;
	struct componentname cn;

	udvp = UNIONFSVPTOUPPERVP(dvp);
	vp = NULLVP;

	error = unionfs_relookup(udvp, &vp, cnp, &cn, td, cnp->cn_nameptr,
	    strlen(cnp->cn_nameptr), CREATE);
	if (error)
		return (error);

	if (vp != NULLVP) {
		if (udvp == vp)
			vrele(vp);
		else
			vput(vp);

		error = EEXIST;
	}

	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

	if (!error) {
		cn.cn_flags |= (cnp->cn_flags & HASBUF);
		cnp->cn_flags = cn.cn_flags;
	}

	return (error);
}

/*
 * relookup for DELETE namei operation.
 *
 * dvp is unionfs vnode. dvp should be locked.
 */
int
unionfs_relookup_for_delete(struct vnode *dvp, struct componentname *cnp,
			    struct thread *td)
{
	int	error;
	struct vnode *udvp;
	struct vnode *vp;
	struct componentname cn;

	udvp = UNIONFSVPTOUPPERVP(dvp);
	vp = NULLVP;

	error = unionfs_relookup(udvp, &vp, cnp, &cn, td, cnp->cn_nameptr,
	    strlen(cnp->cn_nameptr), DELETE);
	if (error)
		return (error);

	if (vp == NULLVP)
		error = ENOENT;
	else {
		if (udvp == vp)
			vrele(vp);
		else
			vput(vp);
	}

	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

	if (!error) {
		cn.cn_flags |= (cnp->cn_flags & HASBUF);
		cnp->cn_flags = cn.cn_flags;
	}

	return (error);
}

/*
 * relookup for RENAME namei operation.
 *
 * dvp is unionfs vnode. dvp should be locked.
 */
int
unionfs_relookup_for_rename(struct vnode *dvp, struct componentname *cnp,
			    struct thread *td)
{
	int error;
	struct vnode *udvp;
	struct vnode *vp;
	struct componentname cn;

	udvp = UNIONFSVPTOUPPERVP(dvp);
	vp = NULLVP;

	error = unionfs_relookup(udvp, &vp, cnp, &cn, td, cnp->cn_nameptr,
	    strlen(cnp->cn_nameptr), RENAME);
	if (error)
		return (error);

	if (vp != NULLVP) {
		if (udvp == vp)
			vrele(vp);
		else
			vput(vp);
	}

	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

	if (!error) {
		cn.cn_flags |= (cnp->cn_flags & HASBUF);
		cnp->cn_flags = cn.cn_flags;
	}

	return (error);

}

/*
 * Update the unionfs_node.
 * 
 * uvp is new locked upper vnode. unionfs vnode's lock will be exchanged to the
 * uvp's lock and lower's lock will be unlocked.
 */
static void
unionfs_node_update(struct unionfs_node *unp, struct vnode *uvp,
		    struct thread *td)
{
	unsigned	count, lockrec;
	struct vnode   *vp;
	struct vnode   *lvp;
	struct vnode   *dvp;

	vp = UNIONFSTOV(unp);
	lvp = unp->un_lowervp;
	ASSERT_VOP_ELOCKED(lvp, "unionfs_node_update");
	dvp = unp->un_dvp;

	/*
	 * lock update
	 */
	VI_LOCK(vp);
	unp->un_uppervp = uvp;
	vp->v_vnlock = uvp->v_vnlock;
	VI_UNLOCK(vp);
	lockrec = lvp->v_vnlock->lk_recurse;
	for (count = 0; count < lockrec; count++)
		vn_lock(uvp, LK_EXCLUSIVE | LK_CANRECURSE | LK_RETRY);

	/*
	 * cache update
	 */
	if (unp->un_path != NULL && dvp != NULLVP && vp->v_type == VDIR) {
		static struct unionfs_node_hashhead *hd;

		VI_LOCK(dvp);
		hd = unionfs_get_hashhead(dvp, unp->un_path);
		LIST_REMOVE(unp, un_hash);
		LIST_INSERT_HEAD(hd, unp, un_hash);
		VI_UNLOCK(dvp);
	}
}

/*
 * Create a new shadow dir.
 * 
 * udvp should be locked on entry and will be locked on return.
 * 
 * If no error returned, unp will be updated.
 */
int
unionfs_mkshadowdir(struct unionfs_mount *ump, struct vnode *udvp,
		    struct unionfs_node *unp, struct componentname *cnp,
		    struct thread *td)
{
	int		error;
	struct vnode   *lvp;
	struct vnode   *uvp;
	struct vattr	va;
	struct vattr	lva;
	struct componentname cn;
	struct mount   *mp;
	struct ucred   *cred;
	struct ucred   *credbk;
	struct uidinfo *rootinfo;

	if (unp->un_uppervp != NULLVP)
		return (EEXIST);

	lvp = unp->un_lowervp;
	uvp = NULLVP;
	credbk = cnp->cn_cred;

	/* Authority change to root */
	rootinfo = uifind((uid_t)0);
	cred = crdup(cnp->cn_cred);
	/*
	 * The calls to chgproccnt() are needed to compensate for change_ruid()
	 * calling chgproccnt().
	 */
	chgproccnt(cred->cr_ruidinfo, 1, 0);
	change_euid(cred, rootinfo);
	change_ruid(cred, rootinfo);
	change_svuid(cred, (uid_t)0);
	uifree(rootinfo);
	cnp->cn_cred = cred;

	memset(&cn, 0, sizeof(cn));

	if ((error = VOP_GETATTR(lvp, &lva, cnp->cn_cred)))
		goto unionfs_mkshadowdir_abort;

	if ((error = unionfs_relookup(udvp, &uvp, cnp, &cn, td, cnp->cn_nameptr, cnp->cn_namelen, CREATE)))
		goto unionfs_mkshadowdir_abort;
	if (uvp != NULLVP) {
		if (udvp == uvp)
			vrele(uvp);
		else
			vput(uvp);

		error = EEXIST;
		goto unionfs_mkshadowdir_free_out;
	}

	if ((error = vn_start_write(udvp, &mp, V_WAIT | PCATCH)))
		goto unionfs_mkshadowdir_free_out;
	unionfs_create_uppervattr_core(ump, &lva, &va, td);

	error = VOP_MKDIR(udvp, &uvp, &cn, &va);

	if (!error) {
		unionfs_node_update(unp, uvp, td);

		/*
		 * XXX The bug which cannot set uid/gid was corrected.
		 * Ignore errors.
		 */
		va.va_type = VNON;
		VOP_SETATTR(uvp, &va, cn.cn_cred);
	}
	vn_finished_write(mp);

unionfs_mkshadowdir_free_out:
	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

unionfs_mkshadowdir_abort:
	cnp->cn_cred = credbk;
	chgproccnt(cred->cr_ruidinfo, -1, 0);
	crfree(cred);

	return (error);
}

/*
 * Create a new whiteout.
 * 
 * dvp should be locked on entry and will be locked on return.
 */
int
unionfs_mkwhiteout(struct vnode *dvp, struct componentname *cnp,
		   struct thread *td, char *path)
{
	int		error;
	struct vnode   *wvp;
	struct componentname cn;
	struct mount   *mp;

	if (path == NULL)
		path = cnp->cn_nameptr;

	wvp = NULLVP;
	if ((error = unionfs_relookup(dvp, &wvp, cnp, &cn, td, path, strlen(path), CREATE)))
		return (error);
	if (wvp != NULLVP) {
		if (cn.cn_flags & HASBUF) {
			uma_zfree(namei_zone, cn.cn_pnbuf);
			cn.cn_flags &= ~HASBUF;
		}
		if (dvp == wvp)
			vrele(wvp);
		else
			vput(wvp);

		return (EEXIST);
	}

	if ((error = vn_start_write(dvp, &mp, V_WAIT | PCATCH)))
		goto unionfs_mkwhiteout_free_out;
	error = VOP_WHITEOUT(dvp, &cn, CREATE);

	vn_finished_write(mp);

unionfs_mkwhiteout_free_out:
	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

	return (error);
}

/*
 * Create a new vnode for create a new shadow file.
 * 
 * If an error is returned, *vpp will be invalid, otherwise it will hold a
 * locked, referenced and opened vnode.
 * 
 * unp is never updated.
 */
static int
unionfs_vn_create_on_upper(struct vnode **vpp, struct vnode *udvp,
			   struct unionfs_node *unp, struct vattr *uvap,
			   struct thread *td)
{
	struct unionfs_mount *ump;
	struct vnode   *vp;
	struct vnode   *lvp;
	struct ucred   *cred;
	struct vattr	lva;
	int		fmode;
	int		error;
	struct componentname cn;

	ump = MOUNTTOUNIONFSMOUNT(UNIONFSTOV(unp)->v_mount);
	vp = NULLVP;
	lvp = unp->un_lowervp;
	cred = td->td_ucred;
	fmode = FFLAGS(O_WRONLY | O_CREAT | O_TRUNC | O_EXCL);
	error = 0;

	if ((error = VOP_GETATTR(lvp, &lva, cred)) != 0)
		return (error);
	unionfs_create_uppervattr_core(ump, &lva, uvap, td);

	if (unp->un_path == NULL)
		panic("unionfs: un_path is null");

	cn.cn_namelen = strlen(unp->un_path);
	cn.cn_pnbuf = uma_zalloc(namei_zone, M_WAITOK);
	bcopy(unp->un_path, cn.cn_pnbuf, cn.cn_namelen + 1);
	cn.cn_nameiop = CREATE;
	cn.cn_flags = (LOCKPARENT | LOCKLEAF | HASBUF | SAVENAME | ISLASTCN);
	cn.cn_lkflags = LK_EXCLUSIVE;
	cn.cn_thread = td;
	cn.cn_cred = cred;
	cn.cn_nameptr = cn.cn_pnbuf;

	vref(udvp);
	if ((error = relookup(udvp, &vp, &cn)) != 0)
		goto unionfs_vn_create_on_upper_free_out2;
	vrele(udvp);

	if (vp != NULLVP) {
		if (vp == udvp)
			vrele(vp);
		else
			vput(vp);
		error = EEXIST;
		goto unionfs_vn_create_on_upper_free_out1;
	}

	if ((error = VOP_CREATE(udvp, &vp, &cn, uvap)) != 0)
		goto unionfs_vn_create_on_upper_free_out1;

	if ((error = VOP_OPEN(vp, fmode, cred, td, NULL)) != 0) {
		vput(vp);
		goto unionfs_vn_create_on_upper_free_out1;
	}
	VOP_ADD_WRITECOUNT(vp, 1);
	CTR3(KTR_VFS, "%s: vp %p v_writecount increased to %d",  __func__, vp,
	    vp->v_writecount);
	*vpp = vp;

unionfs_vn_create_on_upper_free_out1:
	VOP_UNLOCK(udvp, LK_RELEASE);

unionfs_vn_create_on_upper_free_out2:
	if (cn.cn_flags & HASBUF) {
		uma_zfree(namei_zone, cn.cn_pnbuf);
		cn.cn_flags &= ~HASBUF;
	}

	return (error);
}

/*
 * Copy from lvp to uvp.
 * 
 * lvp and uvp should be locked and opened on entry and will be locked and
 * opened on return.
 */
static int
unionfs_copyfile_core(struct vnode *lvp, struct vnode *uvp,
		      struct ucred *cred, struct thread *td)
{
	int		error;
	off_t		offset;
	int		count;
	int		bufoffset;
	char           *buf;
	struct uio	uio;
	struct iovec	iov;

	error = 0;
	memset(&uio, 0, sizeof(uio));

	uio.uio_td = td;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_offset = 0;

	buf = malloc(MAXBSIZE, M_TEMP, M_WAITOK);

	while (error == 0) {
		offset = uio.uio_offset;

		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		iov.iov_base = buf;
		iov.iov_len = MAXBSIZE;
		uio.uio_resid = iov.iov_len;
		uio.uio_rw = UIO_READ;

		if ((error = VOP_READ(lvp, &uio, 0, cred)) != 0)
			break;
		if ((count = MAXBSIZE - uio.uio_resid) == 0)
			break;

		bufoffset = 0;
		while (bufoffset < count) {
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			iov.iov_base = buf + bufoffset;
			iov.iov_len = count - bufoffset;
			uio.uio_offset = offset + bufoffset;
			uio.uio_resid = iov.iov_len;
			uio.uio_rw = UIO_WRITE;

			if ((error = VOP_WRITE(uvp, &uio, 0, cred)) != 0)
				break;

			bufoffset += (count - bufoffset) - uio.uio_resid;
		}

		uio.uio_offset = offset + bufoffset;
	}

	free(buf, M_TEMP);

	return (error);
}

/*
 * Copy file from lower to upper.
 * 
 * If you need copy of the contents, set 1 to docopy. Otherwise, set 0 to
 * docopy.
 * 
 * If no error returned, unp will be updated.
 */
int
unionfs_copyfile(struct unionfs_node *unp, int docopy, struct ucred *cred,
		 struct thread *td)
{
	int		error;
	struct mount   *mp;
	struct vnode   *udvp;
	struct vnode   *lvp;
	struct vnode   *uvp;
	struct vattr	uva;

	lvp = unp->un_lowervp;
	uvp = NULLVP;

	if ((UNIONFSTOV(unp)->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (unp->un_dvp == NULLVP)
		return (EINVAL);
	if (unp->un_uppervp != NULLVP)
		return (EEXIST);
	udvp = VTOUNIONFS(unp->un_dvp)->un_uppervp;
	if (udvp == NULLVP)
		return (EROFS);
	if ((udvp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);

	error = VOP_ACCESS(lvp, VREAD, cred, td);
	if (error != 0)
		return (error);

	if ((error = vn_start_write(udvp, &mp, V_WAIT | PCATCH)) != 0)
		return (error);
	error = unionfs_vn_create_on_upper(&uvp, udvp, unp, &uva, td);
	if (error != 0) {
		vn_finished_write(mp);
		return (error);
	}

	if (docopy != 0) {
		error = VOP_OPEN(lvp, FREAD, cred, td, NULL);
		if (error == 0) {
			error = unionfs_copyfile_core(lvp, uvp, cred, td);
			VOP_CLOSE(lvp, FREAD, cred, td);
		}
	}
	VOP_CLOSE(uvp, FWRITE, cred, td);
	VOP_ADD_WRITECOUNT(uvp, -1);
	CTR3(KTR_VFS, "%s: vp %p v_writecount decreased to %d", __func__, uvp,
	    uvp->v_writecount);

	vn_finished_write(mp);

	if (error == 0) {
		/* Reset the attributes. Ignore errors. */
		uva.va_type = VNON;
		VOP_SETATTR(uvp, &uva, cred);
	}

	unionfs_node_update(unp, uvp, td);

	return (error);
}

/*
 * It checks whether vp can rmdir. (check empty)
 *
 * vp is unionfs vnode.
 * vp should be locked.
 */
int
unionfs_check_rmdir(struct vnode *vp, struct ucred *cred, struct thread *td)
{
	int		error;
	int		eofflag;
	int		lookuperr;
	struct vnode   *uvp;
	struct vnode   *lvp;
	struct vnode   *tvp;
	struct vattr	va;
	struct componentname cn;
	/*
	 * The size of buf needs to be larger than DIRBLKSIZ.
	 */
	char		buf[256 * 6];
	struct dirent  *dp;
	struct dirent  *edp;
	struct uio	uio;
	struct iovec	iov;

	ASSERT_VOP_ELOCKED(vp, "unionfs_check_rmdir");

	eofflag = 0;
	uvp = UNIONFSVPTOUPPERVP(vp);
	lvp = UNIONFSVPTOLOWERVP(vp);

	/* check opaque */
	if ((error = VOP_GETATTR(uvp, &va, cred)) != 0)
		return (error);
	if (va.va_flags & OPAQUE)
		return (0);

	/* open vnode */
#ifdef MAC
	if ((error = mac_vnode_check_open(cred, vp, VEXEC|VREAD)) != 0)
		return (error);
#endif
	if ((error = VOP_ACCESS(vp, VEXEC|VREAD, cred, td)) != 0)
		return (error);
	if ((error = VOP_OPEN(vp, FREAD, cred, td, NULL)) != 0)
		return (error);

	uio.uio_rw = UIO_READ;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_td = td;
	uio.uio_offset = 0;

#ifdef MAC
	error = mac_vnode_check_readdir(td->td_ucred, lvp);
#endif
	while (!error && !eofflag) {
		iov.iov_base = buf;
		iov.iov_len = sizeof(buf);
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_resid = iov.iov_len;

		error = VOP_READDIR(lvp, &uio, cred, &eofflag, NULL, NULL);
		if (error != 0)
			break;
		if (eofflag == 0 && uio.uio_resid == sizeof(buf)) {
#ifdef DIAGNOSTIC
			panic("bad readdir response from lower FS.");
#endif
			break;
		}

		edp = (struct dirent*)&buf[sizeof(buf) - uio.uio_resid];
		for (dp = (struct dirent*)buf; !error && dp < edp;
		     dp = (struct dirent*)((caddr_t)dp + dp->d_reclen)) {
			if (dp->d_type == DT_WHT || dp->d_fileno == 0 ||
			    (dp->d_namlen == 1 && dp->d_name[0] == '.') ||
			    (dp->d_namlen == 2 && !bcmp(dp->d_name, "..", 2)))
				continue;

			cn.cn_namelen = dp->d_namlen;
			cn.cn_pnbuf = NULL;
			cn.cn_nameptr = dp->d_name;
			cn.cn_nameiop = LOOKUP;
			cn.cn_flags = (LOCKPARENT | LOCKLEAF | SAVENAME | RDONLY | ISLASTCN);
			cn.cn_lkflags = LK_EXCLUSIVE;
			cn.cn_thread = td;
			cn.cn_cred = cred;

			/*
			 * check entry in lower.
			 * Sometimes, readdir function returns
			 * wrong entry.
			 */
			lookuperr = VOP_LOOKUP(lvp, &tvp, &cn);

			if (!lookuperr)
				vput(tvp);
			else
				continue; /* skip entry */

			/*
			 * check entry
			 * If it has no exist/whiteout entry in upper,
			 * directory is not empty.
			 */
			cn.cn_flags = (LOCKPARENT | LOCKLEAF | SAVENAME | RDONLY | ISLASTCN);
			lookuperr = VOP_LOOKUP(uvp, &tvp, &cn);

			if (!lookuperr)
				vput(tvp);

			/* ignore exist or whiteout entry */
			if (!lookuperr ||
			    (lookuperr == ENOENT && (cn.cn_flags & ISWHITEOUT)))
				continue;

			error = ENOTEMPTY;
		}
	}

	/* close vnode */
	VOP_CLOSE(vp, FREAD, cred, td);

	return (error);
}

#ifdef DIAGNOSTIC

struct vnode   *
unionfs_checkuppervp(struct vnode *vp, char *fil, int lno)
{
	struct unionfs_node *unp;

	unp = VTOUNIONFS(vp);

#ifdef notyet
	if (vp->v_op != unionfs_vnodeop_p) {
		printf("unionfs_checkuppervp: on non-unionfs-node.\n");
#ifdef KDB
		kdb_enter(KDB_WHY_UNIONFS,
		    "unionfs_checkuppervp: on non-unionfs-node.\n");
#endif
		panic("unionfs_checkuppervp");
	}
#endif
	return (unp->un_uppervp);
}

struct vnode   *
unionfs_checklowervp(struct vnode *vp, char *fil, int lno)
{
	struct unionfs_node *unp;

	unp = VTOUNIONFS(vp);

#ifdef notyet
	if (vp->v_op != unionfs_vnodeop_p) {
		printf("unionfs_checklowervp: on non-unionfs-node.\n");
#ifdef KDB
		kdb_enter(KDB_WHY_UNIONFS,
		    "unionfs_checklowervp: on non-unionfs-node.\n");
#endif
		panic("unionfs_checklowervp");
	}
#endif
	return (unp->un_lowervp);
}
#endif
