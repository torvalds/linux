/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_VNODE_H_
#define	_OPENSOLARIS_SYS_VNODE_H_

#ifdef _KERNEL

struct vnode;
struct vattr;

typedef	struct vnode	vnode_t;
typedef	struct vattr	vattr_t;
typedef enum vtype vtype_t;

#include <sys/namei.h>
enum symfollow { NO_FOLLOW = NOFOLLOW };

#include <sys/proc.h>
#include_next <sys/vnode.h>
#include <sys/mount.h>
#include <sys/cred.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/syscallsubr.h>

typedef	struct vop_vector	vnodeops_t;
#define	VOP_FID		VOP_VPTOFH
#define	vop_fid		vop_vptofh
#define	vop_fid_args	vop_vptofh_args
#define	a_fid		a_fhp

#define	IS_XATTRDIR(dvp)	(0)

#define	v_count	v_usecount

#define	V_APPEND	VAPPEND

#define	rootvfs		(rootvnode == NULL ? NULL : rootvnode->v_mount)

static __inline int
vn_is_readonly(vnode_t *vp)
{
	return (vp->v_mount->mnt_flag & MNT_RDONLY);
}
#define	vn_vfswlock(vp)		(0)
#define	vn_vfsunlock(vp)	do { } while (0)
#define	vn_ismntpt(vp)		((vp)->v_type == VDIR && (vp)->v_mountedhere != NULL)
#define	vn_mountedvfs(vp)	((vp)->v_mountedhere)
#define	vn_has_cached_data(vp)	\
	((vp)->v_object != NULL && \
	 (vp)->v_object->resident_page_count > 0)
#define	vn_exists(vp)		do { } while (0)
#define	vn_invalid(vp)		do { } while (0)
#define	vn_renamepath(tdvp, svp, tnm, lentnm)	do { } while (0)
#define	vn_free(vp)		do { } while (0)
#define	vn_matchops(vp, vops)	((vp)->v_op == &(vops))

#define	VN_HOLD(v)	vref(v)
#define	VN_RELE(v)	vrele(v)
#define	VN_URELE(v)	vput(v)

#define	vnevent_create(vp, ct)			do { } while (0)
#define	vnevent_link(vp, ct)			do { } while (0)
#define	vnevent_remove(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rmdir(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_src(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_dest(vp, dvp, name, ct)	do { } while (0)
#define	vnevent_rename_dest_dir(vp, ct)		do { } while (0)

#define	specvp(vp, rdev, type, cr)	(VN_HOLD(vp), (vp))
#define	MANDMODE(mode)		(0)
#define	MANDLOCK(vp, mode)	(0)
#define	chklock(vp, op, offset, size, mode, ct)	(0)
#define	cleanlocks(vp, pid, foo)	do { } while (0)
#define	cleanshares(vp, pid)		do { } while (0)

/*
 * We will use va_spare is place of Solaris' va_mask.
 * This field is initialized in zfs_setattr().
 */
#define	va_mask		va_spare
/* TODO: va_fileid is shorter than va_nodeid !!! */
#define	va_nodeid	va_fileid
/* TODO: This field needs conversion! */
#define	va_nblocks	va_bytes
#define	va_blksize	va_blocksize
#define	va_seq		va_gen

#define	MAXOFFSET_T	OFF_MAX
#define	EXCL		0

#define	ACCESSED		(AT_ATIME)
#define	STATE_CHANGED		(AT_CTIME)
#define	CONTENT_MODIFIED	(AT_MTIME | AT_CTIME)

static __inline void
vattr_init_mask(vattr_t *vap)
{

	vap->va_mask = 0;

	if (vap->va_type != VNON)
		vap->va_mask |= AT_TYPE;
	if (vap->va_uid != (uid_t)VNOVAL)
		vap->va_mask |= AT_UID;
	if (vap->va_gid != (gid_t)VNOVAL)
		vap->va_mask |= AT_GID;
	if (vap->va_size != (u_quad_t)VNOVAL)
		vap->va_mask |= AT_SIZE;
	if (vap->va_atime.tv_sec != VNOVAL)
		vap->va_mask |= AT_ATIME;
	if (vap->va_mtime.tv_sec != VNOVAL)
		vap->va_mask |= AT_MTIME;
	if (vap->va_mode != (u_short)VNOVAL)
		vap->va_mask |= AT_MODE;
	if (vap->va_flags != VNOVAL)
		vap->va_mask |= AT_XVATTR;
}

#define	FCREAT		O_CREAT
#define	FTRUNC		O_TRUNC
#define	FEXCL		O_EXCL
#define	FDSYNC		FFSYNC
#define	FRSYNC		FFSYNC
#define	FSYNC		FFSYNC
#define	FOFFMAX		0x00
#define	FIGNORECASE	0x00

static __inline int
vn_openat(char *pnamep, enum uio_seg seg, int filemode, int createmode,
    vnode_t **vpp, enum create crwhy, mode_t umask, struct vnode *startvp,
    int fd)
{
	struct thread *td = curthread;
	struct nameidata nd;
	int error, operation;

	ASSERT(seg == UIO_SYSSPACE);
	if ((filemode & FCREAT) != 0) {
		ASSERT(filemode == (FWRITE | FCREAT | FTRUNC | FOFFMAX));
		ASSERT(crwhy == CRCREAT);
		operation = CREATE;
	} else {
		ASSERT(filemode == (FREAD | FOFFMAX) ||
		    filemode == (FREAD | FWRITE | FOFFMAX));
		ASSERT(crwhy == 0);
		operation = LOOKUP;
	}
	ASSERT(umask == 0);

	pwd_ensure_dirs();

	if (startvp != NULL)
		vref(startvp);
	NDINIT_ATVP(&nd, operation, 0, UIO_SYSSPACE, pnamep, startvp, td);
	filemode |= O_NOFOLLOW;
	error = vn_open_cred(&nd, &filemode, createmode, 0, td->td_ucred, NULL);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	if (error == 0) {
		/* We just unlock so we hold a reference. */
		VOP_UNLOCK(nd.ni_vp, 0);
		*vpp = nd.ni_vp;
	}
	return (error);
}

static __inline int
zfs_vn_open(char *pnamep, enum uio_seg seg, int filemode, int createmode,
    vnode_t **vpp, enum create crwhy, mode_t umask)
{

	return (vn_openat(pnamep, seg, filemode, createmode, vpp, crwhy,
	    umask, NULL, -1));
}
#define	vn_open(pnamep, seg, filemode, createmode, vpp, crwhy, umask)	\
	zfs_vn_open((pnamep), (seg), (filemode), (createmode), (vpp), (crwhy), (umask))

#define	RLIM64_INFINITY	0
static __inline int
zfs_vn_rdwr(enum uio_rw rw, vnode_t *vp, caddr_t base, ssize_t len,
    offset_t offset, enum uio_seg seg, int ioflag, int ulimit, cred_t *cr,
    ssize_t *residp)
{
	struct thread *td = curthread;
	int error;
	ssize_t resid;

	ASSERT(ioflag == 0);
	ASSERT(ulimit == RLIM64_INFINITY);

	if (rw == UIO_WRITE) {
		ioflag = IO_SYNC;
	} else {
		ioflag = IO_DIRECT;
	}
	error = vn_rdwr(rw, vp, base, len, offset, seg, ioflag, cr, NOCRED,
	    &resid, td);
	if (residp != NULL)
		*residp = (ssize_t)resid;
	return (error);
}
#define	vn_rdwr(rw, vp, base, len, offset, seg, ioflag, ulimit, cr, residp) \
	zfs_vn_rdwr((rw), (vp), (base), (len), (offset), (seg), (ioflag), (ulimit), (cr), (residp))

static __inline int
zfs_vop_fsync(vnode_t *vp, int flag, cred_t *cr)
{
	struct mount *mp;
	int error;

	ASSERT(flag == FSYNC);

	if ((error = vn_start_write(vp, &mp, V_WAIT | PCATCH)) != 0)
		goto drop;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	error = VOP_FSYNC(vp, MNT_WAIT, curthread);
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
drop:
	return (error);
}
#define	VOP_FSYNC(vp, flag, cr, ct)	zfs_vop_fsync((vp), (flag), (cr))

static __inline int
zfs_vop_close(vnode_t *vp, int flag, int count, offset_t offset, cred_t *cr)
{
	int error;

	ASSERT(count == 1);
	ASSERT(offset == 0);

	error = vn_close(vp, flag, cr, curthread);
	return (error);
}
#define	VOP_CLOSE(vp, oflags, count, offset, cr, ct)			\
	zfs_vop_close((vp), (oflags), (count), (offset), (cr))

static __inline int
vn_rename(char *from, char *to, enum uio_seg seg)
{

	ASSERT(seg == UIO_SYSSPACE);

	return (kern_renameat(curthread, AT_FDCWD, from, AT_FDCWD, to, seg));
}

static __inline int
vn_remove(char *fnamep, enum uio_seg seg, enum rm dirflag)
{

	ASSERT(seg == UIO_SYSSPACE);
	ASSERT(dirflag == RMFILE);

	return (kern_unlinkat(curthread, AT_FDCWD, fnamep, seg, 0, 0));
}

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_VNODE_H_ */
