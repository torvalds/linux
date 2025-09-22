/*	$OpenBSD: tmpfs_vnops.c,v 1.57 2025/09/20 13:53:36 mpi Exp $	*/
/*	$NetBSD: tmpfs_vnops.c,v 1.100 2012/11/05 17:27:39 dholland Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007, 2012 The NetBSD Foundation, Inc.
 * Copyright (c) 2013 Pedro Martelletto
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program, and by Taylor R Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tmpfs vnode interface.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/event.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/file.h>

#include <miscfs/fifofs/fifo.h>
#include <tmpfs/tmpfs_vnops.h>
#include <tmpfs/tmpfs.h>

int tmpfs_kqfilter(void *v);

/*
 * vnode operations vector used for files stored in a tmpfs file system.
 */
const struct vops tmpfs_vops = {
	.vop_lookup	= tmpfs_lookup,
	.vop_create	= tmpfs_create,
	.vop_mknod	= tmpfs_mknod,
	.vop_open	= tmpfs_open,
	.vop_close	= tmpfs_close,
	.vop_access	= tmpfs_access,
	.vop_getattr	= tmpfs_getattr,
	.vop_setattr	= tmpfs_setattr,
	.vop_read	= tmpfs_read,
	.vop_write	= tmpfs_write,
	.vop_ioctl	= tmpfs_ioctl,
	.vop_kqfilter	= tmpfs_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_fsync	= tmpfs_fsync,
	.vop_remove	= tmpfs_remove,
	.vop_link	= tmpfs_link,
	.vop_rename	= tmpfs_rename,
	.vop_mkdir	= tmpfs_mkdir,
	.vop_rmdir	= tmpfs_rmdir,
	.vop_symlink	= tmpfs_symlink,
	.vop_readdir	= tmpfs_readdir,
	.vop_readlink	= tmpfs_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= tmpfs_inactive,
	.vop_reclaim	= tmpfs_reclaim,
	.vop_lock	= tmpfs_lock,
	.vop_unlock	= tmpfs_unlock,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= tmpfs_strategy,
	.vop_print	= tmpfs_print,
	.vop_islocked	= tmpfs_islocked,
	.vop_pathconf	= tmpfs_pathconf,
	.vop_advlock	= tmpfs_advlock,
	.vop_bwrite	= tmpfs_bwrite,
};

/*
 * tmpfs_lookup: path name traversal routine.
 *
 * Arguments: dvp (directory being searched), vpp (result),
 * cnp (component name - path).
 *
 * => Caller holds a reference and lock on dvp.
 * => We return looked-up vnode (vpp) locked, with a reference held.
 */
int
tmpfs_lookup(void *v)
{
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp, **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	const int lastcn = (cnp->cn_flags & ISLASTCN) != 0;
	const int lockparent = (cnp->cn_flags & LOCKPARENT) != 0;
	tmpfs_node_t *dnode, *tnode;
	tmpfs_dirent_t *de;
	int cachefound;
	int error;

	KASSERT(VOP_ISLOCKED(dvp));

	dnode = VP_TO_TMPFS_DIR(dvp);
	cnp->cn_flags &= ~PDIRUNLOCK;
	*vpp = NULL;

	/* Check accessibility of directory. */
	error = VOP_ACCESS(dvp, VEXEC, cred, curproc);
	if (error) {
		goto out;
	}

	/*
	 * If requesting the last path component on a read-only file system
	 * with a write operation, deny it.
	 */
	if (lastcn && (dvp->v_mount->mnt_flag & MNT_RDONLY) != 0 &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		error = EROFS;
		goto out;
	}

	/*
	 * Avoid doing a linear scan of the directory if the requested
	 * directory/name couple is already in the cache.
	 */
	cachefound = cache_lookup(dvp, vpp, cnp);
	if (cachefound == ENOENT /* && *vpp == NULL */)
		return ENOENT; /* Negative cache hit. */
	else if (cachefound != -1)
		return 0; /* Found in cache. */

	if (cnp->cn_flags & ISDOTDOT) {
		tmpfs_node_t *pnode;

		/*
		 * Lookup of ".." case.
		 */
		if (lastcn) {
			if (cnp->cn_nameiop == RENAME) {
				error = EINVAL;
				goto out;
			}
			if (cnp->cn_nameiop == DELETE) {
				/* Keep the name for tmpfs_rmdir(). */
				cnp->cn_flags |= SAVENAME;
			}
		}
		KASSERT(dnode->tn_type == VDIR);
		pnode = dnode->tn_spec.tn_dir.tn_parent;
		if (pnode == NULL) {
			error = ENOENT;
			goto out;
		}

		/*
		 * Lock the parent tn_nlock before releasing the vnode lock,
		 * and thus prevents parent from disappearing.
		 */
		rw_enter_write(&pnode->tn_nlock);
		VOP_UNLOCK(dvp);

		/*
		 * Get a vnode of the '..' entry and re-acquire the lock.
		 * Release the tn_nlock.
		 */
		error = tmpfs_vnode_get(dvp->v_mount, pnode, vpp);
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		goto out;

	} else if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		/*
		 * Lookup of "." case.
		 */
		if (lastcn && cnp->cn_nameiop == RENAME) {
			error = EISDIR;
			goto out;
		}
		vref(dvp);
		*vpp = dvp;
		error = 0;
		goto done;
	}

	/*
	 * Other lookup cases: perform directory scan.
	 */
	de = tmpfs_dir_lookup(dnode, cnp);
	if (de == NULL) {
		/*
		 * The entry was not found in the directory.  This is valid
		 * if we are creating or renaming an entry and are working
		 * on the last component of the path name.
		 */
		if (lastcn && (cnp->cn_nameiop == CREATE ||
		    cnp->cn_nameiop == RENAME)) {
			error = VOP_ACCESS(dvp, VWRITE, cred, curproc);
			if (error) {
				goto out;
			}
			/*
			 * We are creating an entry in the file system, so
			 * save its name for further use by tmpfs_create().
			 */
			cnp->cn_flags |= SAVENAME;
			error = EJUSTRETURN;
		} else {
			error = ENOENT;
		}
		goto done;
	}

	tnode = de->td_node;

	/*
	 * If it is not the last path component and found a non-directory
	 * or non-link entry (which may itself be pointing to a directory),
	 * raise an error.
	 */
	if (!lastcn && tnode->tn_type != VDIR && tnode->tn_type != VLNK) {
		error = ENOTDIR;
		goto out;
	}

	/* Check the permissions. */
	if (lastcn && (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)) {
		error = VOP_ACCESS(dvp, VWRITE, cred, curproc);
		if (error)
			goto out;

		/*
		 * If not root and directory is sticky, check for permission
		 * on directory or on file. This implements append-only
		 * directories.
		 */
		if ((dnode->tn_mode & S_ISTXT) != 0) {
			if (cred->cr_uid != 0 &&
			    cred->cr_uid != dnode->tn_uid &&
			    cred->cr_uid != tnode->tn_uid) {
				error = EPERM;
				goto out;
			}
		}

		/*
		 * XXX pedro: We might need cn_nameptr later in tmpfs_remove()
		 * or tmpfs_rmdir() for a tmpfs_dir_lookup(). We should really
		 * get rid of SAVENAME at some point.
		 */
		if (cnp->cn_nameiop == DELETE)
			cnp->cn_flags |= SAVENAME;
	}

	/* Get a vnode for the matching entry. */
	rw_enter_write(&tnode->tn_nlock);
	error = tmpfs_vnode_get(dvp->v_mount, tnode, vpp);
done:
	/*
	 * Cache the result, unless request was for creation (as it does
	 * not improve the performance).
	 */
	if ((cnp->cn_flags & MAKEENTRY) && cnp->cn_nameiop != CREATE) {
		cache_enter(dvp, *vpp, cnp);
	}
out:
	/*
	 * If (1) we succeeded, (2) found a distinct vnode != .. to return and (3)
	 * were either explicitly told to keep the parent locked or are in the
	 * middle of a lookup, unlock the parent vnode.
	 */
	if ((error == 0 || error == EJUSTRETURN) && /* (1) */
	    (*vpp != dvp || (cnp->cn_flags & ISDOTDOT))  && /* (2) */
	    (!lockparent || !lastcn)) {		    /* (3) */
		VOP_UNLOCK(dvp);
		cnp->cn_flags |= PDIRUNLOCK;
	} else
		KASSERT(VOP_ISLOCKED(dvp));

	KASSERT((*vpp && VOP_ISLOCKED(*vpp)) || error);

	return error;
}

int
tmpfs_create(void *v)
{
	struct vop_create_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp, **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;

	KASSERT(VOP_ISLOCKED(dvp));
	KASSERT(cnp->cn_flags & HASBUF);
	KASSERT(vap->va_type == VREG || vap->va_type == VSOCK);
	return tmpfs_alloc_file(dvp, vpp, vap, cnp, NULL);
}

int
tmpfs_mknod(void *v)
{
	struct vop_mknod_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp, **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	enum vtype vt = vap->va_type;
	int error;

	if (vt != VBLK && vt != VCHR && vt != VFIFO)
		return EINVAL;

	error = tmpfs_alloc_file(dvp, vpp, vap, cnp, NULL);

	if (error == 0)
		vput(*vpp);

	return error;
}

int
tmpfs_open(void *v)
{
	struct vop_open_args /* {
		struct vnode	*a_vp;
		int		a_mode;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	mode_t mode = ap->a_mode;
	tmpfs_node_t *node;

	KASSERT(VOP_ISLOCKED(vp));

	node = VP_TO_TMPFS_NODE(vp);
	if (node->tn_links < 1) {
		/*
		 * The file is still active, but all its names have been
		 * removed (e.g. by a "rmdir $(pwd)").  It cannot be opened
		 * any more, as it is about to be destroyed.
		 */
		return ENOENT;
	}

	/* If the file is marked append-only, deny write requests. */
	if ((node->tn_flags & APPEND) != 0 &&
	    (mode & (FWRITE | O_APPEND)) == FWRITE) {
		return EPERM;
	}
	return 0;
}

int
tmpfs_close(void *v)
{
#ifdef DIAGNOSTIC
	struct vop_close_args /* {
		struct vnode	*a_vp;
		int		a_fflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;

	KASSERT(VOP_ISLOCKED(vp));
#endif
	return 0;
}

int
tmpfs_access(void *v)
{
	struct vop_access_args /* {
		struct vnode	*a_vp;
		int		a_mode;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	mode_t mode = ap->a_mode;
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	const int writing = (mode & VWRITE) != 0;

	KASSERT(VOP_ISLOCKED(vp));

	/* Possible? */
	switch (vp->v_type) {
	case VDIR:
	case VLNK:
	case VREG:
		if (writing && (vp->v_mount->mnt_flag & MNT_RDONLY) != 0) {
			return EROFS;
		}
		break;
	case VBLK:
	case VCHR:
	case VSOCK:
	case VFIFO:
		break;
	default:
		return EINVAL;
	}
	if (writing && (node->tn_flags & IMMUTABLE) != 0) {
		return EPERM;
	}

	return (vaccess(vp->v_type, node->tn_mode, node->tn_uid, node->tn_gid,
	    mode, ap->a_cred));
}

int
tmpfs_getattr(void *v)
{
	struct vop_getattr_args /* {
		struct vnode	*a_vp;
		struct vattr	*a_vap;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);

	vattr_null(vap);

	vap->va_type = vp->v_type;
	vap->va_mode = node->tn_mode;
	vap->va_nlink = node->tn_links;
	vap->va_uid = node->tn_uid;
	vap->va_gid = node->tn_gid;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = node->tn_id;
	vap->va_size = node->tn_size;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_atime = node->tn_atime;
	vap->va_mtime = node->tn_mtime;
	vap->va_ctime = node->tn_ctime;
	/* vap->va_birthtime = node->tn_birthtime; */
	vap->va_gen = TMPFS_NODE_GEN(node);
	vap->va_flags = node->tn_flags;
	vap->va_rdev = (vp->v_type == VBLK || vp->v_type == VCHR) ?
	    node->tn_spec.tn_dev.tn_rdev : VNOVAL;
	vap->va_bytes = round_page(node->tn_size);
	vap->va_filerev = VNOVAL;
	vap->va_vaflags = 0;
	vap->va_spare = VNOVAL; /* XXX */

	return 0;
}

#define GOODTIME(tv)	((tv)->tv_nsec != VNOVAL)
/* XXX Should this operation be atomic?  I think it should, but code in
 * XXX other places (e.g., ufs) doesn't seem to be... */
int
tmpfs_setattr(void *v)
{
	struct vop_setattr_args /* {
		struct vnode	*a_vp;
		struct vattr	*a_vap;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct proc *p = curproc;
	int error = 0;

	KASSERT(VOP_ISLOCKED(vp));

	/* Abort if any unsettable attribute is given. */
	if (vap->va_type != VNON || vap->va_nlink != VNOVAL ||
	    vap->va_fsid != VNOVAL || vap->va_fileid != VNOVAL ||
	    vap->va_blocksize != VNOVAL || GOODTIME(&vap->va_ctime) ||
	    vap->va_gen != VNOVAL || vap->va_rdev != VNOVAL ||
	    vap->va_bytes != VNOVAL) {
		return EINVAL;
	}
	if (error == 0 && (vap->va_flags != VNOVAL))
		error = tmpfs_chflags(vp, vap->va_flags, cred, p);

	if (error == 0 && (vap->va_size != VNOVAL))
		error = tmpfs_chsize(vp, vap->va_size, cred, p);

	if (error == 0 && (vap->va_uid != VNOVAL || vap->va_gid != VNOVAL))
		error = tmpfs_chown(vp, vap->va_uid, vap->va_gid, cred, p);

	if (error == 0 && (vap->va_mode != VNOVAL))
		error = tmpfs_chmod(vp, vap->va_mode, cred, p);

	if (error == 0 && ((vap->va_vaflags & VA_UTIMES_CHANGE)
	    || GOODTIME(&vap->va_atime)
	    || GOODTIME(&vap->va_mtime)))
		error = tmpfs_chtimes(vp, &vap->va_atime, &vap->va_mtime,
		    vap->va_vaflags, cred, p);

	return error;
}

int
tmpfs_read(void *v)
{
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		struct ucred *a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	/* const int ioflag = ap->a_ioflag; */
	tmpfs_node_t *node;
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	if (vp->v_type != VREG) {
		return EISDIR;
	}
	if (uio->uio_offset < 0) {
		return EINVAL;
	}
	if (uio->uio_resid == 0)
		return 0;

	node = VP_TO_TMPFS_NODE(vp);
	error = 0;

	while (error == 0 && uio->uio_resid > 0) {
		vsize_t len;

		if (node->tn_size <= uio->uio_offset) {
			break;
		}
		len = MIN(node->tn_size - uio->uio_offset, uio->uio_resid);
		if (len == 0) {
			break;
		}
		error = tmpfs_uiomove(node, uio, len);
	}

	if (!(vp->v_mount->mnt_flag & MNT_NOATIME))
		tmpfs_update(node, TMPFS_NODE_ACCESSED);

	return error;
}

int
tmpfs_write(void *v)
{
	struct vop_write_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		int		a_ioflag;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	const int ioflag = ap->a_ioflag;
	tmpfs_node_t *node;
	off_t oldsize;
	ssize_t overrun;
	int extended;
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	node = VP_TO_TMPFS_NODE(vp);
	oldsize = node->tn_size;

	if (vp->v_type != VREG)
		return (EINVAL);

	if (uio->uio_resid == 0)
		return (0);

	if (ioflag & IO_APPEND) {
		uio->uio_offset = node->tn_size;
	}

	if (uio->uio_offset < 0 ||
	    (u_int64_t)uio->uio_offset + uio->uio_resid > LLONG_MAX)
		return (EFBIG);

	/* do the filesize rlimit check */
	if ((error = vn_fsizechk(vp, uio, ioflag, &overrun)))
		return (error);

	extended = uio->uio_offset + uio->uio_resid > node->tn_size;
	if (extended) {
		error = tmpfs_reg_resize(vp, uio->uio_offset + uio->uio_resid);
		if (error)
			goto out;
	}

	error = 0;
	while (error == 0 && uio->uio_resid > 0) {
		vsize_t len;
		uvm_vnp_uncache(vp);
		len = MIN(node->tn_size - uio->uio_offset, uio->uio_resid);
		if (len == 0) {
			break;
		}
		error = tmpfs_uiomove(node, uio, len);
	}
	if (error) {
		(void)tmpfs_reg_resize(vp, oldsize);
	}

	tmpfs_update(node, TMPFS_NODE_MODIFIED | TMPFS_NODE_CHANGED);
	if (extended)
		VN_KNOTE(vp, NOTE_WRITE | NOTE_EXTEND);
	else
		VN_KNOTE(vp, NOTE_WRITE);
out:
	if (error) {
		KASSERT(oldsize == node->tn_size);
	} else {
		KASSERT(uio->uio_resid == 0);

		/* correct the result for writes clamped by vn_fsizechk() */
		uio->uio_resid += overrun;

	}
	return error;
}

int
tmpfs_fsync(void *v)
{
#ifdef DIAGNOSTIC
	struct vop_fsync_args /* {
		struct vnode *a_vp;
		struct ucred *a_cred;
		int a_flags;
		off_t a_offlo;
		off_t a_offhi;
		struct lwp *a_l;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;

	/* Nothing to do.  Just update. */
	KASSERT(VOP_ISLOCKED(vp));
#endif
	return 0;
}

/*
 * tmpfs_remove: unlink a file.
 *
 * => Both directory (dvp) and file (vp) are locked.
 * => We unlock and drop the reference on both.
 */
int
tmpfs_remove(void *v)
{
	struct vop_remove_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp, *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	tmpfs_node_t *dnode, *node;
	tmpfs_dirent_t *de;
	int error;

	KASSERT(cnp->cn_flags & HASBUF);

	if (vp->v_type == VDIR) {
		error = EPERM;
		goto out;
	}

	dnode = VP_TO_TMPFS_NODE(dvp);
	node = VP_TO_TMPFS_NODE(vp);

	/* Files marked as immutable or append-only cannot be deleted. */
	if (node->tn_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}

	/*
	 * Likewise, files residing on directories marked as append-only cannot
	 * be deleted.
	 */
	if (dnode->tn_flags & APPEND) {
		error = EPERM;
		goto out;
	}

	/* Lookup the directory entry (check the cached hint first). */
	de = tmpfs_dir_cached(node);
	if (de == NULL) {
		de = tmpfs_dir_lookup(dnode, cnp);
	}

	KASSERT(de && de->td_node == node);

	/*
	 * Remove the entry from the directory (drops the link count) and
	 * destroy it.
	 * Note: the inode referred by it will not be destroyed
	 * until the vnode is reclaimed/recycled.
	 */
	tmpfs_dir_detach(dnode, de);
	tmpfs_free_dirent(VFS_TO_TMPFS(vp->v_mount), de);
	if (node->tn_links > 0)  {
		/* We removed a hard link. */
		tmpfs_update(node, TMPFS_NODE_CHANGED);
	}
	error = 0;
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return error;
}

/*
 * tmpfs_link: create a hard link.
 */
int
tmpfs_link(void *v)
{
	struct vop_link_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	tmpfs_node_t *dnode, *node;
	tmpfs_dirent_t *de;
	int error;

	KASSERT(VOP_ISLOCKED(dvp));
	KASSERT(dvp != vp);

	dnode = VP_TO_TMPFS_DIR(dvp);
	node = VP_TO_TMPFS_NODE(vp);

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/* Check for maximum number of links limit. */
	if (node->tn_links == LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	KASSERT(node->tn_links < LINK_MAX);

	/* We cannot create links of files marked immutable or append-only. */
	if (node->tn_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}

	if (TMPFS_DIRSEQ_FULL(dnode)) {
		error = ENOSPC;
		goto out;
	}

	/* Allocate a new directory entry to represent the inode. */
	error = tmpfs_alloc_dirent(VFS_TO_TMPFS(vp->v_mount),
	    cnp->cn_nameptr, cnp->cn_namelen, &de);
	if (error) {
		goto out;
	}

	/*
	 * Insert the entry into the directory.
	 * It will increase the inode link count.
	 */
	tmpfs_dir_attach(dnode, de, node);

	/* Update the timestamps and trigger the event. */
	if (node->tn_vnode) {
		VN_KNOTE(node->tn_vnode, NOTE_LINK);
	}
	tmpfs_update(node, TMPFS_NODE_CHANGED);
	error = 0;
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	VOP_UNLOCK(vp);
	vput(dvp);
	return error;
}

int
tmpfs_mkdir(void *v)
{
	struct vop_mkdir_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	int error;

	KASSERT(vap->va_type == VDIR);
	error = tmpfs_alloc_file(dvp, vpp, vap, cnp, NULL);
	vput(dvp);
	return error;
}

int
tmpfs_rmdir(void *v)
{
	struct vop_rmdir_args /* {
		struct vnode		*a_dvp;
		struct vnode		*a_vp;
		struct componentname	*a_cnp;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(dvp->v_mount);
	tmpfs_node_t *dnode = VP_TO_TMPFS_DIR(dvp);
	tmpfs_node_t *node = VP_TO_TMPFS_DIR(vp);
	tmpfs_dirent_t *de;
	int error = 0;

	KASSERT(VOP_ISLOCKED(dvp));
	KASSERT(VOP_ISLOCKED(vp));
	KASSERT(cnp->cn_flags & HASBUF);

	if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' &&
	    cnp->cn_nameptr[1] == '.') {
		error = ENOTEMPTY;
		goto out;
	}

	KASSERT(node->tn_spec.tn_dir.tn_parent == dnode);

	/*
	 * Directories with more than two entries ('.' and '..') cannot be
	 * removed.
	 */
	if (node->tn_size > 0) {
		KASSERT(error == 0);
		TAILQ_FOREACH(de, &node->tn_spec.tn_dir.tn_dir, td_entries) {
			error = ENOTEMPTY;
			break;
		}
		if (error)
			goto out;
	}

	/* Lookup the directory entry (check the cached hint first). */
	de = tmpfs_dir_cached(node);
	if (de == NULL)
		de = tmpfs_dir_lookup(dnode, cnp);

	KASSERT(de && de->td_node == node);

	/* Check flags to see if we are allowed to remove the directory. */
	if (dnode->tn_flags & APPEND || node->tn_flags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto out;
	}

	/* Decrement the link count for the virtual '.' entry. */
	node->tn_links--;
	tmpfs_update(node, TMPFS_NODE_STATUSALL);

	/* Detach the directory entry from the directory. */
	tmpfs_dir_detach(dnode, de);

	/* Purge the cache for parent. */
	cache_purge(dvp);

	/*
	 * Destroy the directory entry.
	 * Note: the inode referred by it will not be destroyed
	 * until the vnode is reclaimed.
	 */
	tmpfs_free_dirent(tmp, de);
	KASSERT(TAILQ_FIRST(&node->tn_spec.tn_dir.tn_dir) == NULL);

	KASSERT(node->tn_links == 0);
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	/* Release the nodes. */
	vput(dvp);
	vput(vp);
	return error;
}

int
tmpfs_symlink(void *v)
{
	struct vop_symlink_args /* {
		struct vnode		*a_dvp;
		struct vnode		**a_vpp;
		struct componentname	*a_cnp;
		struct vattr		*a_vap;
		char			*a_target;
	} */ *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	char *target = ap->a_target;
	int error;

	KASSERT(vap->va_type == 0);
	vap->va_type = VLNK;

	error = tmpfs_alloc_file(dvp, vpp, vap, cnp, target);
	vput(dvp);
	if (error == 0)
		vput(*vpp);

	return error;
}

int
tmpfs_readdir(void *v)
{
	struct vop_readdir_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		kauth_cred_t	a_cred;
		int		*a_eofflag;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int *eofflag = ap->a_eofflag;
	tmpfs_node_t *node;
	int error;

	KASSERT(VOP_ISLOCKED(vp));

	/* This operation only makes sense on directory nodes. */
	if (vp->v_type != VDIR) {
		return ENOTDIR;
	}
	node = VP_TO_TMPFS_DIR(vp);
	/*
	 * Retrieve the directory entries, unless it is being destroyed.
	 */
	if (node->tn_links) {
		error = tmpfs_dir_getdents(node, uio);
	} else {
		error = 0;
	}

	if (eofflag != NULL) {
		*eofflag = !error && uio->uio_offset == TMPFS_DIRSEQ_EOF;
	}
	return error;
}

int
tmpfs_readlink(void *v)
{
	struct vop_readlink_args /* {
		struct vnode	*a_vp;
		struct uio	*a_uio;
		kauth_cred_t	a_cred;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	tmpfs_node_t *node;
	int error;

	KASSERT(VOP_ISLOCKED(vp));
	KASSERT(uio->uio_offset == 0);
	KASSERT(vp->v_type == VLNK);

	node = VP_TO_TMPFS_NODE(vp);
	error = uiomove(node->tn_spec.tn_lnk.tn_link,
	    MIN((size_t)node->tn_size, uio->uio_resid), uio);

	if (!(vp->v_mount->mnt_flag & MNT_NOATIME))
		tmpfs_update(node, TMPFS_NODE_ACCESSED);

	return error;
}

int
tmpfs_inactive(void *v)
{
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		int *a_recycle;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	tmpfs_node_t *node;

	KASSERT(VOP_ISLOCKED(vp));

	node = VP_TO_TMPFS_NODE(vp);

	if (vp->v_type == VREG && tmpfs_uio_cached(node))
		tmpfs_uio_uncache(node);

	VOP_UNLOCK(vp);

	/*
	 * If we are done with the node, reclaim it so that it can be reused
	 * immediately.
	 */
	if (node->tn_links == 0)
		vrecycle(vp, curproc);

	return 0;
}

int
tmpfs_reclaim(void *v)
{
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	tmpfs_mount_t *tmp = VFS_TO_TMPFS(vp->v_mount);
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);
	int racing;

	/* Disassociate inode from vnode. */
	rw_enter_write(&node->tn_nlock);
	node->tn_vnode = NULL;
	vp->v_data = NULL;
	/* Check if tmpfs_vnode_get() is racing with us. */
	racing = TMPFS_NODE_RECLAIMING(node);
	rw_exit_write(&node->tn_nlock);

	cache_purge(vp);

	/*
	 * If inode is not referenced, i.e. no links, then destroy it.
	 * Note: if racing - inode is about to get a new vnode, leave it.
	 */
	if (node->tn_links == 0 && !racing) {
		tmpfs_free_node(tmp, node);
	}
	return 0;
}

int
tmpfs_pathconf(void *v)
{
	struct vop_pathconf_args /* {
		struct vnode	*a_vp;
		int		a_name;
		register_t	*a_retval;
	} */ *ap = v;
	const int name = ap->a_name;
	register_t *retval = ap->a_retval;
	int error = 0;

	switch (name) {
	case _PC_LINK_MAX:
		*retval = LINK_MAX;
		break;
	case _PC_NAME_MAX:
		*retval = TMPFS_MAXNAMLEN;
		break;
	case _PC_CHOWN_RESTRICTED:
		*retval = 1;
		break;
	case _PC_NO_TRUNC:
		*retval = 1;
		break;
	case _PC_FILESIZEBITS:
		*retval = 64;
		break;
	case _PC_TIMESTAMP_RESOLUTION:
		*retval = 1;
		break;
	default:
		error = EINVAL;
	}
	return error;
}

int
tmpfs_advlock(void *v)
{
	struct vop_advlock_args /* {
		struct vnode	*a_vp;
		void *		a_id;
		int		a_op;
		struct flock	*a_fl;
		int		a_flags;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);

	return lf_advlock(&node->tn_lockf, node->tn_size, ap->a_id, ap->a_op,
	    ap->a_fl, ap->a_flags);
}

int
tmpfs_print(void *v)
{
#if defined(DEBUG) || defined(DIAGNOSTIC) || defined(VFSLCKDEBUG)
	struct vop_print_args /* {
		struct vnode	*a_vp;
	} */ *ap = v;
	struct vnode *vp = ap->a_vp;
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);

	printf("tag VT_TMPFS, tmpfs_node %p, flags 0x%x, links %d\n"
	    "\tmode 0%o, owner %d, group %d, size %lld",
	    node, node->tn_flags, node->tn_links, node->tn_mode, node->tn_uid,
	    node->tn_gid, node->tn_size);
#ifdef FIFO
	if (vp->v_type == VFIFO)
		fifo_printinfo(vp);
#endif
	printf("\n");
#endif
	return 0;
}

/* a null op */
int
tmpfs_bwrite(void *v)
{
	return 0;
}

int
tmpfs_strategy(void *v)
{
	return EOPNOTSUPP;
}

int
tmpfs_ioctl(void *v)
{
	return ENOTTY;
}

int
tmpfs_lock(void *v)
{
	struct vop_lock_args *ap = v;
	tmpfs_node_t *tnp = VP_TO_TMPFS_NODE(ap->a_vp);

	return rrw_enter(&tnp->tn_vlock, ap->a_flags & LK_RWFLAGS);
}

int
tmpfs_unlock(void *v)
{
	struct vop_unlock_args *ap = v;
	tmpfs_node_t *tnp = VP_TO_TMPFS_NODE(ap->a_vp);

	rrw_exit(&tnp->tn_vlock);
	return 0;
}

int
tmpfs_islocked(void *v)
{
	struct vop_islocked_args *ap = v;
	tmpfs_node_t *tnp = VP_TO_TMPFS_NODE(ap->a_vp);

	return rrw_status(&tnp->tn_vlock);
}

/*
 * tmpfs_rename: rename routine, the hairiest system call, with the
 * insane API.
 *
 * Arguments: fdvp (from-parent vnode), fvp (from-leaf), tdvp (to-parent)
 * and tvp (to-leaf), if exists (NULL if not).
 *
 * => Caller holds a reference on fdvp and fvp, they are unlocked.
 *    Note: fdvp and fvp can refer to the same object (i.e. when it is root).
 *
 * => Both tdvp and tvp are referenced and locked.  It is our responsibility
 *    to release the references and unlock them (or destroy).
 */

/*
 * First, some forward declarations of subroutines.
 */

int tmpfs_sane_rename(struct vnode *, struct componentname *,
    struct vnode *, struct componentname *, struct ucred *, int);
int tmpfs_rename_enter(struct mount *, struct tmpfs_mount *,
    struct ucred *,
    struct vnode *, struct tmpfs_node *, struct componentname *,
    struct tmpfs_dirent **, struct vnode **,
    struct vnode *, struct tmpfs_node *, struct componentname *,
    struct tmpfs_dirent **, struct vnode **);
int tmpfs_rename_enter_common(struct mount *, struct tmpfs_mount *,
    struct ucred *,
    struct vnode *, struct tmpfs_node *,
    struct componentname *, struct tmpfs_dirent **, struct vnode **,
    struct componentname *, struct tmpfs_dirent **, struct vnode **);
int tmpfs_rename_enter_separate(struct mount *, struct tmpfs_mount *,
    struct ucred *,
    struct vnode *, struct tmpfs_node *, struct componentname *,
    struct tmpfs_dirent **, struct vnode **,
    struct vnode *, struct tmpfs_node *, struct componentname *,
    struct tmpfs_dirent **, struct vnode **);
void tmpfs_rename_exit(struct tmpfs_mount *,
    struct vnode *, struct vnode *, struct vnode *, struct vnode *);
int tmpfs_rename_lock_directory(struct vnode *, struct tmpfs_node *);
int tmpfs_rename_genealogy(struct tmpfs_node *, struct tmpfs_node *,
    struct tmpfs_node **);
int tmpfs_rename_lock(struct mount *, struct ucred *, int,
    struct vnode *, struct tmpfs_node *, struct componentname *, int,
    struct tmpfs_dirent **, struct vnode **,
    struct vnode *, struct tmpfs_node *, struct componentname *, int,
    struct tmpfs_dirent **, struct vnode **);
void tmpfs_rename_attachdetach(struct tmpfs_mount *,
    struct vnode *, struct tmpfs_dirent *, struct vnode *,
    struct vnode *, struct tmpfs_dirent *, struct vnode *);
int tmpfs_do_remove(struct tmpfs_mount *, struct vnode *,
    struct tmpfs_node *, struct tmpfs_dirent *, struct vnode *, struct ucred *);
int tmpfs_rename_check_possible(struct tmpfs_node *,
    struct tmpfs_node *, struct tmpfs_node *, struct tmpfs_node *);
int tmpfs_rename_check_permitted(struct ucred *,
    struct tmpfs_node *, struct tmpfs_node *,
    struct tmpfs_node *, struct tmpfs_node *);
int tmpfs_remove_check_possible(struct tmpfs_node *,
    struct tmpfs_node *);
int tmpfs_remove_check_permitted(struct ucred *,
    struct tmpfs_node *, struct tmpfs_node *);
int tmpfs_check_sticky(struct ucred *,
    struct tmpfs_node *, struct tmpfs_node *);
void tmpfs_rename_cache_purge(struct vnode *, struct vnode *, struct vnode *,
    struct vnode *);
void tmpfs_rename_abort(void *);

int
tmpfs_rename(void *v)
{
	struct vop_rename_args  /* {
		struct vnode		*a_fdvp;
		struct vnode		*a_fvp;
		struct componentname	*a_fcnp;
		struct vnode		*a_tdvp;
		struct vnode		*a_tvp;
		struct componentname	*a_tcnp;
	} */ *ap = v;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *fvp = ap->a_fvp;
	struct componentname *fcnp = ap->a_fcnp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *tvp = ap->a_tvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct ucred *cred;
	int error;

	KASSERT(fdvp != NULL);
	KASSERT(fvp != NULL);
	KASSERT(fcnp != NULL);
	KASSERT(fcnp->cn_nameptr != NULL);
	KASSERT(tdvp != NULL);
	KASSERT(tcnp != NULL);
	KASSERT(fcnp->cn_nameptr != NULL);
	/* KASSERT(VOP_ISLOCKED(fdvp) != LK_EXCLUSIVE); */
	/* KASSERT(VOP_ISLOCKED(fvp) != LK_EXCLUSIVE); */
	KASSERT(fdvp->v_type == VDIR);
	KASSERT(tdvp->v_type == VDIR);
	KASSERT(fcnp->cn_flags & HASBUF);
	KASSERT(tcnp->cn_flags & HASBUF);

	cred = fcnp->cn_cred;
	KASSERT(tcnp->cn_cred == cred);

	/*
	 * Check for cross-device rename.
	 * Also don't allow renames of mount points.
	 */
	if (fvp->v_mount != tdvp->v_mount ||
	    fdvp->v_mount != fvp->v_mount ||
	    (tvp != NULL && (fvp->v_mount != tvp->v_mount))) {
	    	tmpfs_rename_abort(v);
		return EXDEV;
	}

	/*
	 * Can't check the locks on these until we know they're on
	 * the same FS, as not all FS do locking the same way.
	 */
	KASSERT(VOP_ISLOCKED(tdvp) == LK_EXCLUSIVE);
	KASSERT((tvp == NULL) || (VOP_ISLOCKED(tvp) == LK_EXCLUSIVE));

	/*
	 * Reject renaming '.' and '..'.
	 */
	if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
	    (fcnp->cn_namelen == 2 && fcnp->cn_nameptr[0] == '.' &&
	    fcnp->cn_nameptr[1] == '.')) {
		tmpfs_rename_abort(v);
		return EINVAL;
	}

	/*
	 * Sanitize our world from the VFS insanity.  Unlock the target
	 * directory and node, which are locked.  Release the children,
	 * which are referenced.  Check for rename("x", "y/."), which
	 * it is our responsibility to reject, not the caller's.  (But
	 * the caller does reject rename("x/.", "y").  Go figure.)
	 */

	VOP_UNLOCK(tdvp);
	if ((tvp != NULL) && (tvp != tdvp))
		VOP_UNLOCK(tvp);

	vrele(fvp);
	if (tvp != NULL)
		vrele(tvp);

	if (tvp == tdvp) {
		error = EINVAL;
		goto out;
	}

	error = tmpfs_sane_rename(fdvp, fcnp, tdvp, tcnp, cred, 0);

out:	/*
	 * All done, whether with success or failure.  Release the
	 * directory nodes now, as the caller expects from the VFS
	 * protocol.
	 */
	vrele(fdvp);
	vrele(tdvp);

	return error;
}

/*
 * tmpfs_sane_rename: rename routine, the hairiest system call, with
 * the sane API.
 *
 * Arguments:
 *
 * . fdvp (from directory vnode),
 * . fcnp (from component name),
 * . tdvp (to directory vnode), and
 * . tcnp (to component name).
 *
 * fdvp and tdvp must be referenced and unlocked.
 */
int
tmpfs_sane_rename(struct vnode *fdvp, struct componentname *fcnp,
    struct vnode *tdvp, struct componentname *tcnp, struct ucred *cred,
    int posixly_correct)
{
	struct mount *mount;
	struct tmpfs_mount *tmpfs;
	struct tmpfs_node *fdnode, *tdnode;
	struct tmpfs_dirent *fde, *tde;
	struct vnode *fvp, *tvp;
	char *newname;
	int error;

	KASSERT(fdvp != NULL);
	KASSERT(fcnp != NULL);
	KASSERT(tdvp != NULL);
	KASSERT(tcnp != NULL);
	/* KASSERT(VOP_ISLOCKED(fdvp) != LK_EXCLUSIVE); */
	/* KASSERT(VOP_ISLOCKED(tdvp) != LK_EXCLUSIVE); */
	KASSERT(fdvp->v_type == VDIR);
	KASSERT(tdvp->v_type == VDIR);
	KASSERT(fdvp->v_mount == tdvp->v_mount);
	KASSERT((fcnp->cn_flags & ISDOTDOT) == 0);
	KASSERT((tcnp->cn_flags & ISDOTDOT) == 0);
	KASSERT((fcnp->cn_namelen != 1) || (fcnp->cn_nameptr[0] != '.'));
	KASSERT((tcnp->cn_namelen != 1) || (tcnp->cn_nameptr[0] != '.'));
	KASSERT((fcnp->cn_namelen != 2) || (fcnp->cn_nameptr[0] != '.') ||
	    (fcnp->cn_nameptr[1] != '.'));
	KASSERT((tcnp->cn_namelen != 2) || (tcnp->cn_nameptr[0] != '.') ||
	    (tcnp->cn_nameptr[1] != '.'));

	/*
	 * Pull out the tmpfs data structures.
	 */
	fdnode = VP_TO_TMPFS_NODE(fdvp);
	tdnode = VP_TO_TMPFS_NODE(tdvp);
	KASSERT(fdnode != NULL);
	KASSERT(tdnode != NULL);
	KASSERT(fdnode->tn_vnode == fdvp);
	KASSERT(tdnode->tn_vnode == tdvp);
	KASSERT(fdnode->tn_type == VDIR);
	KASSERT(tdnode->tn_type == VDIR);

	mount = fdvp->v_mount;
	KASSERT(mount != NULL);
	KASSERT(mount == tdvp->v_mount);
	/* XXX How can we be sure this stays true?  (Not that you're
	 * likely to mount a tmpfs read-only...)  */
	KASSERT((mount->mnt_flag & MNT_RDONLY) == 0);
	tmpfs = VFS_TO_TMPFS(mount);
	KASSERT(tmpfs != NULL);

	/*
	 * Decide whether we need a new name, and allocate memory for
	 * it if so.  Do this before locking anything or taking
	 * destructive actions so that we can back out safely and sleep
	 * safely.  XXX Is sleeping an issue here?  Can this just be
	 * moved into tmpfs_rename_attachdetach?
	 */
	if (tmpfs_strname_neqlen(fcnp, tcnp)) {
		newname = tmpfs_strname_alloc(tmpfs, tcnp->cn_namelen);
		if (newname == NULL) {
			error = ENOSPC;
			goto out_unlocked;
		}
	} else {
		newname = NULL;
	}

	/*
	 * Lock and look up everything.  GCC is not very clever.
	 */
	fde = tde = NULL;
	fvp = tvp = NULL;
	error = tmpfs_rename_enter(mount, tmpfs, cred,
	    fdvp, fdnode, fcnp, &fde, &fvp,
	    tdvp, tdnode, tcnp, &tde, &tvp);
	if (error)
		goto out_unlocked;

	/*
	 * Check that everything is locked and looks right.
	 */
	KASSERT(fde != NULL);
	KASSERT(fvp != NULL);
	KASSERT(fde->td_node != NULL);
	KASSERT(fde->td_node->tn_vnode == fvp);
	KASSERT(fde->td_node->tn_type == fvp->v_type);
	KASSERT((tde == NULL) == (tvp == NULL));
	KASSERT((tde == NULL) || (tde->td_node != NULL));
	KASSERT((tde == NULL) || (tde->td_node->tn_vnode == tvp));
	KASSERT((tde == NULL) || (tde->td_node->tn_type == tvp->v_type));
	KASSERT(VOP_ISLOCKED(fdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(fvp) == LK_EXCLUSIVE);
	KASSERT((tvp == NULL) || (VOP_ISLOCKED(tvp) == LK_EXCLUSIVE));

	/*
	 * If the source and destination are the same object, we need
	 * only at most delete the source entry.
	 */
	if (fvp == tvp) {
		KASSERT(tvp != NULL);
		if (fde->td_node->tn_type == VDIR) {
			/* XXX How can this possibly happen?  */
			error = EINVAL;
			goto out_locked;
		}
		if (!posixly_correct && (fde != tde)) {
			/* XXX Doesn't work because of locking.
			 * error = VOP_REMOVE(fdvp, fvp);
			 */
			error = tmpfs_do_remove(tmpfs, fdvp, fdnode, fde, fvp,
			    cred);
			if (error)
				goto out_locked;
		}
		goto success;
	}
	KASSERT(fde != tde);
	KASSERT(fvp != tvp);

	/*
	 * If the target exists, refuse to rename a directory over a
	 * non-directory or vice versa, or to clobber a non-empty
	 * directory.
	 */
	if (tvp != NULL) {
		KASSERT(tde != NULL);
		KASSERT(tde->td_node != NULL);
		if (fvp->v_type == VDIR && tvp->v_type == VDIR)
			error = ((tde->td_node->tn_size > 0)? ENOTEMPTY : 0);
		else if (fvp->v_type == VDIR && tvp->v_type != VDIR)
			error = ENOTDIR;
		else if (fvp->v_type != VDIR && tvp->v_type == VDIR)
			error = EISDIR;
		else
			error = 0;
		if (error)
			goto out_locked;
		KASSERT((fvp->v_type == VDIR) == (tvp->v_type == VDIR));
	}

	/*
	 * Authorize the rename.
	 */
	error = tmpfs_rename_check_possible(fdnode, fde->td_node,
	    tdnode, (tde? tde->td_node : NULL));
	if (error)
		goto out_locked;
	error = tmpfs_rename_check_permitted(cred, fdnode, fde->td_node,
	    tdnode, (tde? tde->td_node : NULL));
	if (error)
		goto out_locked;

	/*
	 * Everything is hunky-dory.  Shuffle the directory entries.
	 */
	tmpfs_rename_attachdetach(tmpfs, fdvp, fde, fvp, tdvp, tde, tvp);

	/*
	 * Update the directory entry's name necessary, and flag
	 * metadata updates.  A memory allocation failure here is not
	 * OK because we've already committed some changes that we
	 * can't back out at this point, and we have things locked so
	 * we can't sleep, hence the early allocation above.
	 */
	if (newname != NULL) {
		KASSERT(tcnp->cn_namelen <= TMPFS_MAXNAMLEN);

		tmpfs_strname_free(tmpfs, fde->td_name, fde->td_namelen);
		fde->td_namelen = (uint16_t)tcnp->cn_namelen;
		(void)memcpy(newname, tcnp->cn_nameptr, tcnp->cn_namelen);
		/* Commit newname and don't free it on the way out.  */
		fde->td_name = newname;
		newname = NULL;

		tmpfs_update(fde->td_node, TMPFS_NODE_CHANGED);
		tmpfs_update(tdnode, TMPFS_NODE_MODIFIED);
	}

success:
	VN_KNOTE(fvp, NOTE_RENAME);
	tmpfs_rename_cache_purge(fdvp, fvp, tdvp, tvp);
	error = 0;

out_locked:
	tmpfs_rename_exit(tmpfs, fdvp, fvp, tdvp, tvp);

out_unlocked:
	/* KASSERT(VOP_ISLOCKED(fdvp) != LK_EXCLUSIVE); */
	/* KASSERT(VOP_ISLOCKED(tdvp) != LK_EXCLUSIVE); */
	/* KASSERT((fvp == NULL) || (VOP_ISLOCKED(fvp) != LK_EXCLUSIVE)); */
	/* KASSERT((tvp == NULL) || (VOP_ISLOCKED(tvp) != LK_EXCLUSIVE)); */

	if (newname != NULL)
		tmpfs_strname_free(tmpfs, newname, tcnp->cn_namelen);

	return error;
}

/*
 * Look up fcnp in fdnode/fdvp and store its directory entry in fde_ret
 * and the associated vnode in fvp_ret; fail if not found.  Look up
 * tcnp in tdnode/tdvp and store its directory entry in tde_ret and the
 * associated vnode in tvp_ret; store null instead if not found.  Fail
 * if anything has been mounted on any of the nodes involved.
 *
 * fdvp and tdvp must be referenced.
 *
 * On entry, nothing is locked.
 *
 * On success, everything is locked, and *fvp_ret, and *tvp_ret if
 * nonnull, are referenced.  The only pairs of vnodes that may be
 * identical are {fdvp, tdvp} and {fvp, tvp}.
 *
 * On failure, everything remains as was.
 *
 * Locking everything including the source and target nodes is
 * necessary to make sure that, e.g., link count updates are OK.  The
 * locking order is, in general, ancestor-first, matching the order you
 * need to use to look up a descendant anyway.
 */
int
tmpfs_rename_enter(struct mount *mount, struct tmpfs_mount *tmpfs,
    struct ucred *cred,
    struct vnode *fdvp, struct tmpfs_node *fdnode, struct componentname *fcnp,
    struct tmpfs_dirent **fde_ret, struct vnode **fvp_ret,
    struct vnode *tdvp, struct tmpfs_node *tdnode, struct componentname *tcnp,
    struct tmpfs_dirent **tde_ret, struct vnode **tvp_ret)
{
	int error;

	KASSERT(mount != NULL);
	KASSERT(tmpfs != NULL);
	KASSERT(fdvp != NULL);
	KASSERT(fdnode != NULL);
	KASSERT(fcnp != NULL);
	KASSERT(fde_ret != NULL);
	KASSERT(fvp_ret != NULL);
	KASSERT(tdvp != NULL);
	KASSERT(tdnode != NULL);
	KASSERT(tcnp != NULL);
	KASSERT(tde_ret != NULL);
	KASSERT(tvp_ret != NULL);
	KASSERT(fdnode->tn_vnode == fdvp);
	KASSERT(tdnode->tn_vnode == tdvp);
	KASSERT(fdnode->tn_type == VDIR);
	KASSERT(tdnode->tn_type == VDIR);

	if (fdvp == tdvp) {
		KASSERT(fdnode == tdnode);
		error = tmpfs_rename_enter_common(mount, tmpfs, cred, fdvp,
		    fdnode, fcnp, fde_ret, fvp_ret, tcnp, tde_ret, tvp_ret);
	} else {
		KASSERT(fdnode != tdnode);
		error = tmpfs_rename_enter_separate(mount, tmpfs, cred,
		    fdvp, fdnode, fcnp, fde_ret, fvp_ret,
		    tdvp, tdnode, tcnp, tde_ret, tvp_ret);
	}

	if (error)
		return error;

	KASSERT(*fde_ret != NULL);
	KASSERT(*fvp_ret != NULL);
	KASSERT((*tde_ret == NULL) == (*tvp_ret == NULL));
	KASSERT((*tde_ret == NULL) || ((*tde_ret)->td_node != NULL));
	KASSERT((*tde_ret == NULL) ||
	    ((*tde_ret)->td_node->tn_vnode == *tvp_ret));
	KASSERT(VOP_ISLOCKED(fdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(*fvp_ret) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdvp) == LK_EXCLUSIVE);
	KASSERT((*tvp_ret == NULL) ||
	    (VOP_ISLOCKED(*tvp_ret) == LK_EXCLUSIVE));
	KASSERT(*fvp_ret != fdvp);
	KASSERT(*fvp_ret != tdvp);
	KASSERT(*tvp_ret != fdvp);
	KASSERT(*tvp_ret != tdvp);
	return 0;
}

/*
 * Lock and look up with a common source/target directory.
 */
int
tmpfs_rename_enter_common(struct mount *mount, struct tmpfs_mount *tmpfs,
    struct ucred *cred,
    struct vnode *dvp, struct tmpfs_node *dnode,
    struct componentname *fcnp,
    struct tmpfs_dirent **fde_ret, struct vnode **fvp_ret,
    struct componentname *tcnp,
    struct tmpfs_dirent **tde_ret, struct vnode **tvp_ret)
{
	struct tmpfs_dirent *fde, *tde;
	struct vnode *fvp, *tvp;
	int error;

	error = tmpfs_rename_lock_directory(dvp, dnode);
	if (error)
		goto fail0;

	/* Did we lose a race with mount?  */
	if (dvp->v_mountedhere != NULL) {
		error = EBUSY;
		goto fail1;
	}

	/* Make sure the caller may read the directory.  */
	error = VOP_ACCESS(dvp, VEXEC, cred, curproc);
	if (error)
		goto fail1;

	/*
	 * The order in which we lock the source and target nodes is
	 * irrelevant because there can only be one rename on this
	 * directory in flight at a time, and we have it locked.
	 */

	fde = tmpfs_dir_lookup(dnode, fcnp);
	if (fde == NULL) {
		error = ENOENT;
		goto fail1;
	}

	KASSERT(fde->td_node != NULL);
	/* We ruled out `.' earlier.  */
	KASSERT(fde->td_node != dnode);
	/* We ruled out `..' earlier.  */
	KASSERT(fde->td_node != dnode->tn_spec.tn_dir.tn_parent);
	rw_enter_write(&fde->td_node->tn_nlock);
	error = tmpfs_vnode_get(mount, fde->td_node, &fvp);
	if (error)
		goto fail1;
	KASSERT(fvp != NULL);
	KASSERT(VOP_ISLOCKED(fvp) == LK_EXCLUSIVE);
	KASSERT(fvp != dvp);
	KASSERT(fvp->v_mount == mount);

	/* Refuse to rename a mount point.  */
	if ((fvp->v_type == VDIR) && (fvp->v_mountedhere != NULL)) {
		error = EBUSY;
		goto fail2;
	}

	tde = tmpfs_dir_lookup(dnode, tcnp);
	if (tde == NULL) {
		tvp = NULL;
	} else {
		KASSERT(tde->td_node != NULL);
		/* We ruled out `.' earlier.  */
		KASSERT(tde->td_node != dnode);
		/* We ruled out `..' earlier.  */
		KASSERT(tde->td_node != dnode->tn_spec.tn_dir.tn_parent);
		if (tde->td_node != fde->td_node) {
			rw_enter_write(&tde->td_node->tn_nlock);
			error = tmpfs_vnode_get(mount, tde->td_node, &tvp);
			if (error)
				goto fail2;
			KASSERT(tvp->v_mount == mount);
			/* Refuse to rename over a mount point.  */
			if ((tvp->v_type == VDIR) &&
			    (tvp->v_mountedhere != NULL)) {
				error = EBUSY;
				goto fail3;
			}
		} else {
			tvp = fvp;
			vref(tvp);
		}
		KASSERT(tvp != NULL);
		KASSERT(VOP_ISLOCKED(tvp) == LK_EXCLUSIVE);
	}
	KASSERT(tvp != dvp);

	*fde_ret = fde;
	*fvp_ret = fvp;
	*tde_ret = tde;
	*tvp_ret = tvp;
	return 0;

fail3:	if (tvp != NULL) {
		if (tvp != fvp)
			vput(tvp);
		else
			vrele(tvp);
	}

fail2:	vput(fvp);
fail1:	VOP_UNLOCK(dvp);
fail0:	return error;
}

/*
 * Lock and look up with separate source and target directories.
 */
int
tmpfs_rename_enter_separate(struct mount *mount, struct tmpfs_mount *tmpfs,
    struct ucred *cred,
    struct vnode *fdvp, struct tmpfs_node *fdnode, struct componentname *fcnp,
    struct tmpfs_dirent **fde_ret, struct vnode **fvp_ret,
    struct vnode *tdvp, struct tmpfs_node *tdnode, struct componentname *tcnp,
    struct tmpfs_dirent **tde_ret, struct vnode **tvp_ret)
{
	struct tmpfs_node *intermediate_node;
	struct tmpfs_dirent *fde, *tde;
	struct vnode *fvp, *tvp;
	int error;

	KASSERT(fdvp != tdvp);
	KASSERT(fdnode != tdnode);

#if 0				/* XXX */
	mutex_enter(&tmpfs->tm_rename_lock);
#endif

	error = tmpfs_rename_genealogy(fdnode, tdnode, &intermediate_node);
	if (error)
		goto fail;

	/*
	 * intermediate_node == NULL means fdnode is not an ancestor of
	 * tdnode.
	 */
	if (intermediate_node == NULL)
		error = tmpfs_rename_lock(mount, cred, ENOTEMPTY,
		    tdvp, tdnode, tcnp, 1, &tde, &tvp,
		    fdvp, fdnode, fcnp, 0, &fde, &fvp);
	else
		error = tmpfs_rename_lock(mount, cred, EINVAL,
		    fdvp, fdnode, fcnp, 0, &fde, &fvp,
		    tdvp, tdnode, tcnp, 1, &tde, &tvp);
	if (error)
		goto fail;

	KASSERT(fde != NULL);
	KASSERT(fde->td_node != NULL);

	/*
	 * Reject rename("foo/bar", "foo/bar/baz/quux/zot").
	 */
	if (fde->td_node == intermediate_node) {
		tmpfs_rename_exit(tmpfs, fdvp, fvp, tdvp, tvp);
		return EINVAL;
	}

	*fde_ret = fde;
	*fvp_ret = fvp;
	*tde_ret = tde;
	*tvp_ret = tvp;
	return 0;

fail:
#if 0				/* XXX */
	mutex_exit(&tmpfs->tm_rename_lock);
#endif
	return error;
}

/*
 * Unlock everything we locked for rename.
 *
 * fdvp and tdvp must be referenced.
 *
 * On entry, everything is locked, and fvp and tvp referenced.
 *
 * On exit, everything is unlocked, and fvp and tvp are released.
 */
void
tmpfs_rename_exit(struct tmpfs_mount *tmpfs,
    struct vnode *fdvp, struct vnode *fvp,
    struct vnode *tdvp, struct vnode *tvp)
{

	KASSERT(tmpfs != NULL);
	KASSERT(fdvp != NULL);
	KASSERT(fvp != NULL);
	KASSERT(fdvp != fvp);
	KASSERT(fdvp != tvp);
	KASSERT(tdvp != tvp);
	KASSERT(tdvp != fvp);
	KASSERT(VOP_ISLOCKED(fdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(fvp) == LK_EXCLUSIVE);
	KASSERT((tvp == NULL) || (VOP_ISLOCKED(tvp) == LK_EXCLUSIVE));

	if (tvp != NULL) {
		if (tvp != fvp)
			vput(tvp);
		else
			vrele(tvp);
	}
	VOP_UNLOCK(tdvp);
	vput(fvp);
	if (fdvp != tdvp)
		VOP_UNLOCK(fdvp);

#if 0				/* XXX */
	if (fdvp != tdvp)
		mutex_exit(&tmpfs->tm_rename_lock);
#endif
}

/*
 * Lock a directory, but fail if it has been rmdir'd.
 *
 * vp must be referenced.
 */
int
tmpfs_rename_lock_directory(struct vnode *vp, struct tmpfs_node *node)
{

	KASSERT(vp != NULL);
	KASSERT(node != NULL);
	KASSERT(node->tn_vnode == vp);
	KASSERT(node->tn_type == VDIR);

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	if (node->tn_spec.tn_dir.tn_parent == NULL) {
		VOP_UNLOCK(vp);
		return ENOENT;
	}

	return 0;
}

/*
 * Analyze the genealogy of the source and target nodes.
 *
 * On success, stores in *intermediate_node_ret either the child of
 * fdnode of which tdnode is a descendant, or null if tdnode is not a
 * descendant of fdnode at all.
 *
 * fdnode and tdnode must be unlocked and referenced.  The file
 * system's rename lock must also be held, to exclude concurrent
 * changes to the file system's genealogy other than rmdir.
 *
 * XXX This causes an extra lock/unlock of tdnode in the case when
 * we're just about to lock it again before locking anything else.
 * However, changing that requires reorganizing the code to make it
 * even more horrifically obscure.
 */
int
tmpfs_rename_genealogy(struct tmpfs_node *fdnode, struct tmpfs_node *tdnode,
    struct tmpfs_node **intermediate_node_ret)
{
	struct tmpfs_node *node = tdnode, *parent;
	int error;

	KASSERT(fdnode != NULL);
	KASSERT(tdnode != NULL);
	KASSERT(fdnode != tdnode);
	KASSERT(intermediate_node_ret != NULL);

	KASSERT(fdnode->tn_vnode != NULL);
	KASSERT(tdnode->tn_vnode != NULL);
	KASSERT(fdnode->tn_type == VDIR);
	KASSERT(tdnode->tn_type == VDIR);

	/*
	 * We need to provisionally lock tdnode->tn_vnode to keep rmdir
	 * from deleting it -- or any ancestor -- at an inopportune
	 * moment.
	 */
	error = tmpfs_rename_lock_directory(tdnode->tn_vnode, tdnode);
	if (error)
		return error;

	for (;;) {
		parent = node->tn_spec.tn_dir.tn_parent;
		KASSERT(parent != NULL);
		KASSERT(parent->tn_type == VDIR);

		/* Did we hit the root without finding fdnode?  */
		if (parent == node) {
			*intermediate_node_ret = NULL;
			break;
		}

		/* Did we find that fdnode is an ancestor?  */
		if (parent == fdnode) {
			*intermediate_node_ret = node;
			break;
		}

		/* Neither -- keep ascending the family tree.  */
		node = parent;
	}

	VOP_UNLOCK(tdnode->tn_vnode);
	return 0;
}

/*
 * Lock directories a and b, which must be distinct, and look up and
 * lock nodes a and b.  Do a first and then b.  Directory b may not be
 * an ancestor of directory a, although directory a may be an ancestor
 * of directory b.  Fail with overlap_error if node a is directory b.
 * Neither componentname may be `.' or `..'.
 *
 * a_dvp and b_dvp must be referenced.
 *
 * On entry, a_dvp and b_dvp are unlocked.
 *
 * On success,
 * . a_dvp and b_dvp are locked,
 * . *a_dirent_ret is filled with a directory entry whose node is
 *     locked and referenced,
 * . *b_vp_ret is filled with the corresponding vnode,
 * . *b_dirent_ret is filled either with null or with a directory entry
 *     whose node is locked and referenced,
 * . *b_vp is filled either with null or with the corresponding vnode,
 *     and
 * . the only pair of vnodes that may be identical is a_vp and b_vp.
 *
 * On failure, a_dvp and b_dvp are left unlocked, and *a_dirent_ret,
 * *a_vp, *b_dirent_ret, and *b_vp are left alone.
 */
int
tmpfs_rename_lock(struct mount *mount, struct ucred *cred, int overlap_error,
    struct vnode *a_dvp, struct tmpfs_node *a_dnode,
    struct componentname *a_cnp, int a_missing_ok,
    struct tmpfs_dirent **a_dirent_ret, struct vnode **a_vp_ret,
    struct vnode *b_dvp, struct tmpfs_node *b_dnode,
    struct componentname *b_cnp, int b_missing_ok,
    struct tmpfs_dirent **b_dirent_ret, struct vnode **b_vp_ret)
{
	struct tmpfs_dirent *a_dirent, *b_dirent;
	struct vnode *a_vp, *b_vp;
	int error;

	KASSERT(a_dvp != NULL);
	KASSERT(a_dnode != NULL);
	KASSERT(a_cnp != NULL);
	KASSERT(a_dirent_ret != NULL);
	KASSERT(a_vp_ret != NULL);
	KASSERT(b_dvp != NULL);
	KASSERT(b_dnode != NULL);
	KASSERT(b_cnp != NULL);
	KASSERT(b_dirent_ret != NULL);
	KASSERT(b_vp_ret != NULL);
	KASSERT(a_dvp != b_dvp);
	KASSERT(a_dnode != b_dnode);
	KASSERT(a_dnode->tn_vnode == a_dvp);
	KASSERT(b_dnode->tn_vnode == b_dvp);
	KASSERT(a_dnode->tn_type == VDIR);
	KASSERT(b_dnode->tn_type == VDIR);
	KASSERT(a_missing_ok != b_missing_ok);

	error = tmpfs_rename_lock_directory(a_dvp, a_dnode);
	if (error)
		goto fail0;

	/* Did we lose a race with mount?  */
	if (a_dvp->v_mountedhere != NULL) {
		error = EBUSY;
		goto fail1;
	}

	/* Make sure the caller may read the directory.  */
	error = VOP_ACCESS(a_dvp, VEXEC, cred, curproc);
	if (error)
		goto fail1;

	a_dirent = tmpfs_dir_lookup(a_dnode, a_cnp);
	if (a_dirent != NULL) {
		KASSERT(a_dirent->td_node != NULL);
		/* We ruled out `.' earlier.  */
		KASSERT(a_dirent->td_node != a_dnode);
		/* We ruled out `..' earlier.  */
		KASSERT(a_dirent->td_node !=
		    a_dnode->tn_spec.tn_dir.tn_parent);
		if (a_dirent->td_node == b_dnode) {
			error = overlap_error;
			goto fail1;
		}
		rw_enter_write(&a_dirent->td_node->tn_nlock);
		error = tmpfs_vnode_get(mount, a_dirent->td_node, &a_vp);
		if (error)
			goto fail1;
		KASSERT(a_vp->v_mount == mount);
		/* Refuse to rename (over) a mount point.  */
		if ((a_vp->v_type == VDIR) && (a_vp->v_mountedhere != NULL)) {
			error = EBUSY;
			goto fail2;
		}
	} else if (!a_missing_ok) {
		error = ENOENT;
		goto fail1;
	} else {
		a_vp = NULL;
	}
	KASSERT(a_vp != a_dvp);
	KASSERT(a_vp != b_dvp);

	error = tmpfs_rename_lock_directory(b_dvp, b_dnode);
	if (error)
		goto fail2;

	/* Did we lose a race with mount?  */
	if (b_dvp->v_mountedhere != NULL) {
		error = EBUSY;
		goto fail3;
	}

	/* Make sure the caller may read the directory.  */
	error = VOP_ACCESS(b_dvp, VEXEC, cred, curproc);
	if (error)
		goto fail3;

	b_dirent = tmpfs_dir_lookup(b_dnode, b_cnp);
	if (b_dirent != NULL) {
		KASSERT(b_dirent->td_node != NULL);
		/* We ruled out `.' earlier.  */
		KASSERT(b_dirent->td_node != b_dnode);
		/* We ruled out `..' earlier.  */
		KASSERT(b_dirent->td_node !=
		    b_dnode->tn_spec.tn_dir.tn_parent);
		/* b is not an ancestor of a.  */
		KASSERT(b_dirent->td_node != a_dnode);
		/* But the source and target nodes might be the same.  */
		if ((a_dirent == NULL) ||
		    (a_dirent->td_node != b_dirent->td_node)) {
			rw_enter_write(&b_dirent->td_node->tn_nlock);
			error = tmpfs_vnode_get(mount, b_dirent->td_node,
			    &b_vp);
			if (error)
				goto fail3;
			KASSERT(b_vp->v_mount == mount);
			KASSERT(a_vp != b_vp);
			/* Refuse to rename (over) a mount point.  */
			if ((b_vp->v_type == VDIR) &&
			    (b_vp->v_mountedhere != NULL)) {
				error = EBUSY;
				goto fail4;
			}
		} else {
			b_vp = a_vp;
			vref(b_vp);
		}
	} else if (!b_missing_ok) {
		error = ENOENT;
		goto fail3;
	} else {
		b_vp = NULL;
	}
	KASSERT(b_vp != a_dvp);
	KASSERT(b_vp != b_dvp);

	KASSERT(VOP_ISLOCKED(a_dvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(b_dvp) == LK_EXCLUSIVE);
	KASSERT(a_missing_ok || (a_dirent != NULL));
	KASSERT(a_missing_ok || (a_dirent->td_node != NULL));
	KASSERT(b_missing_ok || (b_dirent != NULL));
	KASSERT(b_missing_ok || (b_dirent->td_node != NULL));
	KASSERT((a_dirent == NULL) || (a_dirent->td_node != NULL));
	KASSERT((a_dirent == NULL) || (a_dirent->td_node->tn_vnode == a_vp));
	KASSERT((b_dirent == NULL) || (b_dirent->td_node != NULL));
	KASSERT((b_dirent == NULL) || (b_dirent->td_node->tn_vnode == b_vp));
	KASSERT((a_vp == NULL) || (VOP_ISLOCKED(a_vp) == LK_EXCLUSIVE));
	KASSERT((b_vp == NULL) || (VOP_ISLOCKED(b_vp) == LK_EXCLUSIVE));

	*a_dirent_ret = a_dirent;
	*b_dirent_ret = b_dirent;
	*a_vp_ret = a_vp;
	*b_vp_ret = b_vp;
	return 0;

fail4:	if (b_vp != NULL) {
		KASSERT(VOP_ISLOCKED(b_vp) == LK_EXCLUSIVE);
		if (b_vp != a_vp)
			vput(b_vp);
		else
			vrele(a_vp);
	}

fail3:	KASSERT(VOP_ISLOCKED(b_dvp) == LK_EXCLUSIVE);
	VOP_UNLOCK(b_dvp);

fail2:	if (a_vp != NULL) {
		KASSERT(VOP_ISLOCKED(a_vp) == LK_EXCLUSIVE);
		vput(a_vp);
	}

fail1:	KASSERT(VOP_ISLOCKED(a_dvp) == LK_EXCLUSIVE);
	VOP_UNLOCK(a_dvp);

fail0:	/* KASSERT(VOP_ISLOCKED(a_dvp) != LK_EXCLUSIVE); */
	/* KASSERT(VOP_ISLOCKED(b_dvp) != LK_EXCLUSIVE); */
	/* KASSERT((a_vp == NULL) || (VOP_ISLOCKED(a_vp) != LK_EXCLUSIVE)); */
	/* KASSERT((b_vp == NULL) || (VOP_ISLOCKED(b_vp) != LK_EXCLUSIVE)); */
	return error;
}

/*
 * Shuffle the directory entries to move fvp from the directory fdvp
 * into the directory tdvp.  fde is fvp's directory entry in fdvp.  If
 * we are overwriting a target node, it is tvp, and tde is its
 * directory entry in tdvp.
 *
 * fdvp, fvp, tdvp, and tvp must all be locked and referenced.
 */
void
tmpfs_rename_attachdetach(struct tmpfs_mount *tmpfs,
    struct vnode *fdvp, struct tmpfs_dirent *fde, struct vnode *fvp,
    struct vnode *tdvp, struct tmpfs_dirent *tde, struct vnode *tvp)
{

	KASSERT(tmpfs != NULL);
	KASSERT(fdvp != NULL);
	KASSERT(fde != NULL);
	KASSERT(fvp != NULL);
	KASSERT(tdvp != NULL);
	KASSERT(fde->td_node != NULL);
	KASSERT(fde->td_node->tn_vnode == fvp);
	KASSERT((tde == NULL) == (tvp == NULL));
	KASSERT((tde == NULL) || (tde->td_node != NULL));
	KASSERT((tde == NULL) || (tde->td_node->tn_vnode == tvp));
	KASSERT(VOP_ISLOCKED(fdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(fvp) == LK_EXCLUSIVE);
	KASSERT((tvp == NULL) || (VOP_ISLOCKED(tvp) == LK_EXCLUSIVE));

	/*
	 * If we are moving from one directory to another, detach the
	 * source entry and reattach it to the target directory.
	 */
	if (fdvp != tdvp) {
		/* tmpfs_dir_detach clobbers fde->td_node, so save it.  */
		struct tmpfs_node *fnode = fde->td_node;
		tmpfs_node_t *fdnode = VP_TO_TMPFS_DIR(fdvp);
		tmpfs_node_t *tdnode = VP_TO_TMPFS_DIR(tdvp);
		tmpfs_dir_detach(fdnode, fde);
		tmpfs_dir_attach(tdnode, fde, fnode);
	} else if (tvp == NULL) {
		/*
		 * We are changing the directory.  tmpfs_dir_attach and
		 * tmpfs_dir_detach note the events for us, but for
		 * this case we don't call them, so we must note the
		 * event explicitly.
		 */
		VN_KNOTE(fdvp, NOTE_WRITE);
	}

	/*
	 * If we are replacing an existing target entry, delete it.
	 */
	if (tde != NULL) {
		tmpfs_node_t *tdnode = VP_TO_TMPFS_DIR(tdvp);
		KASSERT(tvp != NULL);
		KASSERT(tde->td_node != NULL);
		KASSERT((fvp->v_type == VDIR) == (tvp->v_type == VDIR));
		if (tde->td_node->tn_type == VDIR) {
			KASSERT(tde->td_node->tn_size == 0);
			KASSERT(tde->td_node->tn_links == 2);
			/* Decrement the extra link count for `.' so
			 * the vnode will be recycled when released.  */
			tde->td_node->tn_links--;
		}
		tmpfs_dir_detach(tdnode, tde);
		tmpfs_free_dirent(tmpfs, tde);
	}
}

/*
 * Remove the entry de for the non-directory vp from the directory dvp.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_do_remove(struct tmpfs_mount *tmpfs, struct vnode *dvp,
    struct tmpfs_node *dnode, struct tmpfs_dirent *de, struct vnode *vp,
    struct ucred *cred)
{
	int error;

	KASSERT(tmpfs != NULL);
	KASSERT(dvp != NULL);
	KASSERT(dnode != NULL);
	KASSERT(de != NULL);
	KASSERT(vp != NULL);
	KASSERT(dnode->tn_vnode == dvp);
	KASSERT(de->td_node != NULL);
	KASSERT(de->td_node->tn_vnode == vp);
	KASSERT(VOP_ISLOCKED(dvp) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(vp) == LK_EXCLUSIVE);

	error = tmpfs_remove_check_possible(dnode, de->td_node);
	if (error)
		return error;

	error = tmpfs_remove_check_permitted(cred, dnode, de->td_node);
	if (error)
		return error;

	/*
	 * If not root and directory is sticky, check for permission on
	 * directory or on file. This implements append-only directories.
	 */
	if ((dnode->tn_mode & S_ISTXT) != 0)
		if (cred->cr_uid != 0 && cred->cr_uid != dnode->tn_uid &&
		    cred->cr_uid != de->td_node->tn_uid)
			return EPERM;

	tmpfs_dir_detach(dnode, de);
	tmpfs_free_dirent(tmpfs, de);

	return 0;
}

/*
 * Check whether a rename is possible independent of credentials.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_rename_check_possible(
    struct tmpfs_node *fdnode, struct tmpfs_node *fnode,
    struct tmpfs_node *tdnode, struct tmpfs_node *tnode)
{

	KASSERT(fdnode != NULL);
	KASSERT(fnode != NULL);
	KASSERT(tdnode != NULL);
	KASSERT(fdnode != fnode);
	KASSERT(tdnode != tnode);
	KASSERT(fnode != tnode);
	KASSERT(fdnode->tn_vnode != NULL);
	KASSERT(fnode->tn_vnode != NULL);
	KASSERT(tdnode->tn_vnode != NULL);
	KASSERT((tnode == NULL) || (tnode->tn_vnode != NULL));
	KASSERT(VOP_ISLOCKED(fdnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(fnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT((tnode == NULL) ||
	    (VOP_ISLOCKED(tnode->tn_vnode) == LK_EXCLUSIVE));

	/*
	 * If fdnode is immutable, we can't write to it.  If fdnode is
	 * append-only, the only change we can make is to add entries
	 * to it.  If fnode is immutable, we can't change the links to
	 * it.  If fnode is append-only...well, this is what UFS does.
	 */
	if ((fdnode->tn_flags | fnode->tn_flags) & (IMMUTABLE | APPEND))
		return EPERM;

	/*
	 * If tdnode is immutable, we can't write to it.  If tdnode is
	 * append-only, we can add entries, but we can't change
	 * existing entries.
	 */
	if (tdnode->tn_flags & (IMMUTABLE | (tnode? APPEND : 0)))
		return EPERM;

	/*
	 * If tnode is immutable, we can't replace links to it.  If
	 * tnode is append-only...well, this is what UFS does.
	 */
	if (tnode != NULL) {
		KASSERT(tnode != NULL);
		if ((tnode->tn_flags & (IMMUTABLE | APPEND)) != 0)
			return EPERM;
	}

	return 0;
}

/*
 * Check whether a rename is permitted given our credentials.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_rename_check_permitted(struct ucred *cred,
    struct tmpfs_node *fdnode, struct tmpfs_node *fnode,
    struct tmpfs_node *tdnode, struct tmpfs_node *tnode)
{
	int error;

	KASSERT(fdnode != NULL);
	KASSERT(fnode != NULL);
	KASSERT(tdnode != NULL);
	KASSERT(fdnode != fnode);
	KASSERT(tdnode != tnode);
	KASSERT(fnode != tnode);
	KASSERT(fdnode->tn_vnode != NULL);
	KASSERT(fnode->tn_vnode != NULL);
	KASSERT(tdnode->tn_vnode != NULL);
	KASSERT((tnode == NULL) || (tnode->tn_vnode != NULL));
	KASSERT(VOP_ISLOCKED(fdnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(fnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(tdnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT((tnode == NULL) ||
	    (VOP_ISLOCKED(tnode->tn_vnode) == LK_EXCLUSIVE));

	/*
	 * We need to remove or change an entry in the source directory.
	 */
	error = VOP_ACCESS(fdnode->tn_vnode, VWRITE, cred, curproc);
	if (error)
		return error;

	/*
	 * If we are changing directories, then we need to write to the
	 * target directory to add or change an entry.  Also, if fnode
	 * is a directory, we need to write to it to change its `..'
	 * entry.
	 */
	if (fdnode != tdnode) {
		error = VOP_ACCESS(tdnode->tn_vnode, VWRITE, cred, curproc);
		if (error)
			return error;
		if (fnode->tn_type == VDIR) {
			error = VOP_ACCESS(fnode->tn_vnode, VWRITE, cred,
			    curproc);
			if (error)
				return error;
		}
	}

	error = tmpfs_check_sticky(cred, fdnode, fnode);
	if (error)
		return error;

	if (TMPFS_DIRSEQ_FULL(tdnode))
		return (ENOSPC);

	error = tmpfs_check_sticky(cred, tdnode, tnode);
	if (error)
		return error;

	return 0;
}

/*
 * Check whether removing node's entry in dnode is possible independent
 * of credentials.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_remove_check_possible(struct tmpfs_node *dnode, struct tmpfs_node *node)
{

	KASSERT(dnode != NULL);
	KASSERT(dnode->tn_vnode != NULL);
	KASSERT(node != NULL);
	KASSERT(dnode != node);
	KASSERT(VOP_ISLOCKED(dnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(node->tn_vnode) == LK_EXCLUSIVE);

	/*
	 * We want to delete the entry.  If dnode is immutable, we
	 * can't write to it to delete the entry.  If dnode is
	 * append-only, the only change we can make is to add entries,
	 * so we can't delete entries.  If node is immutable, we can't
	 * change the links to it, so we can't delete the entry.  If
	 * node is append-only...well, this is what UFS does.
	 */
	if ((dnode->tn_flags | node->tn_flags) & (IMMUTABLE | APPEND))
		return EPERM;

	return 0;
}

/*
 * Check whether removing node's entry in dnode is permitted given our
 * credentials.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_remove_check_permitted(struct ucred *cred,
    struct tmpfs_node *dnode, struct tmpfs_node *node)
{
	int error;

	KASSERT(dnode != NULL);
	KASSERT(dnode->tn_vnode != NULL);
	KASSERT(node != NULL);
	KASSERT(dnode != node);
	KASSERT(VOP_ISLOCKED(dnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT(VOP_ISLOCKED(node->tn_vnode) == LK_EXCLUSIVE);

	/*
	 * Check whether we are permitted to write to the source
	 * directory in order to delete an entry from it.
	 */
	error = VOP_ACCESS(dnode->tn_vnode, VWRITE, cred, curproc);
	if (error)
		return error;

	error = tmpfs_check_sticky(cred, dnode, node);
	if (error)
		return error;

	return 0;
}

/*
 * Check whether we may change an entry in a sticky directory.  If the
 * directory is sticky, the user must own either the directory or, if
 * it exists, the node, in order to change the entry.
 *
 * Everything must be locked and referenced.
 */
int
tmpfs_check_sticky(struct ucred *cred,
    struct tmpfs_node *dnode, struct tmpfs_node *node)
{

	KASSERT(dnode != NULL);
	KASSERT(dnode->tn_vnode != NULL);
	KASSERT(VOP_ISLOCKED(dnode->tn_vnode) == LK_EXCLUSIVE);
	KASSERT((node == NULL) || (node->tn_vnode != NULL));
	KASSERT((node == NULL) ||
	    (VOP_ISLOCKED(dnode->tn_vnode) == LK_EXCLUSIVE));

	if (node == NULL)
		return 0;

	if (dnode->tn_mode & S_ISTXT) {
		if (cred->cr_uid != 0 &&
		    cred->cr_uid != dnode->tn_uid &&
		    cred->cr_uid != node->tn_uid)
			return EPERM;
	}

	return 0;
}

void
tmpfs_rename_cache_purge(struct vnode *fdvp, struct vnode *fvp,
    struct vnode *tdvp, struct vnode *tvp)
{

	KASSERT(fdvp != NULL);
	KASSERT(fvp != NULL);
	KASSERT(tdvp != NULL);
	KASSERT(fdvp != fvp);
	KASSERT(fdvp != tvp);
	KASSERT(tdvp != fvp);
	KASSERT(tdvp != tvp);
	KASSERT(fvp != tvp);
	KASSERT(fdvp->v_type == VDIR);
	KASSERT(tdvp->v_type == VDIR);

	/*
	 * XXX What actually needs to be purged?
	 */

	cache_purge(fdvp);

	if (fvp->v_type == VDIR)
		cache_purge(fvp);

	if (tdvp != fdvp)
		cache_purge(tdvp);

	if ((tvp != NULL) && (tvp->v_type == VDIR))
		cache_purge(tvp);
}

void
tmpfs_rename_abort(void *v)
{
	struct vop_rename_args *ap = v;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *fvp = ap->a_fvp;
	struct componentname *fcnp = ap->a_fcnp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *tvp = ap->a_tvp;
	struct componentname *tcnp = ap->a_tcnp;

	VOP_ABORTOP(tdvp, tcnp);
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp != NULL)
		vput(tvp);
	VOP_ABORTOP(fdvp, fcnp);
	vrele(fdvp);
	vrele(fvp);
}

void filt_tmpfsdetach(struct knote *kn);
int filt_tmpfsread(struct knote *kn, long hint);
int filt_tmpfswrite(struct knote *kn, long hint);
int filt_tmpfsvnode(struct knote *kn, long hint);

const struct filterops tmpfsread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_tmpfsdetach,
	.f_event	= filt_tmpfsread,
};

const struct filterops tmpfswrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_tmpfsdetach,
	.f_event	= filt_tmpfswrite,
};

const struct filterops tmpfsvnode_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_tmpfsdetach,
	.f_event	= filt_tmpfsvnode,
};

int
tmpfs_kqfilter(void *v)
{
	struct vop_kqfilter_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &tmpfsread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &tmpfswrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &tmpfsvnode_filtops;
		break;
	default:
		return (EINVAL);
	}

	kn->kn_hook = (caddr_t)vp;

	klist_insert_locked(&vp->v_klist, kn);

	return (0);
}

void
filt_tmpfsdetach(struct knote *kn)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;

	klist_remove_locked(&vp->v_klist, kn);
}

int
filt_tmpfsread(struct knote *kn, long hint)
{
	struct vnode *vp = (struct vnode *)kn->kn_hook;
	tmpfs_node_t *node = VP_TO_TMPFS_NODE(vp);

	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = node->tn_size - foffset(kn->kn_fp);
	if (kn->kn_data == 0 && kn->kn_sfflags & NOTE_EOF) {
		kn->kn_fflags |= NOTE_EOF;
		return (1);
	}

	if (kn->kn_flags & (__EV_POLL | __EV_SELECT))
		return (1);

	return (kn->kn_data != 0);
}

int
filt_tmpfswrite(struct knote *kn, long hint)
{
	/*
	 * filesystem is gone, so set the EOF flag and schedule
	 * the knote for deletion.
	 */
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return (1);
	}

	kn->kn_data = 0;
	return (1);
}

int
filt_tmpfsvnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}
