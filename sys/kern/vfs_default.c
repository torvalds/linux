/*	$OpenBSD: vfs_default.c,v 1.52 2025/04/15 05:51:51 jsg Exp $  */

/*
 * Portions of this code are:
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/pool.h>
#include <sys/specdev.h>

/*
 * Eliminate all activity associated with the requested vnode
 * and with all vnodes aliased to the requested vnode.
 */
int
vop_generic_revoke(void *v)
{
	struct vop_revoke_args *ap = v;
	struct vnode *vp, *vq;
	struct proc *p = curproc;

#ifdef DIAGNOSTIC
	if ((ap->a_flags & REVOKEALL) == 0)
		panic("vop_generic_revoke");
#endif

	vp = ap->a_vp;
 
	while (vp->v_type == VBLK && vp->v_specinfo != NULL &&
	    vp->v_specmountpoint != NULL) {
		struct mount *mp = vp->v_specmountpoint;

		/*
		 * If we have a mount point associated with the vnode, we must
		 * flush it out now, as to not leave a dangling zombie mount
		 * point laying around in VFS.
		 */
		if (!vfs_busy(mp, VB_WRITE|VB_WAIT)) {
			dounmount(mp, MNT_FORCE | MNT_DOOMED, p);
			break;
		}
	}

	if (vp->v_flag & VALIASED) {
		/*
		 * If a vgone (or vclean) is already in progress,
		 * wait until it is done and return.
		 */
		mtx_enter(&vnode_mtx);
		if (vp->v_lflag & VXLOCK) {
			vp->v_lflag |= VXWANT;
			msleep_nsec(vp, &vnode_mtx, PINOD,
			    "vop_generic_revokeall", INFSLP);
			mtx_leave(&vnode_mtx);
			return(0);
		}

		/*
		 * Ensure that vp will not be vgone'd while we
		 * are eliminating its aliases.
		 */
		vp->v_lflag |= VXLOCK;
		mtx_leave(&vnode_mtx);

		while (vp->v_flag & VALIASED) {
			SLIST_FOREACH(vq, vp->v_hashchain, v_specnext) {
				if (vq->v_rdev != vp->v_rdev ||
				    vq->v_type != vp->v_type || vp == vq)
					continue;
				vgonel(vq, p);
				break;
			}
		}

		/*
		 * Remove the lock so that vgone below will
		 * really eliminate the vnode after which time
		 * vgone will awaken any sleepers.
		 */
		mtx_enter(&vnode_mtx);
		vp->v_lflag &= ~VXLOCK;
		mtx_leave(&vnode_mtx);
	}

	vgonel(vp, p);

	return (0);
}

int
vop_generic_badop(void *v)
{
	panic("%s", __func__);
}

int
vop_generic_bmap(void *v)
{
	struct vop_bmap_args *ap = v;

	if (ap->a_vpp)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp)
		*ap->a_bnp = ap->a_bn;
	if (ap->a_runp)
		*ap->a_runp = 0;

	return (0);
}

int
vop_generic_bwrite(void *v)
{
	struct vop_bwrite_args *ap = v;

	return (bwrite(ap->a_bp));
}

int
vop_generic_abortop(void *v)
{
	struct vop_abortop_args *ap = v;
 
	if ((ap->a_cnp->cn_flags & (HASBUF | SAVESTART)) == HASBUF)
		pool_put(&namei_pool, ap->a_cnp->cn_pnbuf);

	return (0);
}

/* Trivial lookup routine that always fails. */
int
vop_generic_lookup(void *v)
{
	struct vop_lookup_args	*ap = v;

	*ap->a_vpp = NULL;
	return (ENOTDIR);
}
