/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Copyright (c) 2012 Konstantin Belousov <kib@FreeBSD.org>
 * Copyright (c) 2013, 2014 The FreeBSD Foundation
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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
 *	@(#)vfs_vnops.c	8.2 (Berkeley) 1/21/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/disk.h>
#include <sys/fail.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kdb.h>
#include <sys/stat.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/filio.h>
#include <sys/resourcevar.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/ttycom.h>
#include <sys/conf.h>
#include <sys/syslog.h>
#include <sys/unistd.h>
#include <sys/user.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vnode_pager.h>

#ifdef HWPMC_HOOKS
#include <sys/pmckern.h>
#endif

static fo_rdwr_t	vn_read;
static fo_rdwr_t	vn_write;
static fo_rdwr_t	vn_io_fault;
static fo_truncate_t	vn_truncate;
static fo_ioctl_t	vn_ioctl;
static fo_poll_t	vn_poll;
static fo_kqfilter_t	vn_kqfilter;
static fo_stat_t	vn_statfile;
static fo_close_t	vn_closefile;
static fo_mmap_t	vn_mmap;

struct 	fileops vnops = {
	.fo_read = vn_io_fault,
	.fo_write = vn_io_fault,
	.fo_truncate = vn_truncate,
	.fo_ioctl = vn_ioctl,
	.fo_poll = vn_poll,
	.fo_kqfilter = vn_kqfilter,
	.fo_stat = vn_statfile,
	.fo_close = vn_closefile,
	.fo_chmod = vn_chmod,
	.fo_chown = vn_chown,
	.fo_sendfile = vn_sendfile,
	.fo_seek = vn_seek,
	.fo_fill_kinfo = vn_fill_kinfo,
	.fo_mmap = vn_mmap,
	.fo_flags = DFLAG_PASSABLE | DFLAG_SEEKABLE
};

static const int io_hold_cnt = 16;
static int vn_io_fault_enable = 1;
SYSCTL_INT(_debug, OID_AUTO, vn_io_fault_enable, CTLFLAG_RW,
    &vn_io_fault_enable, 0, "Enable vn_io_fault lock avoidance");
static int vn_io_fault_prefault = 0;
SYSCTL_INT(_debug, OID_AUTO, vn_io_fault_prefault, CTLFLAG_RW,
    &vn_io_fault_prefault, 0, "Enable vn_io_fault prefaulting");
static u_long vn_io_faults_cnt;
SYSCTL_ULONG(_debug, OID_AUTO, vn_io_faults, CTLFLAG_RD,
    &vn_io_faults_cnt, 0, "Count of vn_io_fault lock avoidance triggers");

/*
 * Returns true if vn_io_fault mode of handling the i/o request should
 * be used.
 */
static bool
do_vn_io_fault(struct vnode *vp, struct uio *uio)
{
	struct mount *mp;

	return (uio->uio_segflg == UIO_USERSPACE && vp->v_type == VREG &&
	    (mp = vp->v_mount) != NULL &&
	    (mp->mnt_kern_flag & MNTK_NO_IOPF) != 0 && vn_io_fault_enable);
}

/*
 * Structure used to pass arguments to vn_io_fault1(), to do either
 * file- or vnode-based I/O calls.
 */
struct vn_io_fault_args {
	enum {
		VN_IO_FAULT_FOP,
		VN_IO_FAULT_VOP
	} kind;
	struct ucred *cred;
	int flags;
	union {
		struct fop_args_tag {
			struct file *fp;
			fo_rdwr_t *doio;
		} fop_args;
		struct vop_args_tag {
			struct vnode *vp;
		} vop_args;
	} args;
};

static int vn_io_fault1(struct vnode *vp, struct uio *uio,
    struct vn_io_fault_args *args, struct thread *td);

int
vn_open(struct nameidata *ndp, int *flagp, int cmode, struct file *fp)
{
	struct thread *td = ndp->ni_cnd.cn_thread;

	return (vn_open_cred(ndp, flagp, cmode, 0, td->td_ucred, fp));
}

/*
 * Common code for vnode open operations via a name lookup.
 * Lookup the vnode and invoke VOP_CREATE if needed.
 * Check permissions, and call the VOP_OPEN or VOP_CREATE routine.
 * 
 * Note that this does NOT free nameidata for the successful case,
 * due to the NDINIT being done elsewhere.
 */
int
vn_open_cred(struct nameidata *ndp, int *flagp, int cmode, u_int vn_open_flags,
    struct ucred *cred, struct file *fp)
{
	struct vnode *vp;
	struct mount *mp;
	struct thread *td = ndp->ni_cnd.cn_thread;
	struct vattr vat;
	struct vattr *vap = &vat;
	int fmode, error;

restart:
	fmode = *flagp;
	if ((fmode & (O_CREAT | O_EXCL | O_DIRECTORY)) == (O_CREAT |
	    O_EXCL | O_DIRECTORY))
		return (EINVAL);
	else if ((fmode & (O_CREAT | O_DIRECTORY)) == O_CREAT) {
		ndp->ni_cnd.cn_nameiop = CREATE;
		/*
		 * Set NOCACHE to avoid flushing the cache when
		 * rolling in many files at once.
		*/
		ndp->ni_cnd.cn_flags = ISOPEN | LOCKPARENT | LOCKLEAF | NOCACHE;
		if ((fmode & O_EXCL) == 0 && (fmode & O_NOFOLLOW) == 0)
			ndp->ni_cnd.cn_flags |= FOLLOW;
		if ((fmode & O_BENEATH) != 0)
			ndp->ni_cnd.cn_flags |= BENEATH;
		if (!(vn_open_flags & VN_OPEN_NOAUDIT))
			ndp->ni_cnd.cn_flags |= AUDITVNODE1;
		if (vn_open_flags & VN_OPEN_NOCAPCHECK)
			ndp->ni_cnd.cn_flags |= NOCAPCHECK;
		bwillwrite();
		if ((error = namei(ndp)) != 0)
			return (error);
		if (ndp->ni_vp == NULL) {
			VATTR_NULL(vap);
			vap->va_type = VREG;
			vap->va_mode = cmode;
			if (fmode & O_EXCL)
				vap->va_vaflags |= VA_EXCLUSIVE;
			if (vn_start_write(ndp->ni_dvp, &mp, V_NOWAIT) != 0) {
				NDFREE(ndp, NDF_ONLY_PNBUF);
				vput(ndp->ni_dvp);
				if ((error = vn_start_write(NULL, &mp,
				    V_XSLEEP | PCATCH)) != 0)
					return (error);
				goto restart;
			}
			if ((vn_open_flags & VN_OPEN_NAMECACHE) != 0)
				ndp->ni_cnd.cn_flags |= MAKEENTRY;
#ifdef MAC
			error = mac_vnode_check_create(cred, ndp->ni_dvp,
			    &ndp->ni_cnd, vap);
			if (error == 0)
#endif
				error = VOP_CREATE(ndp->ni_dvp, &ndp->ni_vp,
						   &ndp->ni_cnd, vap);
			vput(ndp->ni_dvp);
			vn_finished_write(mp);
			if (error) {
				NDFREE(ndp, NDF_ONLY_PNBUF);
				return (error);
			}
			fmode &= ~O_TRUNC;
			vp = ndp->ni_vp;
		} else {
			if (ndp->ni_dvp == ndp->ni_vp)
				vrele(ndp->ni_dvp);
			else
				vput(ndp->ni_dvp);
			ndp->ni_dvp = NULL;
			vp = ndp->ni_vp;
			if (fmode & O_EXCL) {
				error = EEXIST;
				goto bad;
			}
			fmode &= ~O_CREAT;
		}
	} else {
		ndp->ni_cnd.cn_nameiop = LOOKUP;
		ndp->ni_cnd.cn_flags = ISOPEN |
		    ((fmode & O_NOFOLLOW) ? NOFOLLOW : FOLLOW) | LOCKLEAF;
		if (!(fmode & FWRITE))
			ndp->ni_cnd.cn_flags |= LOCKSHARED;
		if ((fmode & O_BENEATH) != 0)
			ndp->ni_cnd.cn_flags |= BENEATH;
		if (!(vn_open_flags & VN_OPEN_NOAUDIT))
			ndp->ni_cnd.cn_flags |= AUDITVNODE1;
		if (vn_open_flags & VN_OPEN_NOCAPCHECK)
			ndp->ni_cnd.cn_flags |= NOCAPCHECK;
		if ((error = namei(ndp)) != 0)
			return (error);
		vp = ndp->ni_vp;
	}
	error = vn_open_vnode(vp, fmode, cred, td, fp);
	if (error)
		goto bad;
	*flagp = fmode;
	return (0);
bad:
	NDFREE(ndp, NDF_ONLY_PNBUF);
	vput(vp);
	*flagp = fmode;
	ndp->ni_vp = NULL;
	return (error);
}

/*
 * Common code for vnode open operations once a vnode is located.
 * Check permissions, and call the VOP_OPEN routine.
 */
int
vn_open_vnode(struct vnode *vp, int fmode, struct ucred *cred,
    struct thread *td, struct file *fp)
{
	accmode_t accmode;
	struct flock lf;
	int error, lock_flags, type;

	if (vp->v_type == VLNK)
		return (EMLINK);
	if (vp->v_type == VSOCK)
		return (EOPNOTSUPP);
	if (vp->v_type != VDIR && fmode & O_DIRECTORY)
		return (ENOTDIR);
	accmode = 0;
	if (fmode & (FWRITE | O_TRUNC)) {
		if (vp->v_type == VDIR)
			return (EISDIR);
		accmode |= VWRITE;
	}
	if (fmode & FREAD)
		accmode |= VREAD;
	if (fmode & FEXEC)
		accmode |= VEXEC;
	if ((fmode & O_APPEND) && (fmode & FWRITE))
		accmode |= VAPPEND;
#ifdef MAC
	if (fmode & O_CREAT)
		accmode |= VCREAT;
	if (fmode & O_VERIFY)
		accmode |= VVERIFY;
	error = mac_vnode_check_open(cred, vp, accmode);
	if (error)
		return (error);

	accmode &= ~(VCREAT | VVERIFY);
#endif
	if ((fmode & O_CREAT) == 0) {
		if (accmode & VWRITE) {
			error = vn_writechk(vp);
			if (error)
				return (error);
		}
		if (accmode) {
		        error = VOP_ACCESS(vp, accmode, cred, td);
			if (error)
				return (error);
		}
	}
	if (vp->v_type == VFIFO && VOP_ISLOCKED(vp) != LK_EXCLUSIVE)
		vn_lock(vp, LK_UPGRADE | LK_RETRY);
	if ((error = VOP_OPEN(vp, fmode, cred, td, fp)) != 0)
		return (error);

	while ((fmode & (O_EXLOCK | O_SHLOCK)) != 0) {
		KASSERT(fp != NULL, ("open with flock requires fp"));
		if (fp->f_type != DTYPE_NONE && fp->f_type != DTYPE_VNODE) {
			error = EOPNOTSUPP;
			break;
		}
		lock_flags = VOP_ISLOCKED(vp);
		VOP_UNLOCK(vp, 0);
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		if (fmode & O_EXLOCK)
			lf.l_type = F_WRLCK;
		else
			lf.l_type = F_RDLCK;
		type = F_FLOCK;
		if ((fmode & FNONBLOCK) == 0)
			type |= F_WAIT;
		error = VOP_ADVLOCK(vp, (caddr_t)fp, F_SETLK, &lf, type);
		if (error == 0)
			fp->f_flag |= FHASLOCK;
		vn_lock(vp, lock_flags | LK_RETRY);
		if (error != 0)
			break;
		if ((vp->v_iflag & VI_DOOMED) != 0) {
			error = ENOENT;
			break;
		}

		/*
		 * Another thread might have used this vnode as an
		 * executable while the vnode lock was dropped.
		 * Ensure the vnode is still able to be opened for
		 * writing after the lock has been obtained.
		 */
		if ((accmode & VWRITE) != 0)
			error = vn_writechk(vp);
		break;
	}

	if (error != 0) {
		fp->f_flag |= FOPENFAILED;
		fp->f_vnode = vp;
		if (fp->f_ops == &badfileops) {
			fp->f_type = DTYPE_VNODE;
			fp->f_ops = &vnops;
		}
		vref(vp);
	} else if  ((fmode & FWRITE) != 0) {
		VOP_ADD_WRITECOUNT(vp, 1);
		CTR3(KTR_VFS, "%s: vp %p v_writecount increased to %d",
		    __func__, vp, vp->v_writecount);
	}
	ASSERT_VOP_LOCKED(vp, "vn_open_vnode");
	return (error);
}

/*
 * Check for write permissions on the specified vnode.
 * Prototype text segments cannot be written.
 */
int
vn_writechk(struct vnode *vp)
{

	ASSERT_VOP_LOCKED(vp, "vn_writechk");
	/*
	 * If there's shared text associated with
	 * the vnode, try to free it up once.  If
	 * we fail, we can't allow writing.
	 */
	if (VOP_IS_TEXT(vp))
		return (ETXTBSY);

	return (0);
}

/*
 * Vnode close call
 */
static int
vn_close1(struct vnode *vp, int flags, struct ucred *file_cred,
    struct thread *td, bool keep_ref)
{
	struct mount *mp;
	int error, lock_flags;

	if (vp->v_type != VFIFO && (flags & FWRITE) == 0 &&
	    MNT_EXTENDED_SHARED(vp->v_mount))
		lock_flags = LK_SHARED;
	else
		lock_flags = LK_EXCLUSIVE;

	vn_start_write(vp, &mp, V_WAIT);
	vn_lock(vp, lock_flags | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
	if ((flags & (FWRITE | FOPENFAILED)) == FWRITE) {
		VNASSERT(vp->v_writecount > 0, vp, 
		    ("vn_close: negative writecount"));
		VOP_ADD_WRITECOUNT(vp, -1);
		CTR3(KTR_VFS, "%s: vp %p v_writecount decreased to %d",
		    __func__, vp, vp->v_writecount);
	}
	error = VOP_CLOSE(vp, flags, file_cred, td);
	if (keep_ref)
		VOP_UNLOCK(vp, 0);
	else
		vput(vp);
	vn_finished_write(mp);
	return (error);
}

int
vn_close(struct vnode *vp, int flags, struct ucred *file_cred,
    struct thread *td)
{

	return (vn_close1(vp, flags, file_cred, td, false));
}

/*
 * Heuristic to detect sequential operation.
 */
static int
sequential_heuristic(struct uio *uio, struct file *fp)
{

	ASSERT_VOP_LOCKED(fp->f_vnode, __func__);
	if (fp->f_flag & FRDAHEAD)
		return (fp->f_seqcount << IO_SEQSHIFT);

	/*
	 * Offset 0 is handled specially.  open() sets f_seqcount to 1 so
	 * that the first I/O is normally considered to be slightly
	 * sequential.  Seeking to offset 0 doesn't change sequentiality
	 * unless previous seeks have reduced f_seqcount to 0, in which
	 * case offset 0 is not special.
	 */
	if ((uio->uio_offset == 0 && fp->f_seqcount > 0) ||
	    uio->uio_offset == fp->f_nextoff) {
		/*
		 * f_seqcount is in units of fixed-size blocks so that it
		 * depends mainly on the amount of sequential I/O and not
		 * much on the number of sequential I/O's.  The fixed size
		 * of 16384 is hard-coded here since it is (not quite) just
		 * a magic size that works well here.  This size is more
		 * closely related to the best I/O size for real disks than
		 * to any block size used by software.
		 */
		fp->f_seqcount += howmany(uio->uio_resid, 16384);
		if (fp->f_seqcount > IO_SEQMAX)
			fp->f_seqcount = IO_SEQMAX;
		return (fp->f_seqcount << IO_SEQSHIFT);
	}

	/* Not sequential.  Quickly draw-down sequentiality. */
	if (fp->f_seqcount > 1)
		fp->f_seqcount = 1;
	else
		fp->f_seqcount = 0;
	return (0);
}

/*
 * Package up an I/O request on a vnode into a uio and do it.
 */
int
vn_rdwr(enum uio_rw rw, struct vnode *vp, void *base, int len, off_t offset,
    enum uio_seg segflg, int ioflg, struct ucred *active_cred,
    struct ucred *file_cred, ssize_t *aresid, struct thread *td)
{
	struct uio auio;
	struct iovec aiov;
	struct mount *mp;
	struct ucred *cred;
	void *rl_cookie;
	struct vn_io_fault_args args;
	int error, lock_flags;

	if (offset < 0 && vp->v_type != VCHR)
		return (EINVAL);
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = base;
	aiov.iov_len = len;
	auio.uio_resid = len;
	auio.uio_offset = offset;
	auio.uio_segflg = segflg;
	auio.uio_rw = rw;
	auio.uio_td = td;
	error = 0;

	if ((ioflg & IO_NODELOCKED) == 0) {
		if ((ioflg & IO_RANGELOCKED) == 0) {
			if (rw == UIO_READ) {
				rl_cookie = vn_rangelock_rlock(vp, offset,
				    offset + len);
			} else {
				rl_cookie = vn_rangelock_wlock(vp, offset,
				    offset + len);
			}
		} else
			rl_cookie = NULL;
		mp = NULL;
		if (rw == UIO_WRITE) { 
			if (vp->v_type != VCHR &&
			    (error = vn_start_write(vp, &mp, V_WAIT | PCATCH))
			    != 0)
				goto out;
			if (MNT_SHARED_WRITES(mp) ||
			    ((mp == NULL) && MNT_SHARED_WRITES(vp->v_mount)))
				lock_flags = LK_SHARED;
			else
				lock_flags = LK_EXCLUSIVE;
		} else
			lock_flags = LK_SHARED;
		vn_lock(vp, lock_flags | LK_RETRY);
	} else
		rl_cookie = NULL;

	ASSERT_VOP_LOCKED(vp, "IO_NODELOCKED with no vp lock held");
#ifdef MAC
	if ((ioflg & IO_NOMACCHECK) == 0) {
		if (rw == UIO_READ)
			error = mac_vnode_check_read(active_cred, file_cred,
			    vp);
		else
			error = mac_vnode_check_write(active_cred, file_cred,
			    vp);
	}
#endif
	if (error == 0) {
		if (file_cred != NULL)
			cred = file_cred;
		else
			cred = active_cred;
		if (do_vn_io_fault(vp, &auio)) {
			args.kind = VN_IO_FAULT_VOP;
			args.cred = cred;
			args.flags = ioflg;
			args.args.vop_args.vp = vp;
			error = vn_io_fault1(vp, &auio, &args, td);
		} else if (rw == UIO_READ) {
			error = VOP_READ(vp, &auio, ioflg, cred);
		} else /* if (rw == UIO_WRITE) */ {
			error = VOP_WRITE(vp, &auio, ioflg, cred);
		}
	}
	if (aresid)
		*aresid = auio.uio_resid;
	else
		if (auio.uio_resid && error == 0)
			error = EIO;
	if ((ioflg & IO_NODELOCKED) == 0) {
		VOP_UNLOCK(vp, 0);
		if (mp != NULL)
			vn_finished_write(mp);
	}
 out:
	if (rl_cookie != NULL)
		vn_rangelock_unlock(vp, rl_cookie);
	return (error);
}

/*
 * Package up an I/O request on a vnode into a uio and do it.  The I/O
 * request is split up into smaller chunks and we try to avoid saturating
 * the buffer cache while potentially holding a vnode locked, so we 
 * check bwillwrite() before calling vn_rdwr().  We also call kern_yield()
 * to give other processes a chance to lock the vnode (either other processes
 * core'ing the same binary, or unrelated processes scanning the directory).
 */
int
vn_rdwr_inchunks(enum uio_rw rw, struct vnode *vp, void *base, size_t len,
    off_t offset, enum uio_seg segflg, int ioflg, struct ucred *active_cred,
    struct ucred *file_cred, size_t *aresid, struct thread *td)
{
	int error = 0;
	ssize_t iaresid;

	do {
		int chunk;

		/*
		 * Force `offset' to a multiple of MAXBSIZE except possibly
		 * for the first chunk, so that filesystems only need to
		 * write full blocks except possibly for the first and last
		 * chunks.
		 */
		chunk = MAXBSIZE - (uoff_t)offset % MAXBSIZE;

		if (chunk > len)
			chunk = len;
		if (rw != UIO_READ && vp->v_type == VREG)
			bwillwrite();
		iaresid = 0;
		error = vn_rdwr(rw, vp, base, chunk, offset, segflg,
		    ioflg, active_cred, file_cred, &iaresid, td);
		len -= chunk;	/* aresid calc already includes length */
		if (error)
			break;
		offset += chunk;
		base = (char *)base + chunk;
		kern_yield(PRI_USER);
	} while (len);
	if (aresid)
		*aresid = len + iaresid;
	return (error);
}

off_t
foffset_lock(struct file *fp, int flags)
{
	struct mtx *mtxp;
	off_t res;

	KASSERT((flags & FOF_OFFSET) == 0, ("FOF_OFFSET passed"));

#if OFF_MAX <= LONG_MAX
	/*
	 * Caller only wants the current f_offset value.  Assume that
	 * the long and shorter integer types reads are atomic.
	 */
	if ((flags & FOF_NOLOCK) != 0)
		return (fp->f_offset);
#endif

	/*
	 * According to McKusick the vn lock was protecting f_offset here.
	 * It is now protected by the FOFFSET_LOCKED flag.
	 */
	mtxp = mtx_pool_find(mtxpool_sleep, fp);
	mtx_lock(mtxp);
	if ((flags & FOF_NOLOCK) == 0) {
		while (fp->f_vnread_flags & FOFFSET_LOCKED) {
			fp->f_vnread_flags |= FOFFSET_LOCK_WAITING;
			msleep(&fp->f_vnread_flags, mtxp, PUSER -1,
			    "vofflock", 0);
		}
		fp->f_vnread_flags |= FOFFSET_LOCKED;
	}
	res = fp->f_offset;
	mtx_unlock(mtxp);
	return (res);
}

void
foffset_unlock(struct file *fp, off_t val, int flags)
{
	struct mtx *mtxp;

	KASSERT((flags & FOF_OFFSET) == 0, ("FOF_OFFSET passed"));

#if OFF_MAX <= LONG_MAX
	if ((flags & FOF_NOLOCK) != 0) {
		if ((flags & FOF_NOUPDATE) == 0)
			fp->f_offset = val;
		if ((flags & FOF_NEXTOFF) != 0)
			fp->f_nextoff = val;
		return;
	}
#endif

	mtxp = mtx_pool_find(mtxpool_sleep, fp);
	mtx_lock(mtxp);
	if ((flags & FOF_NOUPDATE) == 0)
		fp->f_offset = val;
	if ((flags & FOF_NEXTOFF) != 0)
		fp->f_nextoff = val;
	if ((flags & FOF_NOLOCK) == 0) {
		KASSERT((fp->f_vnread_flags & FOFFSET_LOCKED) != 0,
		    ("Lost FOFFSET_LOCKED"));
		if (fp->f_vnread_flags & FOFFSET_LOCK_WAITING)
			wakeup(&fp->f_vnread_flags);
		fp->f_vnread_flags = 0;
	}
	mtx_unlock(mtxp);
}

void
foffset_lock_uio(struct file *fp, struct uio *uio, int flags)
{

	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = foffset_lock(fp, flags);
}

void
foffset_unlock_uio(struct file *fp, struct uio *uio, int flags)
{

	if ((flags & FOF_OFFSET) == 0)
		foffset_unlock(fp, uio->uio_offset, flags);
}

static int
get_advice(struct file *fp, struct uio *uio)
{
	struct mtx *mtxp;
	int ret;

	ret = POSIX_FADV_NORMAL;
	if (fp->f_advice == NULL || fp->f_vnode->v_type != VREG)
		return (ret);

	mtxp = mtx_pool_find(mtxpool_sleep, fp);
	mtx_lock(mtxp);
	if (fp->f_advice != NULL &&
	    uio->uio_offset >= fp->f_advice->fa_start &&
	    uio->uio_offset + uio->uio_resid <= fp->f_advice->fa_end)
		ret = fp->f_advice->fa_advice;
	mtx_unlock(mtxp);
	return (ret);
}

/*
 * File table vnode read routine.
 */
static int
vn_read(struct file *fp, struct uio *uio, struct ucred *active_cred, int flags,
    struct thread *td)
{
	struct vnode *vp;
	off_t orig_offset;
	int error, ioflag;
	int advice;

	KASSERT(uio->uio_td == td, ("uio_td %p is not td %p",
	    uio->uio_td, td));
	KASSERT(flags & FOF_OFFSET, ("No FOF_OFFSET"));
	vp = fp->f_vnode;
	ioflag = 0;
	if (fp->f_flag & FNONBLOCK)
		ioflag |= IO_NDELAY;
	if (fp->f_flag & O_DIRECT)
		ioflag |= IO_DIRECT;
	advice = get_advice(fp, uio);
	vn_lock(vp, LK_SHARED | LK_RETRY);

	switch (advice) {
	case POSIX_FADV_NORMAL:
	case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_NOREUSE:
		ioflag |= sequential_heuristic(uio, fp);
		break;
	case POSIX_FADV_RANDOM:
		/* Disable read-ahead for random I/O. */
		break;
	}
	orig_offset = uio->uio_offset;

#ifdef MAC
	error = mac_vnode_check_read(active_cred, fp->f_cred, vp);
	if (error == 0)
#endif
		error = VOP_READ(vp, uio, ioflag, fp->f_cred);
	fp->f_nextoff = uio->uio_offset;
	VOP_UNLOCK(vp, 0);
	if (error == 0 && advice == POSIX_FADV_NOREUSE &&
	    orig_offset != uio->uio_offset)
		/*
		 * Use POSIX_FADV_DONTNEED to flush pages and buffers
		 * for the backing file after a POSIX_FADV_NOREUSE
		 * read(2).
		 */
		error = VOP_ADVISE(vp, orig_offset, uio->uio_offset - 1,
		    POSIX_FADV_DONTNEED);
	return (error);
}

/*
 * File table vnode write routine.
 */
static int
vn_write(struct file *fp, struct uio *uio, struct ucred *active_cred, int flags,
    struct thread *td)
{
	struct vnode *vp;
	struct mount *mp;
	off_t orig_offset;
	int error, ioflag, lock_flags;
	int advice;

	KASSERT(uio->uio_td == td, ("uio_td %p is not td %p",
	    uio->uio_td, td));
	KASSERT(flags & FOF_OFFSET, ("No FOF_OFFSET"));
	vp = fp->f_vnode;
	if (vp->v_type == VREG)
		bwillwrite();
	ioflag = IO_UNIT;
	if (vp->v_type == VREG && (fp->f_flag & O_APPEND))
		ioflag |= IO_APPEND;
	if (fp->f_flag & FNONBLOCK)
		ioflag |= IO_NDELAY;
	if (fp->f_flag & O_DIRECT)
		ioflag |= IO_DIRECT;
	if ((fp->f_flag & O_FSYNC) ||
	    (vp->v_mount && (vp->v_mount->mnt_flag & MNT_SYNCHRONOUS)))
		ioflag |= IO_SYNC;
	mp = NULL;
	if (vp->v_type != VCHR &&
	    (error = vn_start_write(vp, &mp, V_WAIT | PCATCH)) != 0)
		goto unlock;

	advice = get_advice(fp, uio);

	if (MNT_SHARED_WRITES(mp) ||
	    (mp == NULL && MNT_SHARED_WRITES(vp->v_mount))) {
		lock_flags = LK_SHARED;
	} else {
		lock_flags = LK_EXCLUSIVE;
	}

	vn_lock(vp, lock_flags | LK_RETRY);
	switch (advice) {
	case POSIX_FADV_NORMAL:
	case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_NOREUSE:
		ioflag |= sequential_heuristic(uio, fp);
		break;
	case POSIX_FADV_RANDOM:
		/* XXX: Is this correct? */
		break;
	}
	orig_offset = uio->uio_offset;

#ifdef MAC
	error = mac_vnode_check_write(active_cred, fp->f_cred, vp);
	if (error == 0)
#endif
		error = VOP_WRITE(vp, uio, ioflag, fp->f_cred);
	fp->f_nextoff = uio->uio_offset;
	VOP_UNLOCK(vp, 0);
	if (vp->v_type != VCHR)
		vn_finished_write(mp);
	if (error == 0 && advice == POSIX_FADV_NOREUSE &&
	    orig_offset != uio->uio_offset)
		/*
		 * Use POSIX_FADV_DONTNEED to flush pages and buffers
		 * for the backing file after a POSIX_FADV_NOREUSE
		 * write(2).
		 */
		error = VOP_ADVISE(vp, orig_offset, uio->uio_offset - 1,
		    POSIX_FADV_DONTNEED);
unlock:
	return (error);
}

/*
 * The vn_io_fault() is a wrapper around vn_read() and vn_write() to
 * prevent the following deadlock:
 *
 * Assume that the thread A reads from the vnode vp1 into userspace
 * buffer buf1 backed by the pages of vnode vp2.  If a page in buf1 is
 * currently not resident, then system ends up with the call chain
 *   vn_read() -> VOP_READ(vp1) -> uiomove() -> [Page Fault] ->
 *     vm_fault(buf1) -> vnode_pager_getpages(vp2) -> VOP_GETPAGES(vp2)
 * which establishes lock order vp1->vn_lock, then vp2->vn_lock.
 * If, at the same time, thread B reads from vnode vp2 into buffer buf2
 * backed by the pages of vnode vp1, and some page in buf2 is not
 * resident, we get a reversed order vp2->vn_lock, then vp1->vn_lock.
 *
 * To prevent the lock order reversal and deadlock, vn_io_fault() does
 * not allow page faults to happen during VOP_READ() or VOP_WRITE().
 * Instead, it first tries to do the whole range i/o with pagefaults
 * disabled. If all pages in the i/o buffer are resident and mapped,
 * VOP will succeed (ignoring the genuine filesystem errors).
 * Otherwise, we get back EFAULT, and vn_io_fault() falls back to do
 * i/o in chunks, with all pages in the chunk prefaulted and held
 * using vm_fault_quick_hold_pages().
 *
 * Filesystems using this deadlock avoidance scheme should use the
 * array of the held pages from uio, saved in the curthread->td_ma,
 * instead of doing uiomove().  A helper function
 * vn_io_fault_uiomove() converts uiomove request into
 * uiomove_fromphys() over td_ma array.
 *
 * Since vnode locks do not cover the whole i/o anymore, rangelocks
 * make the current i/o request atomic with respect to other i/os and
 * truncations.
 */

/*
 * Decode vn_io_fault_args and perform the corresponding i/o.
 */
static int
vn_io_fault_doio(struct vn_io_fault_args *args, struct uio *uio,
    struct thread *td)
{
	int error, save;

	error = 0;
	save = vm_fault_disable_pagefaults();
	switch (args->kind) {
	case VN_IO_FAULT_FOP:
		error = (args->args.fop_args.doio)(args->args.fop_args.fp,
		    uio, args->cred, args->flags, td);
		break;
	case VN_IO_FAULT_VOP:
		if (uio->uio_rw == UIO_READ) {
			error = VOP_READ(args->args.vop_args.vp, uio,
			    args->flags, args->cred);
		} else if (uio->uio_rw == UIO_WRITE) {
			error = VOP_WRITE(args->args.vop_args.vp, uio,
			    args->flags, args->cred);
		}
		break;
	default:
		panic("vn_io_fault_doio: unknown kind of io %d %d",
		    args->kind, uio->uio_rw);
	}
	vm_fault_enable_pagefaults(save);
	return (error);
}

static int
vn_io_fault_touch(char *base, const struct uio *uio)
{
	int r;

	r = fubyte(base);
	if (r == -1 || (uio->uio_rw == UIO_READ && subyte(base, r) == -1))
		return (EFAULT);
	return (0);
}

static int
vn_io_fault_prefault_user(const struct uio *uio)
{
	char *base;
	const struct iovec *iov;
	size_t len;
	ssize_t resid;
	int error, i;

	KASSERT(uio->uio_segflg == UIO_USERSPACE,
	    ("vn_io_fault_prefault userspace"));

	error = i = 0;
	iov = uio->uio_iov;
	resid = uio->uio_resid;
	base = iov->iov_base;
	len = iov->iov_len;
	while (resid > 0) {
		error = vn_io_fault_touch(base, uio);
		if (error != 0)
			break;
		if (len < PAGE_SIZE) {
			if (len != 0) {
				error = vn_io_fault_touch(base + len - 1, uio);
				if (error != 0)
					break;
				resid -= len;
			}
			if (++i >= uio->uio_iovcnt)
				break;
			iov = uio->uio_iov + i;
			base = iov->iov_base;
			len = iov->iov_len;
		} else {
			len -= PAGE_SIZE;
			base += PAGE_SIZE;
			resid -= PAGE_SIZE;
		}
	}
	return (error);
}

/*
 * Common code for vn_io_fault(), agnostic to the kind of i/o request.
 * Uses vn_io_fault_doio() to make the call to an actual i/o function.
 * Used from vn_rdwr() and vn_io_fault(), which encode the i/o request
 * into args and call vn_io_fault1() to handle faults during the user
 * mode buffer accesses.
 */
static int
vn_io_fault1(struct vnode *vp, struct uio *uio, struct vn_io_fault_args *args,
    struct thread *td)
{
	vm_page_t ma[io_hold_cnt + 2];
	struct uio *uio_clone, short_uio;
	struct iovec short_iovec[1];
	vm_page_t *prev_td_ma;
	vm_prot_t prot;
	vm_offset_t addr, end;
	size_t len, resid;
	ssize_t adv;
	int error, cnt, saveheld, prev_td_ma_cnt;

	if (vn_io_fault_prefault) {
		error = vn_io_fault_prefault_user(uio);
		if (error != 0)
			return (error); /* Or ignore ? */
	}

	prot = uio->uio_rw == UIO_READ ? VM_PROT_WRITE : VM_PROT_READ;

	/*
	 * The UFS follows IO_UNIT directive and replays back both
	 * uio_offset and uio_resid if an error is encountered during the
	 * operation.  But, since the iovec may be already advanced,
	 * uio is still in an inconsistent state.
	 *
	 * Cache a copy of the original uio, which is advanced to the redo
	 * point using UIO_NOCOPY below.
	 */
	uio_clone = cloneuio(uio);
	resid = uio->uio_resid;

	short_uio.uio_segflg = UIO_USERSPACE;
	short_uio.uio_rw = uio->uio_rw;
	short_uio.uio_td = uio->uio_td;

	error = vn_io_fault_doio(args, uio, td);
	if (error != EFAULT)
		goto out;

	atomic_add_long(&vn_io_faults_cnt, 1);
	uio_clone->uio_segflg = UIO_NOCOPY;
	uiomove(NULL, resid - uio->uio_resid, uio_clone);
	uio_clone->uio_segflg = uio->uio_segflg;

	saveheld = curthread_pflags_set(TDP_UIOHELD);
	prev_td_ma = td->td_ma;
	prev_td_ma_cnt = td->td_ma_cnt;

	while (uio_clone->uio_resid != 0) {
		len = uio_clone->uio_iov->iov_len;
		if (len == 0) {
			KASSERT(uio_clone->uio_iovcnt >= 1,
			    ("iovcnt underflow"));
			uio_clone->uio_iov++;
			uio_clone->uio_iovcnt--;
			continue;
		}
		if (len > io_hold_cnt * PAGE_SIZE)
			len = io_hold_cnt * PAGE_SIZE;
		addr = (uintptr_t)uio_clone->uio_iov->iov_base;
		end = round_page(addr + len);
		if (end < addr) {
			error = EFAULT;
			break;
		}
		cnt = atop(end - trunc_page(addr));
		/*
		 * A perfectly misaligned address and length could cause
		 * both the start and the end of the chunk to use partial
		 * page.  +2 accounts for such a situation.
		 */
		cnt = vm_fault_quick_hold_pages(&td->td_proc->p_vmspace->vm_map,
		    addr, len, prot, ma, io_hold_cnt + 2);
		if (cnt == -1) {
			error = EFAULT;
			break;
		}
		short_uio.uio_iov = &short_iovec[0];
		short_iovec[0].iov_base = (void *)addr;
		short_uio.uio_iovcnt = 1;
		short_uio.uio_resid = short_iovec[0].iov_len = len;
		short_uio.uio_offset = uio_clone->uio_offset;
		td->td_ma = ma;
		td->td_ma_cnt = cnt;

		error = vn_io_fault_doio(args, &short_uio, td);
		vm_page_unhold_pages(ma, cnt);
		adv = len - short_uio.uio_resid;

		uio_clone->uio_iov->iov_base =
		    (char *)uio_clone->uio_iov->iov_base + adv;
		uio_clone->uio_iov->iov_len -= adv;
		uio_clone->uio_resid -= adv;
		uio_clone->uio_offset += adv;

		uio->uio_resid -= adv;
		uio->uio_offset += adv;

		if (error != 0 || adv == 0)
			break;
	}
	td->td_ma = prev_td_ma;
	td->td_ma_cnt = prev_td_ma_cnt;
	curthread_pflags_restore(saveheld);
out:
	free(uio_clone, M_IOV);
	return (error);
}

static int
vn_io_fault(struct file *fp, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{
	fo_rdwr_t *doio;
	struct vnode *vp;
	void *rl_cookie;
	struct vn_io_fault_args args;
	int error;

	doio = uio->uio_rw == UIO_READ ? vn_read : vn_write;
	vp = fp->f_vnode;
	foffset_lock_uio(fp, uio, flags);
	if (do_vn_io_fault(vp, uio)) {
		args.kind = VN_IO_FAULT_FOP;
		args.args.fop_args.fp = fp;
		args.args.fop_args.doio = doio;
		args.cred = active_cred;
		args.flags = flags | FOF_OFFSET;
		if (uio->uio_rw == UIO_READ) {
			rl_cookie = vn_rangelock_rlock(vp, uio->uio_offset,
			    uio->uio_offset + uio->uio_resid);
		} else if ((fp->f_flag & O_APPEND) != 0 ||
		    (flags & FOF_OFFSET) == 0) {
			/* For appenders, punt and lock the whole range. */
			rl_cookie = vn_rangelock_wlock(vp, 0, OFF_MAX);
		} else {
			rl_cookie = vn_rangelock_wlock(vp, uio->uio_offset,
			    uio->uio_offset + uio->uio_resid);
		}
		error = vn_io_fault1(vp, uio, &args, td);
		vn_rangelock_unlock(vp, rl_cookie);
	} else {
		error = doio(fp, uio, active_cred, flags | FOF_OFFSET, td);
	}
	foffset_unlock_uio(fp, uio, flags);
	return (error);
}

/*
 * Helper function to perform the requested uiomove operation using
 * the held pages for io->uio_iov[0].iov_base buffer instead of
 * copyin/copyout.  Access to the pages with uiomove_fromphys()
 * instead of iov_base prevents page faults that could occur due to
 * pmap_collect() invalidating the mapping created by
 * vm_fault_quick_hold_pages(), or pageout daemon, page laundry or
 * object cleanup revoking the write access from page mappings.
 *
 * Filesystems specified MNTK_NO_IOPF shall use vn_io_fault_uiomove()
 * instead of plain uiomove().
 */
int
vn_io_fault_uiomove(char *data, int xfersize, struct uio *uio)
{
	struct uio transp_uio;
	struct iovec transp_iov[1];
	struct thread *td;
	size_t adv;
	int error, pgadv;

	td = curthread;
	if ((td->td_pflags & TDP_UIOHELD) == 0 ||
	    uio->uio_segflg != UIO_USERSPACE)
		return (uiomove(data, xfersize, uio));

	KASSERT(uio->uio_iovcnt == 1, ("uio_iovcnt %d", uio->uio_iovcnt));
	transp_iov[0].iov_base = data;
	transp_uio.uio_iov = &transp_iov[0];
	transp_uio.uio_iovcnt = 1;
	if (xfersize > uio->uio_resid)
		xfersize = uio->uio_resid;
	transp_uio.uio_resid = transp_iov[0].iov_len = xfersize;
	transp_uio.uio_offset = 0;
	transp_uio.uio_segflg = UIO_SYSSPACE;
	/*
	 * Since transp_iov points to data, and td_ma page array
	 * corresponds to original uio->uio_iov, we need to invert the
	 * direction of the i/o operation as passed to
	 * uiomove_fromphys().
	 */
	switch (uio->uio_rw) {
	case UIO_WRITE:
		transp_uio.uio_rw = UIO_READ;
		break;
	case UIO_READ:
		transp_uio.uio_rw = UIO_WRITE;
		break;
	}
	transp_uio.uio_td = uio->uio_td;
	error = uiomove_fromphys(td->td_ma,
	    ((vm_offset_t)uio->uio_iov->iov_base) & PAGE_MASK,
	    xfersize, &transp_uio);
	adv = xfersize - transp_uio.uio_resid;
	pgadv =
	    (((vm_offset_t)uio->uio_iov->iov_base + adv) >> PAGE_SHIFT) -
	    (((vm_offset_t)uio->uio_iov->iov_base) >> PAGE_SHIFT);
	td->td_ma += pgadv;
	KASSERT(td->td_ma_cnt >= pgadv, ("consumed pages %d %d", td->td_ma_cnt,
	    pgadv));
	td->td_ma_cnt -= pgadv;
	uio->uio_iov->iov_base = (char *)uio->uio_iov->iov_base + adv;
	uio->uio_iov->iov_len -= adv;
	uio->uio_resid -= adv;
	uio->uio_offset += adv;
	return (error);
}

int
vn_io_fault_pgmove(vm_page_t ma[], vm_offset_t offset, int xfersize,
    struct uio *uio)
{
	struct thread *td;
	vm_offset_t iov_base;
	int cnt, pgadv;

	td = curthread;
	if ((td->td_pflags & TDP_UIOHELD) == 0 ||
	    uio->uio_segflg != UIO_USERSPACE)
		return (uiomove_fromphys(ma, offset, xfersize, uio));

	KASSERT(uio->uio_iovcnt == 1, ("uio_iovcnt %d", uio->uio_iovcnt));
	cnt = xfersize > uio->uio_resid ? uio->uio_resid : xfersize;
	iov_base = (vm_offset_t)uio->uio_iov->iov_base;
	switch (uio->uio_rw) {
	case UIO_WRITE:
		pmap_copy_pages(td->td_ma, iov_base & PAGE_MASK, ma,
		    offset, cnt);
		break;
	case UIO_READ:
		pmap_copy_pages(ma, offset, td->td_ma, iov_base & PAGE_MASK,
		    cnt);
		break;
	}
	pgadv = ((iov_base + cnt) >> PAGE_SHIFT) - (iov_base >> PAGE_SHIFT);
	td->td_ma += pgadv;
	KASSERT(td->td_ma_cnt >= pgadv, ("consumed pages %d %d", td->td_ma_cnt,
	    pgadv));
	td->td_ma_cnt -= pgadv;
	uio->uio_iov->iov_base = (char *)(iov_base + cnt);
	uio->uio_iov->iov_len -= cnt;
	uio->uio_resid -= cnt;
	uio->uio_offset += cnt;
	return (0);
}


/*
 * File table truncate routine.
 */
static int
vn_truncate(struct file *fp, off_t length, struct ucred *active_cred,
    struct thread *td)
{
	struct vattr vattr;
	struct mount *mp;
	struct vnode *vp;
	void *rl_cookie;
	int error;

	vp = fp->f_vnode;

	/*
	 * Lock the whole range for truncation.  Otherwise split i/o
	 * might happen partly before and partly after the truncation.
	 */
	rl_cookie = vn_rangelock_wlock(vp, 0, OFF_MAX);
	error = vn_start_write(vp, &mp, V_WAIT | PCATCH);
	if (error)
		goto out1;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
	if (vp->v_type == VDIR) {
		error = EISDIR;
		goto out;
	}
#ifdef MAC
	error = mac_vnode_check_write(active_cred, fp->f_cred, vp);
	if (error)
		goto out;
#endif
	error = vn_writechk(vp);
	if (error == 0) {
		VATTR_NULL(&vattr);
		vattr.va_size = length;
		if ((fp->f_flag & O_FSYNC) != 0)
			vattr.va_vaflags |= VA_SYNC;
		error = VOP_SETATTR(vp, &vattr, fp->f_cred);
	}
out:
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
out1:
	vn_rangelock_unlock(vp, rl_cookie);
	return (error);
}

/*
 * File table vnode stat routine.
 */
static int
vn_statfile(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{
	struct vnode *vp = fp->f_vnode;
	int error;

	vn_lock(vp, LK_SHARED | LK_RETRY);
	error = vn_stat(vp, sb, active_cred, fp->f_cred, td);
	VOP_UNLOCK(vp, 0);

	return (error);
}

/*
 * Stat a vnode; implementation for the stat syscall
 */
int
vn_stat(struct vnode *vp, struct stat *sb, struct ucred *active_cred,
    struct ucred *file_cred, struct thread *td)
{
	struct vattr vattr;
	struct vattr *vap;
	int error;
	u_short mode;

	AUDIT_ARG_VNODE1(vp);
#ifdef MAC
	error = mac_vnode_check_stat(active_cred, file_cred, vp);
	if (error)
		return (error);
#endif

	vap = &vattr;

	/*
	 * Initialize defaults for new and unusual fields, so that file
	 * systems which don't support these fields don't need to know
	 * about them.
	 */
	vap->va_birthtime.tv_sec = -1;
	vap->va_birthtime.tv_nsec = 0;
	vap->va_fsid = VNOVAL;
	vap->va_rdev = NODEV;

	error = VOP_GETATTR(vp, vap, active_cred);
	if (error)
		return (error);

	/*
	 * Zero the spare stat fields
	 */
	bzero(sb, sizeof *sb);

	/*
	 * Copy from vattr table
	 */
	if (vap->va_fsid != VNOVAL)
		sb->st_dev = vap->va_fsid;
	else
		sb->st_dev = vp->v_mount->mnt_stat.f_fsid.val[0];
	sb->st_ino = vap->va_fileid;
	mode = vap->va_mode;
	switch (vap->va_type) {
	case VREG:
		mode |= S_IFREG;
		break;
	case VDIR:
		mode |= S_IFDIR;
		break;
	case VBLK:
		mode |= S_IFBLK;
		break;
	case VCHR:
		mode |= S_IFCHR;
		break;
	case VLNK:
		mode |= S_IFLNK;
		break;
	case VSOCK:
		mode |= S_IFSOCK;
		break;
	case VFIFO:
		mode |= S_IFIFO;
		break;
	default:
		return (EBADF);
	}
	sb->st_mode = mode;
	sb->st_nlink = vap->va_nlink;
	sb->st_uid = vap->va_uid;
	sb->st_gid = vap->va_gid;
	sb->st_rdev = vap->va_rdev;
	if (vap->va_size > OFF_MAX)
		return (EOVERFLOW);
	sb->st_size = vap->va_size;
	sb->st_atim = vap->va_atime;
	sb->st_mtim = vap->va_mtime;
	sb->st_ctim = vap->va_ctime;
	sb->st_birthtim = vap->va_birthtime;

        /*
	 * According to www.opengroup.org, the meaning of st_blksize is 
	 *   "a filesystem-specific preferred I/O block size for this 
	 *    object.  In some filesystem types, this may vary from file
	 *    to file"
	 * Use miminum/default of PAGE_SIZE (e.g. for VCHR).
	 */

	sb->st_blksize = max(PAGE_SIZE, vap->va_blocksize);
	
	sb->st_flags = vap->va_flags;
	if (priv_check(td, PRIV_VFS_GENERATION))
		sb->st_gen = 0;
	else
		sb->st_gen = vap->va_gen;

	sb->st_blocks = vap->va_bytes / S_BLKSIZE;
	return (0);
}

/*
 * File table vnode ioctl routine.
 */
static int
vn_ioctl(struct file *fp, u_long com, void *data, struct ucred *active_cred,
    struct thread *td)
{
	struct vattr vattr;
	struct vnode *vp;
	int error;

	vp = fp->f_vnode;
	switch (vp->v_type) {
	case VDIR:
	case VREG:
		switch (com) {
		case FIONREAD:
			vn_lock(vp, LK_SHARED | LK_RETRY);
			error = VOP_GETATTR(vp, &vattr, active_cred);
			VOP_UNLOCK(vp, 0);
			if (error == 0)
				*(int *)data = vattr.va_size - fp->f_offset;
			return (error);
		case FIONBIO:
		case FIOASYNC:
			return (0);
		default:
			return (VOP_IOCTL(vp, com, data, fp->f_flag,
			    active_cred, td));
		}
		break;
	case VCHR:
		return (VOP_IOCTL(vp, com, data, fp->f_flag,
		    active_cred, td));
	default:
		return (ENOTTY);
	}
}

/*
 * File table vnode poll routine.
 */
static int
vn_poll(struct file *fp, int events, struct ucred *active_cred,
    struct thread *td)
{
	struct vnode *vp;
	int error;

	vp = fp->f_vnode;
#ifdef MAC
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
	error = mac_vnode_check_poll(active_cred, fp->f_cred, vp);
	VOP_UNLOCK(vp, 0);
	if (!error)
#endif

	error = VOP_POLL(vp, events, fp->f_cred, td);
	return (error);
}

/*
 * Acquire the requested lock and then check for validity.  LK_RETRY
 * permits vn_lock to return doomed vnodes.
 */
int
_vn_lock(struct vnode *vp, int flags, char *file, int line)
{
	int error;

	VNASSERT((flags & LK_TYPE_MASK) != 0, vp,
	    ("vn_lock: no locktype"));
	VNASSERT(vp->v_holdcnt != 0, vp, ("vn_lock: zero hold count"));
retry:
	error = VOP_LOCK1(vp, flags, file, line);
	flags &= ~LK_INTERLOCK;	/* Interlock is always dropped. */
	KASSERT((flags & LK_RETRY) == 0 || error == 0,
	    ("vn_lock: error %d incompatible with flags %#x", error, flags));

	if ((flags & LK_RETRY) == 0) {
		if (error == 0 && (vp->v_iflag & VI_DOOMED) != 0) {
			VOP_UNLOCK(vp, 0);
			error = ENOENT;
		}
	} else if (error != 0)
		goto retry;
	return (error);
}

/*
 * File table vnode close routine.
 */
static int
vn_closefile(struct file *fp, struct thread *td)
{
	struct vnode *vp;
	struct flock lf;
	int error;
	bool ref;

	vp = fp->f_vnode;
	fp->f_ops = &badfileops;
	ref= (fp->f_flag & FHASLOCK) != 0 && fp->f_type == DTYPE_VNODE;

	error = vn_close1(vp, fp->f_flag, fp->f_cred, td, ref);

	if (__predict_false(ref)) {
		lf.l_whence = SEEK_SET;
		lf.l_start = 0;
		lf.l_len = 0;
		lf.l_type = F_UNLCK;
		(void) VOP_ADVLOCK(vp, fp, F_UNLCK, &lf, F_FLOCK);
		vrele(vp);
	}
	return (error);
}

static bool
vn_suspendable(struct mount *mp)
{

	return (mp->mnt_op->vfs_susp_clean != NULL);
}

/*
 * Preparing to start a filesystem write operation. If the operation is
 * permitted, then we bump the count of operations in progress and
 * proceed. If a suspend request is in progress, we wait until the
 * suspension is over, and then proceed.
 */
static int
vn_start_write_locked(struct mount *mp, int flags)
{
	int error, mflags;

	mtx_assert(MNT_MTX(mp), MA_OWNED);
	error = 0;

	/*
	 * Check on status of suspension.
	 */
	if ((curthread->td_pflags & TDP_IGNSUSP) == 0 ||
	    mp->mnt_susp_owner != curthread) {
		mflags = ((mp->mnt_vfc->vfc_flags & VFCF_SBDRY) != 0 ?
		    (flags & PCATCH) : 0) | (PUSER - 1);
		while ((mp->mnt_kern_flag & MNTK_SUSPEND) != 0) {
			if (flags & V_NOWAIT) {
				error = EWOULDBLOCK;
				goto unlock;
			}
			error = msleep(&mp->mnt_flag, MNT_MTX(mp), mflags,
			    "suspfs", 0);
			if (error)
				goto unlock;
		}
	}
	if (flags & V_XSLEEP)
		goto unlock;
	mp->mnt_writeopcount++;
unlock:
	if (error != 0 || (flags & V_XSLEEP) != 0)
		MNT_REL(mp);
	MNT_IUNLOCK(mp);
	return (error);
}

int
vn_start_write(struct vnode *vp, struct mount **mpp, int flags)
{
	struct mount *mp;
	int error;

	KASSERT((flags & V_MNTREF) == 0 || (*mpp != NULL && vp == NULL),
	    ("V_MNTREF requires mp"));

	error = 0;
	/*
	 * If a vnode is provided, get and return the mount point that
	 * to which it will write.
	 */
	if (vp != NULL) {
		if ((error = VOP_GETWRITEMOUNT(vp, mpp)) != 0) {
			*mpp = NULL;
			if (error != EOPNOTSUPP)
				return (error);
			return (0);
		}
	}
	if ((mp = *mpp) == NULL)
		return (0);

	if (!vn_suspendable(mp)) {
		if (vp != NULL || (flags & V_MNTREF) != 0)
			vfs_rel(mp);
		return (0);
	}

	/*
	 * VOP_GETWRITEMOUNT() returns with the mp refcount held through
	 * a vfs_ref().
	 * As long as a vnode is not provided we need to acquire a
	 * refcount for the provided mountpoint too, in order to
	 * emulate a vfs_ref().
	 */
	MNT_ILOCK(mp);
	if (vp == NULL && (flags & V_MNTREF) == 0)
		MNT_REF(mp);

	return (vn_start_write_locked(mp, flags));
}

/*
 * Secondary suspension. Used by operations such as vop_inactive
 * routines that are needed by the higher level functions. These
 * are allowed to proceed until all the higher level functions have
 * completed (indicated by mnt_writeopcount dropping to zero). At that
 * time, these operations are halted until the suspension is over.
 */
int
vn_start_secondary_write(struct vnode *vp, struct mount **mpp, int flags)
{
	struct mount *mp;
	int error;

	KASSERT((flags & V_MNTREF) == 0 || (*mpp != NULL && vp == NULL),
	    ("V_MNTREF requires mp"));

 retry:
	if (vp != NULL) {
		if ((error = VOP_GETWRITEMOUNT(vp, mpp)) != 0) {
			*mpp = NULL;
			if (error != EOPNOTSUPP)
				return (error);
			return (0);
		}
	}
	/*
	 * If we are not suspended or have not yet reached suspended
	 * mode, then let the operation proceed.
	 */
	if ((mp = *mpp) == NULL)
		return (0);

	if (!vn_suspendable(mp)) {
		if (vp != NULL || (flags & V_MNTREF) != 0)
			vfs_rel(mp);
		return (0);
	}

	/*
	 * VOP_GETWRITEMOUNT() returns with the mp refcount held through
	 * a vfs_ref().
	 * As long as a vnode is not provided we need to acquire a
	 * refcount for the provided mountpoint too, in order to
	 * emulate a vfs_ref().
	 */
	MNT_ILOCK(mp);
	if (vp == NULL && (flags & V_MNTREF) == 0)
		MNT_REF(mp);
	if ((mp->mnt_kern_flag & (MNTK_SUSPENDED | MNTK_SUSPEND2)) == 0) {
		mp->mnt_secondary_writes++;
		mp->mnt_secondary_accwrites++;
		MNT_IUNLOCK(mp);
		return (0);
	}
	if (flags & V_NOWAIT) {
		MNT_REL(mp);
		MNT_IUNLOCK(mp);
		return (EWOULDBLOCK);
	}
	/*
	 * Wait for the suspension to finish.
	 */
	error = msleep(&mp->mnt_flag, MNT_MTX(mp), (PUSER - 1) | PDROP |
	    ((mp->mnt_vfc->vfc_flags & VFCF_SBDRY) != 0 ? (flags & PCATCH) : 0),
	    "suspfs", 0);
	vfs_rel(mp);
	if (error == 0)
		goto retry;
	return (error);
}

/*
 * Filesystem write operation has completed. If we are suspending and this
 * operation is the last one, notify the suspender that the suspension is
 * now in effect.
 */
void
vn_finished_write(struct mount *mp)
{
	if (mp == NULL || !vn_suspendable(mp))
		return;
	MNT_ILOCK(mp);
	MNT_REL(mp);
	mp->mnt_writeopcount--;
	if (mp->mnt_writeopcount < 0)
		panic("vn_finished_write: neg cnt");
	if ((mp->mnt_kern_flag & MNTK_SUSPEND) != 0 &&
	    mp->mnt_writeopcount <= 0)
		wakeup(&mp->mnt_writeopcount);
	MNT_IUNLOCK(mp);
}


/*
 * Filesystem secondary write operation has completed. If we are
 * suspending and this operation is the last one, notify the suspender
 * that the suspension is now in effect.
 */
void
vn_finished_secondary_write(struct mount *mp)
{
	if (mp == NULL || !vn_suspendable(mp))
		return;
	MNT_ILOCK(mp);
	MNT_REL(mp);
	mp->mnt_secondary_writes--;
	if (mp->mnt_secondary_writes < 0)
		panic("vn_finished_secondary_write: neg cnt");
	if ((mp->mnt_kern_flag & MNTK_SUSPEND) != 0 &&
	    mp->mnt_secondary_writes <= 0)
		wakeup(&mp->mnt_secondary_writes);
	MNT_IUNLOCK(mp);
}



/*
 * Request a filesystem to suspend write operations.
 */
int
vfs_write_suspend(struct mount *mp, int flags)
{
	int error;

	MPASS(vn_suspendable(mp));

	MNT_ILOCK(mp);
	if (mp->mnt_susp_owner == curthread) {
		MNT_IUNLOCK(mp);
		return (EALREADY);
	}
	while (mp->mnt_kern_flag & MNTK_SUSPEND)
		msleep(&mp->mnt_flag, MNT_MTX(mp), PUSER - 1, "wsuspfs", 0);

	/*
	 * Unmount holds a write reference on the mount point.  If we
	 * own busy reference and drain for writers, we deadlock with
	 * the reference draining in the unmount path.  Callers of
	 * vfs_write_suspend() must specify VS_SKIP_UNMOUNT if
	 * vfs_busy() reference is owned and caller is not in the
	 * unmount context.
	 */
	if ((flags & VS_SKIP_UNMOUNT) != 0 &&
	    (mp->mnt_kern_flag & MNTK_UNMOUNT) != 0) {
		MNT_IUNLOCK(mp);
		return (EBUSY);
	}

	mp->mnt_kern_flag |= MNTK_SUSPEND;
	mp->mnt_susp_owner = curthread;
	if (mp->mnt_writeopcount > 0)
		(void) msleep(&mp->mnt_writeopcount, 
		    MNT_MTX(mp), (PUSER - 1)|PDROP, "suspwt", 0);
	else
		MNT_IUNLOCK(mp);
	if ((error = VFS_SYNC(mp, MNT_SUSPEND)) != 0)
		vfs_write_resume(mp, 0);
	return (error);
}

/*
 * Request a filesystem to resume write operations.
 */
void
vfs_write_resume(struct mount *mp, int flags)
{

	MPASS(vn_suspendable(mp));

	MNT_ILOCK(mp);
	if ((mp->mnt_kern_flag & MNTK_SUSPEND) != 0) {
		KASSERT(mp->mnt_susp_owner == curthread, ("mnt_susp_owner"));
		mp->mnt_kern_flag &= ~(MNTK_SUSPEND | MNTK_SUSPEND2 |
				       MNTK_SUSPENDED);
		mp->mnt_susp_owner = NULL;
		wakeup(&mp->mnt_writeopcount);
		wakeup(&mp->mnt_flag);
		curthread->td_pflags &= ~TDP_IGNSUSP;
		if ((flags & VR_START_WRITE) != 0) {
			MNT_REF(mp);
			mp->mnt_writeopcount++;
		}
		MNT_IUNLOCK(mp);
		if ((flags & VR_NO_SUSPCLR) == 0)
			VFS_SUSP_CLEAN(mp);
	} else if ((flags & VR_START_WRITE) != 0) {
		MNT_REF(mp);
		vn_start_write_locked(mp, 0);
	} else {
		MNT_IUNLOCK(mp);
	}
}

/*
 * Helper loop around vfs_write_suspend() for filesystem unmount VFS
 * methods.
 */
int
vfs_write_suspend_umnt(struct mount *mp)
{
	int error;

	MPASS(vn_suspendable(mp));
	KASSERT((curthread->td_pflags & TDP_IGNSUSP) == 0,
	    ("vfs_write_suspend_umnt: recursed"));

	/* dounmount() already called vn_start_write(). */
	for (;;) {
		vn_finished_write(mp);
		error = vfs_write_suspend(mp, 0);
		if (error != 0) {
			vn_start_write(NULL, &mp, V_WAIT);
			return (error);
		}
		MNT_ILOCK(mp);
		if ((mp->mnt_kern_flag & MNTK_SUSPENDED) != 0)
			break;
		MNT_IUNLOCK(mp);
		vn_start_write(NULL, &mp, V_WAIT);
	}
	mp->mnt_kern_flag &= ~(MNTK_SUSPENDED | MNTK_SUSPEND2);
	wakeup(&mp->mnt_flag);
	MNT_IUNLOCK(mp);
	curthread->td_pflags |= TDP_IGNSUSP;
	return (0);
}

/*
 * Implement kqueues for files by translating it to vnode operation.
 */
static int
vn_kqfilter(struct file *fp, struct knote *kn)
{

	return (VOP_KQFILTER(fp->f_vnode, kn));
}

/*
 * Simplified in-kernel wrapper calls for extended attribute access.
 * Both calls pass in a NULL credential, authorizing as "kernel" access.
 * Set IO_NODELOCKED in ioflg if the vnode is already locked.
 */
int
vn_extattr_get(struct vnode *vp, int ioflg, int attrnamespace,
    const char *attrname, int *buflen, char *buf, struct thread *td)
{
	struct uio	auio;
	struct iovec	iov;
	int	error;

	iov.iov_len = *buflen;
	iov.iov_base = buf;

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;
	auio.uio_resid = *buflen;

	if ((ioflg & IO_NODELOCKED) == 0)
		vn_lock(vp, LK_SHARED | LK_RETRY);

	ASSERT_VOP_LOCKED(vp, "IO_NODELOCKED with no vp lock held");

	/* authorize attribute retrieval as kernel */
	error = VOP_GETEXTATTR(vp, attrnamespace, attrname, &auio, NULL, NULL,
	    td);

	if ((ioflg & IO_NODELOCKED) == 0)
		VOP_UNLOCK(vp, 0);

	if (error == 0) {
		*buflen = *buflen - auio.uio_resid;
	}

	return (error);
}

/*
 * XXX failure mode if partially written?
 */
int
vn_extattr_set(struct vnode *vp, int ioflg, int attrnamespace,
    const char *attrname, int buflen, char *buf, struct thread *td)
{
	struct uio	auio;
	struct iovec	iov;
	struct mount	*mp;
	int	error;

	iov.iov_len = buflen;
	iov.iov_base = buf;

	auio.uio_iov = &iov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_WRITE;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;
	auio.uio_resid = buflen;

	if ((ioflg & IO_NODELOCKED) == 0) {
		if ((error = vn_start_write(vp, &mp, V_WAIT)) != 0)
			return (error);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	}

	ASSERT_VOP_LOCKED(vp, "IO_NODELOCKED with no vp lock held");

	/* authorize attribute setting as kernel */
	error = VOP_SETEXTATTR(vp, attrnamespace, attrname, &auio, NULL, td);

	if ((ioflg & IO_NODELOCKED) == 0) {
		vn_finished_write(mp);
		VOP_UNLOCK(vp, 0);
	}

	return (error);
}

int
vn_extattr_rm(struct vnode *vp, int ioflg, int attrnamespace,
    const char *attrname, struct thread *td)
{
	struct mount	*mp;
	int	error;

	if ((ioflg & IO_NODELOCKED) == 0) {
		if ((error = vn_start_write(vp, &mp, V_WAIT)) != 0)
			return (error);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	}

	ASSERT_VOP_LOCKED(vp, "IO_NODELOCKED with no vp lock held");

	/* authorize attribute removal as kernel */
	error = VOP_DELETEEXTATTR(vp, attrnamespace, attrname, NULL, td);
	if (error == EOPNOTSUPP)
		error = VOP_SETEXTATTR(vp, attrnamespace, attrname, NULL,
		    NULL, td);

	if ((ioflg & IO_NODELOCKED) == 0) {
		vn_finished_write(mp);
		VOP_UNLOCK(vp, 0);
	}

	return (error);
}

static int
vn_get_ino_alloc_vget(struct mount *mp, void *arg, int lkflags,
    struct vnode **rvp)
{

	return (VFS_VGET(mp, *(ino_t *)arg, lkflags, rvp));
}

int
vn_vget_ino(struct vnode *vp, ino_t ino, int lkflags, struct vnode **rvp)
{

	return (vn_vget_ino_gen(vp, vn_get_ino_alloc_vget, &ino,
	    lkflags, rvp));
}

int
vn_vget_ino_gen(struct vnode *vp, vn_get_ino_t alloc, void *alloc_arg,
    int lkflags, struct vnode **rvp)
{
	struct mount *mp;
	int ltype, error;

	ASSERT_VOP_LOCKED(vp, "vn_vget_ino_get");
	mp = vp->v_mount;
	ltype = VOP_ISLOCKED(vp);
	KASSERT(ltype == LK_EXCLUSIVE || ltype == LK_SHARED,
	    ("vn_vget_ino: vp not locked"));
	error = vfs_busy(mp, MBF_NOWAIT);
	if (error != 0) {
		vfs_ref(mp);
		VOP_UNLOCK(vp, 0);
		error = vfs_busy(mp, 0);
		vn_lock(vp, ltype | LK_RETRY);
		vfs_rel(mp);
		if (error != 0)
			return (ENOENT);
		if (vp->v_iflag & VI_DOOMED) {
			vfs_unbusy(mp);
			return (ENOENT);
		}
	}
	VOP_UNLOCK(vp, 0);
	error = alloc(mp, alloc_arg, lkflags, rvp);
	vfs_unbusy(mp);
	if (*rvp != vp)
		vn_lock(vp, ltype | LK_RETRY);
	if (vp->v_iflag & VI_DOOMED) {
		if (error == 0) {
			if (*rvp == vp)
				vunref(vp);
			else
				vput(*rvp);
		}
		error = ENOENT;
	}
	return (error);
}

int
vn_rlimit_fsize(const struct vnode *vp, const struct uio *uio,
    struct thread *td)
{

	if (vp->v_type != VREG || td == NULL)
		return (0);
	if ((uoff_t)uio->uio_offset + uio->uio_resid >
	    lim_cur(td, RLIMIT_FSIZE)) {
		PROC_LOCK(td->td_proc);
		kern_psignal(td->td_proc, SIGXFSZ);
		PROC_UNLOCK(td->td_proc);
		return (EFBIG);
	}
	return (0);
}

int
vn_chmod(struct file *fp, mode_t mode, struct ucred *active_cred,
    struct thread *td)
{
	struct vnode *vp;

	vp = fp->f_vnode;
#ifdef AUDIT
	vn_lock(vp, LK_SHARED | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
	VOP_UNLOCK(vp, 0);
#endif
	return (setfmode(td, active_cred, vp, mode));
}

int
vn_chown(struct file *fp, uid_t uid, gid_t gid, struct ucred *active_cred,
    struct thread *td)
{
	struct vnode *vp;

	vp = fp->f_vnode;
#ifdef AUDIT
	vn_lock(vp, LK_SHARED | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
	VOP_UNLOCK(vp, 0);
#endif
	return (setfown(td, active_cred, vp, uid, gid));
}

void
vn_pages_remove(struct vnode *vp, vm_pindex_t start, vm_pindex_t end)
{
	vm_object_t object;

	if ((object = vp->v_object) == NULL)
		return;
	VM_OBJECT_WLOCK(object);
	vm_object_page_remove(object, start, end, 0);
	VM_OBJECT_WUNLOCK(object);
}

int
vn_bmap_seekhole(struct vnode *vp, u_long cmd, off_t *off, struct ucred *cred)
{
	struct vattr va;
	daddr_t bn, bnp;
	uint64_t bsize;
	off_t noff;
	int error;

	KASSERT(cmd == FIOSEEKHOLE || cmd == FIOSEEKDATA,
	    ("Wrong command %lu", cmd));

	if (vn_lock(vp, LK_SHARED) != 0)
		return (EBADF);
	if (vp->v_type != VREG) {
		error = ENOTTY;
		goto unlock;
	}
	error = VOP_GETATTR(vp, &va, cred);
	if (error != 0)
		goto unlock;
	noff = *off;
	if (noff >= va.va_size) {
		error = ENXIO;
		goto unlock;
	}
	bsize = vp->v_mount->mnt_stat.f_iosize;
	for (bn = noff / bsize; noff < va.va_size; bn++, noff += bsize) {
		error = VOP_BMAP(vp, bn, NULL, &bnp, NULL, NULL);
		if (error == EOPNOTSUPP) {
			error = ENOTTY;
			goto unlock;
		}
		if ((bnp == -1 && cmd == FIOSEEKHOLE) ||
		    (bnp != -1 && cmd == FIOSEEKDATA)) {
			noff = bn * bsize;
			if (noff < *off)
				noff = *off;
			goto unlock;
		}
	}
	if (noff > va.va_size)
		noff = va.va_size;
	/* noff == va.va_size. There is an implicit hole at the end of file. */
	if (cmd == FIOSEEKDATA)
		error = ENXIO;
unlock:
	VOP_UNLOCK(vp, 0);
	if (error == 0)
		*off = noff;
	return (error);
}

int
vn_seek(struct file *fp, off_t offset, int whence, struct thread *td)
{
	struct ucred *cred;
	struct vnode *vp;
	struct vattr vattr;
	off_t foffset, size;
	int error, noneg;

	cred = td->td_ucred;
	vp = fp->f_vnode;
	foffset = foffset_lock(fp, 0);
	noneg = (vp->v_type != VCHR);
	error = 0;
	switch (whence) {
	case L_INCR:
		if (noneg &&
		    (foffset < 0 ||
		    (offset > 0 && foffset > OFF_MAX - offset))) {
			error = EOVERFLOW;
			break;
		}
		offset += foffset;
		break;
	case L_XTND:
		vn_lock(vp, LK_SHARED | LK_RETRY);
		error = VOP_GETATTR(vp, &vattr, cred);
		VOP_UNLOCK(vp, 0);
		if (error)
			break;

		/*
		 * If the file references a disk device, then fetch
		 * the media size and use that to determine the ending
		 * offset.
		 */
		if (vattr.va_size == 0 && vp->v_type == VCHR &&
		    fo_ioctl(fp, DIOCGMEDIASIZE, &size, cred, td) == 0)
			vattr.va_size = size;
		if (noneg &&
		    (vattr.va_size > OFF_MAX ||
		    (offset > 0 && vattr.va_size > OFF_MAX - offset))) {
			error = EOVERFLOW;
			break;
		}
		offset += vattr.va_size;
		break;
	case L_SET:
		break;
	case SEEK_DATA:
		error = fo_ioctl(fp, FIOSEEKDATA, &offset, cred, td);
		break;
	case SEEK_HOLE:
		error = fo_ioctl(fp, FIOSEEKHOLE, &offset, cred, td);
		break;
	default:
		error = EINVAL;
	}
	if (error == 0 && noneg && offset < 0)
		error = EINVAL;
	if (error != 0)
		goto drop;
	VFS_KNOTE_UNLOCKED(vp, 0);
	td->td_uretoff.tdu_off = offset;
drop:
	foffset_unlock(fp, offset, error != 0 ? FOF_NOUPDATE : 0);
	return (error);
}

int
vn_utimes_perm(struct vnode *vp, struct vattr *vap, struct ucred *cred,
    struct thread *td)
{
	int error;

	/*
	 * Grant permission if the caller is the owner of the file, or
	 * the super-user, or has ACL_WRITE_ATTRIBUTES permission on
	 * on the file.  If the time pointer is null, then write
	 * permission on the file is also sufficient.
	 *
	 * From NFSv4.1, draft 21, 6.2.1.3.1, Discussion of Mask Attributes:
	 * A user having ACL_WRITE_DATA or ACL_WRITE_ATTRIBUTES
	 * will be allowed to set the times [..] to the current
	 * server time.
	 */
	error = VOP_ACCESSX(vp, VWRITE_ATTRIBUTES, cred, td);
	if (error != 0 && (vap->va_vaflags & VA_UTIMES_NULL) != 0)
		error = VOP_ACCESS(vp, VWRITE, cred, td);
	return (error);
}

int
vn_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{
	struct vnode *vp;
	int error;

	if (fp->f_type == DTYPE_FIFO)
		kif->kf_type = KF_TYPE_FIFO;
	else
		kif->kf_type = KF_TYPE_VNODE;
	vp = fp->f_vnode;
	vref(vp);
	FILEDESC_SUNLOCK(fdp);
	error = vn_fill_kinfo_vnode(vp, kif);
	vrele(vp);
	FILEDESC_SLOCK(fdp);
	return (error);
}

static inline void
vn_fill_junk(struct kinfo_file *kif)
{
	size_t len, olen;

	/*
	 * Simulate vn_fullpath returning changing values for a given
	 * vp during e.g. coredump.
	 */
	len = (arc4random() % (sizeof(kif->kf_path) - 2)) + 1;
	olen = strlen(kif->kf_path);
	if (len < olen)
		strcpy(&kif->kf_path[len - 1], "$");
	else
		for (; olen < len; olen++)
			strcpy(&kif->kf_path[olen], "A");
}

int
vn_fill_kinfo_vnode(struct vnode *vp, struct kinfo_file *kif)
{
	struct vattr va;
	char *fullpath, *freepath;
	int error;

	kif->kf_un.kf_file.kf_file_type = vntype_to_kinfo(vp->v_type);
	freepath = NULL;
	fullpath = "-";
	error = vn_fullpath(curthread, vp, &fullpath, &freepath);
	if (error == 0) {
		strlcpy(kif->kf_path, fullpath, sizeof(kif->kf_path));
	}
	if (freepath != NULL)
		free(freepath, M_TEMP);

	KFAIL_POINT_CODE(DEBUG_FP, fill_kinfo_vnode__random_path,
		vn_fill_junk(kif);
	);

	/*
	 * Retrieve vnode attributes.
	 */
	va.va_fsid = VNOVAL;
	va.va_rdev = NODEV;
	vn_lock(vp, LK_SHARED | LK_RETRY);
	error = VOP_GETATTR(vp, &va, curthread->td_ucred);
	VOP_UNLOCK(vp, 0);
	if (error != 0)
		return (error);
	if (va.va_fsid != VNOVAL)
		kif->kf_un.kf_file.kf_file_fsid = va.va_fsid;
	else
		kif->kf_un.kf_file.kf_file_fsid =
		    vp->v_mount->mnt_stat.f_fsid.val[0];
	kif->kf_un.kf_file.kf_file_fsid_freebsd11 =
	    kif->kf_un.kf_file.kf_file_fsid; /* truncate */
	kif->kf_un.kf_file.kf_file_fileid = va.va_fileid;
	kif->kf_un.kf_file.kf_file_mode = MAKEIMODE(va.va_type, va.va_mode);
	kif->kf_un.kf_file.kf_file_size = va.va_size;
	kif->kf_un.kf_file.kf_file_rdev = va.va_rdev;
	kif->kf_un.kf_file.kf_file_rdev_freebsd11 =
	    kif->kf_un.kf_file.kf_file_rdev; /* truncate */
	return (0);
}

int
vn_mmap(struct file *fp, vm_map_t map, vm_offset_t *addr, vm_size_t size,
    vm_prot_t prot, vm_prot_t cap_maxprot, int flags, vm_ooffset_t foff,
    struct thread *td)
{
#ifdef HWPMC_HOOKS
	struct pmckern_map_in pkm;
#endif
	struct mount *mp;
	struct vnode *vp;
	vm_object_t object;
	vm_prot_t maxprot;
	boolean_t writecounted;
	int error;

#if defined(COMPAT_FREEBSD7) || defined(COMPAT_FREEBSD6) || \
    defined(COMPAT_FREEBSD5) || defined(COMPAT_FREEBSD4)
	/*
	 * POSIX shared-memory objects are defined to have
	 * kernel persistence, and are not defined to support
	 * read(2)/write(2) -- or even open(2).  Thus, we can
	 * use MAP_ASYNC to trade on-disk coherence for speed.
	 * The shm_open(3) library routine turns on the FPOSIXSHM
	 * flag to request this behavior.
	 */
	if ((fp->f_flag & FPOSIXSHM) != 0)
		flags |= MAP_NOSYNC;
#endif
	vp = fp->f_vnode;

	/*
	 * Ensure that file and memory protections are
	 * compatible.  Note that we only worry about
	 * writability if mapping is shared; in this case,
	 * current and max prot are dictated by the open file.
	 * XXX use the vnode instead?  Problem is: what
	 * credentials do we use for determination? What if
	 * proc does a setuid?
	 */
	mp = vp->v_mount;
	if (mp != NULL && (mp->mnt_flag & MNT_NOEXEC) != 0) {
		maxprot = VM_PROT_NONE;
		if ((prot & VM_PROT_EXECUTE) != 0)
			return (EACCES);
	} else
		maxprot = VM_PROT_EXECUTE;
	if ((fp->f_flag & FREAD) != 0)
		maxprot |= VM_PROT_READ;
	else if ((prot & VM_PROT_READ) != 0)
		return (EACCES);

	/*
	 * If we are sharing potential changes via MAP_SHARED and we
	 * are trying to get write permission although we opened it
	 * without asking for it, bail out.
	 */
	if ((flags & MAP_SHARED) != 0) {
		if ((fp->f_flag & FWRITE) != 0)
			maxprot |= VM_PROT_WRITE;
		else if ((prot & VM_PROT_WRITE) != 0)
			return (EACCES);
	} else {
		maxprot |= VM_PROT_WRITE;
		cap_maxprot |= VM_PROT_WRITE;
	}
	maxprot &= cap_maxprot;

	/*
	 * For regular files and shared memory, POSIX requires that
	 * the value of foff be a legitimate offset within the data
	 * object.  In particular, negative offsets are invalid.
	 * Blocking negative offsets and overflows here avoids
	 * possible wraparound or user-level access into reserved
	 * ranges of the data object later.  In contrast, POSIX does
	 * not dictate how offsets are used by device drivers, so in
	 * the case of a device mapping a negative offset is passed
	 * on.
	 */
	if (
#ifdef _LP64
	    size > OFF_MAX ||
#endif
	    foff < 0 || foff > OFF_MAX - size)
		return (EINVAL);

	writecounted = FALSE;
	error = vm_mmap_vnode(td, size, prot, &maxprot, &flags, vp,
	    &foff, &object, &writecounted);
	if (error != 0)
		return (error);
	error = vm_mmap_object(map, addr, size, prot, maxprot, flags, object,
	    foff, writecounted, td);
	if (error != 0) {
		/*
		 * If this mapping was accounted for in the vnode's
		 * writecount, then undo that now.
		 */
		if (writecounted)
			vnode_pager_release_writecount(object, 0, size);
		vm_object_deallocate(object);
	}
#ifdef HWPMC_HOOKS
	/* Inform hwpmc(4) if an executable is being mapped. */
	if (PMC_HOOK_INSTALLED(PMC_FN_MMAP)) {
		if ((prot & VM_PROT_EXECUTE) != 0 && error == 0) {
			pkm.pm_file = vp;
			pkm.pm_address = (uintptr_t) *addr;
			PMC_CALL_HOOK_UNLOCKED(td, PMC_FN_MMAP, (void *) &pkm);
		}
	}
#endif
	return (error);
}

void
vn_fsid(struct vnode *vp, struct vattr *va)
{
	fsid_t *f;

	f = &vp->v_mount->mnt_stat.f_fsid;
	va->va_fsid = (uint32_t)f->val[1];
	va->va_fsid <<= sizeof(f->val[1]) * NBBY;
	va->va_fsid += (uint32_t)f->val[0];
}
