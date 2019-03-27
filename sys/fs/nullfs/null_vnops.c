/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 *	@(#)null_vnops.c	8.6 (Berkeley) 5/27/95
 *
 * Ancestors:
 *	@(#)lofs_vnops.c	1.2 (Berkeley) 6/18/92
 *	...and...
 *	@(#)null_vnodeops.c 1.20 92/07/07 UCLA Ficus project
 *
 * $FreeBSD$
 */

/*
 * Null Layer
 *
 * (See mount_nullfs(8) for more information.)
 *
 * The null layer duplicates a portion of the filesystem
 * name space under a new name.  In this respect, it is
 * similar to the loopback filesystem.  It differs from
 * the loopback fs in two respects:  it is implemented using
 * a stackable layers techniques, and its "null-node"s stack above
 * all lower-layer vnodes, not just over directory vnodes.
 *
 * The null layer has two purposes.  First, it serves as a demonstration
 * of layering by proving a layer which does nothing.  (It actually
 * does everything the loopback filesystem does, which is slightly
 * more than nothing.)  Second, the null layer can serve as a prototype
 * layer.  Since it provides all necessary layer framework,
 * new filesystem layers can be created very easily be starting
 * with a null layer.
 *
 * The remainder of this man page examines the null layer as a basis
 * for constructing new layers.
 *
 *
 * INSTANTIATING NEW NULL LAYERS
 *
 * New null layers are created with mount_nullfs(8).
 * Mount_nullfs(8) takes two arguments, the pathname
 * of the lower vfs (target-pn) and the pathname where the null
 * layer will appear in the namespace (alias-pn).  After
 * the null layer is put into place, the contents
 * of target-pn subtree will be aliased under alias-pn.
 *
 *
 * OPERATION OF A NULL LAYER
 *
 * The null layer is the minimum filesystem layer,
 * simply bypassing all possible operations to the lower layer
 * for processing there.  The majority of its activity centers
 * on the bypass routine, through which nearly all vnode operations
 * pass.
 *
 * The bypass routine accepts arbitrary vnode operations for
 * handling by the lower layer.  It begins by examing vnode
 * operation arguments and replacing any null-nodes by their
 * lower-layer equivlants.  It then invokes the operation
 * on the lower layer.  Finally, it replaces the null-nodes
 * in the arguments and, if a vnode is return by the operation,
 * stacks a null-node on top of the returned vnode.
 *
 * Although bypass handles most operations, vop_getattr, vop_lock,
 * vop_unlock, vop_inactive, vop_reclaim, and vop_print are not
 * bypassed. Vop_getattr must change the fsid being returned.
 * Vop_lock and vop_unlock must handle any locking for the
 * current vnode as well as pass the lock request down.
 * Vop_inactive and vop_reclaim are not bypassed so that
 * they can handle freeing null-layer specific data. Vop_print
 * is not bypassed to avoid excessive debugging information.
 * Also, certain vnode operations change the locking state within
 * the operation (create, mknod, remove, link, rename, mkdir, rmdir,
 * and symlink). Ideally these operations should not change the
 * lock state, but should be changed to let the caller of the
 * function unlock them. Otherwise all intermediate vnode layers
 * (such as union, umapfs, etc) must catch these functions to do
 * the necessary locking at their layer.
 *
 *
 * INSTANTIATING VNODE STACKS
 *
 * Mounting associates the null layer with a lower layer,
 * effect stacking two VFSes.  Vnode stacks are instead
 * created on demand as files are accessed.
 *
 * The initial mount creates a single vnode stack for the
 * root of the new null layer.  All other vnode stacks
 * are created as a result of vnode operations on
 * this or other null vnode stacks.
 *
 * New vnode stacks come into existence as a result of
 * an operation which returns a vnode.
 * The bypass routine stacks a null-node above the new
 * vnode before returning it to the caller.
 *
 * For example, imagine mounting a null layer with
 * "mount_nullfs /usr/include /dev/layer/null".
 * Changing directory to /dev/layer/null will assign
 * the root null-node (which was created when the null layer was mounted).
 * Now consider opening "sys".  A vop_lookup would be
 * done on the root null-node.  This operation would bypass through
 * to the lower layer which would return a vnode representing
 * the UFS "sys".  Null_bypass then builds a null-node
 * aliasing the UFS "sys" and returns this to the caller.
 * Later operations on the null-node "sys" will repeat this
 * process when constructing other vnode stacks.
 *
 *
 * CREATING OTHER FILE SYSTEM LAYERS
 *
 * One of the easiest ways to construct new filesystem layers is to make
 * a copy of the null layer, rename all files and variables, and
 * then begin modifing the copy.  Sed can be used to easily rename
 * all variables.
 *
 * The umap layer is an example of a layer descended from the
 * null layer.
 *
 *
 * INVOKING OPERATIONS ON LOWER LAYERS
 *
 * There are two techniques to invoke operations on a lower layer
 * when the operation cannot be completely bypassed.  Each method
 * is appropriate in different situations.  In both cases,
 * it is the responsibility of the aliasing layer to make
 * the operation arguments "correct" for the lower layer
 * by mapping a vnode arguments to the lower layer.
 *
 * The first approach is to call the aliasing layer's bypass routine.
 * This method is most suitable when you wish to invoke the operation
 * currently being handled on the lower layer.  It has the advantage
 * that the bypass routine already must do argument mapping.
 * An example of this is null_getattrs in the null layer.
 *
 * A second approach is to directly invoke vnode operations on
 * the lower layer with the VOP_OPERATIONNAME interface.
 * The advantage of this method is that it is easy to invoke
 * arbitrary operations on the lower layer.  The disadvantage
 * is that vnode arguments must be manualy mapped.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <fs/nullfs/null.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

static int null_bug_bypass = 0;   /* for debugging: enables bypass printf'ing */
SYSCTL_INT(_debug, OID_AUTO, nullfs_bug_bypass, CTLFLAG_RW, 
	&null_bug_bypass, 0, "");

/*
 * This is the 10-Apr-92 bypass routine.
 *    This version has been optimized for speed, throwing away some
 * safety checks.  It should still always work, but it's not as
 * robust to programmer errors.
 *
 * In general, we map all vnodes going down and unmap them on the way back.
 * As an exception to this, vnodes can be marked "unmapped" by setting
 * the Nth bit in operation's vdesc_flags.
 *
 * Also, some BSD vnode operations have the side effect of vrele'ing
 * their arguments.  With stacking, the reference counts are held
 * by the upper node, not the lower one, so we must handle these
 * side-effects here.  This is not of concern in Sun-derived systems
 * since there are no such side-effects.
 *
 * This makes the following assumptions:
 * - only one returned vpp
 * - no INOUT vpp's (Sun's vop_open has one of these)
 * - the vnode operation vector of the first vnode should be used
 *   to determine what implementation of the op should be invoked
 * - all mapped vnodes are of our vnode-type (NEEDSWORK:
 *   problems on rmdir'ing mount points and renaming?)
 */
int
null_bypass(struct vop_generic_args *ap)
{
	struct vnode **this_vp_p;
	int error;
	struct vnode *old_vps[VDESC_MAX_VPS];
	struct vnode **vps_p[VDESC_MAX_VPS];
	struct vnode ***vppp;
	struct vnodeop_desc *descp = ap->a_desc;
	int reles, i;

	if (null_bug_bypass)
		printf ("null_bypass: %s\n", descp->vdesc_name);

#ifdef DIAGNOSTIC
	/*
	 * We require at least one vp.
	 */
	if (descp->vdesc_vp_offsets == NULL ||
	    descp->vdesc_vp_offsets[0] == VDESC_NO_OFFSET)
		panic ("null_bypass: no vp's in map");
#endif

	/*
	 * Map the vnodes going in.
	 * Later, we'll invoke the operation based on
	 * the first mapped vnode's operation vector.
	 */
	reles = descp->vdesc_flags;
	for (i = 0; i < VDESC_MAX_VPS; reles >>= 1, i++) {
		if (descp->vdesc_vp_offsets[i] == VDESC_NO_OFFSET)
			break;   /* bail out at end of list */
		vps_p[i] = this_vp_p =
			VOPARG_OFFSETTO(struct vnode**,descp->vdesc_vp_offsets[i],ap);
		/*
		 * We're not guaranteed that any but the first vnode
		 * are of our type.  Check for and don't map any
		 * that aren't.  (We must always map first vp or vclean fails.)
		 */
		if (i && (*this_vp_p == NULLVP ||
		    (*this_vp_p)->v_op != &null_vnodeops)) {
			old_vps[i] = NULLVP;
		} else {
			old_vps[i] = *this_vp_p;
			*(vps_p[i]) = NULLVPTOLOWERVP(*this_vp_p);
			/*
			 * XXX - Several operations have the side effect
			 * of vrele'ing their vp's.  We must account for
			 * that.  (This should go away in the future.)
			 */
			if (reles & VDESC_VP0_WILLRELE)
				VREF(*this_vp_p);
		}

	}

	/*
	 * Call the operation on the lower layer
	 * with the modified argument structure.
	 */
	if (vps_p[0] && *vps_p[0])
		error = VCALL(ap);
	else {
		printf("null_bypass: no map for %s\n", descp->vdesc_name);
		error = EINVAL;
	}

	/*
	 * Maintain the illusion of call-by-value
	 * by restoring vnodes in the argument structure
	 * to their original value.
	 */
	reles = descp->vdesc_flags;
	for (i = 0; i < VDESC_MAX_VPS; reles >>= 1, i++) {
		if (descp->vdesc_vp_offsets[i] == VDESC_NO_OFFSET)
			break;   /* bail out at end of list */
		if (old_vps[i]) {
			*(vps_p[i]) = old_vps[i];
#if 0
			if (reles & VDESC_VP0_WILLUNLOCK)
				VOP_UNLOCK(*(vps_p[i]), 0);
#endif
			if (reles & VDESC_VP0_WILLRELE)
				vrele(*(vps_p[i]));
		}
	}

	/*
	 * Map the possible out-going vpp
	 * (Assumes that the lower layer always returns
	 * a VREF'ed vpp unless it gets an error.)
	 */
	if (descp->vdesc_vpp_offset != VDESC_NO_OFFSET &&
	    !(descp->vdesc_flags & VDESC_NOMAP_VPP) &&
	    !error) {
		/*
		 * XXX - even though some ops have vpp returned vp's,
		 * several ops actually vrele this before returning.
		 * We must avoid these ops.
		 * (This should go away when these ops are regularized.)
		 */
		if (descp->vdesc_flags & VDESC_VPP_WILLRELE)
			goto out;
		vppp = VOPARG_OFFSETTO(struct vnode***,
				 descp->vdesc_vpp_offset,ap);
		if (*vppp)
			error = null_nodeget(old_vps[0]->v_mount, **vppp, *vppp);
	}

 out:
	return (error);
}

static int
null_add_writecount(struct vop_add_writecount_args *ap)
{
	struct vnode *lvp, *vp;
	int error;

	vp = ap->a_vp;
	lvp = NULLVPTOLOWERVP(vp);
	KASSERT(vp->v_writecount + ap->a_inc >= 0, ("wrong writecount inc"));
	if (vp->v_writecount > 0 && vp->v_writecount + ap->a_inc == 0)
		error = VOP_ADD_WRITECOUNT(lvp, -1);
	else if (vp->v_writecount == 0 && vp->v_writecount + ap->a_inc > 0)
		error = VOP_ADD_WRITECOUNT(lvp, 1);
	else
		error = 0;
	if (error == 0)
		vp->v_writecount += ap->a_inc;
	return (error);
}

/*
 * We have to carry on the locking protocol on the null layer vnodes
 * as we progress through the tree. We also have to enforce read-only
 * if this layer is mounted read-only.
 */
static int
null_lookup(struct vop_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	int flags = cnp->cn_flags;
	struct vnode *vp, *ldvp, *lvp;
	struct mount *mp;
	int error;

	mp = dvp->v_mount;
	if ((flags & ISLASTCN) != 0 && (mp->mnt_flag & MNT_RDONLY) != 0 &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);
	/*
	 * Although it is possible to call null_bypass(), we'll do
	 * a direct call to reduce overhead
	 */
	ldvp = NULLVPTOLOWERVP(dvp);
	vp = lvp = NULL;
	KASSERT((ldvp->v_vflag & VV_ROOT) == 0 ||
	    ((dvp->v_vflag & VV_ROOT) != 0 && (flags & ISDOTDOT) == 0),
	    ("ldvp %p fl %#x dvp %p fl %#x flags %#x", ldvp, ldvp->v_vflag,
	     dvp, dvp->v_vflag, flags));

	/*
	 * Hold ldvp.  The reference on it, owned by dvp, is lost in
	 * case of dvp reclamation, and we need ldvp to move our lock
	 * from ldvp to dvp.
	 */
	vhold(ldvp);

	error = VOP_LOOKUP(ldvp, &lvp, cnp);

	/*
	 * VOP_LOOKUP() on lower vnode may unlock ldvp, which allows
	 * dvp to be reclaimed due to shared v_vnlock.  Check for the
	 * doomed state and return error.
	 */
	if ((error == 0 || error == EJUSTRETURN) &&
	    (dvp->v_iflag & VI_DOOMED) != 0) {
		error = ENOENT;
		if (lvp != NULL)
			vput(lvp);

		/*
		 * If vgone() did reclaimed dvp before curthread
		 * relocked ldvp, the locks of dvp and ldpv are no
		 * longer shared.  In this case, relock of ldvp in
		 * lower fs VOP_LOOKUP() does not restore the locking
		 * state of dvp.  Compensate for this by unlocking
		 * ldvp and locking dvp, which is also correct if the
		 * locks are still shared.
		 */
		VOP_UNLOCK(ldvp, 0);
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
	}
	vdrop(ldvp);

	if (error == EJUSTRETURN && (flags & ISLASTCN) != 0 &&
	    (mp->mnt_flag & MNT_RDONLY) != 0 &&
	    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME))
		error = EROFS;

	if ((error == 0 || error == EJUSTRETURN) && lvp != NULL) {
		if (ldvp == lvp) {
			*ap->a_vpp = dvp;
			VREF(dvp);
			vrele(lvp);
		} else {
			error = null_nodeget(mp, lvp, &vp);
			if (error == 0)
				*ap->a_vpp = vp;
		}
	}
	return (error);
}

static int
null_open(struct vop_open_args *ap)
{
	int retval;
	struct vnode *vp, *ldvp;

	vp = ap->a_vp;
	ldvp = NULLVPTOLOWERVP(vp);
	retval = null_bypass(&ap->a_gen);
	if (retval == 0)
		vp->v_object = ldvp->v_object;
	return (retval);
}

/*
 * Setattr call. Disallow write attempts if the layer is mounted read-only.
 */
static int
null_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;

  	if ((vap->va_flags != VNOVAL || vap->va_uid != (uid_t)VNOVAL ||
	    vap->va_gid != (gid_t)VNOVAL || vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL || vap->va_mode != (mode_t)VNOVAL) &&
	    (vp->v_mount->mnt_flag & MNT_RDONLY))
		return (EROFS);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		case VDIR:
 			return (EISDIR);
 		case VCHR:
 		case VBLK:
 		case VSOCK:
 		case VFIFO:
			if (vap->va_flags != VNOVAL)
				return (EOPNOTSUPP);
			return (0);
		case VREG:
		case VLNK:
 		default:
			/*
			 * Disallow write attempts if the filesystem is
			 * mounted read-only.
			 */
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
		}
	}

	return (null_bypass((struct vop_generic_args *)ap));
}

/*
 *  We handle getattr only to change the fsid.
 */
static int
null_getattr(struct vop_getattr_args *ap)
{
	int error;

	if ((error = null_bypass((struct vop_generic_args *)ap)) != 0)
		return (error);

	ap->a_vap->va_fsid = ap->a_vp->v_mount->mnt_stat.f_fsid.val[0];
	return (0);
}

/*
 * Handle to disallow write access if mounted read-only.
 */
static int
null_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;

	/*
	 * Disallow write attempts on read-only layers;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the filesystem.
	 */
	if (accmode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
	}
	return (null_bypass((struct vop_generic_args *)ap));
}

static int
null_accessx(struct vop_accessx_args *ap)
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;

	/*
	 * Disallow write attempts on read-only layers;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the filesystem.
	 */
	if (accmode & VWRITE) {
		switch (vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		default:
			break;
		}
	}
	return (null_bypass((struct vop_generic_args *)ap));
}

/*
 * Increasing refcount of lower vnode is needed at least for the case
 * when lower FS is NFS to do sillyrename if the file is in use.
 * Unfortunately v_usecount is incremented in many places in
 * the kernel and, as such, there may be races that result in
 * the NFS client doing an extraneous silly rename, but that seems
 * preferable to not doing a silly rename when it is needed.
 */
static int
null_remove(struct vop_remove_args *ap)
{
	int retval, vreleit;
	struct vnode *lvp, *vp;

	vp = ap->a_vp;
	if (vrefcnt(vp) > 1) {
		lvp = NULLVPTOLOWERVP(vp);
		VREF(lvp);
		vreleit = 1;
	} else
		vreleit = 0;
	VTONULL(vp)->null_flags |= NULLV_DROP;
	retval = null_bypass(&ap->a_gen);
	if (vreleit != 0)
		vrele(lvp);
	return (retval);
}

/*
 * We handle this to eliminate null FS to lower FS
 * file moving. Don't know why we don't allow this,
 * possibly we should.
 */
static int
null_rename(struct vop_rename_args *ap)
{
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tvp = ap->a_tvp;
	struct null_node *tnn;

	/* Check for cross-device rename. */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		vrele(fdvp);
		vrele(fvp);
		return (EXDEV);
	}

	if (tvp != NULL) {
		tnn = VTONULL(tvp);
		tnn->null_flags |= NULLV_DROP;
	}
	return (null_bypass((struct vop_generic_args *)ap));
}

static int
null_rmdir(struct vop_rmdir_args *ap)
{

	VTONULL(ap->a_vp)->null_flags |= NULLV_DROP;
	return (null_bypass(&ap->a_gen));
}

/*
 * We need to process our own vnode lock and then clear the
 * interlock flag as it applies only to our vnode, not the
 * vnodes below us on the stack.
 */
static int
null_lock(struct vop_lock1_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	struct null_node *nn;
	struct vnode *lvp;
	int error;


	if ((flags & LK_INTERLOCK) == 0) {
		VI_LOCK(vp);
		ap->a_flags = flags |= LK_INTERLOCK;
	}
	nn = VTONULL(vp);
	/*
	 * If we're still active we must ask the lower layer to
	 * lock as ffs has special lock considerations in its
	 * vop lock.
	 */
	if (nn != NULL && (lvp = NULLVPTOLOWERVP(vp)) != NULL) {
		VI_LOCK_FLAGS(lvp, MTX_DUPOK);
		VI_UNLOCK(vp);
		/*
		 * We have to hold the vnode here to solve a potential
		 * reclaim race.  If we're forcibly vgone'd while we
		 * still have refs, a thread could be sleeping inside
		 * the lowervp's vop_lock routine.  When we vgone we will
		 * drop our last ref to the lowervp, which would allow it
		 * to be reclaimed.  The lowervp could then be recycled,
		 * in which case it is not legal to be sleeping in its VOP.
		 * We prevent it from being recycled by holding the vnode
		 * here.
		 */
		vholdl(lvp);
		error = VOP_LOCK(lvp, flags);

		/*
		 * We might have slept to get the lock and someone might have
		 * clean our vnode already, switching vnode lock from one in
		 * lowervp to v_lock in our own vnode structure.  Handle this
		 * case by reacquiring correct lock in requested mode.
		 */
		if (VTONULL(vp) == NULL && error == 0) {
			ap->a_flags &= ~(LK_TYPE_MASK | LK_INTERLOCK);
			switch (flags & LK_TYPE_MASK) {
			case LK_SHARED:
				ap->a_flags |= LK_SHARED;
				break;
			case LK_UPGRADE:
			case LK_EXCLUSIVE:
				ap->a_flags |= LK_EXCLUSIVE;
				break;
			default:
				panic("Unsupported lock request %d\n",
				    ap->a_flags);
			}
			VOP_UNLOCK(lvp, 0);
			error = vop_stdlock(ap);
		}
		vdrop(lvp);
	} else
		error = vop_stdlock(ap);

	return (error);
}

/*
 * We need to process our own vnode unlock and then clear the
 * interlock flag as it applies only to our vnode, not the
 * vnodes below us on the stack.
 */
static int
null_unlock(struct vop_unlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int flags = ap->a_flags;
	int mtxlkflag = 0;
	struct null_node *nn;
	struct vnode *lvp;
	int error;

	if ((flags & LK_INTERLOCK) != 0)
		mtxlkflag = 1;
	else if (mtx_owned(VI_MTX(vp)) == 0) {
		VI_LOCK(vp);
		mtxlkflag = 2;
	}
	nn = VTONULL(vp);
	if (nn != NULL && (lvp = NULLVPTOLOWERVP(vp)) != NULL) {
		VI_LOCK_FLAGS(lvp, MTX_DUPOK);
		flags |= LK_INTERLOCK;
		vholdl(lvp);
		VI_UNLOCK(vp);
		error = VOP_UNLOCK(lvp, flags);
		vdrop(lvp);
		if (mtxlkflag == 0)
			VI_LOCK(vp);
	} else {
		if (mtxlkflag == 2)
			VI_UNLOCK(vp);
		error = vop_stdunlock(ap);
	}

	return (error);
}

/*
 * Do not allow the VOP_INACTIVE to be passed to the lower layer,
 * since the reference count on the lower vnode is not related to
 * ours.
 */
static int
null_inactive(struct vop_inactive_args *ap __unused)
{
	struct vnode *vp, *lvp;
	struct null_node *xp;
	struct mount *mp;
	struct null_mount *xmp;

	vp = ap->a_vp;
	xp = VTONULL(vp);
	lvp = NULLVPTOLOWERVP(vp);
	mp = vp->v_mount;
	xmp = MOUNTTONULLMOUNT(mp);
	if ((xmp->nullm_flags & NULLM_CACHE) == 0 ||
	    (xp->null_flags & NULLV_DROP) != 0 ||
	    (lvp->v_vflag & VV_NOSYNC) != 0) {
		/*
		 * If this is the last reference and caching of the
		 * nullfs vnodes is not enabled, or the lower vnode is
		 * deleted, then free up the vnode so as not to tie up
		 * the lower vnodes.
		 */
		vp->v_object = NULL;
		vrecycle(vp);
	}
	return (0);
}

/*
 * Now, the nullfs vnode and, due to the sharing lock, the lower
 * vnode, are exclusively locked, and we shall destroy the null vnode.
 */
static int
null_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct null_node *xp;
	struct vnode *lowervp;

	vp = ap->a_vp;
	xp = VTONULL(vp);
	lowervp = xp->null_lowervp;

	KASSERT(lowervp != NULL && vp->v_vnlock != &vp->v_lock,
	    ("Reclaiming incomplete null vnode %p", vp));

	null_hashrem(xp);
	/*
	 * Use the interlock to protect the clearing of v_data to
	 * prevent faults in null_lock().
	 */
	lockmgr(&vp->v_lock, LK_EXCLUSIVE, NULL);
	VI_LOCK(vp);
	vp->v_data = NULL;
	vp->v_object = NULL;
	vp->v_vnlock = &vp->v_lock;
	VI_UNLOCK(vp);

	/*
	 * If we were opened for write, we leased one write reference
	 * to the lower vnode.  If this is a reclamation due to the
	 * forced unmount, undo the reference now.
	 */
	if (vp->v_writecount > 0)
		VOP_ADD_WRITECOUNT(lowervp, -1);
	if ((xp->null_flags & NULLV_NOUNLOCK) != 0)
		vunref(lowervp);
	else
		vput(lowervp);
	free(xp, M_NULLFSNODE);

	return (0);
}

static int
null_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;

	printf("\tvp=%p, lowervp=%p\n", vp, VTONULL(vp)->null_lowervp);
	return (0);
}

/* ARGSUSED */
static int
null_getwritemount(struct vop_getwritemount_args *ap)
{
	struct null_node *xp;
	struct vnode *lowervp;
	struct vnode *vp;

	vp = ap->a_vp;
	VI_LOCK(vp);
	xp = VTONULL(vp);
	if (xp && (lowervp = xp->null_lowervp)) {
		VI_LOCK_FLAGS(lowervp, MTX_DUPOK);
		VI_UNLOCK(vp);
		vholdl(lowervp);
		VI_UNLOCK(lowervp);
		VOP_GETWRITEMOUNT(lowervp, ap->a_mpp);
		vdrop(lowervp);
	} else {
		VI_UNLOCK(vp);
		*(ap->a_mpp) = NULL;
	}
	return (0);
}

static int
null_vptofh(struct vop_vptofh_args *ap)
{
	struct vnode *lvp;

	lvp = NULLVPTOLOWERVP(ap->a_vp);
	return VOP_VPTOFH(lvp, ap->a_fhp);
}

static int
null_vptocnp(struct vop_vptocnp_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode **dvp = ap->a_vpp;
	struct vnode *lvp, *ldvp;
	struct ucred *cred = ap->a_cred;
	struct mount *mp;
	int error, locked;

	locked = VOP_ISLOCKED(vp);
	lvp = NULLVPTOLOWERVP(vp);
	vhold(lvp);
	mp = vp->v_mount;
	vfs_ref(mp);
	VOP_UNLOCK(vp, 0); /* vp is held by vn_vptocnp_locked that called us */
	ldvp = lvp;
	vref(lvp);
	error = vn_vptocnp(&ldvp, cred, ap->a_buf, ap->a_buflen);
	vdrop(lvp);
	if (error != 0) {
		vn_lock(vp, locked | LK_RETRY);
		vfs_rel(mp);
		return (ENOENT);
	}

	/*
	 * Exclusive lock is required by insmntque1 call in
	 * null_nodeget()
	 */
	error = vn_lock(ldvp, LK_EXCLUSIVE);
	if (error != 0) {
		vrele(ldvp);
		vn_lock(vp, locked | LK_RETRY);
		vfs_rel(mp);
		return (ENOENT);
	}
	error = null_nodeget(mp, ldvp, dvp);
	if (error == 0) {
#ifdef DIAGNOSTIC
		NULLVPTOLOWERVP(*dvp);
#endif
		VOP_UNLOCK(*dvp, 0); /* keep reference on *dvp */
	}
	vn_lock(vp, locked | LK_RETRY);
	vfs_rel(mp);
	return (error);
}

/*
 * Global vfs data structures
 */
struct vop_vector null_vnodeops = {
	.vop_bypass =		null_bypass,
	.vop_access =		null_access,
	.vop_accessx =		null_accessx,
	.vop_advlockpurge =	vop_stdadvlockpurge,
	.vop_bmap =		VOP_EOPNOTSUPP,
	.vop_getattr =		null_getattr,
	.vop_getwritemount =	null_getwritemount,
	.vop_inactive =		null_inactive,
	.vop_islocked =		vop_stdislocked,
	.vop_lock1 =		null_lock,
	.vop_lookup =		null_lookup,
	.vop_open =		null_open,
	.vop_print =		null_print,
	.vop_reclaim =		null_reclaim,
	.vop_remove =		null_remove,
	.vop_rename =		null_rename,
	.vop_rmdir =		null_rmdir,
	.vop_setattr =		null_setattr,
	.vop_strategy =		VOP_EOPNOTSUPP,
	.vop_unlock =		null_unlock,
	.vop_vptocnp =		null_vptocnp,
	.vop_vptofh =		null_vptofh,
	.vop_add_writecount =	null_add_writecount,
};
