/*	$OpenBSD: ext2fs_vnops.c,v 1.95 2025/06/01 00:32:54 rsadowski Exp $	*/
/*	$NetBSD: ext2fs_vnops.c,v 1.1 1997/06/11 09:34:09 bouyer Exp $	*/

/*
 * Copyright (c) 1997 Manuel Bouyer.
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *
 *	@(#)ufs_vnops.c	8.14 (Berkeley) 10/26/94
 * Modified for ext2fs by Manuel Bouyer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/resourcevar.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/signalvar.h>
#include <sys/specdev.h>
#include <sys/unistd.h>

#include <miscfs/fifofs/fifo.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufsmount.h>

#include <ufs/ext2fs/ext2fs.h>
#include <ufs/ext2fs/ext2fs_extern.h>
#include <ufs/ext2fs/ext2fs_dir.h>

#include <uvm/uvm_extern.h>

static int ext2fs_chmod(struct vnode *, mode_t, struct ucred *);
static int ext2fs_chown(struct vnode *, uid_t, gid_t, struct ucred *);

/*
 * Create a regular file
 */
int
ext2fs_create(void *v)
{
	struct vop_create_args *ap = v;

	return (ext2fs_makeinode(MAKEIMODE(ap->a_vap->va_type,
	    ap->a_vap->va_mode), ap->a_dvp, ap->a_vpp, ap->a_cnp));
}

/*
 * Mknod vnode call
 */
int
ext2fs_mknod(void *v)
{
	struct vop_mknod_args *ap = v;
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp = ap->a_vpp;
	struct inode *ip;
	int error;

	error = ext2fs_makeinode(MAKEIMODE(vap->va_type, vap->va_mode),
	    ap->a_dvp, vpp, ap->a_cnp);
	if (error != 0)
		return (error);
	ip = VTOI(*vpp);
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	if (vap->va_rdev != VNOVAL) {
		/*
		 * Want to be able to use this to make badblock
		 * inodes, so don't truncate the dev number.
		 */
		ip->i_e2din->e2di_rdev = htole32(vap->va_rdev);
	}
	/*
	 * Remove inode so that it will be reloaded by VFS_VGET and
	 * checked to see if it is an alias of an existing entry in
	 * the inode cache.
	 */
	vput(*vpp);
	(*vpp)->v_type = VNON;
	vgone(*vpp);
	*vpp = NULL;
	return (0);
}

/*
 * Open called.
 *
 * Just check the APPEND flag.
 */
int
ext2fs_open(void *v)
{
	struct vop_open_args *ap = v;

	/*
	 * Files marked append-only must be opened for appending.
	 */
	if ((VTOI(ap->a_vp)->i_e2fs_flags & EXT2_APPEND) &&
		(ap->a_mode & (FWRITE | O_APPEND)) == FWRITE)
		return (EPERM);
	return (0);
}

int
ext2fs_access(void *v)
{
	struct vop_access_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	mode_t mode = ap->a_mode;

	/* If immutable bit set, nobody gets to write it. */
	if ((mode & VWRITE) && (ip->i_e2fs_flags & EXT2_IMMUTABLE))
		return (EPERM);

	return (vaccess(vp->v_type, ip->i_e2fs_mode, ip->i_e2fs_uid,
			ip->i_e2fs_gid, mode, ap->a_cred));
}

int
ext2fs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct vattr *vap = ap->a_vap;

	EXT2FS_ITIMES(ip);
	/*
	 * Copy from inode table
	 */
	vap->va_fsid = ip->i_dev;
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_e2fs_mode & ALLPERMS;
	vap->va_nlink = ip->i_e2fs_nlink;
	vap->va_uid = ip->i_e2fs_uid;
	vap->va_gid = ip->i_e2fs_gid;
	vap->va_rdev = (dev_t) letoh32(ip->i_e2din->e2di_rdev);
	vap->va_size = ext2fs_size(ip);
	vap->va_atime.tv_sec = ip->i_e2fs_atime;
	vap->va_atime.tv_nsec = 0;
	vap->va_mtime.tv_sec = ip->i_e2fs_mtime;
	vap->va_mtime.tv_nsec = 0;
	vap->va_ctime.tv_sec = ip->i_e2fs_ctime;
	vap->va_ctime.tv_nsec = 0;
	vap->va_flags = (ip->i_e2fs_flags & EXT2_APPEND) ? SF_APPEND : 0;
	vap->va_flags |= (ip->i_e2fs_flags & EXT2_IMMUTABLE) ? SF_IMMUTABLE : 0;
	vap->va_gen = ip->i_e2fs_gen;
	/* this doesn't belong here */
	if (vp->v_type == VBLK)
		vap->va_blocksize = BLKDEV_IOSIZE;
	else if (vp->v_type == VCHR)
		vap->va_blocksize = MAXBSIZE;
	else
		vap->va_blocksize = vp->v_mount->mnt_stat.f_iosize;
	vap->va_bytes = dbtob((u_quad_t)ip->i_e2fs_nblock);
	vap->va_type = vp->v_type;
	vap->va_filerev = ip->i_modrev;
	return (0);
}

/*
 * Set attribute vnode op. called from several syscalls
 */
int
ext2fs_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	struct ucred *cred = ap->a_cred;
	int error;

	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
		(vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
		(vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
		((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL)) {
		return (EINVAL);
	}
	if (vap->va_flags != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_e2fs_uid &&
			(error = suser_ucred(cred)))
			return (error);
		if (cred->cr_uid == 0) {
			if ((ip->i_e2fs_flags &
			    (EXT2_APPEND | EXT2_IMMUTABLE)) && securelevel > 0)
				return (EPERM);
			ip->i_e2fs_flags &= ~(EXT2_APPEND | EXT2_IMMUTABLE);
			ip->i_e2fs_flags |=
			    (vap->va_flags & SF_APPEND) ? EXT2_APPEND : 0 |
			    (vap->va_flags & SF_IMMUTABLE) ? EXT2_IMMUTABLE: 0;
		} else {
			return (EPERM);
		}
		ip->i_flag |= IN_CHANGE;
		if (vap->va_flags & (IMMUTABLE | APPEND))
			return (0);
	}
	if (ip->i_e2fs_flags & (EXT2_APPEND | EXT2_IMMUTABLE))
		return (EPERM);
	/*
	 * Go through the fields and update iff not VNOVAL.
	 */
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		error = ext2fs_chown(vp, vap->va_uid, vap->va_gid, cred);
		if (error)
			return (error);
	}
	if (vap->va_size != VNOVAL) {
		/*
		 * Disallow write attempts on read-only file systems;
		 * unless the file is a socket, fifo, or a block or
		 * character device resident on the file system.
		 */
		switch (vp->v_type) {
		case VDIR:
			return (EISDIR);
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
		default:
			break;
		}
		error = ext2fs_truncate(ip, vap->va_size, 0, cred);
		if (error)
			return (error);
	}
	if ((vap->va_vaflags & VA_UTIMES_CHANGE) ||
	    vap->va_atime.tv_nsec != VNOVAL ||
	    vap->va_mtime.tv_nsec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		if (cred->cr_uid != ip->i_e2fs_uid &&
			(error = suser_ucred(cred)) &&
			((vap->va_vaflags & VA_UTIMES_NULL) == 0 ||
			(error = VOP_ACCESS(vp, VWRITE, cred, ap->a_p))))
			return (error);
		if (vap->va_mtime.tv_nsec != VNOVAL)
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
		else if (vap->va_vaflags & VA_UTIMES_CHANGE)
			ip->i_flag |= IN_CHANGE;
		if (vap->va_atime.tv_nsec != VNOVAL) {
			if (!(vp->v_mount->mnt_flag & MNT_NOATIME) ||
			    (ip->i_flag & (IN_CHANGE | IN_UPDATE)))
				ip->i_flag |= IN_ACCESS;
		}
		EXT2FS_ITIMES(ip);
		if (vap->va_mtime.tv_nsec != VNOVAL)
			ip->i_e2fs_mtime = vap->va_mtime.tv_sec;
		if (vap->va_atime.tv_nsec != VNOVAL)
			ip->i_e2fs_atime = vap->va_atime.tv_sec;
		error = ext2fs_update(ip, 1);
		if (error)
			return (error);
	}
	error = 0;
	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY)
			return (EROFS);
		error = ext2fs_chmod(vp, vap->va_mode, cred);
	}
	return (error);
}

/*
 * Change the mode on a file.
 * Inode must be locked before calling.
 */
static int
ext2fs_chmod(struct vnode *vp, mode_t mode, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	int error;

	if (cred->cr_uid != ip->i_e2fs_uid && (error = suser_ucred(cred)))
		return (error);
	if (cred->cr_uid) {
		if (vp->v_type != VDIR && (mode & S_ISTXT))
			return (EFTYPE);
		if (!groupmember(ip->i_e2fs_gid, cred) && (mode & ISGID))
			return (EPERM);
	}
	ip->i_e2fs_mode &= ~ALLPERMS;
	ip->i_e2fs_mode |= (mode & ALLPERMS);
	ip->i_flag |= IN_CHANGE;
	if ((vp->v_flag & VTEXT) && (ip->i_e2fs_mode & S_ISTXT) == 0)
		(void) uvm_vnp_uncache(vp);
	return (0);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
static int
ext2fs_chown(struct vnode *vp, uid_t uid, gid_t gid, struct ucred *cred)
{
	struct inode *ip = VTOI(vp);
	uid_t ouid;
	gid_t ogid;
	int error = 0;

	if (uid == (uid_t)VNOVAL)
		uid = ip->i_e2fs_uid;
	if (gid == (gid_t)VNOVAL)
		gid = ip->i_e2fs_gid;
	/*
	 * If we don't own the file, are trying to change the owner
	 * of the file, or are not a member of the target group,
	 * the caller must be superuser or the call fails.
	 */
	if ((cred->cr_uid != ip->i_e2fs_uid || uid != ip->i_e2fs_uid ||
		(gid != ip->i_e2fs_gid && !groupmember(gid, cred))) &&
		(error = suser_ucred(cred)))
		return (error);
	ogid = ip->i_e2fs_gid;
	ouid = ip->i_e2fs_uid;

	ip->i_e2fs_gid = gid;
	ip->i_e2fs_uid = uid;
	if (ouid != uid || ogid != gid)
		ip->i_flag |= IN_CHANGE;
	if (ouid != uid && cred->cr_uid != 0)
		ip->i_e2fs_mode &= ~ISUID;
	if (ogid != gid && cred->cr_uid != 0)
		ip->i_e2fs_mode &= ~ISGID;
	return (0);
}

int
ext2fs_remove(void *v)
{
	struct vop_remove_args *ap = v;
	struct inode *ip;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	int error;

	ip = VTOI(vp);
	if (vp->v_type == VDIR ||
		(ip->i_e2fs_flags & (EXT2_IMMUTABLE | EXT2_APPEND)) ||
	    	(VTOI(dvp)->i_e2fs_flags & EXT2_APPEND)) {
		error = EPERM;
		goto out;
	}
	error = ext2fs_dirremove(dvp, ap->a_cnp);
	if (error == 0) {
		ip->i_e2fs_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
out:
	return (error);
}

/*
 * link vnode call
 */
int
ext2fs_link(void *v)
{
	struct vop_link_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip;
	int error;

#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2fs_link: no name");
#endif
	if (dvp != vp && (error = vn_lock(vp, LK_EXCLUSIVE))) {
		VOP_ABORTOP(dvp, cnp);
		goto out2;
	}
	ip = VTOI(vp);
	if ((nlink_t)ip->i_e2fs_nlink >= LINK_MAX) {
		VOP_ABORTOP(dvp, cnp);
		error = EMLINK;
		goto out1;
	}
	if (ip->i_e2fs_flags & (EXT2_IMMUTABLE | EXT2_APPEND)) {
		VOP_ABORTOP(dvp, cnp);
		error = EPERM;
		goto out1;
	}
	ip->i_e2fs_nlink++;
	ip->i_flag |= IN_CHANGE;
	error = ext2fs_update(ip, 1);
	if (!error)
		error = ext2fs_direnter(ip, dvp, cnp);
	if (error) {
		ip->i_e2fs_nlink--;
		ip->i_flag |= IN_CHANGE;
	}
	pool_put(&namei_pool, cnp->cn_pnbuf);
out1:
	if (dvp != vp)
		VOP_UNLOCK(vp);
out2:
	vput(dvp);
	return (error);
}

/*
 * Rename system call.
 * 	rename("foo", "bar");
 * is essentially
 *	unlink("bar");
 *	link("foo", "bar");
 *	unlink("foo");
 * but ``atomically''.  Can't do full commit without saving state in the
 * inode on disk which isn't feasible at this time.  Best we can do is
 * always guarantee the target exists.
 *
 * Basic algorithm is:
 *
 * 1) Bump link count on source while we're linking it to the
 *    target.  This also ensure the inode won't be deleted out
 *    from underneath us while we work (it may be truncated by
 *    a concurrent `trunc' or `open' for creation).
 * 2) Link source to destination.  If destination already exists,
 *    delete it first.
 * 3) Unlink source reference to inode if still around. If a
 *    directory was moved and the parent of the destination
 *    is different from the source, patch the ".." entry in the
 *    directory.
 */
int
ext2fs_rename(void *v)
{
	struct vop_rename_args  *ap = v;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct inode *ip, *xp, *dp;
	struct ext2fs_dirtemplate dirbuf;
	/* struct timespec ts; */
	int doingdirectory = 0, oldparent = 0, newparent = 0;
	int error = 0;
	u_char namlen;

#ifdef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("ext2fs_rename: no name");
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
	 * Check if just deleting a link name.
	 */
	if (tvp && ((VTOI(tvp)->i_e2fs_flags & (EXT2_IMMUTABLE | EXT2_APPEND)) ||
	    (VTOI(tdvp)->i_e2fs_flags & EXT2_APPEND))) {
		error = EPERM;
		goto abortit;
	}
	if (fvp == tvp) {
		if (fvp->v_type == VDIR) {
			error = EINVAL;
			goto abortit;
		}

		/* Release destination completely. */
		VOP_ABORTOP(tdvp, tcnp);
		vput(tdvp);
		vput(tvp);

		/* Delete source. */
		vrele(fdvp);
		vrele(fvp);
		fcnp->cn_flags &= ~MODMASK;
		fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
		if ((fcnp->cn_flags & SAVESTART) == 0)
			panic("ext2fs_rename: lost from startdir");
		fcnp->cn_nameiop = DELETE;
		(void) vfs_relookup(fdvp, &fvp, fcnp);
		return (VOP_REMOVE(fdvp, fvp, fcnp));
	}
	if ((error = vn_lock(fvp, LK_EXCLUSIVE)) != 0)
		goto abortit;
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
	if ((nlink_t)ip->i_e2fs_nlink >= LINK_MAX) {
		VOP_UNLOCK(fvp);
		error = EMLINK;
		goto abortit;
	}
	if ((ip->i_e2fs_flags & (EXT2_IMMUTABLE | EXT2_APPEND)) ||
		(dp->i_e2fs_flags & EXT2_APPEND)) {
		VOP_UNLOCK(fvp);
		error = EPERM;
		goto abortit;
	}
	if ((ip->i_e2fs_mode & IFMT) == IFDIR) {
		error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_proc);
		if (!error && tvp)
			error = VOP_ACCESS(tvp, VWRITE, tcnp->cn_cred,
			    tcnp->cn_proc);
		if (error) {
			VOP_UNLOCK(fvp);
			error = EACCES;
			goto abortit;
		}
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip ||
			(fcnp->cn_flags&ISDOTDOT) ||
			(tcnp->cn_flags & ISDOTDOT) ||
		    (ip->i_flag & IN_RENAME)) {
			VOP_UNLOCK(fvp);
			error = EINVAL;
			goto abortit;
		}
		ip->i_flag |= IN_RENAME;
		oldparent = dp->i_number;
		doingdirectory++;
	}
	vrele(fdvp);

	/*
	 * When the target exists, both the directory
	 * and target vnodes are returned locked.
	 */
	dp = VTOI(tdvp);
	xp = NULL;
	if (tvp)
		xp = VTOI(tvp);

	/*
	 * 1) Bump link count while we're moving stuff
	 *    around.  If we crash somewhere before
	 *    completing our work, the link count
	 *    may be wrong, but correctable.
	 */
	ip->i_e2fs_nlink++;
	ip->i_flag |= IN_CHANGE;
	if ((error = ext2fs_update(ip, 1)) != 0) {
		VOP_UNLOCK(fvp);
		goto bad;
	}

	/*
	 * If ".." must be changed (ie the directory gets a new
	 * parent) then the source directory must not be in the
	 * directory hierarchy above the target, as this would
	 * orphan everything below the source directory. Also
	 * the user must have write permission in the source so
	 * as to be able to change "..". We must repeat the call
	 * to namei, as the parent directory is unlocked by the
	 * call to checkpath().
	 */
	error = VOP_ACCESS(fvp, VWRITE, tcnp->cn_cred, tcnp->cn_proc);
	VOP_UNLOCK(fvp);
	if (oldparent != dp->i_number)
		newparent = dp->i_number;
	if (doingdirectory && newparent) {
		if (error)	/* write access check above */
			goto bad;
		if (xp != NULL)
			vput(tvp);
		error = ext2fs_checkpath(ip, dp, tcnp->cn_cred);
		if (error != 0)
			goto out;
		if ((tcnp->cn_flags & SAVESTART) == 0)
			panic("ext2fs_rename: lost to startdir");
		if ((error = vfs_relookup(tdvp, &tvp, tcnp)) != 0)
			goto out;
		dp = VTOI(tdvp);
		xp = NULL;
		if (tvp)
			xp = VTOI(tvp);
	}
	/*
	 * 2) If target doesn't exist, link the target
	 *    to the source and unlink the source.
	 *    Otherwise, rewrite the target directory
	 *    entry to reference the source inode and
	 *    expunge the original entry's existence.
	 */
	if (xp == NULL) {
		if (dp->i_dev != ip->i_dev)
			panic("rename: EXDEV");
		/*
		 * Account for ".." in new directory.
		 * When source and destination have the same
		 * parent we don't fool with the link count.
		 */
		if (doingdirectory && newparent) {
			if ((nlink_t)dp->i_e2fs_nlink >= LINK_MAX) {
				error = EMLINK;
				goto bad;
			}
			dp->i_e2fs_nlink++;
			dp->i_flag |= IN_CHANGE;
			if ((error = ext2fs_update(dp, 1)) != 0)
				goto bad;
		}
		error = ext2fs_direnter(ip, tdvp, tcnp);
		if (error != 0) {
			if (doingdirectory && newparent) {
				dp->i_e2fs_nlink--;
				dp->i_flag |= IN_CHANGE;
				(void)ext2fs_update(dp, 1);
			}
			goto bad;
		}
		vput(tdvp);
	} else {
		if (xp->i_dev != dp->i_dev || xp->i_dev != ip->i_dev)
			panic("rename: EXDEV");
		/*
		 * Short circuit rename(foo, foo).
		 */
		if (xp->i_number == ip->i_number)
			panic("rename: same file");
		/*
		 * If the parent directory is "sticky", then the user must
		 * own the parent directory, or the destination of the rename,
		 * otherwise the destination may not be changed (except by
		 * root). This implements append-only directories.
		 */
		if ((dp->i_e2fs_mode & S_ISTXT) && tcnp->cn_cred->cr_uid != 0 &&
		    tcnp->cn_cred->cr_uid != dp->i_e2fs_uid &&
		    xp->i_e2fs_uid != tcnp->cn_cred->cr_uid) {
			error = EPERM;
			goto bad;
		}
		/*
		 * Target must be empty if a directory and have no links
		 * to it. Also, ensure source and target are compatible
		 * (both directories, or both not directories).
		 */
		if ((xp->i_e2fs_mode & IFMT) == IFDIR) {
			if (!ext2fs_dirempty(xp, dp->i_number, tcnp->cn_cred) ||
				xp->i_e2fs_nlink > 2) {
				error = ENOTEMPTY;
				goto bad;
			}
			if (!doingdirectory) {
				error = ENOTDIR;
				goto bad;
			}
			cache_purge(tdvp);
		} else if (doingdirectory) {
			error = EISDIR;
			goto bad;
		}
		error = ext2fs_dirrewrite(dp, ip, tcnp);
		if (error != 0)
			goto bad;
		/*
		 * If the target directory is in the same
		 * directory as the source directory,
		 * decrement the link count on the parent
		 * of the target directory.
		 */
		if (doingdirectory && !newparent) {
			dp->i_e2fs_nlink--;
			dp->i_flag |= IN_CHANGE;
		}
		vput(tdvp);
		/*
		 * Adjust the link count of the target to
		 * reflect the dirrewrite above.  If this is
		 * a directory it is empty and there are
		 * no links to it, so we can squash the inode and
		 * any space associated with it.  We disallowed
		 * renaming over top of a directory with links to
		 * it above, as the remaining link would point to
		 * a directory without "." or ".." entries.
		 */
		xp->i_e2fs_nlink--;
		if (doingdirectory) {
			if (--xp->i_e2fs_nlink != 0)
				panic("rename: linked directory");
			error = ext2fs_truncate(xp, (off_t)0, IO_SYNC,
			    tcnp->cn_cred);
		}
		xp->i_flag |= IN_CHANGE;
		vput(tvp);
		xp = NULL;
	}

	/*
	 * 3) Unlink the source.
	 */
	fcnp->cn_flags &= ~MODMASK;
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	if ((fcnp->cn_flags & SAVESTART) == 0)
		panic("ext2fs_rename: lost from startdir");
	(void) vfs_relookup(fdvp, &fvp, fcnp);
	if (fvp != NULL) {
		xp = VTOI(fvp);
		dp = VTOI(fdvp);
	} else {
		/*
		 * From name has disappeared.
		 */
		if (doingdirectory)
			panic("ext2fs_rename: lost dir entry");
		vrele(ap->a_fvp);
		return (0);
	}
	/*
	 * Ensure that the directory entry still exists and has not
	 * changed while the new name has been entered. If the source is
	 * a file then the entry may have been unlinked or renamed. In
	 * either case there is no further work to be done. If the source
	 * is a directory then it cannot have been rmdir'ed; its link
	 * count of three would cause a rmdir to fail with ENOTEMPTY.
	 * The IRENAME flag ensures that it cannot be moved by another
	 * rename.
	 */
	if (xp != ip) {
		if (doingdirectory)
			panic("ext2fs_rename: lost dir entry");
	} else {
		/*
		 * If the source is a directory with a
		 * new parent, the link count of the old
		 * parent directory must be decremented
		 * and ".." set to point to the new parent.
		 */
		if (doingdirectory && newparent) {
			dp->i_e2fs_nlink--;
			dp->i_flag |= IN_CHANGE;
			error = vn_rdwr(UIO_READ, fvp, (caddr_t)&dirbuf,
				sizeof (struct ext2fs_dirtemplate), (off_t)0,
				UIO_SYSSPACE, IO_NODELOCKED,
				tcnp->cn_cred, NULL, curproc);
			if (error == 0) {
				namlen = dirbuf.dotdot_namlen;
				if (namlen != 2 ||
				    dirbuf.dotdot_name[0] != '.' ||
				    dirbuf.dotdot_name[1] != '.') {
					ufs_dirbad(xp, (doff_t)12,
					    "ext2fs_rename: mangled dir");
				} else {
					dirbuf.dotdot_ino = htole32(newparent);
					(void) vn_rdwr(UIO_WRITE, fvp,
					    (caddr_t)&dirbuf,
					    sizeof (struct dirtemplate),
					    (off_t)0, UIO_SYSSPACE,
					    IO_NODELOCKED|IO_SYNC,
					    tcnp->cn_cred, NULL, curproc);
					cache_purge(fdvp);
				}
			}
		}
		error = ext2fs_dirremove(fdvp, fcnp);
		if (!error) {
			xp->i_e2fs_nlink--;
			xp->i_flag |= IN_CHANGE;
		}
		xp->i_flag &= ~IN_RENAME;
	}
	if (dp)
		vput(fdvp);
	if (xp)
		vput(fvp);
	vrele(ap->a_fvp);
	return (error);

bad:
	if (xp)
		vput(ITOV(xp));
	vput(ITOV(dp));
out:
	if (doingdirectory)
		ip->i_flag &= ~IN_RENAME;
	if (vn_lock(fvp, LK_EXCLUSIVE) == 0) {
		ip->i_e2fs_nlink--;
		ip->i_flag |= IN_CHANGE;
		vput(fvp);
	} else
		vrele(fvp);
	return (error);
}

/*
 * Mkdir system call
 */
int
ext2fs_mkdir(void *v)
{
	struct vop_mkdir_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	struct vnode *tvp;
	struct ext2fs_dirtemplate dirtemplate;
	mode_t dmode;
	int error;

#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2fs_mkdir: no name");
#endif
	dp = VTOI(dvp);
	if ((nlink_t)dp->i_e2fs_nlink >= LINK_MAX) {
		error = EMLINK;
		goto out;
	}
	dmode = vap->va_mode & ACCESSPERMS;
	dmode |= IFDIR;
	/*
	 * Must simulate part of ext2fs_makeinode here to acquire the inode,
	 * but not have it entered in the parent directory. The entry is
	 * made later after writing "." and ".." entries.
	 */
	if ((error = ext2fs_inode_alloc(dp, dmode, cnp->cn_cred, &tvp)) != 0)
		goto out;
	ip = VTOI(tvp);
	ip->i_e2fs_uid = cnp->cn_cred->cr_uid;
	ip->i_e2fs_gid = dp->i_e2fs_gid;
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_e2fs_mode = dmode;
	tvp->v_type = VDIR;	/* Rest init'd in getnewvnode(). */
	ip->i_e2fs_nlink = 2;
	error = ext2fs_update(ip, 1);

	/*
	 * Bump link count in parent directory
	 * to reflect work done below.  Should
	 * be done before reference is created
	 * so reparation is possible if we crash.
	 */
	dp->i_e2fs_nlink++;
	dp->i_flag |= IN_CHANGE;
	if ((error = ext2fs_update(dp, 1)) != 0)
		goto bad;

	/* Initialize directory with "." and ".." from static template. */
	memset(&dirtemplate, 0, sizeof(dirtemplate));
	dirtemplate.dot_ino = htole32(ip->i_number);
	dirtemplate.dot_reclen = htole16(12);
	dirtemplate.dot_namlen = 1;
	if (ip->i_e2fs->e2fs.e2fs_rev > E2FS_REV0 &&
	    (ip->i_e2fs->e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE)) {
		dirtemplate.dot_type = EXT2_FT_DIR;
	}
	dirtemplate.dot_name[0] = '.';
	dirtemplate.dotdot_ino = htole32(dp->i_number);
	dirtemplate.dotdot_reclen = htole16(VTOI(dvp)->i_e2fs->e2fs_bsize - 12);
	dirtemplate.dotdot_namlen = 2;
	if (ip->i_e2fs->e2fs.e2fs_rev > E2FS_REV0 &&
	    (ip->i_e2fs->e2fs.e2fs_features_incompat & EXT2F_INCOMPAT_FTYPE)) {
		dirtemplate.dotdot_type = EXT2_FT_DIR;
	}
	dirtemplate.dotdot_name[0] = dirtemplate.dotdot_name[1] = '.';
	error = vn_rdwr(UIO_WRITE, tvp, (caddr_t)&dirtemplate,
	    sizeof (dirtemplate), (off_t)0, UIO_SYSSPACE,
	    IO_NODELOCKED|IO_SYNC, cnp->cn_cred, NULL, curproc);
	if (error) {
		dp->i_e2fs_nlink--;
		dp->i_flag |= IN_CHANGE;
		goto bad;
	}
	if (VTOI(dvp)->i_e2fs->e2fs_bsize >
							VFSTOUFS(dvp->v_mount)->um_mountp->mnt_stat.f_bsize)
		panic("ext2fs_mkdir: blksize"); /* XXX should grow with balloc() */
	else {
		error = ext2fs_setsize(ip, VTOI(dvp)->i_e2fs->e2fs_bsize);
		if (error) {
			dp->i_e2fs_nlink--;
			dp->i_flag |= IN_CHANGE;
			goto bad;
		}
		ip->i_flag |= IN_CHANGE;
	}

	/* Directory set up, now install its entry in the parent directory. */
	error = ext2fs_direnter(ip, dvp, cnp);
	if (error != 0) {
		dp->i_e2fs_nlink--;
		dp->i_flag |= IN_CHANGE;
	}
bad:
	/*
	 * No need to do an explicit VOP_TRUNCATE here, vrele will do this
	 * for us because we set the link count to 0.
	 */
	if (error) {
		ip->i_e2fs_nlink = 0;
		ip->i_flag |= IN_CHANGE;
		vput(tvp);
	} else
		*ap->a_vpp = tvp;
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	vput(dvp);
	return (error);
}

/*
 * Rmdir system call.
 */
int
ext2fs_rmdir(void *v)
{
	struct vop_rmdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct inode *ip, *dp;
	int error;

	ip = VTOI(vp);
	dp = VTOI(dvp);
	/*
	 * Verify the directory is empty (and valid).
	 * (Rmdir ".." won't be valid since
	 *  ".." will contain a reference to
	 *  the current directory and thus be
	 *  non-empty.)
	 */
	error = 0;
	if (ip->i_e2fs_nlink != 2 ||
	    !ext2fs_dirempty(ip, dp->i_number, cnp->cn_cred)) {
		error = ENOTEMPTY;
		goto out;
	}
	if ((dp->i_e2fs_flags & EXT2_APPEND) ||
				 (ip->i_e2fs_flags & (EXT2_IMMUTABLE | EXT2_APPEND))) {
		error = EPERM;
		goto out;
	}
	/*
	 * Delete reference to directory before purging
	 * inode.  If we crash in between, the directory
	 * will be reattached to lost+found,
	 */
	error = ext2fs_dirremove(dvp, cnp);
	if (error != 0)
		goto out;
	dp->i_e2fs_nlink--;
	dp->i_flag |= IN_CHANGE;
	cache_purge(dvp);
	vput(dvp);
	dvp = NULL;
	/*
	 * Truncate inode.  The only stuff left
	 * in the directory is "." and "..".  The
	 * "." reference is inconsequential since
	 * we're quashing it.  The ".." reference
	 * has already been adjusted above.  We've
	 * removed the "." reference and the reference
	 * in the parent directory, but there may be
	 * other hard links so decrement by 2 and
	 * worry about them later.
	 */
	ip->i_e2fs_nlink -= 2;
	error = ext2fs_truncate(ip, (off_t)0, IO_SYNC, cnp->cn_cred);
	cache_purge(ITOV(ip));
out:
	if (dvp)
		vput(dvp);
	vput(vp);
	return (error);
}

/*
 * symlink -- make a symbolic link
 */
int
ext2fs_symlink(void *v)
{
	struct vop_symlink_args *ap = v;
	struct vnode *vp, **vpp = ap->a_vpp;
	struct inode *ip;
	int len, error;

	error = ext2fs_makeinode(IFLNK | ap->a_vap->va_mode, ap->a_dvp,
			      vpp, ap->a_cnp);
	vput(ap->a_dvp);
	if (error)
		return (error);
	vp = *vpp;
	len = strlen(ap->a_target);
	if (len < EXT2_MAXSYMLINKLEN) {
		ip = VTOI(vp);
		memcpy(ip->i_e2din->e2di_shortlink, ap->a_target, len);
		error = ext2fs_setsize(ip, len);
		if (error)
			goto bad;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	} else
		error = vn_rdwr(UIO_WRITE, vp, ap->a_target, len, (off_t)0,
		    UIO_SYSSPACE, IO_NODELOCKED, ap->a_cnp->cn_cred, NULL,
		    curproc);
bad:
	vput(vp);
	return (error);
}

/*
 * Return target name of a symbolic link
 */
int
ext2fs_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip = VTOI(vp);
	u_int64_t isize;

	isize = ext2fs_size(ip);
	if (isize < EXT2_MAXSYMLINKLEN) {
		return (uiomove((char *)ip->i_e2din->e2di_shortlink, isize,
		    ap->a_uio));
	}
	return (VOP_READ(vp, ap->a_uio, 0, ap->a_cred));
}

/*
 * Return POSIX pathconf information applicable to ext2 filesystems.
 */
int
ext2fs_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_TIMESTAMP_RESOLUTION:
		*ap->a_retval = 1000000000;	/* 1 billion nanoseconds */
		break;
	default:
		return (ufs_pathconf(v));
	}

	return (error);
}

/*
 * Advisory record locking support
 */
int
ext2fs_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct inode *ip = VTOI(ap->a_vp);

	return (lf_advlock(&ip->i_lockf, ext2fs_size(ip), ap->a_id, ap->a_op,
	    ap->a_fl, ap->a_flags));
}

/*
 * Allocate a new inode.
 */
int
ext2fs_makeinode(int mode, struct vnode *dvp, struct vnode **vpp,
    struct componentname *cnp)
{
	struct inode *ip, *pdir;
	struct vnode *tvp;
	int error;

	pdir = VTOI(dvp);
#ifdef DIAGNOSTIC
	if ((cnp->cn_flags & HASBUF) == 0)
		panic("ext2fs_makeinode: no name");
#endif
	*vpp = NULL;
	if ((mode & IFMT) == 0)
		mode |= IFREG;

	if ((error = ext2fs_inode_alloc(pdir, mode, cnp->cn_cred, &tvp))
	    != 0) {
		pool_put(&namei_pool, cnp->cn_pnbuf);
		return (error);
	}
	ip = VTOI(tvp);
	ip->i_e2fs_gid = pdir->i_e2fs_gid;
	ip->i_e2fs_uid = cnp->cn_cred->cr_uid;
	ip->i_flag |= IN_ACCESS | IN_CHANGE | IN_UPDATE;
	ip->i_e2fs_mode = mode;
	tvp->v_type = IFTOVT(mode);	/* Rest init'd in getnewvnode(). */
	ip->i_e2fs_nlink = 1;
	if ((ip->i_e2fs_mode & ISGID) &&
		!groupmember(ip->i_e2fs_gid, cnp->cn_cred) &&
	    suser_ucred(cnp->cn_cred))
		ip->i_e2fs_mode &= ~ISGID;

	/*
	 * Make sure inode goes to disk before directory entry.
	 */
	if ((error = ext2fs_update(ip, 1)) != 0)
		goto bad;
	error = ext2fs_direnter(ip, dvp, cnp);
	if (error != 0)
		goto bad;
	if ((cnp->cn_flags & SAVESTART) == 0)
		pool_put(&namei_pool, cnp->cn_pnbuf);
	*vpp = tvp;
	return (0);

bad:
	/*
	 * Write error occurred trying to update the inode
	 * or the directory so must deallocate the inode.
	 */
	pool_put(&namei_pool, cnp->cn_pnbuf);
	ip->i_e2fs_nlink = 0;
	ip->i_flag |= IN_CHANGE;
	tvp->v_type = VNON;
	vput(tvp);
	return (error);
}

/*
 * Synch an open file.
 */
int
ext2fs_fsync(void *v)
{
	struct vop_fsync_args *ap = v;
	struct vnode *vp = ap->a_vp;

	vflushbuf(vp, ap->a_waitfor == MNT_WAIT);
	return (ext2fs_update(VTOI(ap->a_vp), ap->a_waitfor == MNT_WAIT));
}

/*
 * Reclaim an inode so that it can be used for other purposes.
 */
int
ext2fs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct inode *ip;
#ifdef DIAGNOSTIC
	extern int prtactive;

	if (prtactive && vp->v_usecount != 0)
		vprint("ext2fs_reclaim: pushing active", vp);
#endif

	/*
	 * Remove the inode from its hash chain.
	 */
	ip = VTOI(vp);
	ufs_ihashrem(ip);

	/*
	 * Purge old data structures associated with the inode.
	 */
	cache_purge(vp);
	if (ip->i_devvp)
		vrele(ip->i_devvp);

	if (ip->i_e2din != NULL)
		pool_put(&ext2fs_dinode_pool, ip->i_e2din);

	pool_put(&ext2fs_inode_pool, ip);

	vp->v_data = NULL;

	return (0);
}

/* Global vfs data structures for ext2fs. */
const struct vops ext2fs_vops = {
        .vop_lookup     = ext2fs_lookup,
        .vop_create     = ext2fs_create,
        .vop_mknod      = ext2fs_mknod,
        .vop_open       = ext2fs_open,
        .vop_close      = ufs_close,
        .vop_access     = ext2fs_access,
        .vop_getattr    = ext2fs_getattr,
        .vop_setattr    = ext2fs_setattr,
        .vop_read       = ext2fs_read,
        .vop_write      = ext2fs_write,
        .vop_ioctl      = ufs_ioctl,
        .vop_kqfilter   = ufs_kqfilter,
        .vop_revoke     = NULL,
        .vop_fsync      = ext2fs_fsync,
        .vop_remove     = ext2fs_remove,
        .vop_link       = ext2fs_link,
        .vop_rename     = ext2fs_rename,
        .vop_mkdir      = ext2fs_mkdir,
        .vop_rmdir      = ext2fs_rmdir,
        .vop_symlink    = ext2fs_symlink,
        .vop_readdir    = ext2fs_readdir,
        .vop_readlink   = ext2fs_readlink,
        .vop_abortop    = vop_generic_abortop,
        .vop_inactive   = ext2fs_inactive,
        .vop_reclaim    = ext2fs_reclaim,
        .vop_lock       = ufs_lock,
        .vop_unlock     = ufs_unlock,
        .vop_bmap       = ext2fs_bmap,
        .vop_strategy   = ufs_strategy,
        .vop_print      = ufs_print,
        .vop_islocked   = ufs_islocked,
        .vop_pathconf   = ext2fs_pathconf,
        .vop_advlock    = ext2fs_advlock,
        .vop_bwrite     = vop_generic_bwrite,
};

const struct vops ext2fs_specvops = {
        .vop_close      = ufsspec_close,
        .vop_access     = ext2fs_access,
        .vop_getattr    = ext2fs_getattr,
        .vop_setattr    = ext2fs_setattr,
        .vop_read       = ufsspec_read,
        .vop_write      = ufsspec_write,
        .vop_fsync      = ext2fs_fsync,
        .vop_inactive   = ext2fs_inactive,
        .vop_reclaim    = ext2fs_reclaim,
        .vop_lock       = ufs_lock,
        .vop_unlock     = ufs_unlock,
        .vop_print      = ufs_print,
        .vop_islocked   = ufs_islocked,

        /* XXX: Keep in sync with spec_vops. */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= spec_open,
	.vop_ioctl	= spec_ioctl,
	.vop_kqfilter	= spec_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= spec_strategy,
	.vop_pathconf	= spec_pathconf,
	.vop_advlock	= spec_advlock,
	.vop_bwrite	= vop_generic_bwrite,
};

#ifdef FIFO
const struct vops ext2fs_fifovops = {
        .vop_close      = ufsfifo_close,
        .vop_access     = ufsfifo_close,
        .vop_getattr    = ext2fs_getattr,
        .vop_setattr    = ext2fs_setattr,
        .vop_read       = ufsfifo_read,
        .vop_write      = ufsfifo_write,
        .vop_fsync      = ext2fs_fsync,
        .vop_inactive   = ext2fs_inactive,
        .vop_reclaim    = ext2fsfifo_reclaim,
        .vop_lock       = ufs_lock,
        .vop_unlock     = ufs_unlock,
        .vop_print      = ufs_print,
        .vop_islocked   = ufs_islocked,
        .vop_bwrite     = vop_generic_bwrite,

        /* XXX: Keep in sync with fifo_vops */
	.vop_lookup	= vop_generic_lookup,
	.vop_create	= vop_generic_badop,
	.vop_mknod	= vop_generic_badop,
	.vop_open	= fifo_open,
	.vop_ioctl	= fifo_ioctl,
	.vop_kqfilter	= fifo_kqfilter,
	.vop_revoke	= vop_generic_revoke,
	.vop_remove	= vop_generic_badop,
	.vop_link	= vop_generic_badop,
	.vop_rename	= vop_generic_badop,
	.vop_mkdir	= vop_generic_badop,
	.vop_rmdir	= vop_generic_badop,
	.vop_symlink	= vop_generic_badop,
	.vop_readdir	= vop_generic_badop,
	.vop_readlink	= vop_generic_badop,
	.vop_abortop	= vop_generic_badop,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= vop_generic_badop,
	.vop_pathconf	= fifo_pathconf,
	.vop_advlock	= fifo_advlock,
};

int
ext2fsfifo_reclaim(void *v)
{
	fifo_reclaim(v);
	return (ext2fs_reclaim(v));
}
#endif /* FIFO */
