/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/unistd.h>
#include <sys/vnode.h>
#include <sys/limits.h>
#include <sys/lockf.h>
#include <sys/stat.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>


#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>
#include <fs/smbfs/smbfs_subr.h>

/*
 * Prototypes for SMBFS vnode operations
 */
static vop_create_t	smbfs_create;
static vop_mknod_t	smbfs_mknod;
static vop_open_t	smbfs_open;
static vop_close_t	smbfs_close;
static vop_access_t	smbfs_access;
static vop_getattr_t	smbfs_getattr;
static vop_setattr_t	smbfs_setattr;
static vop_read_t	smbfs_read;
static vop_write_t	smbfs_write;
static vop_fsync_t	smbfs_fsync;
static vop_remove_t	smbfs_remove;
static vop_link_t	smbfs_link;
static vop_lookup_t	smbfs_lookup;
static vop_rename_t	smbfs_rename;
static vop_mkdir_t	smbfs_mkdir;
static vop_rmdir_t	smbfs_rmdir;
static vop_symlink_t	smbfs_symlink;
static vop_readdir_t	smbfs_readdir;
static vop_strategy_t	smbfs_strategy;
static vop_print_t	smbfs_print;
static vop_pathconf_t	smbfs_pathconf;
static vop_advlock_t	smbfs_advlock;
static vop_getextattr_t	smbfs_getextattr;

struct vop_vector smbfs_vnodeops = {
	.vop_default =		&default_vnodeops,

	.vop_access =		smbfs_access,
	.vop_advlock =		smbfs_advlock,
	.vop_close =		smbfs_close,
	.vop_create =		smbfs_create,
	.vop_fsync =		smbfs_fsync,
	.vop_getattr =		smbfs_getattr,
	.vop_getextattr = 	smbfs_getextattr,
	.vop_getpages =		smbfs_getpages,
	.vop_inactive =		smbfs_inactive,
	.vop_ioctl =		smbfs_ioctl,
	.vop_link =		smbfs_link,
	.vop_lookup =		smbfs_lookup,
	.vop_mkdir =		smbfs_mkdir,
	.vop_mknod =		smbfs_mknod,
	.vop_open =		smbfs_open,
	.vop_pathconf =		smbfs_pathconf,
	.vop_print =		smbfs_print,
	.vop_putpages =		smbfs_putpages,
	.vop_read =		smbfs_read,
	.vop_readdir =		smbfs_readdir,
	.vop_reclaim =		smbfs_reclaim,
	.vop_remove =		smbfs_remove,
	.vop_rename =		smbfs_rename,
	.vop_rmdir =		smbfs_rmdir,
	.vop_setattr =		smbfs_setattr,
/*	.vop_setextattr =	smbfs_setextattr,*/
	.vop_strategy =		smbfs_strategy,
	.vop_symlink =		smbfs_symlink,
	.vop_write =		smbfs_write,
};

static int
smbfs_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	accmode_t accmode = ap->a_accmode;
	mode_t mpmode;
	struct smbmount *smp = VTOSMBFS(vp);

	SMBVDEBUG("\n");
	if ((accmode & VWRITE) && (vp->v_mount->mnt_flag & MNT_RDONLY)) {
		switch (vp->v_type) {
		    case VREG: case VDIR: case VLNK:
			return EROFS;
		    default:
			break;
		}
	}
	mpmode = vp->v_type == VREG ? smp->sm_file_mode : smp->sm_dir_mode;
	return (vaccess(vp->v_type, mpmode, smp->sm_uid,
	    smp->sm_gid, ap->a_accmode, ap->a_cred, NULL));
}

/* ARGSUSED */
static int
smbfs_open(ap)
	struct vop_open_args /* {
		struct vnode *a_vp;
		int  a_mode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;
	struct vattr vattr;
	int mode = ap->a_mode;
	int error, accmode;

	SMBVDEBUG("%s,%d\n", np->n_name, (np->n_flag & NOPEN) != 0);
	if (vp->v_type != VREG && vp->v_type != VDIR) { 
		SMBFSERR("open eacces vtype=%d\n", vp->v_type);
		return EACCES;
	}
	if (vp->v_type == VDIR) {
		np->n_flag |= NOPEN;
		return 0;
	}
	if (np->n_flag & NMODIFIED) {
		if ((error = smbfs_vinvalbuf(vp, ap->a_td)) == EINTR)
			return error;
		smbfs_attr_cacheremove(vp);
		error = VOP_GETATTR(vp, &vattr, ap->a_cred);
		if (error)
			return error;
		np->n_mtime.tv_sec = vattr.va_mtime.tv_sec;
	} else {
		error = VOP_GETATTR(vp, &vattr, ap->a_cred);
		if (error)
			return error;
		if (np->n_mtime.tv_sec != vattr.va_mtime.tv_sec) {
			error = smbfs_vinvalbuf(vp, ap->a_td);
			if (error == EINTR)
				return error;
			np->n_mtime.tv_sec = vattr.va_mtime.tv_sec;
		}
	}
	if ((np->n_flag & NOPEN) != 0)
		return 0;
	/*
	 * Use DENYNONE to give unixy semantics of permitting
	 * everything not forbidden by permissions.  Ie denial
	 * is up to server with clients/openers needing to use
	 * advisory locks for further control.
	 */
	accmode = SMB_SM_DENYNONE|SMB_AM_OPENREAD;
	if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0)
		accmode = SMB_SM_DENYNONE|SMB_AM_OPENRW;
	scred = smbfs_malloc_scred();
	smb_makescred(scred, ap->a_td, ap->a_cred);
	error = smbfs_smb_open(np, accmode, scred);
	if (error) {
		if (mode & FWRITE)
			return EACCES;
		else if ((vp->v_mount->mnt_flag & MNT_RDONLY) == 0) {
			accmode = SMB_SM_DENYNONE|SMB_AM_OPENREAD;
			error = smbfs_smb_open(np, accmode, scred);
		}
	}
	if (error == 0) {
		np->n_flag |= NOPEN;
		vnode_create_vobject(ap->a_vp, vattr.va_size, ap->a_td);
	}
	smbfs_attr_cacheremove(vp);
	smbfs_free_scred(scred);
	return error;
}

static int
smbfs_close(ap)
	struct vop_close_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		int  a_fflag;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;

	if (vp->v_type == VDIR && (np->n_flag & NOPEN) != 0 &&
	    np->n_dirseq != NULL) {
		scred = smbfs_malloc_scred();
		smb_makescred(scred, td, ap->a_cred);
		smbfs_findclose(np->n_dirseq, scred);
		smbfs_free_scred(scred);
		np->n_dirseq = NULL;
	}
	return 0;
}

/*
 * smbfs_getattr call from vfs.
 */
static int
smbfs_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);
	struct vattr *va=ap->a_vap;
	struct smbfattr fattr;
	struct smb_cred *scred;
	u_quad_t oldsize;
	int error;

	SMBVDEBUG("%lx: '%s' %d\n", (long)vp, np->n_name, (vp->v_vflag & VV_ROOT) != 0);
	error = smbfs_attr_cachelookup(vp, va);
	if (!error)
		return 0;
	SMBVDEBUG("not in the cache\n");
	scred = smbfs_malloc_scred();
	smb_makescred(scred, curthread, ap->a_cred);
	oldsize = np->n_size;
	error = smbfs_smb_lookup(np, NULL, 0, &fattr, scred);
	if (error) {
		SMBVDEBUG("error %d\n", error);
		smbfs_free_scred(scred);
		return error;
	}
	smbfs_attr_cacheenter(vp, &fattr);
	smbfs_attr_cachelookup(vp, va);
	if (np->n_flag & NOPEN)
		np->n_size = oldsize;
	smbfs_free_scred(scred);
	return 0;
}

static int
smbfs_setattr(ap)
	struct vop_setattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);
	struct vattr *vap = ap->a_vap;
	struct timespec *mtime, *atime;
	struct smb_cred *scred;
	struct smb_share *ssp = np->n_mount->sm_share;
	struct smb_vc *vcp = SSTOVC(ssp);
	struct thread *td = curthread;
	u_quad_t tsize = 0;
	int isreadonly, doclose, error = 0;
	int old_n_dosattr;

	SMBVDEBUG("\n");
	isreadonly = (vp->v_mount->mnt_flag & MNT_RDONLY);
	/*
	 * Disallow write attempts if the filesystem is mounted read-only.
	 */
  	if ((vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL || 
	     vap->va_atime.tv_sec != VNOVAL || vap->va_mtime.tv_sec != VNOVAL ||
	     vap->va_mode != (mode_t)VNOVAL || vap->va_flags != VNOVAL) &&
	     isreadonly)
		return EROFS;

	/*
	 * We only support setting four flags.  Don't allow setting others.
	 *
	 * We map UF_READONLY to SMB_FA_RDONLY, unlike the MacOS X version
	 * of this code, which maps both UF_IMMUTABLE AND SF_IMMUTABLE to
	 * SMB_FA_RDONLY.  The immutable flags have different semantics
	 * than readonly, which is the reason for the difference.
	 */
	if (vap->va_flags != VNOVAL) {
		if (vap->va_flags & ~(UF_HIDDEN|UF_SYSTEM|UF_ARCHIVE|
				      UF_READONLY))
			return EINVAL;
	}

	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, ap->a_cred);
	if (vap->va_size != VNOVAL) {
 		switch (vp->v_type) {
 		    case VDIR:
 			error = EISDIR;
			goto out;
 		    case VREG:
			break;
 		    default:
			error = EINVAL;
			goto out;
  		}
		if (isreadonly) {
			error = EROFS;
			goto out;
		}
		doclose = 0;
		vnode_pager_setsize(vp, (u_long)vap->va_size);
 		tsize = np->n_size;
 		np->n_size = vap->va_size;
		if ((np->n_flag & NOPEN) == 0) {
			error = smbfs_smb_open(np,
					       SMB_SM_DENYNONE|SMB_AM_OPENRW,
					       scred);
			if (error == 0)
				doclose = 1;
		}
		if (error == 0)
			error = smbfs_smb_setfsize(np,
			    (int64_t)vap->va_size, scred);
		if (doclose)
			smbfs_smb_close(ssp, np->n_fid, NULL, scred);
		if (error) {
			np->n_size = tsize;
			vnode_pager_setsize(vp, (u_long)tsize);
			goto out;
		}
  	}
	if ((vap->va_flags != VNOVAL) || (vap->va_mode != (mode_t)VNOVAL)) {
		old_n_dosattr = np->n_dosattr;

		if (vap->va_mode != (mode_t)VNOVAL) {
			if (vap->va_mode & S_IWUSR)
				np->n_dosattr &= ~SMB_FA_RDONLY;
			else
				np->n_dosattr |= SMB_FA_RDONLY;
		}

		if (vap->va_flags != VNOVAL) {
			if (vap->va_flags & UF_HIDDEN)
				np->n_dosattr |= SMB_FA_HIDDEN;
			else
				np->n_dosattr &= ~SMB_FA_HIDDEN;

			if (vap->va_flags & UF_SYSTEM)
				np->n_dosattr |= SMB_FA_SYSTEM;
			else
				np->n_dosattr &= ~SMB_FA_SYSTEM;

			if (vap->va_flags & UF_ARCHIVE)
				np->n_dosattr |= SMB_FA_ARCHIVE;
			else
				np->n_dosattr &= ~SMB_FA_ARCHIVE;

			/*
			 * We only support setting the immutable / readonly
			 * bit for regular files.  According to comments in
			 * the MacOS X version of this code, supporting the
			 * readonly bit on directories doesn't do the same
			 * thing in Windows as in Unix.
			 */
			if (vp->v_type == VREG) {
				if (vap->va_flags & UF_READONLY)
					np->n_dosattr |= SMB_FA_RDONLY;
				else
					np->n_dosattr &= ~SMB_FA_RDONLY;
			}
		}

		if (np->n_dosattr != old_n_dosattr) {
			error = smbfs_smb_setpattr(np, np->n_dosattr, NULL, scred);
			if (error)
				goto out;
		}
	}
	mtime = atime = NULL;
	if (vap->va_mtime.tv_sec != VNOVAL)
		mtime = &vap->va_mtime;
	if (vap->va_atime.tv_sec != VNOVAL)
		atime = &vap->va_atime;
	if (mtime != atime) {
		if (vap->va_vaflags & VA_UTIMES_NULL) {
			error = VOP_ACCESS(vp, VADMIN, ap->a_cred, td);
			if (error)
				error = VOP_ACCESS(vp, VWRITE, ap->a_cred, td);
		} else
			error = VOP_ACCESS(vp, VADMIN, ap->a_cred, td);
#if 0
		if (mtime == NULL)
			mtime = &np->n_mtime;
		if (atime == NULL)
			atime = &np->n_atime;
#endif
		/*
		 * If file is opened, then we can use handle based calls.
		 * If not, use path based ones.
		 */
		if ((np->n_flag & NOPEN) == 0) {
			if (vcp->vc_flags & SMBV_WIN95) {
				error = VOP_OPEN(vp, FWRITE, ap->a_cred, td,
				    NULL);
				if (!error) {
/*					error = smbfs_smb_setfattrNT(np, 0,
					    mtime, atime, scred);
					VOP_GETATTR(vp, &vattr, ap->a_cred); */
					if (mtime)
						np->n_mtime = *mtime;
					VOP_CLOSE(vp, FWRITE, ap->a_cred, td);
				}
			} else if ((vcp->vc_sopt.sv_caps & SMB_CAP_NT_SMBS)) {
				error = smbfs_smb_setptime2(np, mtime, atime, 0, scred);
/*				error = smbfs_smb_setpattrNT(np, 0, mtime, atime, scred);*/
			} else if (SMB_DIALECT(vcp) >= SMB_DIALECT_LANMAN2_0) {
				error = smbfs_smb_setptime2(np, mtime, atime, 0, scred);
			} else {
				error = smbfs_smb_setpattr(np, 0, mtime, scred);
			}
		} else {
			if (vcp->vc_sopt.sv_caps & SMB_CAP_NT_SMBS) {
				error = smbfs_smb_setfattrNT(np, 0, mtime, atime, scred);
			} else if (SMB_DIALECT(vcp) >= SMB_DIALECT_LANMAN1_0) {
				error = smbfs_smb_setftime(np, mtime, atime, scred);
			} else {
				/*
				 * I have no idea how to handle this for core
				 * level servers. The possible solution is to
				 * update mtime after file is closed.
				 */
				 SMBERROR("can't update times on an opened file\n");
			}
		}
	}
	/*
	 * Invalidate attribute cache in case if server doesn't set
	 * required attributes.
	 */
	smbfs_attr_cacheremove(vp);	/* invalidate cache */
	VOP_GETATTR(vp, vap, ap->a_cred);
	np->n_mtime.tv_sec = vap->va_mtime.tv_sec;
out:
	smbfs_free_scred(scred);
	return error;
}
/*
 * smbfs_read call.
 */
static int
smbfs_read(ap)
	struct vop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;

	SMBVDEBUG("\n");
	if (vp->v_type != VREG && vp->v_type != VDIR)
		return EPERM;
	return smbfs_readvnode(vp, uio, ap->a_cred);
}

static int
smbfs_write(ap)
	struct vop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int  a_ioflag;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;

	SMBVDEBUG("%d,ofs=%jd,sz=%zd\n",vp->v_type, (intmax_t)uio->uio_offset, 
	    uio->uio_resid);
	if (vp->v_type != VREG)
		return (EPERM);
	return smbfs_writevnode(vp, uio, ap->a_cred,ap->a_ioflag);
}
/*
 * smbfs_create call
 * Create a regular file. On entry the directory to contain the file being
 * created is locked.  We must release before we return. We must also free
 * the pathname buffer pointed at by cnp->cn_pnbuf, always on error, or
 * only if the SAVESTART bit in cn_flags is clear on success.
 */
static int
smbfs_create(ap)
	struct vop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct vnode **vpp=ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct smbnode *dnp = VTOSMB(dvp);
	struct vnode *vp;
	struct vattr vattr;
	struct smbfattr fattr;
	struct smb_cred *scred;
	char *name = cnp->cn_nameptr;
	int nmlen = cnp->cn_namelen;
	int error;
	

	SMBVDEBUG("\n");
	*vpp = NULL;
	if (vap->va_type != VREG)
		return EOPNOTSUPP;
	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred)))
		return error;
	scred = smbfs_malloc_scred();
	smb_makescred(scred, cnp->cn_thread, cnp->cn_cred);
	
	error = smbfs_smb_create(dnp, name, nmlen, scred);
	if (error)
		goto out;
	error = smbfs_smb_lookup(dnp, name, nmlen, &fattr, scred);
	if (error)
		goto out;
	error = smbfs_nget(VTOVFS(dvp), dvp, name, nmlen, &fattr, &vp);
	if (error)
		goto out;
	*vpp = vp;
	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(dvp, vp, cnp);
out:
	smbfs_free_scred(scred);
	return error;
}

static int
smbfs_remove(ap)
	struct vop_remove_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode * a_dvp;
		struct vnode * a_vp;
		struct componentname * a_cnp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
/*	struct vnode *dvp = ap->a_dvp;*/
	struct componentname *cnp = ap->a_cnp;
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;
	int error;

	if (vp->v_type == VDIR || (np->n_flag & NOPEN) != 0 || vrefcnt(vp) != 1)
		return EPERM;
	scred = smbfs_malloc_scred();
	smb_makescred(scred, cnp->cn_thread, cnp->cn_cred);
	error = smbfs_smb_delete(np, scred);
	if (error == 0)
		np->n_flag |= NGONE;
	cache_purge(vp);
	smbfs_free_scred(scred);
	return error;
}

/*
 * smbfs_file rename call
 */
static int
smbfs_rename(ap)
	struct vop_rename_args  /* {
		struct vnode *a_fdvp;
		struct vnode *a_fvp;
		struct componentname *a_fcnp;
		struct vnode *a_tdvp;
		struct vnode *a_tvp;
		struct componentname *a_tcnp;
	} */ *ap;
{
	struct vnode *fvp = ap->a_fvp;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *tcnp = ap->a_tcnp;
/*	struct componentname *fcnp = ap->a_fcnp;*/
	struct smb_cred *scred;
	u_int16_t flags = 6;
	int error=0;

	scred = NULL;
	/* Check for cross-device rename */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
		goto out;
	}

	if (tvp && vrefcnt(tvp) > 1) {
		error = EBUSY;
		goto out;
	}
	flags = 0x10;			/* verify all writes */
	if (fvp->v_type == VDIR) {
		flags |= 2;
	} else if (fvp->v_type == VREG) {
		flags |= 1;
	} else {
		return EINVAL;
	}
	scred = smbfs_malloc_scred();
	smb_makescred(scred, tcnp->cn_thread, tcnp->cn_cred);
	/*
	 * It seems that Samba doesn't implement SMB_COM_MOVE call...
	 */
#ifdef notnow
	if (SMB_DIALECT(SSTOCN(smp->sm_share)) >= SMB_DIALECT_LANMAN1_0) {
		error = smbfs_smb_move(VTOSMB(fvp), VTOSMB(tdvp),
		    tcnp->cn_nameptr, tcnp->cn_namelen, flags, scred);
	} else
#endif
	{
		/*
		 * We have to do the work atomicaly
		 */
		if (tvp && tvp != fvp) {
			error = smbfs_smb_delete(VTOSMB(tvp), scred);
			if (error)
				goto out_cacherem;
			VTOSMB(fvp)->n_flag |= NGONE;
		}
		error = smbfs_smb_rename(VTOSMB(fvp), VTOSMB(tdvp),
		    tcnp->cn_nameptr, tcnp->cn_namelen, scred);
	}

	if (fvp->v_type == VDIR) {
		if (tvp != NULL && tvp->v_type == VDIR)
			cache_purge(tdvp);
		cache_purge(fdvp);
	}

out_cacherem:
	smbfs_attr_cacheremove(fdvp);
	smbfs_attr_cacheremove(tdvp);
out:
	smbfs_free_scred(scred);
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	if (tvp)
		vput(tvp);
	vrele(fdvp);
	vrele(fvp);
#ifdef possible_mistake
	vgone(fvp);
	if (tvp)
		vgone(tvp);
#endif
	return error;
}

/*
 * somtime it will come true...
 */
static int
smbfs_link(ap)
	struct vop_link_args /* {
		struct vnode *a_tdvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	return EOPNOTSUPP;
}

/*
 * smbfs_symlink link create call.
 * Sometime it will be functional...
 */
static int
smbfs_symlink(ap)
	struct vop_symlink_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
		char *a_target;
	} */ *ap;
{
	return EOPNOTSUPP;
}

static int
smbfs_mknod(ap) 
	struct vop_mknod_args /* {
	} */ *ap;
{
	return EOPNOTSUPP;
}

static int
smbfs_mkdir(ap)
	struct vop_mkdir_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vattr *a_vap;
	} */ *ap;
{
	struct vnode *dvp = ap->a_dvp;
/*	struct vattr *vap = ap->a_vap;*/
	struct vnode *vp;
	struct componentname *cnp = ap->a_cnp;
	struct smbnode *dnp = VTOSMB(dvp);
	struct vattr vattr;
	struct smb_cred *scred;
	struct smbfattr fattr;
	char *name = cnp->cn_nameptr;
	int len = cnp->cn_namelen;
	int error;

	if ((error = VOP_GETATTR(dvp, &vattr, cnp->cn_cred))) {
		return error;
	}	
	if ((name[0] == '.') && ((len == 1) || ((len == 2) && (name[1] == '.'))))
		return EEXIST;
	scred = smbfs_malloc_scred();
	smb_makescred(scred, cnp->cn_thread, cnp->cn_cred);
	error = smbfs_smb_mkdir(dnp, name, len, scred);
	if (error)
		goto out;
	error = smbfs_smb_lookup(dnp, name, len, &fattr, scred);
	if (error)
		goto out;
	error = smbfs_nget(VTOVFS(dvp), dvp, name, len, &fattr, &vp);
	if (error)
		goto out;
	*ap->a_vpp = vp;
out:
	smbfs_free_scred(scred);
	return error;
}

/*
 * smbfs_remove directory call
 */
static int
smbfs_rmdir(ap)
	struct vop_rmdir_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
/*	struct smbmount *smp = VTOSMBFS(vp);*/
	struct smbnode *dnp = VTOSMB(dvp);
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;
	int error;

	if (dvp == vp)
		return EINVAL;

	scred = smbfs_malloc_scred();
	smb_makescred(scred, cnp->cn_thread, cnp->cn_cred);
	error = smbfs_smb_rmdir(np, scred);
	if (error == 0)
		np->n_flag |= NGONE;
	dnp->n_flag |= NMODIFIED;
	smbfs_attr_cacheremove(dvp);
/*	cache_purge(dvp);*/
	cache_purge(vp);
	smbfs_free_scred(scred);
	return error;
}

/*
 * smbfs_readdir call
 */
static int
smbfs_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		u_long *a_cookies;
		int a_ncookies;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int error;

	if (vp->v_type != VDIR)
		return (EPERM);
#ifdef notnow
	if (ap->a_ncookies) {
		printf("smbfs_readdir: no support for cookies now...");
		return (EOPNOTSUPP);
	}
#endif
	error = smbfs_readvnode(vp, uio, ap->a_cred);
	return error;
}

/* ARGSUSED */
static int
smbfs_fsync(ap)
	struct vop_fsync_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode * a_vp;
		struct ucred * a_cred;
		int  a_waitfor;
		struct thread * a_td;
	} */ *ap;
{
/*	return (smb_flush(ap->a_vp, ap->a_cred, ap->a_waitfor, ap->a_td, 1));*/
    return (0);
}

static 
int smbfs_print (ap) 
	struct vop_print_args /* {
	struct vnode *a_vp;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);

	if (np == NULL) {
		printf("no smbnode data\n");
		return (0);
	}
	printf("\tname = %s, parent = %p, open = %d\n", np->n_name,
	    np->n_parent ? np->n_parent : NULL, (np->n_flag & NOPEN) != 0);
	return (0);
}

static int
smbfs_pathconf (ap)
	struct vop_pathconf_args  /* {
	struct vnode *vp;
	int name;
	register_t *retval;
	} */ *ap;
{
	struct smbmount *smp = VFSTOSMBFS(VTOVFS(ap->a_vp));
	struct smb_vc *vcp = SSTOVC(smp->sm_share);
	long *retval = ap->a_retval;
	int error = 0;
	
	switch (ap->a_name) {
	    case _PC_FILESIZEBITS:
		if (vcp->vc_sopt.sv_caps & (SMB_CAP_LARGE_READX |
		    SMB_CAP_LARGE_WRITEX))
		    *retval = 64;
		else
		    *retval = 32;
		break;
	    case _PC_NAME_MAX:
		*retval = (vcp->vc_hflags2 & SMB_FLAGS2_KNOWS_LONG_NAMES) ? 255 : 12;
		break;
	    case _PC_PATH_MAX:
		*retval = 800;	/* XXX: a correct one ? */
		break;
	    case _PC_NO_TRUNC:
		*retval = 1;
		break;
	    default:
		error = vop_stdpathconf(ap);
	}
	return error;
}

static int
smbfs_strategy (ap) 
	struct vop_strategy_args /* {
	struct buf *a_bp
	} */ *ap;
{
	struct buf *bp=ap->a_bp;
	struct ucred *cr;
	struct thread *td;

	SMBVDEBUG("\n");
	if (bp->b_flags & B_ASYNC)
		td = (struct thread *)0;
	else
		td = curthread;	/* XXX */
	if (bp->b_iocmd == BIO_READ)
		cr = bp->b_rcred;
	else
		cr = bp->b_wcred;

	if ((bp->b_flags & B_ASYNC) == 0 )
		(void)smbfs_doio(ap->a_vp, bp, cr, td);
	return (0);
}

int
smbfs_ioctl(ap)
	struct vop_ioctl_args /* {
		struct vnode *a_vp;
		u_long a_command;
		caddr_t a_data;
		int fflag;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{
	return ENOTTY;
}

static char smbfs_atl[] = "rhsvda";
static int
smbfs_getextattr(struct vop_getextattr_args *ap)
/* {
        IN struct vnode *a_vp;
        IN char *a_name;
        INOUT struct uio *a_uio;
        IN struct ucred *a_cred;
        IN struct thread *a_td;
};
*/
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;
	struct uio *uio = ap->a_uio;
	const char *name = ap->a_name;
	struct smbnode *np = VTOSMB(vp);
	struct vattr vattr;
	char buf[10];
	int i, attr, error;

	error = VOP_ACCESS(vp, VREAD, cred, td);
	if (error)
		return error;
	error = VOP_GETATTR(vp, &vattr, cred);
	if (error)
		return error;
	if (strcmp(name, "dosattr") == 0) {
		attr = np->n_dosattr;
		for (i = 0; i < 6; i++, attr >>= 1)
			buf[i] = (attr & 1) ? smbfs_atl[i] : '-';
		buf[i] = 0;
		error = uiomove(buf, i, uio);
		
	} else
		error = EINVAL;
	return error;
}

/*
 * Since we expected to support F_GETLK (and SMB protocol has no such function),
 * it is necessary to use lf_advlock(). It would be nice if this function had
 * a callback mechanism because it will help to improve a level of consistency.
 */
int
smbfs_advlock(ap)
	struct vop_advlock_args /* {
		struct vnode *a_vp;
		caddr_t  a_id;
		int  a_op;
		struct flock *a_fl;
		int  a_flags;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);
	struct flock *fl = ap->a_fl;
	caddr_t id = (caddr_t)1 /* ap->a_id */;
/*	int flags = ap->a_flags;*/
	struct thread *td = curthread;
	struct smb_cred *scred;
	u_quad_t size;
	off_t start, end, oadd;
	int error, lkop;

	if (vp->v_type == VDIR) {
		/*
		 * SMB protocol have no support for directory locking.
		 * Although locks can be processed on local machine, I don't
		 * think that this is a good idea, because some programs
		 * can work wrong assuming directory is locked. So, we just
		 * return 'operation not supported
		 */
		 return EOPNOTSUPP;
	}
	size = np->n_size;
	switch (fl->l_whence) {

	case SEEK_SET:
	case SEEK_CUR:
		start = fl->l_start;
		break;

	case SEEK_END:
		if (size > OFF_MAX ||
		    (fl->l_start > 0 && size > OFF_MAX - fl->l_start))
			return EOVERFLOW;
		start = size + fl->l_start;
		break;

	default:
		return EINVAL;
	}
	if (start < 0)
		return EINVAL;
	if (fl->l_len < 0) {
		if (start == 0)
			return EINVAL;
		end = start - 1;
		start += fl->l_len;
		if (start < 0)
			return EINVAL;
	} else if (fl->l_len == 0)
		end = -1;
	else {
		oadd = fl->l_len - 1;
		if (oadd > OFF_MAX - start)
			return EOVERFLOW;
		end = start + oadd;
	}
	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, td->td_ucred);
	switch (ap->a_op) {
	    case F_SETLK:
		switch (fl->l_type) {
		    case F_WRLCK:
			lkop = SMB_LOCK_EXCL;
			break;
		    case F_RDLCK:
			lkop = SMB_LOCK_SHARED;
			break;
		    case F_UNLCK:
			lkop = SMB_LOCK_RELEASE;
			break;
		    default:
			smbfs_free_scred(scred);
			return EINVAL;
		}
		error = lf_advlock(ap, &vp->v_lockf, size);
		if (error)
			break;
		lkop = SMB_LOCK_EXCL;
		error = smbfs_smb_lock(np, lkop, id, start, end, scred);
		if (error) {
			int oldtype = fl->l_type;
			fl->l_type = F_UNLCK;
			ap->a_op = F_UNLCK;
			lf_advlock(ap, &vp->v_lockf, size);
			fl->l_type = oldtype;
		}
		break;
	    case F_UNLCK:
		lf_advlock(ap, &vp->v_lockf, size);
		error = smbfs_smb_lock(np, SMB_LOCK_RELEASE, id, start, end, scred);
		break;
	    case F_GETLK:
		error = lf_advlock(ap, &vp->v_lockf, size);
		break;
	    default:
		smbfs_free_scred(scred);
		return EINVAL;
	}
	smbfs_free_scred(scred);
	return error;
}

static int
smbfs_pathcheck(struct smbmount *smp, const char *name, int nmlen, int nameiop)
{
	static const char *badchars = "*/:<>?";
	static const char *badchars83 = " +|,[]=;";
	const char *cp;
	int i, error;

	/*
	 * Backslash characters, being a path delimiter, are prohibited
	 * within a path component even for LOOKUP operations.
	 */
	if (strchr(name, '\\') != NULL)
		return ENOENT;

	if (nameiop == LOOKUP)
		return 0;
	error = ENOENT;
	if (SMB_DIALECT(SSTOVC(smp->sm_share)) < SMB_DIALECT_LANMAN2_0) {
		/*
		 * Name should conform 8.3 format
		 */
		if (nmlen > 12)
			return ENAMETOOLONG;
		cp = strchr(name, '.');
		if (cp == NULL)
			return error;
		if (cp == name || (cp - name) > 8)
			return error;
		cp = strchr(cp + 1, '.');
		if (cp != NULL)
			return error;
		for (cp = name, i = 0; i < nmlen; i++, cp++)
			if (strchr(badchars83, *cp) != NULL)
				return error;
	}
	for (cp = name, i = 0; i < nmlen; i++, cp++)
		if (strchr(badchars, *cp) != NULL)
			return error;
	return 0;
}

/*
 * Things go even weird without fixed inode numbers...
 */
int
smbfs_lookup(ap)
	struct vop_lookup_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	struct thread *td = cnp->cn_thread;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *vp;
	struct smbmount *smp;
	struct mount *mp = dvp->v_mount;
	struct smbnode *dnp;
	struct smbfattr fattr, *fap;
	struct smb_cred *scred;
	char *name = cnp->cn_nameptr;
	int flags = cnp->cn_flags;
	int nameiop = cnp->cn_nameiop;
	int nmlen = cnp->cn_namelen;
	int error, islastcn, isdot;
	int killit;
	
	SMBVDEBUG("\n");
	if (dvp->v_type != VDIR)
		return ENOTDIR;
	if ((flags & ISDOTDOT) && (dvp->v_vflag & VV_ROOT)) {
		SMBFSERR("invalid '..'\n");
		return EIO;
	}
	islastcn = flags & ISLASTCN;
	if (islastcn && (mp->mnt_flag & MNT_RDONLY) && (nameiop != LOOKUP))
		return EROFS;
	if ((error = VOP_ACCESS(dvp, VEXEC, cnp->cn_cred, td)) != 0)
		return error;
	smp = VFSTOSMBFS(mp);
	dnp = VTOSMB(dvp);
	isdot = (nmlen == 1 && name[0] == '.');

	error = smbfs_pathcheck(smp, cnp->cn_nameptr, cnp->cn_namelen, nameiop);

	if (error) 
		return ENOENT;

	error = cache_lookup(dvp, vpp, cnp, NULL, NULL);
	SMBVDEBUG("cache_lookup returned %d\n", error);
	if (error > 0)
		return error;
	if (error) {		/* name was found */
		struct vattr vattr;

		killit = 0;
		vp = *vpp;
		error = VOP_GETATTR(vp, &vattr, cnp->cn_cred);
		/*
		 * If the file type on the server is inconsistent
		 * with what it was when we created the vnode,
		 * kill the bogus vnode now and fall through to
		 * the code below to create a new one with the
		 * right type.
		 */
		if (error == 0 &&
		   ((vp->v_type == VDIR &&
		   (VTOSMB(vp)->n_dosattr & SMB_FA_DIR) == 0) ||
		   (vp->v_type == VREG &&
		   (VTOSMB(vp)->n_dosattr & SMB_FA_DIR) != 0)))
		   killit = 1;
		else if (error == 0
	     /*    && vattr.va_ctime.tv_sec == VTOSMB(vp)->n_ctime*/) {
		     if (nameiop != LOOKUP && islastcn)
			     cnp->cn_flags |= SAVENAME;
		     SMBVDEBUG("use cached vnode\n");
		     return (0);
		}
		cache_purge(vp);
		/*
		 * XXX This is not quite right, if '.' is
		 * inconsistent, we really need to start the lookup
		 * all over again.  Hopefully there is some other
		 * guarantee that prevents this case from happening.
		 */
		if (killit && vp != dvp)
			vgone(vp);
		if (vp != dvp)
			vput(vp);
		else
			vrele(vp);
		*vpp = NULLVP;
	}
	/* 
	 * entry is not in the cache or has been expired
	 */
	error = 0;
	*vpp = NULLVP;
	scred = smbfs_malloc_scred();
	smb_makescred(scred, td, cnp->cn_cred);
	fap = &fattr;
	if (flags & ISDOTDOT) {
		/*
		 * In the DOTDOT case, don't go over-the-wire
		 * in order to request attributes. We already
		 * know it's a directory and subsequent call to
		 * smbfs_getattr() will restore consistency.
		 *
		 */
		SMBVDEBUG("smbfs_smb_lookup: dotdot\n");
	} else if (isdot) {
		error = smbfs_smb_lookup(dnp, NULL, 0, fap, scred);
		SMBVDEBUG("result of smbfs_smb_lookup: %d\n", error);
	}
	else {
		error = smbfs_smb_lookup(dnp, name, nmlen, fap, scred);
		SMBVDEBUG("result of smbfs_smb_lookup: %d\n", error);
	}
	if (error && error != ENOENT)
		goto out;
	if (error) {			/* entry not found */
		/*
		 * Handle RENAME or CREATE case...
		 */
		if ((nameiop == CREATE || nameiop == RENAME) && islastcn) {
			error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
			if (error)
				goto out;
			cnp->cn_flags |= SAVENAME;
			error = EJUSTRETURN;
			goto out;
		}
		error = ENOENT;
		goto out;
	}/* else {
		SMBVDEBUG("Found entry %s with id=%d\n", fap->entryName, fap->dirEntNum);
	}*/
	/*
	 * handle DELETE case ...
	 */
	if (nameiop == DELETE && islastcn) { 	/* delete last component */
		error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
		if (error)
			goto out;
		if (isdot) {
			VREF(dvp);
			*vpp = dvp;
			goto out;
		}
		error = smbfs_nget(mp, dvp, name, nmlen, fap, &vp);
		if (error)
			goto out;
		*vpp = vp;
		cnp->cn_flags |= SAVENAME;
		goto out;
	}
	if (nameiop == RENAME && islastcn) {
		error = VOP_ACCESS(dvp, VWRITE, cnp->cn_cred, td);
		if (error)
			goto out;
		if (isdot) {
			error = EISDIR;
			goto out;
		}
		error = smbfs_nget(mp, dvp, name, nmlen, fap, &vp);
		if (error)
			goto out;
		*vpp = vp;
		cnp->cn_flags |= SAVENAME;
		goto out;
	}
	if (flags & ISDOTDOT) {
		mp = dvp->v_mount;
		error = vfs_busy(mp, MBF_NOWAIT);
		if (error != 0) {
			vfs_ref(mp);
			VOP_UNLOCK(dvp, 0);
			error = vfs_busy(mp, 0);
			vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
			vfs_rel(mp);
			if (error) {
				error = ENOENT;
				goto out;
			}
			if ((dvp->v_iflag & VI_DOOMED) != 0) {
				vfs_unbusy(mp);
				error = ENOENT;
				goto out;
			}
		}	
		VOP_UNLOCK(dvp, 0);
		error = smbfs_nget(mp, dvp, name, nmlen, NULL, &vp);
		vfs_unbusy(mp);
		vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY);
		if ((dvp->v_iflag & VI_DOOMED) != 0) {
			if (error == 0)
				vput(vp);
			error = ENOENT;
		}
		if (error)
			goto out;
		*vpp = vp;
	} else if (isdot) {
		vref(dvp);
		*vpp = dvp;
	} else {
		error = smbfs_nget(mp, dvp, name, nmlen, fap, &vp);
		if (error)
			goto out;
		*vpp = vp;
		SMBVDEBUG("lookup: getnewvp!\n");
	}
	if ((cnp->cn_flags & MAKEENTRY)/* && !islastcn*/) {
/*		VTOSMB(*vpp)->n_ctime = VTOSMB(*vpp)->n_vattr.va_ctime.tv_sec;*/
		cache_enter(dvp, *vpp, cnp);
	}
out:
	smbfs_free_scred(scred);
	return (error);
}
