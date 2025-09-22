/*	$OpenBSD: mfs_vnops.c,v 1.62 2024/10/18 05:52:33 miod Exp $	*/
/*	$NetBSD: mfs_vnops.c,v 1.8 1996/03/17 02:16:32 christos Exp $	*/

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
 *	@(#)mfs_vnops.c	8.5 (Berkeley) 7/28/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/specdev.h>

#include <ufs/mfs/mfsnode.h>
#include <ufs/mfs/mfs_extern.h>

/* mfs vnode operations. */
const struct vops mfs_vops = {
        .vop_lookup     = vop_generic_badop,
        .vop_create     = vop_generic_badop,
        .vop_mknod      = vop_generic_badop,
        .vop_open       = mfs_open,
        .vop_close      = mfs_close,
        .vop_access     = vop_generic_badop,
        .vop_getattr    = vop_generic_badop,
        .vop_setattr    = vop_generic_badop,
        .vop_read       = vop_generic_badop,
        .vop_write      = vop_generic_badop,
        .vop_ioctl      = mfs_ioctl,
        .vop_kqfilter   = vop_generic_badop,
        .vop_revoke     = vop_generic_revoke,
        .vop_fsync      = spec_fsync,
        .vop_remove     = vop_generic_badop,
        .vop_link       = vop_generic_badop,
        .vop_rename     = vop_generic_badop,
        .vop_mkdir      = vop_generic_badop,
        .vop_rmdir      = vop_generic_badop,
        .vop_symlink    = vop_generic_badop,
        .vop_readdir    = vop_generic_badop,
        .vop_readlink   = vop_generic_badop,
        .vop_abortop    = vop_generic_badop,
        .vop_inactive   = mfs_inactive,
        .vop_reclaim    = mfs_reclaim,
        .vop_lock       = nullop,
        .vop_unlock     = nullop,
        .vop_islocked   = nullop,
        .vop_bmap       = vop_generic_bmap,
        .vop_strategy   = mfs_strategy,
        .vop_print      = mfs_print,
        .vop_pathconf   = vop_generic_badop,
        .vop_advlock    = vop_generic_badop,
        .vop_bwrite     = vop_generic_bwrite
};

/*
 * Vnode Operations.
 *
 * Open called to allow memory filesystem to initialize and
 * validate before actual IO. Record our process identifier
 * so we can tell when we are doing I/O to ourself.
 */
int
mfs_open(void *v)
{
#ifdef DIAGNOSTIC
	struct vop_open_args *ap = v;

	if (ap->a_vp->v_type != VBLK) {
		panic("mfs_open not VBLK");
	}
#endif
	return (0);
}

/*
 * Ioctl operation.
 */
int
mfs_ioctl(void *v)
{

	return (ENOTTY);
}

/*
 * Pass I/O requests to the memory filesystem process.
 */
int
mfs_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct mfsnode *mfsp;
	struct vnode *vp = ap->a_vp;

	if (vp->v_type != VBLK || vp->v_usecount == 0)
		panic("mfs_strategy: bad dev");

	mfsp = VTOMFS(vp);
	if (mfsp->mfs_tid == curproc->p_tid) {
		mfs_doio(mfsp, bp);
	} else {
		bufq_queue(&mfsp->mfs_bufq, bp);
		wakeup(vp);
	}
	return (0);
}

/*
 * Memory file system I/O.
 */
void
mfs_doio(struct mfsnode *mfsp, struct buf *bp)
{
	caddr_t base;
	long offset = bp->b_blkno << DEV_BSHIFT;
	int s;

	if (bp->b_bcount > mfsp->mfs_size - offset)
		bp->b_bcount = mfsp->mfs_size - offset;

	base = mfsp->mfs_baseoff + offset;
	if (bp->b_flags & B_READ)
		bp->b_error = copyin(base, bp->b_data, bp->b_bcount);
	else
		bp->b_error = copyout(bp->b_data, base, bp->b_bcount);
	if (bp->b_error)
		bp->b_flags |= B_ERROR;
	else
		bp->b_resid = 0;
	s = splbio();
	biodone(bp);
	splx(s);
}

/*
 * Memory filesystem close routine
 */
int
mfs_close(void *v)
{
	struct vop_close_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct mfsnode *mfsp = VTOMFS(vp);
	struct buf *bp;
	int error;

	/*
	 * Finish any pending I/O requests.
	 */
	while (1) {
		bp = bufq_dequeue(&mfsp->mfs_bufq);
		if (bp == NULL)
			break;
		mfs_doio(mfsp, bp);
		wakeup(bp);
	}

	/*
	 * On last close of a memory filesystem we must invalidate any in
	 * core blocks, so that we can free up its vnode.
	 */
	error = vinvalbuf(vp, V_SAVE, ap->a_cred, ap->a_p, 0, INFSLP);
	if (error != 0)
		return (error);

#ifdef DIAGNOSTIC
	/*
	 * There should be no way to have any more buffers on this vnode.
	 */
	if (bufq_peek(&mfsp->mfs_bufq))
		printf("mfs_close: dirty buffers\n");
#endif

	/*
	 * Send a request to the filesystem server to exit.
	 */
	mfsp->mfs_shutdown = 1;
	wakeup(vp);
	return (0);
}

/*
 * Memory filesystem inactive routine
 */
int
mfs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
#ifdef DIAGNOSTIC
	struct mfsnode *mfsp = VTOMFS(ap->a_vp);

	if (mfsp->mfs_shutdown && bufq_peek(&mfsp->mfs_bufq))
		panic("mfs_inactive: not inactive");
#endif
	VOP_UNLOCK(ap->a_vp);
	return (0);
}

/*
 * Reclaim a memory filesystem devvp so that it can be reused.
 */
int
mfs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct mfsnode *mfsp = VTOMFS(vp);

	bufq_destroy(&mfsp->mfs_bufq);

	free(vp->v_data, M_MFSNODE, sizeof(struct mfsnode));
	vp->v_data = NULL;
	return (0);
}

/*
 * Print out the contents of an mfsnode.
 */
int
mfs_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;
	struct mfsnode *mfsp = VTOMFS(ap->a_vp);

	printf("tag VT_MFS, tid %d, base %p, size %ld\n", mfsp->mfs_tid,
	    mfsp->mfs_baseoff, mfsp->mfs_size);
#endif
	return (0);
}
