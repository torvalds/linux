/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf
 * Copyright (c) 2008, 2009 Reinoud Zandijk
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * From: NetBSD: nilfs_vnops.c,v 1.2 2009/08/26 03:40:48 elad
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/buf.h>
#include <sys/bio.h>
#include <sys/fcntl.h>
#include <sys/dirent.h>
#include <sys/rwlock.h>
#include <sys/stat.h>
#include <sys/priv.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_object.h>
#include <vm/vnode_pager.h>

#include <machine/_inttypes.h>

#include <fs/nandfs/nandfs_mount.h>
#include <fs/nandfs/nandfs.h>
#include <fs/nandfs/nandfs_subr.h>

extern uma_zone_t nandfs_node_zone;
static void nandfs_read_filebuf(struct nandfs_node *, struct buf *);
static void nandfs_itimes_locked(struct vnode *);
static int nandfs_truncate(struct vnode *, uint64_t);

static vop_pathconf_t	nandfs_pathconf;

#define UPDATE_CLOSE 0
#define UPDATE_WAIT 0

static int
nandfs_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	int error = 0;

	DPRINTF(VNCALL, ("%s: vp:%p node:%p\n", __func__, vp, node));

	if (node == NULL) {
		DPRINTF(NODE, ("%s: inactive NULL node\n", __func__));
		return (0);
	}

	if (node->nn_inode.i_mode != 0 && !(node->nn_inode.i_links_count)) {
		nandfs_truncate(vp, 0);
		error = nandfs_node_destroy(node);
		if (error)
			nandfs_error("%s: destroy node: %p\n", __func__, node);
		node->nn_flags = 0;
		vrecycle(vp);
	}

	return (error);
}

static int
nandfs_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *nandfs_node = VTON(vp);
	struct nandfs_device *fsdev = nandfs_node->nn_nandfsdev;
	uint64_t ino = nandfs_node->nn_ino;

	DPRINTF(VNCALL, ("%s: vp:%p node:%p\n", __func__, vp, nandfs_node));

	/* Invalidate all entries to a particular vnode. */
	cache_purge(vp);

	/* Destroy the vm object and flush associated pages. */
	vnode_destroy_vobject(vp);

	/* Remove from vfs hash if not system vnode */
	if (!NANDFS_SYS_NODE(nandfs_node->nn_ino))
		vfs_hash_remove(vp);

	/* Dispose all node knowledge */
	nandfs_dispose_node(&nandfs_node);

	if (!NANDFS_SYS_NODE(ino))
		NANDFS_WRITEUNLOCK(fsdev);

	return (0);
}

static int
nandfs_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_device *nandfsdev = node->nn_nandfsdev;
	struct uio *uio = ap->a_uio;
	struct buf *bp;
	uint64_t size;
	uint32_t blocksize;
	off_t bytesinfile;
	ssize_t toread, off;
	daddr_t lbn;
	ssize_t resid;
	int error = 0;

	if (uio->uio_resid == 0)
		return (0);

	size = node->nn_inode.i_size;
	if (uio->uio_offset >= size)
		return (0);

	blocksize = nandfsdev->nd_blocksize;
	bytesinfile = size - uio->uio_offset;

	resid = omin(uio->uio_resid, bytesinfile);

	while (resid) {
		lbn = uio->uio_offset / blocksize;
		off = uio->uio_offset & (blocksize - 1);

		toread = omin(resid, blocksize - off);

		DPRINTF(READ, ("nandfs_read bn: 0x%jx toread: 0x%zx (0x%x)\n",
		    (uintmax_t)lbn, toread, blocksize));

		error = nandfs_bread(node, lbn, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			break;
		}

		error = uiomove(bp->b_data + off, toread, uio);
		if (error) {
			brelse(bp);
			break;
		}

		brelse(bp);
		resid -= toread;
	}

	return (error);
}

static int
nandfs_write(struct vop_write_args *ap)
{
	struct nandfs_device *fsdev;
	struct nandfs_node *node;
	struct vnode *vp;
	struct uio *uio;
	struct buf *bp;
	uint64_t file_size, vblk;
	uint32_t blocksize;
	ssize_t towrite, off;
	daddr_t lbn;
	ssize_t resid;
	int error, ioflag, modified;

	vp = ap->a_vp;
	uio = ap->a_uio;
	ioflag = ap->a_ioflag;
	node = VTON(vp);
	fsdev = node->nn_nandfsdev;

	if (nandfs_fs_full(fsdev))
		return (ENOSPC);

	DPRINTF(WRITE, ("nandfs_write called %#zx at %#jx\n",
	    uio->uio_resid, (uintmax_t)uio->uio_offset));

	if (uio->uio_offset < 0)
		return (EINVAL);
	if (uio->uio_resid == 0)
		return (0);

	blocksize = fsdev->nd_blocksize;
	file_size = node->nn_inode.i_size;

	switch (vp->v_type) {
	case VREG:
		if (ioflag & IO_APPEND)
			uio->uio_offset = file_size;
		break;
	case VDIR:
		return (EISDIR);
	case VLNK:
		break;
	default:
		panic("%s: bad file type vp: %p", __func__, vp);
	}

	/* If explicitly asked to append, uio_offset can be wrong? */
	if (ioflag & IO_APPEND)
		uio->uio_offset = file_size;

	resid = uio->uio_resid;
	modified = error = 0;

	while (uio->uio_resid) {
		lbn = uio->uio_offset / blocksize;
		off = uio->uio_offset & (blocksize - 1);

		towrite = omin(uio->uio_resid, blocksize - off);

		DPRINTF(WRITE, ("%s: lbn: 0x%jd toread: 0x%zx (0x%x)\n",
		    __func__, (uintmax_t)lbn, towrite, blocksize));

		error = nandfs_bmap_lookup(node, lbn, &vblk);
		if (error)
			break;

		DPRINTF(WRITE, ("%s: lbn: 0x%jd toread: 0x%zx (0x%x) "
		    "vblk=%jx\n", __func__, (uintmax_t)lbn, towrite, blocksize,
		    vblk));

		if (vblk != 0)
			error = nandfs_bread(node, lbn, NOCRED, 0, &bp);
		else
			error = nandfs_bcreate(node, lbn, NOCRED, 0, &bp);

		DPRINTF(WRITE, ("%s: vp %p bread bp %p lbn %#jx\n", __func__,
		    vp, bp, (uintmax_t)lbn));
		if (error) {
			if (bp)
				brelse(bp);
			break;
		}

		error = uiomove((char *)bp->b_data + off, (int)towrite, uio);
		if (error)
			break;

		error = nandfs_dirty_buf(bp, 0);
		if (error)
			break;

		modified++;
	}

	/* XXX proper handling when only part of file was properly written */
	if (modified) {
		if (resid > uio->uio_resid && ap->a_cred &&
		    ap->a_cred->cr_uid != 0)
			node->nn_inode.i_mode &= ~(ISUID | ISGID);

		if (file_size < uio->uio_offset + uio->uio_resid) {
			node->nn_inode.i_size = uio->uio_offset +
			    uio->uio_resid;
			node->nn_flags |= IN_CHANGE | IN_UPDATE;
			vnode_pager_setsize(vp, uio->uio_offset +
			    uio->uio_resid);
			nandfs_itimes(vp);
		}
	}

	DPRINTF(WRITE, ("%s: return:%d\n", __func__, error));

	return (error);
}

static int
nandfs_lookup(struct vop_cachedlookup_args *ap)
{
	struct vnode *dvp, **vpp;
	struct componentname *cnp;
	struct ucred *cred;
	struct thread *td;
	struct nandfs_node *dir_node, *node;
	struct nandfsmount *nmp;
	uint64_t ino, off;
	const char *name;
	int namelen, nameiop, islastcn, mounted_ro;
	int error, found;

	DPRINTF(VNCALL, ("%s\n", __func__));

	dvp = ap->a_dvp;
	vpp = ap->a_vpp;
	*vpp = NULL;

	cnp = ap->a_cnp;
	cred = cnp->cn_cred;
	td = cnp->cn_thread;

	dir_node = VTON(dvp);
	nmp = dir_node->nn_nmp;

	/* Simplify/clarification flags */
	nameiop = cnp->cn_nameiop;
	islastcn = cnp->cn_flags & ISLASTCN;
	mounted_ro = dvp->v_mount->mnt_flag & MNT_RDONLY;

	/*
	 * If requesting a modify on the last path element on a read-only
	 * filingsystem, reject lookup;
	 */
	if (islastcn && mounted_ro && (nameiop == DELETE || nameiop == RENAME))
		return (EROFS);

	if (dir_node->nn_inode.i_links_count == 0)
		return (ENOENT);

	/*
	 * Obviously, the file is not (anymore) in the namecache, we have to
	 * search for it. There are three basic cases: '.', '..' and others.
	 *
	 * Following the guidelines of VOP_LOOKUP manpage and tmpfs.
	 */
	error = 0;
	if ((cnp->cn_namelen == 1) && (cnp->cn_nameptr[0] == '.')) {
		DPRINTF(LOOKUP, ("\tlookup '.'\n"));
		/* Special case 1 '.' */
		VREF(dvp);
		*vpp = dvp;
		/* Done */
	} else if (cnp->cn_flags & ISDOTDOT) {
		/* Special case 2 '..' */
		DPRINTF(LOOKUP, ("\tlookup '..'\n"));

		/* Get our node */
		name = "..";
		namelen = 2;
		error = nandfs_lookup_name_in_dir(dvp, name, namelen, &ino,
		    &found, &off);
		if (error)
			goto out;
		if (!found)
			error = ENOENT;

		/* First unlock parent */
		VOP_UNLOCK(dvp, 0);

		if (error == 0) {
			DPRINTF(LOOKUP, ("\tfound '..'\n"));
			/* Try to create/reuse the node */
			error = nandfs_get_node(nmp, ino, &node);

			if (!error) {
				DPRINTF(LOOKUP,
				    ("\tnode retrieved/created OK\n"));
				*vpp = NTOV(node);
			}
		}

		/* Try to relock parent */
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
	} else {
		DPRINTF(LOOKUP, ("\tlookup file\n"));
		/* All other files */
		/* Look up filename in the directory returning its inode */
		name = cnp->cn_nameptr;
		namelen = cnp->cn_namelen;
		error = nandfs_lookup_name_in_dir(dvp, name, namelen,
		    &ino, &found, &off);
		if (error)
			goto out;
		if (!found) {
			DPRINTF(LOOKUP, ("\tNOT found\n"));
			/*
			 * UGH, didn't find name. If we're creating or
			 * renaming on the last name this is OK and we ought
			 * to return EJUSTRETURN if its allowed to be created.
			 */
			error = ENOENT;
			if ((nameiop == CREATE || nameiop == RENAME) &&
			    islastcn) {
				error = VOP_ACCESS(dvp, VWRITE, cred, td);
				if (!error) {
					/* keep the component name */
					cnp->cn_flags |= SAVENAME;
					error = EJUSTRETURN;
				}
			}
			/* Done */
		} else {
			if (ino == NANDFS_WHT_INO)
				cnp->cn_flags |= ISWHITEOUT;

			if ((cnp->cn_flags & ISWHITEOUT) &&
			    (nameiop == LOOKUP))
				return (ENOENT);

			if ((nameiop == DELETE) && islastcn) {
				if ((cnp->cn_flags & ISWHITEOUT) &&
				    (cnp->cn_flags & DOWHITEOUT)) {
					cnp->cn_flags |= SAVENAME;
					dir_node->nn_diroff = off;
					return (EJUSTRETURN);
				}

				error = VOP_ACCESS(dvp, VWRITE, cred,
				    cnp->cn_thread);
				if (error)
					return (error);

				/* Try to create/reuse the node */
				error = nandfs_get_node(nmp, ino, &node);
				if (!error) {
					*vpp = NTOV(node);
					node->nn_diroff = off;
				}

				if ((dir_node->nn_inode.i_mode & ISVTX) &&
				    cred->cr_uid != 0 &&
				    cred->cr_uid != dir_node->nn_inode.i_uid &&
				    node->nn_inode.i_uid != cred->cr_uid) {
					vput(*vpp);
					*vpp = NULL;
					return (EPERM);
				}
			} else if ((nameiop == RENAME) && islastcn) {
				error = VOP_ACCESS(dvp, VWRITE, cred,
				    cnp->cn_thread);
				if (error)
					return (error);

				/* Try to create/reuse the node */
				error = nandfs_get_node(nmp, ino, &node);
				if (!error) {
					*vpp = NTOV(node);
					node->nn_diroff = off;
				}
			} else {
				/* Try to create/reuse the node */
				error = nandfs_get_node(nmp, ino, &node);
				if (!error) {
					*vpp = NTOV(node);
					node->nn_diroff = off;
				}
			}
		}
	}

out:
	/*
	 * Store result in the cache if requested. If we are creating a file,
	 * the file might not be found and thus putting it into the namecache
	 * might be seen as negative caching.
	 */
	if ((cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(dvp, *vpp, cnp);

	return (error);

}

static int
nandfs_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_inode *inode = &node->nn_inode;

	DPRINTF(VNCALL, ("%s: vp: %p\n", __func__, vp));
	nandfs_itimes(vp);

	/* Basic info */
	VATTR_NULL(vap);
	vap->va_atime.tv_sec = inode->i_mtime;
	vap->va_atime.tv_nsec = inode->i_mtime_nsec;
	vap->va_mtime.tv_sec = inode->i_mtime;
	vap->va_mtime.tv_nsec = inode->i_mtime_nsec;
	vap->va_ctime.tv_sec = inode->i_ctime;
	vap->va_ctime.tv_nsec = inode->i_ctime_nsec;
	vap->va_type = IFTOVT(inode->i_mode);
	vap->va_mode = inode->i_mode & ~S_IFMT;
	vap->va_nlink = inode->i_links_count;
	vap->va_uid = inode->i_uid;
	vap->va_gid = inode->i_gid;
	vap->va_rdev = inode->i_special;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	vap->va_fileid = node->nn_ino;
	vap->va_size = inode->i_size;
	vap->va_blocksize = node->nn_nandfsdev->nd_blocksize;
	vap->va_gen = 0;
	vap->va_flags = inode->i_flags;
	vap->va_bytes = inode->i_blocks * vap->va_blocksize;
	vap->va_filerev = 0;
	vap->va_vaflags = 0;

	return (0);
}

static int
nandfs_vtruncbuf(struct vnode *vp, uint64_t nblks)
{
	struct nandfs_device *nffsdev;
	struct bufobj *bo;
	struct buf *bp, *nbp;

	bo = &vp->v_bufobj;
	nffsdev = VTON(vp)->nn_nandfsdev;

	ASSERT_VOP_LOCKED(vp, "nandfs_truncate");
restart:
	BO_LOCK(bo);
restart_locked:
	TAILQ_FOREACH_SAFE(bp, &bo->bo_clean.bv_hd, b_bobufs, nbp) {
		if (bp->b_lblkno < nblks)
			continue;
		if (BUF_LOCK(bp, LK_EXCLUSIVE | LK_NOWAIT, NULL))
			goto restart_locked;

		bremfree(bp);
		bp->b_flags |= (B_INVAL | B_RELBUF);
		bp->b_flags &= ~(B_ASYNC | B_MANAGED);
		BO_UNLOCK(bo);
		brelse(bp);
		BO_LOCK(bo);
	}

	TAILQ_FOREACH_SAFE(bp, &bo->bo_dirty.bv_hd, b_bobufs, nbp) {
		if (bp->b_lblkno < nblks)
			continue;
		if (BUF_LOCK(bp,
		    LK_EXCLUSIVE | LK_SLEEPFAIL | LK_INTERLOCK,
		    BO_LOCKPTR(bo)) == ENOLCK)
			goto restart;
		bp->b_flags |= (B_INVAL | B_RELBUF);
		bp->b_flags &= ~(B_ASYNC | B_MANAGED);
		brelse(bp);
		nandfs_dirty_bufs_decrement(nffsdev);
		BO_LOCK(bo);
	}

	BO_UNLOCK(bo);

	return (0);
}

static int
nandfs_truncate(struct vnode *vp, uint64_t newsize)
{
	struct nandfs_device *nffsdev;
	struct nandfs_node *node;
	struct nandfs_inode *inode;
	struct buf *bp = NULL;
	uint64_t oblks, nblks, vblk, size, rest;
	int error;

	node = VTON(vp);
	nffsdev = node->nn_nandfsdev;
	inode = &node->nn_inode;

	/* Calculate end of file */
	size = inode->i_size;

	if (newsize == size) {
		node->nn_flags |= IN_CHANGE | IN_UPDATE;
		nandfs_itimes(vp);
		return (0);
	}

	if (newsize > size) {
		inode->i_size = newsize;
		vnode_pager_setsize(vp, newsize);
		node->nn_flags |= IN_CHANGE | IN_UPDATE;
		nandfs_itimes(vp);
		return (0);
	}

	nblks = howmany(newsize, nffsdev->nd_blocksize);
	oblks = howmany(size, nffsdev->nd_blocksize);
	rest = newsize % nffsdev->nd_blocksize;

	if (rest) {
		error = nandfs_bmap_lookup(node, nblks - 1, &vblk);
		if (error)
			return (error);

		if (vblk != 0)
			error = nandfs_bread(node, nblks - 1, NOCRED, 0, &bp);
		else
			error = nandfs_bcreate(node, nblks - 1, NOCRED, 0, &bp);

		if (error) {
			if (bp)
				brelse(bp);
			return (error);
		}

		bzero((char *)bp->b_data + rest,
		    (u_int)(nffsdev->nd_blocksize - rest));
		error = nandfs_dirty_buf(bp, 0);
		if (error)
			return (error);
	}

	DPRINTF(VNCALL, ("%s: vp %p oblks %jx nblks %jx\n", __func__, vp, oblks,
	    nblks));

	error = nandfs_bmap_truncate_mapping(node, oblks - 1, nblks - 1);
	if (error) {
		if (bp)
			nandfs_undirty_buf(bp);
		return (error);
	}

	error = nandfs_vtruncbuf(vp, nblks);
	if (error) {
		if (bp)
			nandfs_undirty_buf(bp);
		return (error);
	}

	inode->i_size = newsize;
	vnode_pager_setsize(vp, newsize);
	node->nn_flags |= IN_CHANGE | IN_UPDATE;
	nandfs_itimes(vp);

	return (error);
}

static void
nandfs_itimes_locked(struct vnode *vp)
{
	struct nandfs_node *node;
	struct nandfs_inode *inode;
	struct timespec ts;

	ASSERT_VI_LOCKED(vp, __func__);

	node = VTON(vp);
	inode = &node->nn_inode;

	if ((node->nn_flags & (IN_ACCESS | IN_CHANGE | IN_UPDATE)) == 0)
		return;

	if (((vp->v_mount->mnt_kern_flag &
	    (MNTK_SUSPENDED | MNTK_SUSPEND)) == 0) ||
	    (node->nn_flags & (IN_CHANGE | IN_UPDATE)))
		node->nn_flags |= IN_MODIFIED;

	vfs_timestamp(&ts);
	if (node->nn_flags & IN_UPDATE) {
		inode->i_mtime = ts.tv_sec;
		inode->i_mtime_nsec = ts.tv_nsec;
	}
	if (node->nn_flags & IN_CHANGE) {
		inode->i_ctime = ts.tv_sec;
		inode->i_ctime_nsec = ts.tv_nsec;
	}

	node->nn_flags &= ~(IN_ACCESS | IN_CHANGE | IN_UPDATE);
}

void
nandfs_itimes(struct vnode *vp)
{

	VI_LOCK(vp);
	nandfs_itimes_locked(vp);
	VI_UNLOCK(vp);
}

static int
nandfs_chmod(struct vnode *vp, int mode, struct ucred *cred, struct thread *td)
{
	struct nandfs_node *node = VTON(vp);
	struct nandfs_inode *inode = &node->nn_inode;
	uint16_t nmode;
	int error = 0;

	DPRINTF(VNCALL, ("%s: vp %p, mode %x, cred %p, td %p\n", __func__, vp,
	    mode, cred, td));
	/*
	 * To modify the permissions on a file, must possess VADMIN
	 * for that file.
	 */
	if ((error = VOP_ACCESS(vp, VADMIN, cred, td)))
		return (error);

	/*
	 * Privileged processes may set the sticky bit on non-directories,
	 * as well as set the setgid bit on a file with a group that the
	 * process is not a member of. Both of these are allowed in
	 * jail(8).
	 */
	if (vp->v_type != VDIR && (mode & S_ISTXT)) {
		if (priv_check_cred(cred, PRIV_VFS_STICKYFILE))
			return (EFTYPE);
	}
	if (!groupmember(inode->i_gid, cred) && (mode & ISGID)) {
		error = priv_check_cred(cred, PRIV_VFS_SETGID);
		if (error)
			return (error);
	}

	/*
	 * Deny setting setuid if we are not the file owner.
	 */
	if ((mode & ISUID) && inode->i_uid != cred->cr_uid) {
		error = priv_check_cred(cred, PRIV_VFS_ADMIN);
		if (error)
			return (error);
	}

	nmode = inode->i_mode;
	nmode &= ~ALLPERMS;
	nmode |= (mode & ALLPERMS);
	inode->i_mode = nmode;
	node->nn_flags |= IN_CHANGE;

	DPRINTF(VNCALL, ("%s: to mode %x\n", __func__, nmode));

	return (error);
}

static int
nandfs_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred,
    struct thread *td)
{
	struct nandfs_node *node = VTON(vp);
	struct nandfs_inode *inode = &node->nn_inode;
	uid_t ouid;
	gid_t ogid;
	int error = 0;

	if (uid == (uid_t)VNOVAL)
		uid = inode->i_uid;
	if (gid == (gid_t)VNOVAL)
		gid = inode->i_gid;
	/*
	 * To modify the ownership of a file, must possess VADMIN for that
	 * file.
	 */
	if ((error = VOP_ACCESSX(vp, VWRITE_OWNER, cred, td)))
		return (error);
	/*
	 * To change the owner of a file, or change the group of a file to a
	 * group of which we are not a member, the caller must have
	 * privilege.
	 */
	if (((uid != inode->i_uid && uid != cred->cr_uid) ||
	    (gid != inode->i_gid && !groupmember(gid, cred))) &&
	    (error = priv_check_cred(cred, PRIV_VFS_CHOWN)))
		return (error);
	ogid = inode->i_gid;
	ouid = inode->i_uid;

	inode->i_gid = gid;
	inode->i_uid = uid;

	node->nn_flags |= IN_CHANGE;
	if ((inode->i_mode & (ISUID | ISGID)) &&
	    (ouid != uid || ogid != gid)) {
		if (priv_check_cred(cred, PRIV_VFS_RETAINSUGID))
			inode->i_mode &= ~(ISUID | ISGID);
	}
	DPRINTF(VNCALL, ("%s: vp %p, cred %p, td %p - ret OK\n", __func__, vp,
	    cred, td));
	return (0);
}

static int
nandfs_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_inode *inode = &node->nn_inode;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct thread *td = curthread;
	uint32_t flags;
	int error = 0;

	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    (vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		DPRINTF(VNCALL, ("%s: unsettable attribute\n", __func__));
		return (EINVAL);
	}

	if (vap->va_flags != VNOVAL) {
		DPRINTF(VNCALL, ("%s: vp:%p td:%p flags:%lx\n", __func__, vp,
		    td, vap->va_flags));

		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		/*
		 * Callers may only modify the file flags on objects they
		 * have VADMIN rights for.
		 */
		if ((error = VOP_ACCESS(vp, VADMIN, cred, td)))
			return (error);
		/*
		 * Unprivileged processes are not permitted to unset system
		 * flags, or modify flags if any system flags are set.
		 * Privileged non-jail processes may not modify system flags
		 * if securelevel > 0 and any existing system flags are set.
		 * Privileged jail processes behave like privileged non-jail
		 * processes if the PR_ALLOW_CHFLAGS permission bit is set;
		 * otherwise, they behave like unprivileged processes.
		 */

		flags = inode->i_flags;
		if (!priv_check_cred(cred, PRIV_VFS_SYSFLAGS)) {
			if (flags & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND)) {
				error = securelevel_gt(cred, 0);
				if (error)
					return (error);
			}
			/* Snapshot flag cannot be set or cleared */
			if (((vap->va_flags & SF_SNAPSHOT) != 0 &&
			    (flags & SF_SNAPSHOT) == 0) ||
			    ((vap->va_flags & SF_SNAPSHOT) == 0 &&
			    (flags & SF_SNAPSHOT) != 0))
				return (EPERM);

			inode->i_flags = vap->va_flags;
		} else {
			if (flags & (SF_NOUNLINK | SF_IMMUTABLE | SF_APPEND) ||
			    (vap->va_flags & UF_SETTABLE) != vap->va_flags)
				return (EPERM);

			flags &= SF_SETTABLE;
			flags |= (vap->va_flags & UF_SETTABLE);
			inode->i_flags = flags;
		}
		node->nn_flags |= IN_CHANGE;
		if (vap->va_flags & (IMMUTABLE | APPEND))
			return (0);
	}
	if (inode->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);

	if (vap->va_size != (u_quad_t)VNOVAL) {
		DPRINTF(VNCALL, ("%s: vp:%p td:%p size:%jx\n", __func__, vp, td,
		    (uintmax_t)vap->va_size));

		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			if ((inode->i_flags & SF_SNAPSHOT) != 0)
				return (EPERM);
			break;
		default:
			return (0);
		}

		if (vap->va_size > node->nn_nandfsdev->nd_maxfilesize)
			return (EFBIG);

		KASSERT((vp->v_type == VREG), ("Set size %d", vp->v_type));
		nandfs_truncate(vp, vap->va_size);
		node->nn_flags |= IN_CHANGE;

		return (0);
	}

	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		DPRINTF(VNCALL, ("%s: vp:%p td:%p uid/gid %x/%x\n", __func__,
		    vp, td, vap->va_uid, vap->va_gid));
		error = nandfs_chown(vp, vap->va_uid, vap->va_gid, cred, td);
		if (error)
			return (error);
	}

	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		DPRINTF(VNCALL, ("%s: vp:%p td:%p mode %x\n", __func__, vp, td,
		    vap->va_mode));

		error = nandfs_chmod(vp, (int)vap->va_mode, cred, td);
		if (error)
			return (error);
	}
	if (vap->va_atime.tv_sec != VNOVAL ||
	    vap->va_mtime.tv_sec != VNOVAL ||
	    vap->va_birthtime.tv_sec != VNOVAL) {
		DPRINTF(VNCALL, ("%s: vp:%p td:%p time a/m/b %jx/%jx/%jx\n",
		    __func__, vp, td, (uintmax_t)vap->va_atime.tv_sec,
		    (uintmax_t)vap->va_mtime.tv_sec,
		    (uintmax_t)vap->va_birthtime.tv_sec));

		if (vap->va_atime.tv_sec != VNOVAL)
			node->nn_flags |= IN_ACCESS;
		if (vap->va_mtime.tv_sec != VNOVAL)
			node->nn_flags |= IN_CHANGE | IN_UPDATE;
		if (vap->va_birthtime.tv_sec != VNOVAL)
			node->nn_flags |= IN_MODIFIED;
		nandfs_itimes(vp);
		return (0);
	}

	return (0);
}

static int
nandfs_open(struct vop_open_args *ap)
{
	struct nandfs_node *node = VTON(ap->a_vp);
	uint64_t filesize;

	DPRINTF(VNCALL, ("nandfs_open called ap->a_mode %x\n", ap->a_mode));

	if (ap->a_vp->v_type == VCHR || ap->a_vp->v_type == VBLK)
		return (EOPNOTSUPP);

	if ((node->nn_inode.i_flags & APPEND) &&
	    (ap->a_mode & (FWRITE | O_APPEND)) == FWRITE)
		return (EPERM);

	filesize = node->nn_inode.i_size;
	vnode_create_vobject(ap->a_vp, filesize, ap->a_td);

	return (0);
}

static int
nandfs_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);

	DPRINTF(VNCALL, ("%s: vp %p node %p\n", __func__, vp, node));

	mtx_lock(&vp->v_interlock);
	if (vp->v_usecount > 1)
		nandfs_itimes_locked(vp);
	mtx_unlock(&vp->v_interlock);

	return (0);
}

static int
nandfs_check_possible(struct vnode *vp, struct vattr *vap, mode_t mode)
{

	/* Check if we are allowed to write */
	switch (vap->va_type) {
	case VDIR:
	case VLNK:
	case VREG:
		/*
		 * Normal nodes: check if we're on a read-only mounted
		 * filingsystem and bomb out if we're trying to write.
		 */
		if ((mode & VMODIFY_PERMS) && (vp->v_mount->mnt_flag & MNT_RDONLY))
			return (EROFS);
		break;
	case VBLK:
	case VCHR:
	case VSOCK:
	case VFIFO:
		/*
		 * Special nodes: even on read-only mounted filingsystems
		 * these are allowed to be written to if permissions allow.
		 */
		break;
	default:
		/* No idea what this is */
		return (EINVAL);
	}

	/* No one may write immutable files */
	if ((mode & VWRITE) && (VTON(vp)->nn_inode.i_flags & IMMUTABLE))
		return (EPERM);

	return (0);
}

static int
nandfs_check_permitted(struct vnode *vp, struct vattr *vap, mode_t mode,
    struct ucred *cred)
{

	return (vaccess(vp->v_type, vap->va_mode, vap->va_uid, vap->va_gid, mode,
	    cred, NULL));
}

static int
nandfs_advlock(struct vop_advlock_args *ap)
{
	struct nandfs_node *nvp;
	quad_t size;

	nvp = VTON(ap->a_vp);
	size = nvp->nn_inode.i_size;
	return (lf_advlock(ap, &(nvp->nn_lockf), size));
}

static int
nandfs_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;
	struct ucred *cred = ap->a_cred;
	struct vattr vap;
	int error;

	DPRINTF(VNCALL, ("%s: vp:%p mode: %x\n", __func__, vp, accmode));

	error = VOP_GETATTR(vp, &vap, NULL);
	if (error)
		return (error);

	error = nandfs_check_possible(vp, &vap, accmode);
	if (error)
		return (error);

	error = nandfs_check_permitted(vp, &vap, accmode, cred);

	return (error);
}

static int
nandfs_print(struct vop_print_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *nvp = VTON(vp);

	printf("\tvp=%p, nandfs_node=%p\n", vp, nvp);
	printf("nandfs inode %#jx\n", (uintmax_t)nvp->nn_ino);
	printf("flags = 0x%b\n", (u_int)nvp->nn_flags, PRINT_NODE_FLAGS);

	return (0);
}

static void
nandfs_read_filebuf(struct nandfs_node *node, struct buf *bp)
{
	struct nandfs_device *nandfsdev = node->nn_nandfsdev;
	struct buf *nbp;
	nandfs_daddr_t vblk, pblk;
	nandfs_lbn_t from;
	uint32_t blocksize;
	int error = 0;
	int blk2dev = nandfsdev->nd_blocksize / DEV_BSIZE;

	/*
	 * Translate all the block sectors into a series of buffers to read
	 * asynchronously from the nandfs device. Note that this lookup may
	 * induce readin's too.
	 */

	blocksize = nandfsdev->nd_blocksize;
	if (bp->b_bcount / blocksize != 1)
		panic("invalid b_count in bp %p\n", bp);

	from = bp->b_blkno;

	DPRINTF(READ, ("\tread in from inode %#jx blkno %#jx"
	    " count %#lx\n", (uintmax_t)node->nn_ino, from,
	    bp->b_bcount));

	/* Get virtual block numbers for the vnode's buffer span */
	error = nandfs_bmap_lookup(node, from, &vblk);
	if (error) {
		bp->b_error = EINVAL;
		bp->b_ioflags |= BIO_ERROR;
		bufdone(bp);
		return;
	}

	/* Translate virtual block numbers to physical block numbers */
	error = nandfs_vtop(node, vblk, &pblk);
	if (error) {
		bp->b_error = EINVAL;
		bp->b_ioflags |= BIO_ERROR;
		bufdone(bp);
		return;
	}

	/* Issue translated blocks */
	bp->b_resid = bp->b_bcount;

	/* Note virtual block 0 marks not mapped */
	if (vblk == 0) {
		vfs_bio_clrbuf(bp);
		bufdone(bp);
		return;
	}

	nbp = bp;
	nbp->b_blkno = pblk * blk2dev;
	bp->b_iooffset = dbtob(nbp->b_blkno);
	MPASS(bp->b_iooffset >= 0);
	BO_STRATEGY(&nandfsdev->nd_devvp->v_bufobj, nbp);
	nandfs_vblk_set(bp, vblk);
	DPRINTF(READ, ("read_filebuf : ino %#jx blk %#jx -> "
	    "%#jx -> %#jx [bp %p]\n", (uintmax_t)node->nn_ino,
	    (uintmax_t)(from), (uintmax_t)vblk,
	    (uintmax_t)pblk, nbp));
}

static void
nandfs_write_filebuf(struct nandfs_node *node, struct buf *bp)
{
	struct nandfs_device *nandfsdev = node->nn_nandfsdev;

	bp->b_iooffset = dbtob(bp->b_blkno);
	MPASS(bp->b_iooffset >= 0);
	BO_STRATEGY(&nandfsdev->nd_devvp->v_bufobj, bp);
}

static int
nandfs_strategy(struct vop_strategy_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct buf *bp = ap->a_bp;
	struct nandfs_node *node = VTON(vp);


	/* check if we ought to be here */
	KASSERT((vp->v_type != VBLK && vp->v_type != VCHR),
	    ("nandfs_strategy on type %d", vp->v_type));

	/* Translate if needed and pass on */
	if (bp->b_iocmd == BIO_READ) {
		nandfs_read_filebuf(node, bp);
		return (0);
	}

	/* Send to segment collector */
	nandfs_write_filebuf(node, bp);
	return (0);
}

static int
nandfs_readdir(struct vop_readdir_args *ap)
{
	struct uio *uio = ap->a_uio;
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_dir_entry *ndirent;
	struct dirent dirent;
	struct buf *bp;
	uint64_t file_size, diroffset, transoffset, blkoff;
	uint64_t blocknr;
	uint32_t blocksize = node->nn_nandfsdev->nd_blocksize;
	uint8_t *pos, name_len;
	int error;

	DPRINTF(READDIR, ("nandfs_readdir called\n"));

	if (vp->v_type != VDIR)
		return (ENOTDIR);

	file_size = node->nn_inode.i_size;
	DPRINTF(READDIR, ("nandfs_readdir filesize %jd resid %zd\n",
	    (uintmax_t)file_size, uio->uio_resid ));

	/* We are called just as long as we keep on pushing data in */
	error = 0;
	if ((uio->uio_offset < file_size) &&
	    (uio->uio_resid >= sizeof(struct dirent))) {
		diroffset = uio->uio_offset;
		transoffset = diroffset;

		blocknr = diroffset / blocksize;
		blkoff = diroffset % blocksize;
		error = nandfs_bread(node, blocknr, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (EIO);
		}
		while (diroffset < file_size) {
			DPRINTF(READDIR, ("readdir : offset = %"PRIu64"\n",
			    diroffset));
			if (blkoff >= blocksize) {
				blkoff = 0; blocknr++;
				brelse(bp);
				error = nandfs_bread(node, blocknr, NOCRED, 0,
				    &bp);
				if (error) {
					brelse(bp);
					return (EIO);
				}
			}

			/* Read in one dirent */
			pos = (uint8_t *)bp->b_data + blkoff;
			ndirent = (struct nandfs_dir_entry *)pos;

			name_len = ndirent->name_len;
			memset(&dirent, 0, sizeof(dirent));
			dirent.d_fileno = ndirent->inode;
			if (dirent.d_fileno) {
				dirent.d_type = ndirent->file_type;
				dirent.d_namlen = name_len;
				strncpy(dirent.d_name, ndirent->name, name_len);
				dirent.d_reclen = GENERIC_DIRSIZ(&dirent);
				/* NOTE: d_off is the offset of the *next* entry. */
				dirent.d_off = diroffset + ndirent->rec_len;
				dirent_terminate(&dirent);
				DPRINTF(READDIR, ("copying `%*.*s`\n", name_len,
				    name_len, dirent.d_name));
			}

			/*
			 * If there isn't enough space in the uio to return a
			 * whole dirent, break off read
			 */
			if (uio->uio_resid < GENERIC_DIRSIZ(&dirent))
				break;

			/* Transfer */
			if (dirent.d_fileno)
				uiomove(&dirent, dirent.d_reclen, uio);

			/* Advance */
			diroffset += ndirent->rec_len;
			blkoff += ndirent->rec_len;

			/* Remember the last entry we transferred */
			transoffset = diroffset;
		}
		brelse(bp);

		/* Pass on last transferred offset */
		uio->uio_offset = transoffset;
	}

	if (ap->a_eofflag)
		*ap->a_eofflag = (uio->uio_offset >= file_size);

	return (error);
}

static int
nandfs_dirempty(struct vnode *dvp, uint64_t parentino, struct ucred *cred)
{
	struct nandfs_node *dnode = VTON(dvp);
	struct nandfs_dir_entry *dirent;
	uint64_t file_size = dnode->nn_inode.i_size;
	uint64_t blockcount = dnode->nn_inode.i_blocks;
	uint64_t blocknr;
	uint32_t blocksize = dnode->nn_nandfsdev->nd_blocksize;
	uint32_t limit;
	uint32_t off;
	uint8_t	*pos;
	struct buf *bp;
	int error;

	DPRINTF(LOOKUP, ("%s: dvp %p parentino %#jx cred %p\n", __func__, dvp,
	    (uintmax_t)parentino, cred));

	KASSERT((file_size != 0), ("nandfs_dirempty for NULL dir %p", dvp));

	blocknr = 0;
	while (blocknr < blockcount) {
		error = nandfs_bread(dnode, blocknr, NOCRED, 0, &bp);
		if (error) {
			brelse(bp);
			return (0);
		}

		pos = (uint8_t *)bp->b_data;
		off = 0;

		if (blocknr == (blockcount - 1))
			limit = file_size % blocksize;
		else
			limit = blocksize;

		while (off < limit) {
			dirent = (struct nandfs_dir_entry *)(pos + off);
			off += dirent->rec_len;

			if (dirent->inode == 0)
				continue;

			switch (dirent->name_len) {
			case 0:
				break;
			case 1:
				if (dirent->name[0] != '.')
					goto notempty;

				KASSERT(dirent->inode == dnode->nn_ino,
				    (".'s inode does not match dir"));
				break;
			case 2:
				if (dirent->name[0] != '.' &&
				    dirent->name[1] != '.')
					goto notempty;

				KASSERT(dirent->inode == parentino,
				    ("..'s inode does not match parent"));
				break;
			default:
				goto notempty;
			}
		}

		brelse(bp);
		blocknr++;
	}

	return (1);
notempty:
	brelse(bp);
	return (0);
}

static int
nandfs_link(struct vop_link_args *ap)
{
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_inode *inode = &node->nn_inode;
	int error;

	if (inode->i_links_count >= NANDFS_LINK_MAX)
		return (EMLINK);

	if (inode->i_flags & (IMMUTABLE | APPEND))
		return (EPERM);

	/* Update link count */
	inode->i_links_count++;

	/* Add dir entry */
	error = nandfs_add_dirent(tdvp, node->nn_ino, cnp->cn_nameptr,
	    cnp->cn_namelen, IFTODT(inode->i_mode));
	if (error) {
		inode->i_links_count--;
	}

	node->nn_flags |= IN_CHANGE;
	nandfs_itimes(vp);
	DPRINTF(VNCALL, ("%s: tdvp %p vp %p cnp %p\n",
	    __func__, tdvp, vp, cnp));

	return (0);
}

static int
nandfs_create(struct vop_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	uint16_t mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfsmount *nmp = dir_node->nn_nmp;
	struct nandfs_node *node;
	int error;

	DPRINTF(VNCALL, ("%s: dvp %p\n", __func__, dvp));

	if (nandfs_fs_full(dir_node->nn_nandfsdev))
		return (ENOSPC);

	/* Create new vnode/inode */
	error = nandfs_node_create(nmp, &node, mode);
	if (error)
		return (error);
	node->nn_inode.i_gid = dir_node->nn_inode.i_gid;
	node->nn_inode.i_uid = cnp->cn_cred->cr_uid;

	/* Add new dir entry */
	error = nandfs_add_dirent(dvp, node->nn_ino, cnp->cn_nameptr,
	    cnp->cn_namelen, IFTODT(mode));
	if (error) {
		if (nandfs_node_destroy(node)) {
			nandfs_error("%s: error destroying node %p\n",
			    __func__, node);
		}
		return (error);
	}
	*vpp = NTOV(node);
	if ((cnp->cn_flags & MAKEENTRY) != 0)
		cache_enter(dvp, *vpp, cnp);

	DPRINTF(VNCALL, ("created file vp %p nandnode %p ino %jx\n", *vpp, node,
	    (uintmax_t)node->nn_ino));
	return (0);
}

static int
nandfs_remove(struct vop_remove_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_node *dnode = VTON(dvp);
	struct componentname *cnp = ap->a_cnp;

	DPRINTF(VNCALL, ("%s: dvp %p vp %p nandnode %p ino %#jx link %d\n",
	    __func__, dvp, vp, node, (uintmax_t)node->nn_ino,
	    node->nn_inode.i_links_count));

	if (vp->v_type == VDIR)
		return (EISDIR);

	/* Files marked as immutable or append-only cannot be deleted. */
	if ((node->nn_inode.i_flags & (IMMUTABLE | APPEND | NOUNLINK)) ||
	    (dnode->nn_inode.i_flags & APPEND))
		return (EPERM);

	nandfs_remove_dirent(dvp, node, cnp);
	node->nn_inode.i_links_count--;
	node->nn_flags |= IN_CHANGE;

	return (0);
}

/*
 * Check if source directory is in the path of the target directory.
 * Target is supplied locked, source is unlocked.
 * The target is always vput before returning.
 */
static int
nandfs_checkpath(struct nandfs_node *src, struct nandfs_node *dest,
    struct ucred *cred)
{
	struct vnode *vp;
	int error, rootino;
	struct nandfs_dir_entry dirent;

	vp = NTOV(dest);
	if (src->nn_ino == dest->nn_ino) {
		error = EEXIST;
		goto out;
	}
	rootino = NANDFS_ROOT_INO;
	error = 0;
	if (dest->nn_ino == rootino)
		goto out;

	for (;;) {
		if (vp->v_type != VDIR) {
			error = ENOTDIR;
			break;
		}

		error = vn_rdwr(UIO_READ, vp, (caddr_t)&dirent,
		    NANDFS_DIR_REC_LEN(2), (off_t)0, UIO_SYSSPACE,
		    IO_NODELOCKED | IO_NOMACCHECK, cred, NOCRED,
		    NULL, NULL);
		if (error != 0)
			break;
		if (dirent.name_len != 2 ||
		    dirent.name[0] != '.' ||
		    dirent.name[1] != '.') {
			error = ENOTDIR;
			break;
		}
		if (dirent.inode == src->nn_ino) {
			error = EINVAL;
			break;
		}
		if (dirent.inode == rootino)
			break;
		vput(vp);
		if ((error = VFS_VGET(vp->v_mount, dirent.inode,
		    LK_EXCLUSIVE, &vp)) != 0) {
			vp = NULL;
			break;
		}
	}

out:
	if (error == ENOTDIR)
		printf("checkpath: .. not a directory\n");
	if (vp != NULL)
		vput(vp);
	return (error);
}

static int
nandfs_rename(struct vop_rename_args *ap)
{
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	int doingdirectory = 0, oldparent = 0, newparent = 0;
	int error = 0;

	struct nandfs_node *fdnode, *fnode, *fnode1;
	struct nandfs_node *tdnode = VTON(tdvp);
	struct nandfs_node *tnode;

	uint32_t tdflags, fflags, fdflags;
	uint16_t mode;

	DPRINTF(VNCALL, ("%s: fdvp:%p fvp:%p tdvp:%p tdp:%p\n", __func__, fdvp,
	    fvp, tdvp, tvp));

	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
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

	tdflags = tdnode->nn_inode.i_flags;
	if (tvp &&
	    ((VTON(tvp)->nn_inode.i_flags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (tdflags & APPEND))) {
		error = EPERM;
		goto abortit;
	}

	/*
	 * Renaming a file to itself has no effect.  The upper layers should
	 * not call us in that case.  Temporarily just warn if they do.
	 */
	if (fvp == tvp) {
		printf("nandfs_rename: fvp == tvp (can't happen)\n");
		error = 0;
		goto abortit;
	}

	if ((error = vn_lock(fvp, LK_EXCLUSIVE)) != 0)
		goto abortit;

	fdnode = VTON(fdvp);
	fnode = VTON(fvp);

	if (fnode->nn_inode.i_links_count >= NANDFS_LINK_MAX) {
		VOP_UNLOCK(fvp, 0);
		error = EMLINK;
		goto abortit;
	}

	fflags = fnode->nn_inode.i_flags;
	fdflags = fdnode->nn_inode.i_flags;

	if ((fflags & (NOUNLINK | IMMUTABLE | APPEND)) ||
	    (fdflags & APPEND)) {
		VOP_UNLOCK(fvp, 0);
		error = EPERM;
		goto abortit;
	}

	mode = fnode->nn_inode.i_mode;
	if ((mode & S_IFMT) == S_IFDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */

		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    (fdvp == fvp) ||
		    ((fcnp->cn_flags | tcnp->cn_flags) & ISDOTDOT) ||
		    (fnode->nn_flags & IN_RENAME)) {
			VOP_UNLOCK(fvp, 0);
			error = EINVAL;
			goto abortit;
		}
		fnode->nn_flags |= IN_RENAME;
		doingdirectory = 1;
		DPRINTF(VNCALL, ("%s: doingdirectory dvp %p\n", __func__,
		    tdvp));
		oldparent = fdnode->nn_ino;
	}

	vrele(fdvp);

	tnode = NULL;
	if (tvp)
		tnode = VTON(tvp);

	/*
	 * Bump link count on fvp while we are moving stuff around. If we
	 * crash before completing the work, the link count may be wrong
	 * but correctable.
	 */
	fnode->nn_inode.i_links_count++;

	/* Check for in path moving XXX */
	error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_thread);
	VOP_UNLOCK(fvp, 0);
	if (oldparent != tdnode->nn_ino)
		newparent = tdnode->nn_ino;
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;
		if (tnode != NULL)
			vput(tvp);

		error = nandfs_checkpath(fnode, tdnode, tcnp->cn_cred);
		if (error)
			goto out;

		VREF(tdvp);
		error = relookup(tdvp, &tvp, tcnp);
		if (error)
			goto out;
		vrele(tdvp);
		tdnode = VTON(tdvp);
		tnode = NULL;
		if (tvp)
			tnode = VTON(tvp);
	}

	/*
	 * If the target doesn't exist, link the target to the source and
	 * unlink the source. Otherwise, rewrite the target directory to
	 * reference the source and remove the original entry.
	 */

	if (tvp == NULL) {
		/*
		 * Account for ".." in new directory.
		 */
		if (doingdirectory && fdvp != tdvp)
			tdnode->nn_inode.i_links_count++;

		DPRINTF(VNCALL, ("%s: new entry in dvp:%p\n", __func__, tdvp));
		/*
		 * Add name in new directory.
		 */
		error = nandfs_add_dirent(tdvp, fnode->nn_ino, tcnp->cn_nameptr,
		    tcnp->cn_namelen, IFTODT(fnode->nn_inode.i_mode));
		if (error) {
			if (doingdirectory && fdvp != tdvp)
				tdnode->nn_inode.i_links_count--;
			goto bad;
		}

		vput(tdvp);
	} else {
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((tdnode->nn_inode.i_mode & S_ISTXT) &&
		    tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != tdnode->nn_inode.i_uid &&
		    tnode->nn_inode.i_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		mode = tnode->nn_inode.i_mode;
		if ((mode & S_IFMT) == S_IFDIR) {
			if (!nandfs_dirempty(tvp, tdnode->nn_ino,
			    tcnp->cn_cred)) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
			/*
			 * Update name cache since directory is going away.
			 */
			cache_purge(tdvp);
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad;
		}

		DPRINTF(VNCALL, ("%s: update entry dvp:%p\n", __func__, tdvp));
		/*
		 * Change name tcnp in tdvp to point at fvp.
		 */
		error = nandfs_update_dirent(tdvp, fnode, tnode);
		if (error)
			goto bad;

		if (doingdirectory && !newparent)
			tdnode->nn_inode.i_links_count--;

		vput(tdvp);

		tnode->nn_inode.i_links_count--;
		vput(tvp);
		tnode = NULL;
	}

	/*
	 * Unlink the source.
	 */
	fcnp->cn_flags &= ~MODMASK;
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	VREF(fdvp);
	error = relookup(fdvp, &fvp, fcnp);
	if (error == 0)
		vrele(fdvp);
	if (fvp != NULL) {
		fnode1 = VTON(fvp);
		fdnode = VTON(fdvp);
	} else {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("nandfs_rename: lost dir entry");
		vrele(ap->a_fvp);
		return (0);
	}

	DPRINTF(VNCALL, ("%s: unlink source fnode:%p\n", __func__, fnode));

	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; its link
	 * count of three would cause a rmdir to fail with ENOTEMPTY.
	 * The IN_RENAME flag ensures that it cannot be moved by another
	 * rename.
	 */
	if (fnode != fnode1) {
		if (doingdirectory)
			panic("nandfs: lost dir entry");
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 */
		if (doingdirectory && newparent) {
			DPRINTF(VNCALL, ("%s: new parent %#jx -> %#jx\n",
			    __func__, (uintmax_t) oldparent,
			    (uintmax_t) newparent));
			error = nandfs_update_parent_dir(fvp, newparent);
			if (!error) {
				fdnode->nn_inode.i_links_count--;
				fdnode->nn_flags |= IN_CHANGE;
			}
		}
		error = nandfs_remove_dirent(fdvp, fnode, fcnp);
		if (!error) {
			fnode->nn_inode.i_links_count--;
			fnode->nn_flags |= IN_CHANGE;
		}
		fnode->nn_flags &= ~IN_RENAME;
	}
	if (fdnode)
		vput(fdvp);
	if (fnode)
		vput(fvp);
	vrele(ap->a_fvp);
	return (error);

bad:
	DPRINTF(VNCALL, ("%s: error:%d\n", __func__, error));
	if (tnode)
		vput(NTOV(tnode));
	vput(NTOV(tdnode));
out:
	if (doingdirectory)
		fnode->nn_flags &= ~IN_RENAME;
	if (vn_lock(fvp, LK_EXCLUSIVE) == 0) {
		fnode->nn_inode.i_links_count--;
		fnode->nn_flags |= IN_CHANGE;
		fnode->nn_flags &= ~IN_RENAME;
		vput(fvp);
	} else
		vrele(fvp);
	return (error);
}

static int
nandfs_mkdir(struct vop_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfs_inode *dir_inode = &dir_node->nn_inode;
	struct nandfs_node *node;
	struct nandfsmount *nmp = dir_node->nn_nmp;
	uint16_t mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);
	int error;

	DPRINTF(VNCALL, ("%s: dvp %p\n", __func__, dvp));

	if (nandfs_fs_full(dir_node->nn_nandfsdev))
		return (ENOSPC);

	if (dir_inode->i_links_count >= NANDFS_LINK_MAX)
		return (EMLINK);

	error = nandfs_node_create(nmp, &node, mode);
	if (error)
		return (error);

	node->nn_inode.i_gid = dir_node->nn_inode.i_gid;
	node->nn_inode.i_uid = cnp->cn_cred->cr_uid;

	*vpp = NTOV(node);

	error = nandfs_add_dirent(dvp, node->nn_ino, cnp->cn_nameptr,
	    cnp->cn_namelen, IFTODT(mode));
	if (error) {
		vput(*vpp);
		return (error);
	}

	dir_node->nn_inode.i_links_count++;
	dir_node->nn_flags |= IN_CHANGE;

	error = nandfs_init_dir(NTOV(node), node->nn_ino, dir_node->nn_ino);
	if (error) {
		vput(NTOV(node));
		return (error);
	}

	DPRINTF(VNCALL, ("created dir vp %p nandnode %p ino %jx\n", *vpp, node,
	    (uintmax_t)node->nn_ino));
	return (0);
}

static int
nandfs_mknod(struct vop_mknod_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct vattr *vap = ap->a_vap;
	uint16_t mode = MAKEIMODE(vap->va_type, vap->va_mode);
	struct componentname *cnp = ap->a_cnp;
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfsmount *nmp = dir_node->nn_nmp;
	struct nandfs_node *node;
	int error;

	if (nandfs_fs_full(dir_node->nn_nandfsdev))
		return (ENOSPC);

	error = nandfs_node_create(nmp, &node, mode);
	if (error)
		return (error);
	node->nn_inode.i_gid = dir_node->nn_inode.i_gid;
	node->nn_inode.i_uid = cnp->cn_cred->cr_uid;
	if (vap->va_rdev != VNOVAL)
		node->nn_inode.i_special = vap->va_rdev;

	*vpp = NTOV(node);

	if (nandfs_add_dirent(dvp, node->nn_ino, cnp->cn_nameptr,
	    cnp->cn_namelen, IFTODT(mode))) {
		vput(*vpp);
		return (ENOTDIR);
	}

	node->nn_flags |= IN_ACCESS | IN_CHANGE | IN_UPDATE;

	return (0);
}

static int
nandfs_symlink(struct vop_symlink_args *ap)
{
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	uint16_t mode = MAKEIMODE(ap->a_vap->va_type, ap->a_vap->va_mode);
	struct componentname *cnp = ap->a_cnp;
	struct nandfs_node *dir_node = VTON(dvp);
	struct nandfsmount *nmp = dir_node->nn_nmp;
	struct nandfs_node *node;
	int len, error;

	if (nandfs_fs_full(dir_node->nn_nandfsdev))
		return (ENOSPC);

	error = nandfs_node_create(nmp, &node, S_IFLNK | mode);
	if (error)
		return (error);
	node->nn_inode.i_gid = dir_node->nn_inode.i_gid;
	node->nn_inode.i_uid = cnp->cn_cred->cr_uid;

	*vpp = NTOV(node);

	if (nandfs_add_dirent(dvp, node->nn_ino, cnp->cn_nameptr,
	    cnp->cn_namelen, IFTODT(mode))) {
		vput(*vpp);
		return (ENOTDIR);
	}


	len = strlen(ap->a_target);
	error = vn_rdwr(UIO_WRITE, *vpp, __DECONST(void *, ap->a_target),
	    len, (off_t)0, UIO_SYSSPACE, IO_NODELOCKED | IO_NOMACCHECK,
	    cnp->cn_cred, NOCRED, NULL, NULL);
	if (error)
		vput(*vpp);

	return (error);
}

static int
nandfs_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;

	return (VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

static int
nandfs_rmdir(struct vop_rmdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct nandfs_node *node, *dnode;
	uint32_t dflag, flag;
	int error = 0;

	node = VTON(vp);
	dnode = VTON(dvp);

	/* Files marked as immutable or append-only cannot be deleted. */
	if ((node->nn_inode.i_flags & (IMMUTABLE | APPEND | NOUNLINK)) ||
	    (dnode->nn_inode.i_flags & APPEND))
		return (EPERM);

	DPRINTF(VNCALL, ("%s: dvp %p vp %p nandnode %p ino %#jx\n", __func__,
	    dvp, vp, node, (uintmax_t)node->nn_ino));

	if (node->nn_inode.i_links_count < 2)
		return (EINVAL);

	if (!nandfs_dirempty(vp, dnode->nn_ino, cnp->cn_cred))
		return (ENOTEMPTY);

	/* Files marked as immutable or append-only cannot be deleted. */
	dflag = dnode->nn_inode.i_flags;
	flag = node->nn_inode.i_flags;
	if ((dflag & APPEND) ||
	    (flag & (NOUNLINK | IMMUTABLE | APPEND))) {
		return (EPERM);
	}

	if (vp->v_mountedhere != 0)
		return (EINVAL);

	nandfs_remove_dirent(dvp, node, cnp);
	dnode->nn_inode.i_links_count -= 1;
	dnode->nn_flags |= IN_CHANGE;

	cache_purge(dvp);

	error = nandfs_truncate(vp, (uint64_t)0);
	if (error)
		return (error);

	node->nn_inode.i_links_count -= 2;
	node->nn_flags |= IN_CHANGE;

	cache_purge(vp);

	return (error);
}

static int
nandfs_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	int locked;

	DPRINTF(VNCALL, ("%s: vp %p nandnode %p ino %#jx\n", __func__, vp,
	    node, (uintmax_t)node->nn_ino));

	/*
	 * Start syncing vnode only if inode was modified or
	 * there are some dirty buffers
	 */
	if (VTON(vp)->nn_flags & IN_MODIFIED ||
	    vp->v_bufobj.bo_dirty.bv_cnt) {
		locked = VOP_ISLOCKED(vp);
		VOP_UNLOCK(vp, 0);
		nandfs_wakeup_wait_sync(node->nn_nandfsdev, SYNCER_FSYNC);
		VOP_LOCK(vp, locked | LK_RETRY);
	}

	return (0);
}

static int
nandfs_bmap(struct vop_bmap_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *nnode = VTON(vp);
	struct nandfs_device *nandfsdev = nnode->nn_nandfsdev;
	nandfs_daddr_t l2vmap, v2pmap;
	int error;
	int blk2dev = nandfsdev->nd_blocksize / DEV_BSIZE;

	DPRINTF(VNCALL, ("%s: vp %p nandnode %p ino %#jx\n", __func__, vp,
	    nnode, (uintmax_t)nnode->nn_ino));

	if (ap->a_bop != NULL)
		*ap->a_bop = &nandfsdev->nd_devvp->v_bufobj;
	if (ap->a_bnp == NULL)
		return (0);
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	if (ap->a_runb != NULL)
		*ap->a_runb = 0;

	/*
	 * Translate all the block sectors into a series of buffers to read
	 * asynchronously from the nandfs device. Note that this lookup may
	 * induce readin's too.
	 */

	/* Get virtual block numbers for the vnode's buffer span */
	error = nandfs_bmap_lookup(nnode, ap->a_bn, &l2vmap);
	if (error)
		return (-1);

	/* Translate virtual block numbers to physical block numbers */
	error = nandfs_vtop(nnode, l2vmap, &v2pmap);
	if (error)
		return (-1);

	/* Note virtual block 0 marks not mapped */
	if (l2vmap == 0)
		*ap->a_bnp = -1;
	else
		*ap->a_bnp = v2pmap * blk2dev;	/* in DEV_BSIZE */

	DPRINTF(VNCALL, ("%s: vp %p nandnode %p ino %#jx lblk %jx -> blk %jx\n",
	    __func__, vp, nnode, (uintmax_t)nnode->nn_ino, (uintmax_t)ap->a_bn,
	    (uintmax_t)*ap->a_bnp ));

	return (0);
}

static void
nandfs_force_syncer(struct nandfsmount *nmp)
{

	nmp->nm_flags |= NANDFS_FORCE_SYNCER;
	nandfs_wakeup_wait_sync(nmp->nm_nandfsdev, SYNCER_FFORCE);
}

static int
nandfs_ioctl(struct vop_ioctl_args *ap)
{
	struct vnode *vp = ap->a_vp;
	u_long command = ap->a_command;
	caddr_t data = ap->a_data;
	struct nandfs_node *node = VTON(vp);
	struct nandfs_device *nandfsdev = node->nn_nandfsdev;
	struct nandfsmount *nmp = node->nn_nmp;
	uint64_t *tab, *cno;
	struct nandfs_seg_stat *nss;
	struct nandfs_cpmode *ncpm;
	struct nandfs_argv *nargv;
	struct nandfs_cpstat *ncp;
	int error;

	DPRINTF(VNCALL, ("%s: %x\n", __func__, (uint32_t)command));

	error = priv_check(ap->a_td, PRIV_VFS_MOUNT);
	if (error)
		return (error);

	if (nmp->nm_ronly) {
		switch (command) {
		case NANDFS_IOCTL_GET_FSINFO:
		case NANDFS_IOCTL_GET_SUSTAT:
		case NANDFS_IOCTL_GET_CPINFO:
		case NANDFS_IOCTL_GET_CPSTAT:
		case NANDFS_IOCTL_GET_SUINFO:
		case NANDFS_IOCTL_GET_VINFO:
		case NANDFS_IOCTL_GET_BDESCS:
			break;
		default:
			return (EROFS);
		}
	}

	switch (command) {
	case NANDFS_IOCTL_GET_FSINFO:
		error = nandfs_get_fsinfo(nmp, (struct nandfs_fsinfo *)data);
		break;
	case NANDFS_IOCTL_GET_SUSTAT:
		nss = (struct nandfs_seg_stat *)data;
		error = nandfs_get_seg_stat(nandfsdev, nss);
		break;
	case NANDFS_IOCTL_CHANGE_CPMODE:
		ncpm = (struct nandfs_cpmode *)data;
		error = nandfs_chng_cpmode(nandfsdev->nd_cp_node, ncpm);
		nandfs_force_syncer(nmp);
		break;
	case NANDFS_IOCTL_GET_CPINFO:
		nargv = (struct nandfs_argv *)data;
		error = nandfs_get_cpinfo_ioctl(nandfsdev->nd_cp_node, nargv);
		break;
	case NANDFS_IOCTL_DELETE_CP:
		tab = (uint64_t *)data;
		error = nandfs_delete_cp(nandfsdev->nd_cp_node, tab[0], tab[1]);
		nandfs_force_syncer(nmp);
		break;
	case NANDFS_IOCTL_GET_CPSTAT:
		ncp = (struct nandfs_cpstat *)data;
		error = nandfs_get_cpstat(nandfsdev->nd_cp_node, ncp);
		break;
	case NANDFS_IOCTL_GET_SUINFO:
		nargv = (struct nandfs_argv *)data;
		error = nandfs_get_segment_info_ioctl(nandfsdev, nargv);
		break;
	case NANDFS_IOCTL_GET_VINFO:
		nargv = (struct nandfs_argv *)data;
		error = nandfs_get_dat_vinfo_ioctl(nandfsdev, nargv);
		break;
	case NANDFS_IOCTL_GET_BDESCS:
		nargv = (struct nandfs_argv *)data;
		error = nandfs_get_dat_bdescs_ioctl(nandfsdev, nargv);
		break;
	case NANDFS_IOCTL_SYNC:
		cno = (uint64_t *)data;
		nandfs_force_syncer(nmp);
		*cno = nandfsdev->nd_last_cno;
		error = 0;
		break;
	case NANDFS_IOCTL_MAKE_SNAP:
		cno = (uint64_t *)data;
		error = nandfs_make_snap(nandfsdev, cno);
		nandfs_force_syncer(nmp);
		break;
	case NANDFS_IOCTL_DELETE_SNAP:
		cno = (uint64_t *)data;
		error = nandfs_delete_snap(nandfsdev, *cno);
		nandfs_force_syncer(nmp);
		break;
	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

/*
 * Whiteout vnode call
 */
static int
nandfs_whiteout(struct vop_whiteout_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	int error = 0;

	switch (ap->a_flags) {
	case LOOKUP:
		return (0);
	case CREATE:
		/* Create a new directory whiteout */
#ifdef INVARIANTS
		if ((cnp->cn_flags & SAVENAME) == 0)
			panic("nandfs_whiteout: missing name");
#endif
		error = nandfs_add_dirent(dvp, NANDFS_WHT_INO, cnp->cn_nameptr,
		    cnp->cn_namelen, DT_WHT);
		break;

	case DELETE:
		/* Remove an existing directory whiteout */
		cnp->cn_flags &= ~DOWHITEOUT;
		error = nandfs_remove_dirent(dvp, NULL, cnp);
		break;
	default:
		panic("nandf_whiteout: unknown op: %d", ap->a_flags);
	}

	return (error);
}

static int
nandfs_pathconf(struct vop_pathconf_args *ap)
{
	int error;

	error = 0;
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = NANDFS_LINK_MAX;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NANDFS_NAME_LEN;
		break;
	case _PC_PIPE_BUF:
		if (ap->a_vp->v_type == VDIR || ap->a_vp->v_type == VFIFO)
			*ap->a_retval = PIPE_BUF;
		else
			error = EINVAL;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		break;
	case _PC_ALLOC_SIZE_MIN:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_bsize;
		break;
	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		break;
	case _PC_REC_INCR_XFER_SIZE:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_iosize;
		break;
	case _PC_REC_MAX_XFER_SIZE:
		*ap->a_retval = -1; /* means ``unlimited'' */
		break;
	case _PC_REC_MIN_XFER_SIZE:
		*ap->a_retval = ap->a_vp->v_mount->mnt_stat.f_iosize;
		break;
	default:
		error = vop_stdpathconf(ap);
		break;
	}
	return (error);
}

static int
nandfs_vnlock1(struct vop_lock1_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	int error, vi_locked;

	/*
	 * XXX can vnode go away while we are sleeping?
	 */
	vi_locked = mtx_owned(&vp->v_interlock);
	if (vi_locked)
		VI_UNLOCK(vp);
	error = NANDFS_WRITELOCKFLAGS(node->nn_nandfsdev,
	    ap->a_flags & LK_NOWAIT);
	if (vi_locked && !error)
		VI_LOCK(vp);
	if (error)
		return (error);

	error = vop_stdlock(ap);
	if (error) {
		NANDFS_WRITEUNLOCK(node->nn_nandfsdev);
		return (error);
	}

	return (0);
}

static int
nandfs_vnunlock(struct vop_unlock_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);
	int error;

	error = vop_stdunlock(ap);
	if (error)
		return (error);

	NANDFS_WRITEUNLOCK(node->nn_nandfsdev);

	return (0);
}

/*
 * Global vfs data structures
 */
struct vop_vector nandfs_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_access =		nandfs_access,
	.vop_advlock =		nandfs_advlock,
	.vop_bmap =		nandfs_bmap,
	.vop_close =		nandfs_close,
	.vop_create =		nandfs_create,
	.vop_fsync =		nandfs_fsync,
	.vop_getattr =		nandfs_getattr,
	.vop_inactive =		nandfs_inactive,
	.vop_cachedlookup =	nandfs_lookup,
	.vop_ioctl =		nandfs_ioctl,
	.vop_link =		nandfs_link,
	.vop_lookup =		vfs_cache_lookup,
	.vop_mkdir =		nandfs_mkdir,
	.vop_mknod =		nandfs_mknod,
	.vop_open =		nandfs_open,
	.vop_pathconf =		nandfs_pathconf,
	.vop_print =		nandfs_print,
	.vop_read =		nandfs_read,
	.vop_readdir =		nandfs_readdir,
	.vop_readlink =		nandfs_readlink,
	.vop_reclaim =		nandfs_reclaim,
	.vop_remove =		nandfs_remove,
	.vop_rename =		nandfs_rename,
	.vop_rmdir =		nandfs_rmdir,
	.vop_whiteout =		nandfs_whiteout,
	.vop_write =		nandfs_write,
	.vop_setattr =		nandfs_setattr,
	.vop_strategy =		nandfs_strategy,
	.vop_symlink =		nandfs_symlink,
	.vop_lock1 =		nandfs_vnlock1,
	.vop_unlock =		nandfs_vnunlock,
};

struct vop_vector nandfs_system_vnodeops = {
	.vop_default =		&default_vnodeops,
	.vop_close =		nandfs_close,
	.vop_inactive =		nandfs_inactive,
	.vop_reclaim =		nandfs_reclaim,
	.vop_strategy =		nandfs_strategy,
	.vop_fsync =		nandfs_fsync,
	.vop_bmap =		nandfs_bmap,
	.vop_access =		VOP_PANIC,
	.vop_advlock =		VOP_PANIC,
	.vop_create =		VOP_PANIC,
	.vop_getattr =		VOP_PANIC,
	.vop_cachedlookup =	VOP_PANIC,
	.vop_ioctl =		VOP_PANIC,
	.vop_link =		VOP_PANIC,
	.vop_lookup =		VOP_PANIC,
	.vop_mkdir =		VOP_PANIC,
	.vop_mknod =		VOP_PANIC,
	.vop_open =		VOP_PANIC,
	.vop_pathconf =		VOP_PANIC,
	.vop_print =		VOP_PANIC,
	.vop_read =		VOP_PANIC,
	.vop_readdir =		VOP_PANIC,
	.vop_readlink =		VOP_PANIC,
	.vop_remove =		VOP_PANIC,
	.vop_rename =		VOP_PANIC,
	.vop_rmdir =		VOP_PANIC,
	.vop_whiteout =		VOP_PANIC,
	.vop_write =		VOP_PANIC,
	.vop_setattr =		VOP_PANIC,
	.vop_symlink =		VOP_PANIC,
};

static int
nandfsfifo_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct nandfs_node *node = VTON(vp);

	DPRINTF(VNCALL, ("%s: vp %p node %p\n", __func__, vp, node));

	mtx_lock(&vp->v_interlock);
	if (vp->v_usecount > 1)
		nandfs_itimes_locked(vp);
	mtx_unlock(&vp->v_interlock);

	return (fifo_specops.vop_close(ap));
}

struct vop_vector nandfs_fifoops = {
	.vop_default =		&fifo_specops,
	.vop_fsync =		VOP_PANIC,
	.vop_access =		nandfs_access,
	.vop_close =		nandfsfifo_close,
	.vop_getattr =		nandfs_getattr,
	.vop_inactive =		nandfs_inactive,
	.vop_pathconf =		nandfs_pathconf,
	.vop_print =		nandfs_print,
	.vop_read =		VOP_PANIC,
	.vop_reclaim =		nandfs_reclaim,
	.vop_setattr =		nandfs_setattr,
	.vop_write =		VOP_PANIC,
	.vop_lock1 =		nandfs_vnlock1,
	.vop_unlock =		nandfs_vnunlock,
};

int
nandfs_vinit(struct vnode *vp, uint64_t ino)
{
	struct nandfs_node *node;

	ASSERT_VOP_LOCKED(vp, __func__);

	node = VTON(vp);

	/* Check if we're fetching the root */
	if (ino == NANDFS_ROOT_INO)
		vp->v_vflag |= VV_ROOT;

	if (ino != NANDFS_GC_INO)
		vp->v_type = IFTOVT(node->nn_inode.i_mode);
	else
		vp->v_type = VREG;

	if (vp->v_type == VFIFO)
		vp->v_op = &nandfs_fifoops;

	return (0);
}
