/*	$OpenBSD: spec_vnops.c,v 1.114 2025/03/27 23:30:54 tedu Exp $	*/
/*	$NetBSD: spec_vnops.c,v 1.29 1996/04/22 01:42:38 christos Exp $	*/

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
 *	@(#)spec_vnops.c	8.8 (Berkeley) 11/21/94
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/disklabel.h>
#include <sys/lockf.h>
#include <sys/dkio.h>
#include <sys/malloc.h>
#include <sys/specdev.h>
#include <sys/unistd.h>

#define v_lastr v_specinfo->si_lastr

int	spec_open_clone(struct vop_open_args *);

struct vnodechain speclisth[SPECHSZ];

const struct vops spec_vops = {
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= spec_open,
	.vop_close	= spec_close,
	.vop_access	= spec_access,
	.vop_getattr	= spec_getattr,
	.vop_setattr	= spec_setattr,
	.vop_read	= spec_read,
	.vop_write	= spec_write,
	.vop_ioctl	= spec_ioctl,
	.vop_kqfilter	= spec_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= spec_fsync,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_inactive	= spec_inactive,
	.vop_reclaim	= nullop,
	.vop_lock	= nullop,
	.vop_unlock	= nullop,
	.vop_islocked	= nullop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= spec_strategy,
	.vop_print	= spec_print,
	.vop_pathconf	= spec_pathconf,
	.vop_advlock	= spec_advlock,
	.vop_bwrite	= vop_generic_bwrite,
};

/*
 * Open a special file.
 */
int
spec_open(void *v)
{
	struct vop_open_args *ap = v;
	struct proc *p = ap->a_p;
	struct vnode *vp = ap->a_vp;
	struct vnode *bvp;
	dev_t bdev;
	dev_t dev = (dev_t)vp->v_rdev;
	int maj = major(dev);
	int error;

	/*
	 * Don't allow open if fs is mounted -nodev.
	 */
	if (vp->v_mount && (vp->v_mount->mnt_flag & MNT_NODEV))
		return (ENXIO);

	switch (vp->v_type) {

	case VCHR:
		if ((u_int)maj >= nchrdev)
			return (ENXIO);
		if (ap->a_cred != FSCRED && (ap->a_mode & FWRITE)) {
			/*
			 * When running in very secure mode, do not allow
			 * opens for writing of any disk character devices.
			 */
			if (securelevel >= 2 && cdevsw[maj].d_type == D_DISK)
				return (EPERM);
			/*
			 * When running in secure mode, do not allow opens
			 * for writing of /dev/mem, /dev/kmem, or character
			 * devices whose corresponding block devices are
			 * currently mounted.
			 */
			if (securelevel >= 1) {
				if ((bdev = chrtoblk(dev)) != NODEV &&
				    vfinddev(bdev, VBLK, &bvp) &&
				    bvp->v_usecount > 0 &&
				    (error = vfs_mountedon(bvp)))
					return (error);
				if (iskmemdev(dev))
					return (EPERM);
			}
		}
		if (cdevsw[maj].d_type == D_TTY)
			vp->v_flag |= VISTTY;
		if (cdevsw[maj].d_flags & D_CLONE)
			return (spec_open_clone(ap));
		VOP_UNLOCK(vp);
		error = (*cdevsw[maj].d_open)(dev, ap->a_mode, S_IFCHR, p);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		return (error);

	case VBLK:
		if ((u_int)maj >= nblkdev)
			return (ENXIO);
		/*
		 * When running in very secure mode, do not allow
		 * opens for writing of any disk block devices.
		 */
		if (securelevel >= 2 && ap->a_cred != FSCRED &&
		    (ap->a_mode & FWRITE) && bdevsw[maj].d_type == D_DISK)
			return (EPERM);
		/*
		 * Do not allow opens of block devices that are
		 * currently mounted.
		 */
		if ((error = vfs_mountedon(vp)) != 0)
			return (error);
		return ((*bdevsw[maj].d_open)(dev, ap->a_mode, S_IFBLK, p));
	case VNON:
	case VLNK:
	case VDIR:
	case VREG:
	case VBAD:
	case VFIFO:
	case VSOCK:
		break;
	}
	return (0);
}

/*
 * Vnode op for read
 */
int
spec_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
 	struct proc *p = uio->uio_procp;
	struct buf *bp;
	daddr_t bn, nextbn, bscale;
	int bsize;
	struct partinfo dpart;
	size_t n;
	int on, majordev;
	int (*ioctl)(dev_t, u_long, caddr_t, int, struct proc *);
	int error = 0;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_READ)
		panic("spec_read mode");
	if (uio->uio_segflg == UIO_USERSPACE && uio->uio_procp != curproc)
		panic("spec_read proc");
#endif
	if (uio->uio_resid == 0)
		return (0);

	switch (vp->v_type) {

	case VCHR:
		VOP_UNLOCK(vp);
		error = (*cdevsw[major(vp->v_rdev)].d_read)
			(vp->v_rdev, uio, ap->a_ioflag);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		return (error);

	case VBLK:
		if (uio->uio_offset < 0)
			return (EINVAL);
		bsize = BLKDEV_IOSIZE;
		if ((majordev = major(vp->v_rdev)) < nblkdev &&
		    (ioctl = bdevsw[majordev].d_ioctl) != NULL &&
		    (*ioctl)(vp->v_rdev, DIOCGPART, (caddr_t)&dpart, FREAD, p) == 0) {
			u_int32_t frag =
			    DISKLABELV1_FFS_FRAG(dpart.part->p_fragblock);
			u_int32_t fsize =
			    DISKLABELV1_FFS_FSIZE(dpart.part->p_fragblock);
			if (dpart.part->p_fstype == FS_BSDFFS && frag != 0 &&
			    fsize != 0)
				bsize = frag * fsize;
		}
		bscale = btodb(bsize);
		do {
			bn = btodb(uio->uio_offset) & ~(bscale - 1);
			on = uio->uio_offset % bsize;
			n = ulmin((bsize - on), uio->uio_resid);
			if (vp->v_lastr + bscale == bn) {
				nextbn = bn + bscale;
				error = breadn(vp, bn, bsize, &nextbn, &bsize,
				    1, &bp);
			} else
				error = bread(vp, bn, bsize, &bp);
			vp->v_lastr = bn;
			n = ulmin(n, bsize - bp->b_resid);
			if (error) {
				brelse(bp);
				return (error);
			}
			error = uiomove((char *)bp->b_data + on, n, uio);
			brelse(bp);
		} while (error == 0 && uio->uio_resid > 0 && n != 0);
		return (error);

	default:
		panic("spec_read type");
	}
	/* NOTREACHED */
}

int
spec_inactive(void *v)
{
	struct vop_inactive_args *ap = v;

	VOP_UNLOCK(ap->a_vp);
	return (0);
}

/*
 * Vnode op for write
 */
int
spec_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	struct buf *bp;
	daddr_t bn, bscale;
	int bsize;
	struct partinfo dpart;
	size_t n;
	int on, majordev;
	int (*ioctl)(dev_t, u_long, caddr_t, int, struct proc *);
	int error = 0;

#ifdef DIAGNOSTIC
	if (uio->uio_rw != UIO_WRITE)
		panic("spec_write mode");
	if (uio->uio_segflg == UIO_USERSPACE && uio->uio_procp != curproc)
		panic("spec_write proc");
#endif

	switch (vp->v_type) {

	case VCHR:
		VOP_UNLOCK(vp);
		error = (*cdevsw[major(vp->v_rdev)].d_write)
			(vp->v_rdev, uio, ap->a_ioflag);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		return (error);

	case VBLK:
		if (uio->uio_resid == 0)
			return (0);
		if (uio->uio_offset < 0)
			return (EINVAL);
		bsize = BLKDEV_IOSIZE;
		if ((majordev = major(vp->v_rdev)) < nblkdev &&
		    (ioctl = bdevsw[majordev].d_ioctl) != NULL &&
		    (*ioctl)(vp->v_rdev, DIOCGPART, (caddr_t)&dpart, FREAD, p) == 0) {
			u_int32_t frag =
			    DISKLABELV1_FFS_FRAG(dpart.part->p_fragblock);
			u_int32_t fsize =
			    DISKLABELV1_FFS_FSIZE(dpart.part->p_fragblock);
			if (dpart.part->p_fstype == FS_BSDFFS && frag != 0 &&
			    fsize != 0)
				bsize = frag * fsize;
		}
		bscale = btodb(bsize);
		do {
			bn = btodb(uio->uio_offset) & ~(bscale - 1);
			on = uio->uio_offset % bsize;
			n = ulmin((bsize - on), uio->uio_resid);
			error = bread(vp, bn, bsize, &bp);
			n = ulmin(n, bsize - bp->b_resid);
			if (error) {
				brelse(bp);
				return (error);
			}
			error = uiomove((char *)bp->b_data + on, n, uio);
			if (n + on == bsize)
				bawrite(bp);
			else
				bdwrite(bp);
		} while (error == 0 && uio->uio_resid > 0 && n != 0);
		return (error);

	default:
		panic("spec_write type");
	}
	/* NOTREACHED */
}

/*
 * Device ioctl operation.
 */
int
spec_ioctl(void *v)
{
	struct vop_ioctl_args *ap = v;
	dev_t dev = ap->a_vp->v_rdev;
	int maj = major(dev);

	switch (ap->a_vp->v_type) {

	case VCHR:
		return ((*cdevsw[maj].d_ioctl)(dev, ap->a_command, ap->a_data,
		    ap->a_fflag, ap->a_p));

	case VBLK:
		return ((*bdevsw[maj].d_ioctl)(dev, ap->a_command, ap->a_data,
		    ap->a_fflag, ap->a_p));

	default:
		panic("spec_ioctl");
		/* NOTREACHED */
	}
}

int
spec_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	dev_t dev;

	dev = ap->a_vp->v_rdev;

	switch (ap->a_vp->v_type) {
	default:
		if (ap->a_kn->kn_flags & (__EV_POLL | __EV_SELECT))
			return seltrue_kqfilter(dev, ap->a_kn);
		break;
	case VCHR:
		if (cdevsw[major(dev)].d_kqfilter)
			return (*cdevsw[major(dev)].d_kqfilter)(dev, ap->a_kn);
	}
	return (EOPNOTSUPP);
}

/*
 * Synch buffers associated with a block device
 */
int
spec_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct buf *bp;
	struct buf *nbp;
	int s;

	if (vp->v_type == VCHR)
		return (0);
	/*
	 * Flush all dirty buffers associated with a block device.
	 */
loop:
	s = splbio();
	LIST_FOREACH_SAFE(bp, &vp->v_dirtyblkhd, b_vnbufs, nbp) {
		if ((bp->b_flags & B_BUSY))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("spec_fsync: not dirty");
		bufcache_take(bp);
		buf_acquire(bp);
		splx(s);
		bawrite(bp);
		goto loop;
	}
	if (ap->a_waitfor == MNT_WAIT) {
		vwaitforio (vp, 0, "spec_fsync", INFSLP);

#ifdef DIAGNOSTIC
		if (!LIST_EMPTY(&vp->v_dirtyblkhd)) {
			splx(s);
			vprint("spec_fsync: dirty", vp);
			goto loop;
		}
#endif
	}
	splx(s);
	return (0);
}

int
spec_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	int maj = major(bp->b_dev);

	(*bdevsw[maj].d_strategy)(bp);
	return (0);
}

/*
 * Device close routine
 */
int
spec_close(void *v)
{
	struct vop_close_args *ap = v;
	struct proc *p = ap->a_p;
	struct vnode *vp = ap->a_vp;
	dev_t dev = vp->v_rdev;
	int (*devclose)(dev_t, int, int, struct proc *);
	int mode, relock, xlocked, error;
	int clone = 0;

	mtx_enter(&vnode_mtx);
	xlocked = (vp->v_lflag & VXLOCK);
	mtx_leave(&vnode_mtx);

	switch (vp->v_type) {
	case VCHR:
		/*
		 * Hack: a tty device that is a controlling terminal
		 * has a reference from the session structure.
		 * We cannot easily tell that a character device is
		 * a controlling terminal, unless it is the closing
		 * process' controlling terminal.  In that case,
		 * if the reference count is 2 (this last descriptor
		 * plus the session), release the reference from the session.
		 */
		if (vcount(vp) == 2 && p != NULL && p->p_p->ps_pgrp &&
		    vp == p->p_p->ps_pgrp->pg_session->s_ttyvp) {
			vrele(vp);
			p->p_p->ps_pgrp->pg_session->s_ttyvp = NULL;
		}
		if (cdevsw[major(dev)].d_flags & D_CLONE) {
			clone = 1;
		} else {
			/*
			 * If the vnode is locked, then we are in the midst
			 * of forcibly closing the device, otherwise we only
			 * close on last reference.
			 */
			if (vcount(vp) > 1 && !xlocked)
				return (0);
		}
		devclose = cdevsw[major(dev)].d_close;
		mode = S_IFCHR;
		break;

	case VBLK:
		/*
		 * On last close of a block device (that isn't mounted)
		 * we must invalidate any in core blocks, so that
		 * we can, for instance, change floppy disks. In order to do
		 * that, we must lock the vnode. If we are coming from
		 * vclean(), the vnode is already locked.
		 */
		if (!xlocked)
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = vinvalbuf(vp, V_SAVE, ap->a_cred, p, 0, INFSLP);
		if (!xlocked)
			VOP_UNLOCK(vp);
		if (error)
			return (error);
		/*
		 * We do not want to really close the device if it
		 * is still in use unless we are trying to close it
		 * forcibly. Since every use (buffer, vnode, swap, cmap)
		 * holds a reference to the vnode, and because we mark
		 * any other vnodes that alias this device, when the
		 * sum of the reference counts on all the aliased
		 * vnodes descends to one, we are on last close.
		 */
		if (vcount(vp) > 1 && !xlocked)
			return (0);
		devclose = bdevsw[major(dev)].d_close;
		mode = S_IFBLK;
		break;

	default:
		panic("spec_close: not special");
	}

	/* release lock if held and this isn't coming from vclean() */
	relock = VOP_ISLOCKED(vp) && !xlocked;
	if (relock)
		VOP_UNLOCK(vp);
	error = (*devclose)(dev, ap->a_fflag, mode, p);
	if (relock)
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	if (error == 0 && clone) {
		struct vnode *pvp;

		pvp = vp->v_specparent; /* get parent device */
		clrbit(pvp->v_specbitmap, minor(dev) >> CLONE_SHIFT);
		vrele(pvp);
	}

	return (error);
}

int
spec_getattr(void *v)
{
	struct vop_getattr_args	*ap = v;
	struct vnode		*vp = ap->a_vp;
	int			 error;

	if (!(vp->v_flag & VCLONE))
		return (EBADF);

	vn_lock(vp->v_specparent, LK_EXCLUSIVE|LK_RETRY);
	error = VOP_GETATTR(vp->v_specparent, ap->a_vap, ap->a_cred, ap->a_p);
	VOP_UNLOCK(vp->v_specparent);

	return (error);
}

int
spec_setattr(void *v)
{
	struct vop_getattr_args	*ap = v;
	struct proc		*p = ap->a_p;
	struct vnode		*vp = ap->a_vp;
	int			 error;

	if (!(vp->v_flag & VCLONE))
		return (EBADF);

	vn_lock(vp->v_specparent, LK_EXCLUSIVE|LK_RETRY);
	error = VOP_SETATTR(vp->v_specparent, ap->a_vap, ap->a_cred, p);
	VOP_UNLOCK(vp->v_specparent);

	return (error);
}

int
spec_access(void *v)
{
	struct vop_access_args	*ap = v;
	struct vnode		*vp = ap->a_vp;
	int			 error;

	if (!(vp->v_flag & VCLONE))
		return (EBADF);

	vn_lock(vp->v_specparent, LK_EXCLUSIVE|LK_RETRY);
	error = VOP_ACCESS(vp->v_specparent, ap->a_mode, ap->a_cred, ap->a_p);
	VOP_UNLOCK(vp->v_specparent);

	return (error);
}

/*
 * Print out the contents of a special device vnode.
 */
int
spec_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;

	printf("tag VT_NON, dev %d, %d\n", major(ap->a_vp->v_rdev),
		minor(ap->a_vp->v_rdev));
#endif
	return 0;
}

/*
 * Return POSIX pathconf information applicable to special devices.
 */
int
spec_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		break;
	case _PC_MAX_CANON:
		*ap->a_retval = MAX_CANON;
		break;
	case _PC_MAX_INPUT:
		*ap->a_retval = MAX_INPUT;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_VDISABLE:
		*ap->a_retval = _POSIX_VDISABLE;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*ap->a_retval = 1;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Special device advisory byte-level locks.
 */
int
spec_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	return (lf_advlock(&vp->v_speclockf, (off_t)0, ap->a_id,
		ap->a_op, ap->a_fl, ap->a_flags));
}

/*
 * Copyright (c) 2006 Pedro Martelletto <pedro@ambientworks.net>
 * Copyright (c) 2006 Thordur Bjornsson <thib@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef	CLONE_DEBUG
#define	DNPRINTF(m...)	do { printf(m);  } while (0)
#else
#define	DNPRINTF(m...)	/* nothing */
#endif

int
spec_open_clone(struct vop_open_args *ap)
{
	struct vnode *cvp, *vp = ap->a_vp;
	struct cloneinfo *cip;
	int error, i;

	DNPRINTF("cloning vnode\n");

	if (minor(vp->v_rdev) >= (1 << CLONE_SHIFT))
		return (ENXIO);

	for (i = 1; i < CLONE_MAPSZ * NBBY; i++)
		if (isclr(vp->v_specbitmap, i)) {
			setbit(vp->v_specbitmap, i);
			break;
		}

	if (i == CLONE_MAPSZ * NBBY)
		return (EBUSY); /* too many open instances */

	error = cdevvp(makedev(major(vp->v_rdev),
	    (i << CLONE_SHIFT) | minor(vp->v_rdev)), &cvp);
	if (error) {
		clrbit(vp->v_specbitmap, i);
		return (error); /* out of vnodes */
	}

	VOP_UNLOCK(vp);

	error = cdevsw[major(vp->v_rdev)].d_open(cvp->v_rdev, ap->a_mode,
	    S_IFCHR, ap->a_p);

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	if (error) {
		vput(cvp);
		clrbit(vp->v_specbitmap, i);
		return (error); /* device open failed */
	}

	cvp->v_flag |= VCLONE;

	cip = malloc(sizeof(struct cloneinfo), M_TEMP, M_WAITOK);
	cip->ci_data = vp->v_data;
	cip->ci_vp = cvp;

	cvp->v_specparent = vp;
	vp->v_flag |= VCLONED;
	vp->v_data = cip;

	DNPRINTF("clone of vnode %p is vnode %p\n", vp, cvp);

	return (0); /* device cloned */
}
