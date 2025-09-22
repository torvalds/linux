/* $OpenBSD: fuse_vnops.c,v 1.73 2025/09/09 16:46:55 helg Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/specdev.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/fusebuf.h>

#include "fusefs_node.h"
#include "fusefs.h"

/* Prototypes for fusefs vnode ops */
int	fusefs_kqfilter(void *);
int	fusefs_lookup(void *);
int	fusefs_open(void *);
int	fusefs_close(void *);
int	fusefs_access(void *);
int	fusefs_getattr(void *);
int	fusefs_setattr(void *);
int	fusefs_ioctl(void *);
int	fusefs_link(void *);
int	fusefs_symlink(void *);
int	fusefs_readdir(void *);
int	fusefs_readlink(void *);
int	fusefs_inactive(void *);
int	fusefs_reclaim(void *);
int	fusefs_print(void *);
int	fusefs_create(void *);
int	fusefs_mknod(void *);
int	fusefs_read(void *);
int	fusefs_write(void *);
int	fusefs_remove(void *);
int	fusefs_rename(void *);
int	fusefs_mkdir(void *);
int	fusefs_rmdir(void *);
int	fusefs_strategy(void *);
int	fusefs_lock(void *);
int	fusefs_unlock(void *);
int	fusefs_islocked(void *);
int	fusefs_advlock(void *);
int	fusefs_fsync(void *);

/* Prototypes for fusefs kqfilter */
int	filt_fusefsread(struct knote *, long);
int	filt_fusefswrite(struct knote *, long);
int	filt_fusefsvnode(struct knote *, long);
void	filt_fusefsdetach(struct knote *);

const struct vops fusefs_vops = {
	.vop_lookup	= fusefs_lookup,
	.vop_create	= fusefs_create,
	.vop_mknod	= fusefs_mknod,
	.vop_open	= fusefs_open,
	.vop_close	= fusefs_close,
	.vop_access	= fusefs_access,
	.vop_getattr	= fusefs_getattr,
	.vop_setattr	= fusefs_setattr,
	.vop_read	= fusefs_read,
	.vop_write	= fusefs_write,
	.vop_ioctl	= fusefs_ioctl,
	.vop_kqfilter	= fusefs_kqfilter,
	.vop_revoke	= NULL,
	.vop_fsync	= fusefs_fsync,
	.vop_remove	= fusefs_remove,
	.vop_link	= fusefs_link,
	.vop_rename	= fusefs_rename,
	.vop_mkdir	= fusefs_mkdir,
	.vop_rmdir	= fusefs_rmdir,
	.vop_symlink	= fusefs_symlink,
	.vop_readdir	= fusefs_readdir,
	.vop_readlink	= fusefs_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= fusefs_inactive,
	.vop_reclaim	= fusefs_reclaim,
	.vop_lock	= fusefs_lock,
	.vop_unlock	= fusefs_unlock,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= fusefs_strategy,
	.vop_print	= fusefs_print,
	.vop_islocked	= fusefs_islocked,
	.vop_pathconf	= spec_pathconf,
	.vop_advlock	= fusefs_advlock,
	.vop_bwrite	= NULL,
};

const struct filterops fusefsread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_fusefsdetach,
	.f_event	= filt_fusefsread,
};

const struct filterops fusefswrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_fusefsdetach,
	.f_event	= filt_fusefswrite,
};

const struct filterops fusefsvnode_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_fusefsdetach,
	.f_event	= filt_fusefsvnode,
};

int
fusefs_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &fusefsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &fusefswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &fusefsvnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)vp;

	klist_insert_locked(&vp->v_klist, kn);

	return (0);
}

void
filt_fusefsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	klist_remove_locked(&vp->v_klist, kn);
}

int
filt_fusefsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	struct fusefs_node *ip = VTOI(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = ip->filesize - foffset(kn->kn_fp);
	if (kn->kn_data == 0 && kn->kn_sfflags & NOTE_EOF) {
		kn->kn_fflags |= NOTE_EOF;
		return (1);
	}

	if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
		return (1);

	return (kn->kn_data != 0);
}

int
filt_fusefswrite(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = 0;
	return (1);
}

int
filt_fusefsvnode(struct knote *kn, long int hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}

/*
 * FUSE file systems can maintain a file handle for each VFS file descriptor
 * that is opened. The OpenBSD VFS does not make file descriptors visible to 
 * us so we fake it by mapping open flags to file handles.
 * There is no way for FUSE to know which file descriptor is being used
 * by an application for a file operation. We only maintain 3 descriptors,
 * one each for O_RDONLY, O_WRONLY and O_RDWR. When reading and writing, the
 * first open descriptor is used and this may well not be the one that was set
 * by FUSE open and may have even been opened by another application.
 */
int
fusefs_open(void *v)
{
	struct vop_open_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct vnode *vp;
	enum fufh_type fufh_type = FUFH_RDONLY;
	int flags;
	int error;
	int isdir;

	ap = v;
	vp = ap->a_vp;
	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (ENXIO);

	isdir = 0;
	if (vp->v_type == VDIR)
		isdir = 1;
	else {
		if ((ap->a_mode & FREAD) && (ap->a_mode & FWRITE))
			fufh_type = FUFH_RDWR;
		else if (ap->a_mode & (FWRITE))
			fufh_type = FUFH_WRONLY;

		/*
		 * Due to possible attribute caching, there is no
		 * reliable way to determine if the file was modified
		 * externally (e.g. network file system) so clear the
		 * UVM cache to ensure that it is not stale. The file
		 * can still become stale later on read but this will
		 * satisfy most situations.
		 */
		uvm_vnp_uncache(vp);
	}

	/* already open i think all is ok */
	if (ip->fufh[fufh_type].fh_type != FUFH_INVALID)
		return (0);

	/*
	 * The file has already been created and/or truncated so FUSE dictates
	 * that no creation and truncation flags are passed to open.
	 */
	flags = OFLAGS(ap->a_mode) & ~(O_CREAT|O_EXCL|O_TRUNC);
	error = fusefs_file_open(fmp, ip, fufh_type, flags, isdir, ap->a_p);

	return (error);
}

int
fusefs_close(void *v)
{
	struct vop_close_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	enum fufh_type fufh_type = FUFH_RDONLY;
	int error = 0;

	ap = v;
	ip = VTOI(ap->a_vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (0);

	/*
	 * The file or directory may have been opened more than once so there
	 * is no reliable way to determine when to ask the FUSE daemon to
	 * release its file descriptor. For files, ask the daemon to flush any
	 * buffers to disk now. All open file descriptors will be released on
	 * VOP_INACTIVE(9).
	 */

	if (ap->a_vp->v_type == VDIR)
		return (0);

	/* Implementing flush is optional so don't error. */
	if (fmp->undef_op & UNDEF_FLUSH)
		return (0);

	/* Only flush writeable file descriptors. */
	if ((ap->a_fflag & FREAD) && (ap->a_fflag & FWRITE))
		fufh_type = FUFH_RDWR;
	else if (ap->a_fflag & (FWRITE))
		fufh_type = FUFH_WRONLY;
	else
		return (0);

	if (ip->fufh[fufh_type].fh_type == FUFH_INVALID)
		return (EBADF);

	fbuf = fb_setup(0, ip->i_number, FBT_FLUSH, ap->a_p);
	fbuf->fb_io_fd = ip->fufh[fufh_type].fh_id;
	error = fb_queue(fmp->dev, fbuf);
	fb_delete(fbuf);
	if (error == ENOSYS) {
		fmp->undef_op |= UNDEF_FLUSH;

		/* Implementing flush is optional so don't error. */
		return (0);
	}

	return (error);
}

int
fusefs_access(void *v)
{
	struct vop_access_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct ucred *cred;
	struct vattr vattr;
	struct proc *p;
	int error = 0;

	ap = v;
	p = ap->a_p;
	cred = p->p_ucred;
	ip = VTOI(ap->a_vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	/* 
	 * Only user that mounted the file system can access it unless
	 * allow_other mount option was specified.
	 */
	if (!fmp->allow_other && cred->cr_uid != fmp->mp->mnt_stat.f_owner)
		return (EACCES);

	if (!fmp->sess_init)
		return (ENXIO);

	/*
	 * Disallow write attempts on filesystems mounted read-only;
	 * unless the file is a socket, fifo, or a block or character
	 * device resident on the filesystem.
	 */
	if ((ap->a_mode & VWRITE) && (fmp->mp->mnt_flag & MNT_RDONLY)) {
		switch (ap->a_vp->v_type) {
		case VREG:
		case VDIR:
		case VLNK:
			return (EROFS);
		default:
			break;
		}
	}

	if ((error = VOP_GETATTR(ap->a_vp, &vattr, cred, p)) != 0)
		return (error);

	return (vaccess(ap->a_vp->v_type, vattr.va_mode & ALLPERMS,
	    vattr.va_uid, vattr.va_gid, ap->a_mode,
	    ap->a_cred));
}

int
fusefs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_mnt *fmp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = ap->a_p;
	struct ucred *cred = p->p_ucred;
	struct fusefs_node *ip;
	struct fusebuf *fbuf;
	struct stat *st;
	int error = 0;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	/* 
	 * Only user that mounted the file system can access it unless
	 * allow_other mount option was specified. Return dummy values
	 * for the root inode in this situation.
	 */
	if (!fmp->allow_other && cred->cr_uid != fmp->mp->mnt_stat.f_owner) {
		memset(vap, 0, sizeof(*vap));
		vap->va_type = VNON;
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			vap->va_mode = S_IRUSR | S_IXUSR;
		else
			vap->va_mode = S_IRWXU;
		vap->va_nlink = 1;
		vap->va_uid = fmp->mp->mnt_stat.f_owner;
		vap->va_gid = fmp->mp->mnt_stat.f_owner;
		vap->va_fsid = fmp->mp->mnt_stat.f_fsid.val[0];
		vap->va_fileid = ip->i_number;
		vap->va_size = S_BLKSIZE;
		vap->va_blocksize = S_BLKSIZE;
		vap->va_atime.tv_sec = fmp->mp->mnt_stat.f_ctime;
		vap->va_mtime.tv_sec = fmp->mp->mnt_stat.f_ctime;
		vap->va_ctime.tv_sec = fmp->mp->mnt_stat.f_ctime;
		vap->va_rdev = fmp->dev;
		vap->va_bytes = S_BLKSIZE;
		return (0);
	}

	if (!fmp->sess_init)
		return (ENXIO);

	fbuf = fb_setup(0, ip->i_number, FBT_GETATTR, p);

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		fb_delete(fbuf);
		return (error);
	}

	st = &fbuf->fb_attr;

	memset(vap, 0, sizeof(*vap));
	vap->va_type = IFTOVT(st->st_mode);
	vap->va_mode = st->st_mode & ~S_IFMT;
	vap->va_nlink = st->st_nlink;
	vap->va_uid = st->st_uid;
	vap->va_gid = st->st_gid;
	vap->va_fsid = fmp->mp->mnt_stat.f_fsid.val[0];
	vap->va_fileid = st->st_ino;
	vap->va_size = st->st_size;
	vap->va_blocksize = st->st_blksize;
	vap->va_atime = st->st_atim;
	vap->va_mtime = st->st_mtim;
	vap->va_ctime = st->st_ctim;
	vap->va_rdev = st->st_rdev;
	vap->va_bytes = st->st_blocks * S_BLKSIZE;

	fb_delete(fbuf);
	return (error);
}

int
fusefs_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	struct proc *p = ap->a_p;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct fb_io *io;
	int error = 0;

	fmp = (struct fusefs_mnt *)ip->i_ump;
	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL))
		return (EINVAL);

	if (!fmp->sess_init)
		return (ENXIO);

	if (fmp->undef_op & UNDEF_SETATTR)
		return (ENOSYS);

	fbuf = fb_setup(sizeof(*io), ip->i_number, FBT_SETATTR, p);
	io = fbtod(fbuf, struct fb_io *);
	io->fi_flags = 0;

	if (vap->va_uid != (uid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_attr.st_uid = vap->va_uid;
		io->fi_flags |= FUSE_FATTR_UID;
	}

	if (vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_attr.st_gid = vap->va_gid;
		io->fi_flags |= FUSE_FATTR_GID;
	}

	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			error = EISDIR;
			goto out;
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY) {
				error = EROFS;
				goto out;
			}
			break;
		default:
			break;
		}

		fbuf->fb_attr.st_size = vap->va_size;
		io->fi_flags |= FUSE_FATTR_SIZE;
	}

	if (vap->va_atime.tv_nsec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_attr.st_atim = vap->va_atime;
		io->fi_flags |= FUSE_FATTR_ATIME;
	}

	if (vap->va_mtime.tv_nsec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_attr.st_mtim = vap->va_mtime;
		io->fi_flags |= FUSE_FATTR_MTIME;
	}
	/* XXX should set a flag if (vap->va_vaflags & VA_UTIMES_CHANGE) */

	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}

		/*
		 * chmod returns EFTYPE if the effective user ID is not the
		 * super-user, the mode includes the sticky bit (S_ISVTX), and
		 * path does not refer to a directory
		 */
		if (cred->cr_uid != 0 && vp->v_type != VDIR &&
		    (vap->va_mode & S_ISTXT)) {
			error = EFTYPE;
			goto out;
		}

		fbuf->fb_attr.st_mode = vap->va_mode & ALLPERMS;
		io->fi_flags |= FUSE_FATTR_MODE;
	}

	if (!io->fi_flags) {
		goto out;
	}

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_SETATTR;
		goto out;
	}

	/* truncate was successful, let uvm know */
	if (vap->va_size != VNOVAL && vap->va_size != ip->filesize) {
		ip->filesize = vap->va_size;
		uvm_vnp_setsize(vp, vap->va_size);
	}

	VN_KNOTE(ap->a_vp, NOTE_ATTRIB);

out:
	fb_delete(fbuf);
	return (error);
}

int
fusefs_ioctl(void *v)
{
	return (ENOTTY);
}

int
fusefs_link(void *v)
{
	struct vop_link_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_mnt *fmp;
	struct fusefs_node *ip;
	struct fusefs_node *dip;
	struct fusebuf *fbuf;
	int error = 0;

	ip = VTOI(vp);
	dip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init) {
		VOP_ABORTOP(dvp, cnp);
		error = ENXIO;
		goto out2;
	}
	if (fmp->undef_op & UNDEF_LINK) {
		VOP_ABORTOP(dvp, cnp);
		error = ENOSYS;
		goto out2;
	}
	if (dvp != vp && (error = vn_lock(vp, LK_EXCLUSIVE))) {
		VOP_ABORTOP(dvp, cnp);
		goto out2;
	}

	fbuf = fb_setup(cnp->cn_namelen + 1, dip->i_number,
	    FBT_LINK, p);

	fbuf->fb_io_ino = ip->i_number;
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_LINK;

		fb_delete(fbuf);
		goto out1;
	}

	fb_delete(fbuf);
	VN_KNOTE(vp, NOTE_LINK);
	VN_KNOTE(dvp, NOTE_WRITE);

out1:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	if (dvp != vp)
		VOP_UNLOCK(vp);
out2:
	vput(dvp);
	return (error);
}

int
fusefs_symlink(void *v)
{
	struct vop_symlink_args *ap = v;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct proc *p = cnp->cn_proc;
	char *target = ap->a_target;
	struct fusefs_node *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct vnode *tdp;
	int error = 0;
	int len;

	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)dp->i_ump;

	if (!fmp->sess_init) {
		error = ENXIO;
		goto bad;
	}

	if (fmp->undef_op & UNDEF_SYMLINK) {
		error = ENOSYS;
		goto bad;
	}

	len = strlen(target) + 1;

	fbuf = fb_setup(len + cnp->cn_namelen + 1, dp->i_number,
	    FBT_SYMLINK, p);

	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';
	memcpy(&fbuf->fb_dat[cnp->cn_namelen + 1], target, len);

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_SYMLINK;

		fb_delete(fbuf);
		goto bad;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp))) {
		fb_delete(fbuf);
		goto bad;
	}

	tdp->v_type = VLNK;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);

	*vpp = tdp;
	fb_delete(fbuf);
	vput(tdp);
bad:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	vput(dvp);
	return (error);
}

int
fusefs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct dirent *dp;
	char *edp;
	struct vnode *vp;
	struct proc *p;
	struct uio *uio;
	int error = 0, eofflag = 0, diropen = 0;

	vp = ap->a_vp;
	uio = ap->a_uio;
	p = uio->uio_procp;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (ENXIO);

	if (uio->uio_resid < sizeof(struct dirent))
		return (EINVAL);

	if (ip->fufh[FUFH_RDONLY].fh_type == FUFH_INVALID) {
		error = fusefs_file_open(fmp, ip, FUFH_RDONLY, O_RDONLY, 1, p);
		if (error)
			return (error);

		diropen = 1;
	}

	while (uio->uio_resid > 0) {
		fbuf = fb_setup(0, ip->i_number, FBT_READDIR, p);

		fbuf->fb_io_fd = ip->fufh[FUFH_RDONLY].fh_id;
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = MIN(uio->uio_resid, fmp->max_read);

		error = fb_queue(fmp->dev, fbuf);

		if (error) {
			/*
			 * dirent was larger than residual space left in
			 * buffer.
			 */
			if (error == ENOBUFS)
				error = 0;

			fb_delete(fbuf);
			break;
		}

		/* ack end of readdir */
		if (fbuf->fb_len == 0) {
			eofflag = 1;
			fb_delete(fbuf);
			break;
		}

		/* validate the returned dirents */
		dp = (struct dirent *)fbuf->fb_dat;
		edp = fbuf->fb_dat + fbuf->fb_len;
		while ((char *)dp < edp) {
			if ((char *)dp + offsetof(struct dirent, d_name) >= edp
			    || dp->d_reclen <= offsetof(struct dirent, d_name)
			    || (char *)dp + dp->d_reclen > edp) {
				error = EINVAL;
				break;
			}
			if (dp->d_namlen + offsetof(struct dirent, d_name) >=
			    dp->d_reclen) {
				error = EINVAL;
				break;
			}
			memset(dp->d_name + dp->d_namlen, 0, dp->d_reclen -
			    dp->d_namlen - offsetof(struct dirent, d_name));

			if (memchr(dp->d_name, '/', dp->d_namlen) != NULL) {
				error = EINVAL;
				break;
			}
			dp = (struct dirent *)((char *)dp + dp->d_reclen);
		}
		if (error) {
			fb_delete(fbuf);
			break;
		}

		if ((error = uiomove(fbuf->fb_dat, fbuf->fb_len, uio))) {
			fb_delete(fbuf);
			break;
		}

		fb_delete(fbuf);
	}

	if (!error && ap->a_eofflag != NULL)
		*ap->a_eofflag = eofflag;

	if (diropen)
		fusefs_file_close(fmp, ip, FUFH_RDONLY, O_RDONLY, 1, p);

	return (error);
}

int
fusefs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct proc *p = ap->a_p;
	struct fusefs_node *ip = VTOI(vp);
	struct fusefs_filehandle *fufh = NULL;
	struct fusefs_mnt *fmp;
	int type, flags;

	fmp = (struct fusefs_mnt *)ip->i_ump;

	/* Close all open file handles. */
	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(ip->fufh[type]);
		if (fufh->fh_type != FUFH_INVALID) {

			/*
			 * FUSE file systems expect the same flags to be sent
			 * on release that were sent on open. We don't have a 
			 * record of them so make a best guess.
			 */
			switch (type) {
			case FUFH_RDONLY:
				flags = O_RDONLY;
				break;
			case FUFH_WRONLY:
				flags = O_WRONLY;
				break;
			default:
				flags = O_RDWR;
			}

			fusefs_file_close(fmp, ip, fufh->fh_type, flags,
			    (vp->v_type == VDIR), p);
		}
	}

	VOP_UNLOCK(vp);

	/* Don't return error to prevent kernel panic in vclean(9). */
	return (0);
}

int
fusefs_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct uio *uio;
	struct proc *p;
	int error;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;
	uio = ap->a_uio;
	p = uio->uio_procp;

	if (!fmp->sess_init)
		return (ENXIO);
        if (uio->uio_resid == 0)
                return (0);
        if (uio->uio_offset < 0)
                return (EINVAL);

	if (fmp->undef_op & UNDEF_READLINK)
		return (ENOSYS);

	fbuf = fb_setup(0, ip->i_number, FBT_READLINK, p);

	fbuf->fb_io_off = uio->uio_offset;
	fbuf->fb_io_len = MIN(uio->uio_resid, fmp->max_read);

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_READLINK;

		fb_delete(fbuf);
		return (error);
	}

	if (strnlen(fbuf->fb_dat, fbuf->fb_len) != fbuf->fb_len) {
		/*
		 * TODO
		 * DPRINTF("fusefs: symbolic link contains embedded NUL: %s\n",
		 *     fbuf->fb_dat);
		 */

		fb_delete(fbuf);
		return (EIO);
	}

	error = uiomove(fbuf->fb_dat, fbuf->fb_len, uio);
	fb_delete(fbuf);

	return (error);
}

int
fusefs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct proc *p = ap->a_p;
	struct fusefs_node *ip = VTOI(vp);
	struct fusefs_filehandle *fufh = NULL;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int type, error = 0;

	fmp = (struct fusefs_mnt *)ip->i_ump;

	/* Close opened files. */
	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(ip->fufh[type]);
		if (fufh->fh_type != FUFH_INVALID) {
			printf("fusefs: vnode being reclaimed is valid\n");
			fusefs_file_close(fmp, ip, fufh->fh_type, type,
			    (vp->v_type == VDIR), ap->a_p);
		}
	}

	/*
	 * If the fuse connection is opened ask libfuse to free the vnodes.
	 */
	if (fmp->sess_init && ip->i_number != FUSE_ROOTINO) {
		fbuf = fb_setup(0, ip->i_number, FBT_RECLAIM, p);
		error = fb_queue(fmp->dev, fbuf);
		if (error)
			printf("fusefs: vnode reclaim failed: %d\n", error);
		fb_delete(fbuf);
	}

	/*
	 * Remove the inode from its hash chain.
	 */
	fuse_ihashrem(ip);

	free(ip, M_FUSEFS, sizeof(*ip));
	vp->v_data = NULL;

	/* Must return success otherwise kernel panic in vclean(9). */
	return (0);
}

int
fusefs_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip = VTOI(vp);

	/* Complete the information given by vprint(). */
	printf("tag VT_FUSE, hash id %llu ", ip->i_number);
	printf("\n");
#endif
	return (0);
}

int
fusefs_create(void *v)
{
	struct vop_create_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = cnp->cn_proc;
	struct vnode *tdp = NULL;
	struct fusefs_mnt *fmp;
	struct fusefs_node *ip;
	struct fusebuf *fbuf;
	int error = 0;
	mode_t mode;

	ip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;
	mode = MAKEIMODE(vap->va_type, vap->va_mode);

	if (!fmp->sess_init) {
		VOP_ABORTOP(dvp, cnp);
		return (ENXIO);
	}

	if (fmp->undef_op & UNDEF_MKNOD) {
		VOP_ABORTOP(dvp, cnp);
		return (ENOSYS);
	}

	fbuf = fb_setup(cnp->cn_namelen + 1, ip->i_number,
	    FBT_MKNOD, p);

	fbuf->fb_io_mode = mode;

	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_MKNOD;

		goto out;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp)))
		goto out;

	tdp->v_type = IFTOVT(fbuf->fb_io_mode);

	*vpp = tdp;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
out:
	fb_delete(fbuf);
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return (error);
}

int
fusefs_mknod(void *v)
{
	struct vop_mknod_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = cnp->cn_proc;
	struct vnode *tdp = NULL;
	struct fusefs_mnt *fmp;
	struct fusefs_node *ip;
	struct fusebuf *fbuf;
	int error = 0;

	ip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init) {
		VOP_ABORTOP(dvp, cnp);
		return (ENXIO);
	}

	if (fmp->undef_op & UNDEF_MKNOD) {
		VOP_ABORTOP(dvp, cnp);
		return (ENOSYS);
	}

	fbuf = fb_setup(cnp->cn_namelen + 1, ip->i_number,
	    FBT_MKNOD, p);

	fbuf->fb_io_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	if (vap->va_rdev != VNOVAL)
		fbuf->fb_io_rdev = vap->va_rdev;

	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_MKNOD;

		goto out;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp)))
		goto out;

	tdp->v_type = IFTOVT(fbuf->fb_io_mode);

	*vpp = tdp;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);

	/* Remove inode so that it will be reloaded by VFS_VGET and
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.
	 */
	vput(*vpp);
	(*vpp)->v_type = VNON;
	vgone(*vpp);
	*vpp = NULL;
out:
	fb_delete(fbuf);
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return (error);
}

int
fusefs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf = NULL;
	size_t size;
	int error;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (ENXIO);
	if (uio->uio_resid == 0)
		return (0);
	if (uio->uio_offset < 0)
		return (EINVAL);

	while (uio->uio_resid > 0) {
		fbuf = fb_setup(0, ip->i_number, FBT_READ, p);

		size = MIN(uio->uio_resid, fmp->max_read);
		fbuf->fb_io_fd = fusefs_fd_get(ip, FUFH_RDONLY);
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = size;

		error = fb_queue(fmp->dev, fbuf);

		if (error)
			break;

		error = uiomove(fbuf->fb_dat, ulmin(size, fbuf->fb_len), uio);
		if (error)
			break;

		if (fbuf->fb_len < size)
			break;

		fb_delete(fbuf);
		fbuf = NULL;
	}

	fb_delete(fbuf);
	return (error);
}

int
fusefs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	struct ucred *cred = p->p_ucred;
	struct vattr vattr;
	int ioflag = ap->a_ioflag;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf = NULL;
	size_t len, diff;
	int error;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (ENXIO);
	if (uio->uio_resid == 0)
		return (0);

	if (ioflag & IO_APPEND) {
		if ((error = VOP_GETATTR(vp, &vattr, cred, p)) != 0)
			return (error);

		uio->uio_offset = vattr.va_size;
	}

	while (uio->uio_resid > 0) {
		len = MIN(uio->uio_resid, fmp->max_read);
		fbuf = fb_setup(len, ip->i_number, FBT_WRITE, p);

		fbuf->fb_io_fd = fusefs_fd_get(ip, FUFH_WRONLY);
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = len;

		if ((error = uiomove(fbuf->fb_dat, len, uio))) {
			printf("fusefs: uio error %i\n", error);
			break;
		}

		error = fb_queue(fmp->dev, fbuf);

		if (error)
			break;

		diff = len - fbuf->fb_io_len;
		if (fbuf->fb_io_len > len) {
			error = EINVAL;
			break;
		}

		uio->uio_resid += diff;
		uio->uio_offset -= diff;

		if (uio->uio_offset > ip->filesize) {
			ip->filesize = uio->uio_offset;
			uvm_vnp_setsize(vp, uio->uio_offset);
		}
		uvm_vnp_uncache(vp);

		fb_delete(fbuf);
		fbuf = NULL;
	}

	fb_delete(fbuf);
	return (error);
}

int
fusefs_rename(void *v)
{
	struct vop_rename_args *ap = v;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct proc *p = fcnp->cn_proc;
	struct fusefs_node *ip, *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

#ifdef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("fusefs_rename: no name");
#endif
	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
		VOP_ABORTOP(tdvp, tcnp); /* XXX, why not in NFS? */
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		VOP_ABORTOP(fdvp, fcnp); /* XXX, why not in NFS? */
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	/*
	 * If source and dest are the same, do nothing.
	 */
	if (tvp == fvp) {
		error = 0;
		goto abortit;
	}

	if ((error = vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY)) != 0)
		goto abortit;
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	/*
	 * Be sure we are not renaming ".", "..", or an alias of ".". This
	 * leads to a crippled directory tree.  It's pretty tough to do a
	 * "ls" or "pwd" with the "." directory entry missing, and "cd .."
	 * doesn't work if the ".." entry is missing.
	 */
	if (fvp->v_type == VDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip ||
		    (fcnp->cn_flags & ISDOTDOT) ||
		    (tcnp->cn_flags & ISDOTDOT)) {
			VOP_UNLOCK(fvp);
			error = EINVAL;
			goto abortit;
		}
	}
	VN_KNOTE(fdvp, NOTE_WRITE);	/* XXX right place? */

	if (!fmp->sess_init) {
		error = ENXIO;
		VOP_UNLOCK(fvp);
		goto abortit;
	}

	if (fmp->undef_op & UNDEF_RENAME) {
		error = ENOSYS;
		VOP_UNLOCK(fvp);
		goto abortit;
	}

	fbuf = fb_setup(fcnp->cn_namelen + tcnp->cn_namelen + 2,
	    dp->i_number, FBT_RENAME, p);

	memcpy(fbuf->fb_dat, fcnp->cn_nameptr, fcnp->cn_namelen);
	fbuf->fb_dat[fcnp->cn_namelen] = '\0';
	memcpy(fbuf->fb_dat + fcnp->cn_namelen + 1, tcnp->cn_nameptr,
	    tcnp->cn_namelen);
	fbuf->fb_dat[fcnp->cn_namelen + tcnp->cn_namelen + 1] = '\0';
	fbuf->fb_io_ino = VTOI(tdvp)->i_number;

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS) {
			fmp->undef_op |= UNDEF_RENAME;
		}

		fb_delete(fbuf);
		VOP_UNLOCK(fvp);
		goto abortit;
	}

	fb_delete(fbuf);
	VN_KNOTE(fvp, NOTE_RENAME);

	VOP_UNLOCK(fvp);
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);

	return (error);
}

int
fusefs_mkdir(void *v)
{
	struct vop_mkdir_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = cnp->cn_proc;
	struct vnode *tdp = NULL;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

	ip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;


	if (!fmp->sess_init) {
		error = ENXIO;
		goto out;
	}

	if (fmp->undef_op & UNDEF_MKDIR) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(cnp->cn_namelen + 1, ip->i_number,
	    FBT_MKDIR, p);

	fbuf->fb_io_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_MKDIR;

		fb_delete(fbuf);
		goto out;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp))) {
		fb_delete(fbuf);
		goto out;
	}

	tdp->v_type = IFTOVT(fbuf->fb_io_mode);

	*vpp = tdp;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	fb_delete(fbuf);
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	vput(dvp);
	return (error);
}

int
fusefs_rmdir(void *v)
{
	struct vop_rmdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_node *ip, *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error;

	ip = VTOI(vp);
	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init) {
		error = ENXIO;
		goto out;
	}

	if (fmp->undef_op & UNDEF_RMDIR) {
		error = ENOSYS;
		goto out;
	}

	VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

	fbuf = fb_setup(cnp->cn_namelen + 1, dp->i_number,
	    FBT_RMDIR, p);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_RMDIR;
		if (error != ENOTEMPTY)
			VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

		fb_delete(fbuf);
		goto out;
	}

	vput(dvp);
	dvp = NULL;

	fb_delete(fbuf);
out:
	if (dvp)
		vput(dvp);
	VN_KNOTE(vp, NOTE_DELETE);
	pool_put(&namei_pool, cnp->cn_pnbuf);
	vput(vp);
	return (error);
}

int
fusefs_remove(void *v)
{
	struct vop_remove_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_node *ip;
	struct fusefs_node *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

	ip = VTOI(vp);
	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init) {
		error = ENXIO;
		goto out;
	}

	if (fmp->undef_op & UNDEF_REMOVE) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(cnp->cn_namelen + 1, dp->i_number,
	    FBT_UNLINK, p);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_REMOVE;

		fb_delete(fbuf);
		goto out;
	}

	VN_KNOTE(vp, NOTE_DELETE);
	VN_KNOTE(dvp, NOTE_WRITE);
	fb_delete(fbuf);
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return (error);
}

int
fusefs_strategy(void *v)
{
	return (0);
}

int
fusefs_lock(void *v)
{
	struct vop_lock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	return rrw_enter(&VTOI(vp)->i_lock, ap->a_flags & LK_RWFLAGS);
}

int
fusefs_unlock(void *v)
{
	struct vop_unlock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	rrw_exit(&VTOI(vp)->i_lock);
	return 0;
}

int
fusefs_islocked(void *v)
{
	struct vop_islocked_args *ap = v;

	return rrw_status(&VTOI(ap->a_vp)->i_lock);
}

int
fusefs_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct fusefs_node *ip = VTOI(ap->a_vp);

	return (lf_advlock(&ip->i_lockf, ip->filesize, ap->a_id,
	    ap->a_op, ap->a_fl, ap->a_flags));
}

int
fusefs_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct proc *p = ap->a_p;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusefs_filehandle *fufh;
	struct fusebuf *fbuf;
	int type, error = 0;

	/*
	 * Can't write to directory file handles so no need to fsync.
	 * FUSE has fsyncdir but it doesn't make sense on OpenBSD.
	 */
	if (vp->v_type == VDIR)
		return (0);

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->i_ump;

	if (!fmp->sess_init)
		return (ENXIO);

	/* Implementing fsync is optional so don't error. */
	if (fmp->undef_op & UNDEF_FSYNC)
		return (0);

	/* Sync all writeable file descriptors. */
	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(ip->fufh[type]);
		if (fufh->fh_type == FUFH_WRONLY ||
		    fufh->fh_type == FUFH_RDWR) {

			fbuf = fb_setup(0, ip->i_number, FBT_FSYNC, p);
			fbuf->fb_io_fd = fufh->fh_id;

			/* Always behave as if ap->a_waitfor = MNT_WAIT. */
			error = fb_queue(fmp->dev, fbuf);
			fb_delete(fbuf);
			if (error)
				break;
		}
	}

	if (error == ENOSYS) {
		fmp->undef_op |= UNDEF_FSYNC;

		/* Implementing fsync is optional so don't error. */
		return (0);
	}

	return (error);
}
