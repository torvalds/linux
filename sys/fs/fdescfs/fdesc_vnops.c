/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
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
 *	@(#)fdesc_vnops.c	8.9 (Berkeley) 1/21/94
 *
 * $FreeBSD$
 */

/*
 * /dev/fd Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/conf.h>
#include <sys/dirent.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>	/* boottime */
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/file.h>	/* Must come after sys/malloc.h */
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/syscallsubr.h>
#include <sys/unistd.h>
#include <sys/vnode.h>

#include <fs/fdescfs/fdesc.h>

#define	NFDCACHE 4
#define FD_NHASH(ix) \
	(&fdhashtbl[(ix) & fdhash])
static LIST_HEAD(fdhashhead, fdescnode) *fdhashtbl;
static u_long fdhash;

struct mtx fdesc_hashmtx;

static vop_getattr_t	fdesc_getattr;
static vop_lookup_t	fdesc_lookup;
static vop_open_t	fdesc_open;
static vop_pathconf_t	fdesc_pathconf;
static vop_readdir_t	fdesc_readdir;
static vop_readlink_t	fdesc_readlink;
static vop_reclaim_t	fdesc_reclaim;
static vop_setattr_t	fdesc_setattr;

static struct vop_vector fdesc_vnodeops = {
	.vop_default =		&default_vnodeops,

	.vop_access =		VOP_NULL,
	.vop_getattr =		fdesc_getattr,
	.vop_lookup =		fdesc_lookup,
	.vop_open =		fdesc_open,
	.vop_pathconf =		fdesc_pathconf,
	.vop_readdir =		fdesc_readdir,
	.vop_readlink =		fdesc_readlink,
	.vop_reclaim =		fdesc_reclaim,
	.vop_setattr =		fdesc_setattr,
};

static void fdesc_insmntque_dtr(struct vnode *, void *);
static void fdesc_remove_entry(struct fdescnode *);

/*
 * Initialise cache headers
 */
int
fdesc_init(struct vfsconf *vfsp)
{

	mtx_init(&fdesc_hashmtx, "fdescfs_hash", NULL, MTX_DEF);
	fdhashtbl = hashinit(NFDCACHE, M_CACHE, &fdhash);
	return (0);
}

/*
 * Uninit ready for unload.
 */
int
fdesc_uninit(struct vfsconf *vfsp)
{

	hashdestroy(fdhashtbl, M_CACHE, fdhash);
	mtx_destroy(&fdesc_hashmtx);
	return (0);
}

/*
 * If allocating vnode fails, call this.
 */
static void
fdesc_insmntque_dtr(struct vnode *vp, void *arg)
{

	vgone(vp);
	vput(vp);
}

/*
 * Remove an entry from the hash if it exists.
 */
static void
fdesc_remove_entry(struct fdescnode *fd)
{
	struct fdhashhead *fc;
	struct fdescnode *fd2;

	fc = FD_NHASH(fd->fd_ix);
	mtx_lock(&fdesc_hashmtx);
	LIST_FOREACH(fd2, fc, fd_hash) {
		if (fd == fd2) {
			LIST_REMOVE(fd, fd_hash);
			break;
		}
	}
	mtx_unlock(&fdesc_hashmtx);
}

int
fdesc_allocvp(fdntype ftype, unsigned fd_fd, int ix, struct mount *mp,
    struct vnode **vpp)
{
	struct fdescmount *fmp;
	struct fdhashhead *fc;
	struct fdescnode *fd, *fd2;
	struct vnode *vp, *vp2;
	struct thread *td;
	int error;

	td = curthread;
	fc = FD_NHASH(ix);
loop:
	mtx_lock(&fdesc_hashmtx);
	/*
	 * If a forced unmount is progressing, we need to drop it. The flags are
	 * protected by the hashmtx.
	 */
	fmp = mp->mnt_data;
	if (fmp == NULL || fmp->flags & FMNT_UNMOUNTF) {
		mtx_unlock(&fdesc_hashmtx);
		return (-1);
	}

	LIST_FOREACH(fd, fc, fd_hash) {
		if (fd->fd_ix == ix && fd->fd_vnode->v_mount == mp) {
			/* Get reference to vnode in case it's being free'd */
			vp = fd->fd_vnode;
			VI_LOCK(vp);
			mtx_unlock(&fdesc_hashmtx);
			if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, td))
				goto loop;
			*vpp = vp;
			return (0);
		}
	}
	mtx_unlock(&fdesc_hashmtx);

	fd = malloc(sizeof(struct fdescnode), M_TEMP, M_WAITOK);

	error = getnewvnode("fdescfs", mp, &fdesc_vnodeops, &vp);
	if (error) {
		free(fd, M_TEMP);
		return (error);
	}
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	vp->v_data = fd;
	fd->fd_vnode = vp;
	fd->fd_type = ftype;
	fd->fd_fd = fd_fd;
	fd->fd_ix = ix;
	if (ftype == Fdesc && fmp->flags & FMNT_LINRDLNKF)
		vp->v_vflag |= VV_READLINK;
	error = insmntque1(vp, mp, fdesc_insmntque_dtr, NULL);
	if (error != 0) {
		*vpp = NULLVP;
		return (error);
	}

	/* Make sure that someone didn't beat us when inserting the vnode. */
	mtx_lock(&fdesc_hashmtx);
	/*
	 * If a forced unmount is progressing, we need to drop it. The flags are
	 * protected by the hashmtx.
	 */
	fmp = mp->mnt_data;
	if (fmp == NULL || fmp->flags & FMNT_UNMOUNTF) {
		mtx_unlock(&fdesc_hashmtx);
		vgone(vp);
		vput(vp);
		*vpp = NULLVP;
		return (-1);
	}

	LIST_FOREACH(fd2, fc, fd_hash) {
		if (fd2->fd_ix == ix && fd2->fd_vnode->v_mount == mp) {
			/* Get reference to vnode in case it's being free'd */
			vp2 = fd2->fd_vnode;
			VI_LOCK(vp2);
			mtx_unlock(&fdesc_hashmtx);
			error = vget(vp2, LK_EXCLUSIVE | LK_INTERLOCK, td);
			/* Someone beat us, dec use count and wait for reclaim */
			vgone(vp);
			vput(vp);
			/* If we didn't get it, return no vnode. */
			if (error)
				vp2 = NULLVP;
			*vpp = vp2;
			return (error);
		}
	}

	/* If we came here, we can insert it safely. */
	LIST_INSERT_HEAD(fc, fd, fd_hash);
	mtx_unlock(&fdesc_hashmtx);
	*vpp = vp;
	return (0);
}

struct fdesc_get_ino_args {
	fdntype ftype;
	unsigned fd_fd;
	int ix;
	struct file *fp;
	struct thread *td;
};

static int
fdesc_get_ino_alloc(struct mount *mp, void *arg, int lkflags,
    struct vnode **rvp)
{
	struct fdesc_get_ino_args *a;
	int error;

	a = arg;
	error = fdesc_allocvp(a->ftype, a->fd_fd, a->ix, mp, rvp);
	fdrop(a->fp, a->td);
	return (error);
}


/*
 * vp is the current namei directory
 * ndp is the name to locate in that directory...
 */
static int
fdesc_lookup(struct vop_lookup_args *ap)
{
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	char *pname = cnp->cn_nameptr;
	struct thread *td = cnp->cn_thread;
	struct file *fp;
	struct fdesc_get_ino_args arg;
	int nlen = cnp->cn_namelen;
	u_int fd, fd1;
	int error;
	struct vnode *fvp;

	if ((cnp->cn_flags & ISLASTCN) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		error = EROFS;
		goto bad;
	}

	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		VREF(dvp);
		return (0);
	}

	if (VTOFDESC(dvp)->fd_type != Froot) {
		error = ENOTDIR;
		goto bad;
	}

	fd = 0;
	/* the only time a leading 0 is acceptable is if it's "0" */
	if (*pname == '0' && nlen != 1) {
		error = ENOENT;
		goto bad;
	}
	while (nlen--) {
		if (*pname < '0' || *pname > '9') {
			error = ENOENT;
			goto bad;
		}
		fd1 = 10 * fd + *pname++ - '0';
		if (fd1 < fd) {
			error = ENOENT;
			goto bad;
		}
		fd = fd1;
	}

	/*
	 * No rights to check since 'fp' isn't actually used.
	 */
	if ((error = fget(td, fd, &cap_no_rights, &fp)) != 0)
		goto bad;

	/* Check if we're looking up ourselves. */
	if (VTOFDESC(dvp)->fd_ix == FD_DESC + fd) {
		/*
		 * In case we're holding the last reference to the file, the dvp
		 * will be re-acquired.
		 */
		vhold(dvp);
		VOP_UNLOCK(dvp, 0);
		fdrop(fp, td);

		/* Re-aquire the lock afterwards. */
		vn_lock(dvp, LK_RETRY | LK_EXCLUSIVE);
		vdrop(dvp);
		fvp = dvp;
		if ((dvp->v_iflag & VI_DOOMED) != 0)
			error = ENOENT;
	} else {
		/*
		 * Unlock our root node (dvp) when doing this, since we might
		 * deadlock since the vnode might be locked by another thread
		 * and the root vnode lock will be obtained afterwards (in case
		 * we're looking up the fd of the root vnode), which will be the
		 * opposite lock order. Vhold the root vnode first so we don't
		 * lose it.
		 */
		arg.ftype = Fdesc;
		arg.fd_fd = fd;
		arg.ix = FD_DESC + fd;
		arg.fp = fp;
		arg.td = td;
		error = vn_vget_ino_gen(dvp, fdesc_get_ino_alloc, &arg,
		    LK_EXCLUSIVE, &fvp);
	}

	if (error)
		goto bad;
	*vpp = fvp;
	return (0);

bad:
	*vpp = NULL;
	return (error);
}

static int
fdesc_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;

	if (VTOFDESC(vp)->fd_type == Froot)
		return (0);

	/*
	 * XXX Kludge: set td->td_proc->p_dupfd to contain the value of the file
	 * descriptor being sought for duplication. The error return ensures
	 * that the vnode for this device will be released by vn_open. Open
	 * will detect this special error and take the actions in dupfdopen.
	 * Other callers of vn_open or VOP_OPEN will simply report the
	 * error.
	 */
	ap->a_td->td_dupfd = VTOFDESC(vp)->fd_fd;	/* XXX */
	return (ENODEV);
}

static int
fdesc_pathconf(struct vop_pathconf_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int error;

	switch (ap->a_name) {
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_LINK_MAX:
		if (VTOFDESC(vp)->fd_type == Froot)
			*ap->a_retval = 2;
		else
			*ap->a_retval = 1;
		return (0);
	default:
		if (VTOFDESC(vp)->fd_type == Froot)
			return (vop_stdpathconf(ap));
		vref(vp);
		VOP_UNLOCK(vp, 0);
		error = kern_fpathconf(curthread, VTOFDESC(vp)->fd_fd,
		    ap->a_name, ap->a_retval);
		vn_lock(vp, LK_SHARED | LK_RETRY);
		vunref(vp);
		return (error);
	}
}

static int
fdesc_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct timeval boottime;

	getboottime(&boottime);
	vap->va_mode = S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
	vap->va_fileid = VTOFDESC(vp)->fd_ix;
	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_blocksize = DEV_BSIZE;
	vap->va_atime.tv_sec = boottime.tv_sec;
	vap->va_atime.tv_nsec = 0;
	vap->va_mtime = vap->va_atime;
	vap->va_ctime = vap->va_mtime;
	vap->va_gen = 0;
	vap->va_flags = 0;
	vap->va_bytes = 0;
	vap->va_filerev = 0;

	switch (VTOFDESC(vp)->fd_type) {
	case Froot:
		vap->va_type = VDIR;
		vap->va_nlink = 2;
		vap->va_size = DEV_BSIZE;
		vap->va_rdev = NODEV;
		break;

	case Fdesc:
		vap->va_type = (vp->v_vflag & VV_READLINK) == 0 ? VCHR : VLNK;
		vap->va_nlink = 1;
		vap->va_size = 0;
		vap->va_rdev = makedev(0, vap->va_fileid);
		break;

	default:
		panic("fdesc_getattr");
		break;
	}

	vp->v_type = vap->va_type;
	return (0);
}

static int
fdesc_setattr(struct vop_setattr_args *ap)
{
	struct vattr *vap = ap->a_vap;
	struct vnode *vp;
	struct mount *mp;
	struct file *fp;
	struct thread *td = curthread;
	cap_rights_t rights;
	unsigned fd;
	int error;

	/*
	 * Can't mess with the root vnode
	 */
	if (VTOFDESC(ap->a_vp)->fd_type == Froot)
		return (EACCES);

	fd = VTOFDESC(ap->a_vp)->fd_fd;

	/*
	 * Allow setattr where there is an underlying vnode.
	 */
	error = getvnode(td, fd,
	    cap_rights_init(&rights, CAP_EXTATTR_SET), &fp);
	if (error) {
		/*
		 * getvnode() returns EINVAL if the file descriptor is not
		 * backed by a vnode.  Silently drop all changes except
		 * chflags(2) in this case.
		 */
		if (error == EINVAL) {
			if (vap->va_flags != VNOVAL)
				error = EOPNOTSUPP;
			else
				error = 0;
		}
		return (error);
	}
	vp = fp->f_vnode;
	if ((error = vn_start_write(vp, &mp, V_WAIT | PCATCH)) == 0) {
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		error = VOP_SETATTR(vp, ap->a_vap, ap->a_cred);
		VOP_UNLOCK(vp, 0);
		vn_finished_write(mp);
	}
	fdrop(fp, td);
	return (error);
}

#define UIO_MX _GENERIC_DIRLEN(10) /* number of symbols in INT_MAX printout */

static int
fdesc_readdir(struct vop_readdir_args *ap)
{
	struct fdescmount *fmp;
	struct uio *uio = ap->a_uio;
	struct filedesc *fdp;
	struct dirent d;
	struct dirent *dp = &d;
	int error, i, off, fcnt;

	if (VTOFDESC(ap->a_vp)->fd_type != Froot)
		panic("fdesc_readdir: not dir");

	fmp = VFSTOFDESC(ap->a_vp->v_mount);
	if (ap->a_ncookies != NULL)
		*ap->a_ncookies = 0;

	off = (int)uio->uio_offset;
	if (off != uio->uio_offset || off < 0 || (u_int)off % UIO_MX != 0 ||
	    uio->uio_resid < UIO_MX)
		return (EINVAL);
	i = (u_int)off / UIO_MX;
	fdp = uio->uio_td->td_proc->p_fd;
	error = 0;

	fcnt = i - 2;		/* The first two nodes are `.' and `..' */

	FILEDESC_SLOCK(fdp);
	while (i < fdp->fd_nfiles + 2 && uio->uio_resid >= UIO_MX) {
		bzero((caddr_t)dp, UIO_MX);
		switch (i) {
		case 0:	/* `.' */
		case 1: /* `..' */
			dp->d_fileno = i + FD_ROOT;
			dp->d_namlen = i + 1;
			dp->d_reclen = UIO_MX;
			bcopy("..", dp->d_name, dp->d_namlen);
			dp->d_type = DT_DIR;
			dirent_terminate(dp);
			break;
		default:
			if (fdp->fd_ofiles[fcnt].fde_file == NULL)
				break;
			dp->d_namlen = sprintf(dp->d_name, "%d", fcnt);
			dp->d_reclen = UIO_MX;
			dp->d_type = (fmp->flags & FMNT_LINRDLNKF) == 0 ?
			    DT_CHR : DT_LNK;
			dp->d_fileno = i + FD_DESC;
			dirent_terminate(dp);
			break;
		}
		/* NOTE: d_off is the offset of the *next* entry. */
		dp->d_off = UIO_MX * (i + 1);
		if (dp->d_namlen != 0) {
			/*
			 * And ship to userland
			 */
			FILEDESC_SUNLOCK(fdp);
			error = uiomove(dp, UIO_MX, uio);
			if (error)
				goto done;
			FILEDESC_SLOCK(fdp);
		}
		i++;
		fcnt++;
	}
	FILEDESC_SUNLOCK(fdp);

done:
	uio->uio_offset = i * UIO_MX;
	return (error);
}

static int
fdesc_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp;
	struct fdescnode *fd;

 	vp = ap->a_vp;
 	fd = VTOFDESC(vp);
	fdesc_remove_entry(fd);
	free(vp->v_data, M_TEMP);
	vp->v_data = NULL;
	return (0);
}

static int
fdesc_readlink(struct vop_readlink_args *va)
{
	struct vnode *vp, *vn;
	struct thread *td;
	struct uio *uio;
	struct file *fp;
	char *freepath, *fullpath;
	size_t pathlen;
	int lockflags, fd_fd;
	int error;

	freepath = NULL;
	vn = va->a_vp;
	if (VTOFDESC(vn)->fd_type != Fdesc)
		panic("fdesc_readlink: not fdescfs link");
	fd_fd = ((struct fdescnode *)vn->v_data)->fd_fd;
	lockflags = VOP_ISLOCKED(vn);
	VOP_UNLOCK(vn, 0);

	td = curthread;
	error = fget_cap(td, fd_fd, &cap_no_rights, &fp, NULL);
	if (error != 0)
		goto out;

	switch (fp->f_type) {
	case DTYPE_VNODE:
		vp = fp->f_vnode;
		error = vn_fullpath(td, vp, &fullpath, &freepath);
		break;
	default:
		fullpath = "anon_inode:[unknown]";
		break;
	}
	if (error == 0) {
		uio = va->a_uio;
		pathlen = strlen(fullpath);
		error = uiomove(fullpath, pathlen, uio);
	}
	if (freepath != NULL)
		free(freepath, M_TEMP);
	fdrop(fp, td);

out:
	vn_lock(vn, lockflags | LK_RETRY);
	return (error);
}
