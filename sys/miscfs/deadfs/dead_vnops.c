/*	$OpenBSD: dead_vnops.c,v 1.43 2024/10/18 05:52:32 miod Exp $	*/
/*	$NetBSD: dead_vnops.c,v 1.16 1996/02/13 13:12:48 mycroft Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)dead_vnops.c	8.2 (Berkeley) 11/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/buf.h>

/*
 * Prototypes for dead operations on vnodes.
 */
int	dead_ebadf(void *);

int	dead_open(void *);
int	dead_read(void *);
int	dead_write(void *);
int	dead_ioctl(void *);
int	dead_kqfilter(void *v);
int	dead_inactive(void *);
int	dead_lock(void *);
int	dead_bmap(void *);
int	dead_strategy(void *);
int	dead_print(void *);

int	chkvnlock(struct vnode *);

const struct vops dead_vops = {
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= dead_open,
	.vop_close	= nullop,
	.vop_access	= dead_ebadf,
	.vop_getattr	= dead_ebadf,
	.vop_setattr	= dead_ebadf,
	.vop_read	= dead_read,
	.vop_write	= dead_write,
	.vop_ioctl	= dead_ioctl,
	.vop_kqfilter	= dead_kqfilter,
	.vop_revoke	= NULL,
	.vop_fsync	= nullop,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= dead_ebadf,
	.vop_readlink	= dead_ebadf,
	.vop_abortop	= vop_generic_badop,
	.vop_inactive	= dead_inactive,
	.vop_reclaim	= nullop,
	.vop_lock	= dead_lock,
	.vop_unlock	= nullop,
	.vop_islocked	= nullop,
	.vop_bmap	= dead_bmap,
	.vop_strategy	= dead_strategy,
	.vop_print	= dead_print,
	.vop_pathconf	= dead_ebadf,
	.vop_advlock	= dead_ebadf,
	.vop_bwrite	= nullop,
};

/*
 * Open always fails as if device did not exist.
 */
int
dead_open(void *v)
{
	return (ENXIO);
}

/*
 * Vnode op for read
 */
int
dead_read(void *v)
{
	struct vop_read_args *ap = v;

	if (chkvnlock(ap->a_vp))
		panic("dead_read: lock");
	/*
	 * Return EOF for tty devices, EIO for others
	 */
	if ((ap->a_vp->v_flag & VISTTY) == 0)
		return (EIO);
	return (0);
}

/*
 * Vnode op for write
 */
int
dead_write(void *v)
{
	struct vop_write_args *ap = v;

	if (chkvnlock(ap->a_vp))
		panic("dead_write: lock");
	return (EIO);
}

/*
 * Device ioctl operation.
 */
int
dead_ioctl(void *v)
{
	struct vop_ioctl_args *ap = v;

	if (!chkvnlock(ap->a_vp))
		return (EBADF);
	return ((ap->a_vp->v_op->vop_ioctl)(ap));
}

int
dead_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;

	switch (ap->a_kn->kn_filter) {
	case EVFILT_READ:
	case EVFILT_WRITE:
		ap->a_kn->kn_fop = &dead_filtops;
		break;
	case EVFILT_EXCEPT:
		if ((ap->a_kn->kn_flags & __EV_POLL) == 0)
			return (EINVAL);
		ap->a_kn->kn_fop = &dead_filtops;
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Just call the device strategy routine
 */
int
dead_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	int s;

	if (ap->a_bp->b_vp == NULL || !chkvnlock(ap->a_bp->b_vp)) {
		ap->a_bp->b_flags |= B_ERROR;
		s = splbio();
		biodone(ap->a_bp);
		splx(s);
		return (EIO);
	}
	return (VOP_STRATEGY(ap->a_bp->b_vp, ap->a_bp));
}

int
dead_inactive(void *v)
{
	struct vop_inactive_args *ap = v;

	VOP_UNLOCK(ap->a_vp);
	return (0);
}

/*
 * Wait until the vnode has finished changing state.
 */
int
dead_lock(void *v)
{
	struct vop_lock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	if (ap->a_flags & LK_DRAIN || !chkvnlock(vp))
		return (0);

	return VOP_LOCK(vp, ap->a_flags);
}

/*
 * Wait until the vnode has finished changing state.
 */
int
dead_bmap(void *v)
{
	struct vop_bmap_args *ap = v;

	if (!chkvnlock(ap->a_vp))
		return (EIO);
	return (VOP_BMAP(ap->a_vp, ap->a_bn, ap->a_vpp, ap->a_bnp, ap->a_runp));
}

/*
 * Print out the contents of a dead vnode.
 */
int
dead_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	printf("tag VT_NON, dead vnode\n");
#endif
	return 0;
}

/*
 * Empty vnode failed operation
 */
int
dead_ebadf(void *v)
{
	return (EBADF);
}

/*
 * We have to wait during times when the vnode is
 * in a state of change.
 */
int
chkvnlock(struct vnode *vp)
{
	int locked = 0;

	mtx_enter(&vnode_mtx);
	while (vp->v_lflag & VXLOCK) {
		vp->v_lflag |= VXWANT;
		msleep_nsec(vp, &vnode_mtx, PINOD, "chkvnlock", INFSLP);
		locked = 1;
	}
	mtx_leave(&vnode_mtx);
	return (locked);
}
