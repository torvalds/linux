/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2002 Robert N. M. Watson
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
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
 */

/*
 * Support for filesystem extended attribute: UFS-specific support functions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ufs.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/lock.h>
#include <sys/dirent.h>
#include <sys/extattr.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <vm/uma.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#ifdef UFS_EXTATTR

FEATURE(ufs_extattr, "ufs extended attribute support");

static MALLOC_DEFINE(M_UFS_EXTATTR, "ufs_extattr", "ufs extended attribute");

static int ufs_extattr_sync = 0;
SYSCTL_INT(_debug, OID_AUTO, ufs_extattr_sync, CTLFLAG_RW, &ufs_extattr_sync,
    0, "");

static int	ufs_extattr_valid_attrname(int attrnamespace,
		    const char *attrname);
static int	ufs_extattr_enable_with_open(struct ufsmount *ump,
		    struct vnode *vp, int attrnamespace, const char *attrname,
		    struct thread *td);
static int	ufs_extattr_enable(struct ufsmount *ump, int attrnamespace,
		    const char *attrname, struct vnode *backing_vnode,
		    struct thread *td);
static int	ufs_extattr_disable(struct ufsmount *ump, int attrnamespace,
		    const char *attrname, struct thread *td);
static int	ufs_extattr_get(struct vnode *vp, int attrnamespace,
		    const char *name, struct uio *uio, size_t *size,
		    struct ucred *cred, struct thread *td);
static int	ufs_extattr_set(struct vnode *vp, int attrnamespace,
		    const char *name, struct uio *uio, struct ucred *cred,
		    struct thread *td);
static int	ufs_extattr_rm(struct vnode *vp, int attrnamespace,
		    const char *name, struct ucred *cred, struct thread *td);
#ifdef UFS_EXTATTR_AUTOSTART
static int	ufs_extattr_autostart_locked(struct mount *mp,
		    struct thread *td);
#endif
static int	ufs_extattr_start_locked(struct ufsmount *ump,
		    struct thread *td);

/*
 * Per-FS attribute lock protecting attribute operations.
 *
 * XXXRW: Perhaps something more fine-grained would be appropriate, but at
 * the end of the day we're going to contend on the vnode lock for the
 * backing file anyway.
 */
static void
ufs_extattr_uepm_lock(struct ufsmount *ump)
{

	sx_xlock(&ump->um_extattr.uepm_lock);
}

static void
ufs_extattr_uepm_unlock(struct ufsmount *ump)
{

	sx_xunlock(&ump->um_extattr.uepm_lock);
}

/*-
 * Determine whether the name passed is a valid name for an actual
 * attribute.
 *
 * Invalid currently consists of:
 *	 NULL pointer for attrname
 *	 zero-length attrname (used to retrieve application attribute list)
 */
static int
ufs_extattr_valid_attrname(int attrnamespace, const char *attrname)
{

	if (attrname == NULL)
		return (0);
	if (strlen(attrname) == 0)
		return (0);
	return (1);
}

/*
 * Locate an attribute given a name and mountpoint.
 * Must be holding uepm lock for the mount point.
 */
static struct ufs_extattr_list_entry *
ufs_extattr_find_attr(struct ufsmount *ump, int attrnamespace,
    const char *attrname)
{
	struct ufs_extattr_list_entry *search_attribute;

	sx_assert(&ump->um_extattr.uepm_lock, SA_XLOCKED);

	for (search_attribute = LIST_FIRST(&ump->um_extattr.uepm_list);
	    search_attribute != NULL;
	    search_attribute = LIST_NEXT(search_attribute, uele_entries)) {
		if (!(strncmp(attrname, search_attribute->uele_attrname,
		    UFS_EXTATTR_MAXEXTATTRNAME)) &&
		    (attrnamespace == search_attribute->uele_attrnamespace)) {
			return (search_attribute);
		}
	}

	return (0);
}

/*
 * Initialize per-FS structures supporting extended attributes.  Do not
 * start extended attributes yet.
 */
void
ufs_extattr_uepm_init(struct ufs_extattr_per_mount *uepm)
{

	uepm->uepm_flags = 0;
	LIST_INIT(&uepm->uepm_list);
	sx_init(&uepm->uepm_lock, "ufs_extattr_sx");
	uepm->uepm_flags |= UFS_EXTATTR_UEPM_INITIALIZED;
}

/*
 * Destroy per-FS structures supporting extended attributes.  Assumes
 * that EAs have already been stopped, and will panic if not.
 */
void
ufs_extattr_uepm_destroy(struct ufs_extattr_per_mount *uepm)
{

	if (!(uepm->uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED))
		panic("ufs_extattr_uepm_destroy: not initialized");

	if ((uepm->uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		panic("ufs_extattr_uepm_destroy: called while still started");

	/*
	 * It's not clear that either order for the next two lines is
	 * ideal, and it should never be a problem if this is only called
	 * during unmount, and with vfs_busy().
	 */
	uepm->uepm_flags &= ~UFS_EXTATTR_UEPM_INITIALIZED;
	sx_destroy(&uepm->uepm_lock);
}

/*
 * Start extended attribute support on an FS.
 */
int
ufs_extattr_start(struct mount *mp, struct thread *td)
{
	struct ufsmount *ump;
	int error = 0;

	ump = VFSTOUFS(mp);

	ufs_extattr_uepm_lock(ump);
	error = ufs_extattr_start_locked(ump, td);
	ufs_extattr_uepm_unlock(ump);
	return (error);
}

static int
ufs_extattr_start_locked(struct ufsmount *ump, struct thread *td)
{
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED))
		return (EOPNOTSUPP);
	if (ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)
		return (EBUSY);

	ump->um_extattr.uepm_flags |= UFS_EXTATTR_UEPM_STARTED;
	ump->um_extattr.uepm_ucred = crhold(td->td_ucred);
	return (0);
}

#ifdef UFS_EXTATTR_AUTOSTART
/*
 * Helper routine: given a locked parent directory and filename, return
 * the locked vnode of the inode associated with the name.  Will not
 * follow symlinks, may return any type of vnode.  Lock on parent will
 * be released even in the event of a failure.  In the event that the
 * target is the parent (i.e., "."), there will be two references and
 * one lock, requiring the caller to possibly special-case.
 */
#define	UE_GETDIR_LOCKPARENT	1
#define	UE_GETDIR_LOCKPARENT_DONT	2
static int
ufs_extattr_lookup(struct vnode *start_dvp, int lockparent, char *dirname,
    struct vnode **vp, struct thread *td)
{
	struct vop_cachedlookup_args vargs;
	struct componentname cnp;
	struct vnode *target_vp;
	int error;

	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = LOOKUP;
	cnp.cn_flags = ISLASTCN;
	if (lockparent == UE_GETDIR_LOCKPARENT)
		cnp.cn_flags |= LOCKPARENT;
	cnp.cn_lkflags = LK_EXCLUSIVE;
	cnp.cn_thread = td;
	cnp.cn_cred = td->td_ucred;
	cnp.cn_pnbuf = uma_zalloc(namei_zone, M_WAITOK);
	cnp.cn_nameptr = cnp.cn_pnbuf;
	error = copystr(dirname, cnp.cn_pnbuf, MAXPATHLEN,
	    (size_t *) &cnp.cn_namelen);
	if (error) {
		if (lockparent == UE_GETDIR_LOCKPARENT_DONT) {
			VOP_UNLOCK(start_dvp, 0);
		}
		uma_zfree(namei_zone, cnp.cn_pnbuf);
		printf("ufs_extattr_lookup: copystr failed\n");
		return (error);
	}
	cnp.cn_namelen--;	/* trim nul termination */
	vargs.a_gen.a_desc = NULL;
	vargs.a_dvp = start_dvp;
	vargs.a_vpp = &target_vp;
	vargs.a_cnp = &cnp;
	error = ufs_lookup(&vargs);
	uma_zfree(namei_zone, cnp.cn_pnbuf);
	if (error) {
		/*
		 * Error condition, may have to release the lock on the parent
		 * if ufs_lookup() didn't.
		 */
		if (lockparent == UE_GETDIR_LOCKPARENT_DONT)
			VOP_UNLOCK(start_dvp, 0);

		/*
		 * Check that ufs_lookup() didn't release the lock when we
		 * didn't want it to.
		 */
		if (lockparent == UE_GETDIR_LOCKPARENT)
			ASSERT_VOP_LOCKED(start_dvp, "ufs_extattr_lookup");

		return (error);
	}
/*
	if (target_vp == start_dvp)
		panic("ufs_extattr_lookup: target_vp == start_dvp");
*/

	if (target_vp != start_dvp && lockparent == UE_GETDIR_LOCKPARENT_DONT)
		VOP_UNLOCK(start_dvp, 0);

	if (lockparent == UE_GETDIR_LOCKPARENT)
		ASSERT_VOP_LOCKED(start_dvp, "ufs_extattr_lookup");

	/* printf("ufs_extattr_lookup: success\n"); */
	*vp = target_vp;
	return (0);
}
#endif /* !UFS_EXTATTR_AUTOSTART */

/*
 * Enable an EA using the passed filesystem, backing vnode, attribute name,
 * namespace, and proc.  Will perform a VOP_OPEN() on the vp, so expects vp
 * to be locked when passed in.  The vnode will be returned unlocked,
 * regardless of success/failure of the function.  As a result, the caller
 * will always need to vrele(), but not vput().
 */
static int
ufs_extattr_enable_with_open(struct ufsmount *ump, struct vnode *vp,
    int attrnamespace, const char *attrname, struct thread *td)
{
	int error;

	error = VOP_OPEN(vp, FREAD|FWRITE, td->td_ucred, td, NULL);
	if (error) {
		printf("ufs_extattr_enable_with_open.VOP_OPEN(): failed "
		    "with %d\n", error);
		VOP_UNLOCK(vp, 0);
		return (error);
	}

	VOP_ADD_WRITECOUNT(vp, 1);
	CTR3(KTR_VFS, "%s: vp %p v_writecount increased to %d", __func__, vp,
	    vp->v_writecount);

	vref(vp);

	VOP_UNLOCK(vp, 0);

	error = ufs_extattr_enable(ump, attrnamespace, attrname, vp, td);
	if (error != 0)
		vn_close(vp, FREAD|FWRITE, td->td_ucred, td);
	return (error);
}

#ifdef UFS_EXTATTR_AUTOSTART
/*
 * Given a locked directory vnode, iterate over the names in the directory
 * and use ufs_extattr_lookup() to retrieve locked vnodes of potential
 * attribute files.  Then invoke ufs_extattr_enable_with_open() on each
 * to attempt to start the attribute.  Leaves the directory locked on
 * exit.
 */
static int
ufs_extattr_iterate_directory(struct ufsmount *ump, struct vnode *dvp,
    int attrnamespace, struct thread *td)
{
	struct vop_readdir_args vargs;
	struct dirent *dp, *edp;
	struct vnode *attr_vp;
	struct uio auio;
	struct iovec aiov;
	char *dirbuf;
	int error, eofflag = 0;

	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	dirbuf = malloc(DIRBLKSIZ, M_TEMP, M_WAITOK);

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_td = td;
	auio.uio_offset = 0;

	vargs.a_gen.a_desc = NULL;
	vargs.a_vp = dvp;
	vargs.a_uio = &auio;
	vargs.a_cred = td->td_ucred;
	vargs.a_eofflag = &eofflag;
	vargs.a_ncookies = NULL;
	vargs.a_cookies = NULL;

	while (!eofflag) {
		auio.uio_resid = DIRBLKSIZ;
		aiov.iov_base = dirbuf;
		aiov.iov_len = DIRBLKSIZ;
		error = ufs_readdir(&vargs);
		if (error) {
			printf("ufs_extattr_iterate_directory: ufs_readdir "
			    "%d\n", error);
			return (error);
		}

		edp = (struct dirent *)&dirbuf[DIRBLKSIZ - auio.uio_resid];
		for (dp = (struct dirent *)dirbuf; dp < edp; ) {
			if (dp->d_reclen == 0)
				break;
			error = ufs_extattr_lookup(dvp, UE_GETDIR_LOCKPARENT,
			    dp->d_name, &attr_vp, td);
			if (error) {
				printf("ufs_extattr_iterate_directory: lookup "
				    "%s %d\n", dp->d_name, error);
			} else if (attr_vp == dvp) {
				vrele(attr_vp);
			} else if (attr_vp->v_type != VREG) {
				vput(attr_vp);
			} else {
				error = ufs_extattr_enable_with_open(ump,
				    attr_vp, attrnamespace, dp->d_name, td);
				vrele(attr_vp);
				if (error) {
					printf("ufs_extattr_iterate_directory: "
					    "enable %s %d\n", dp->d_name,
					    error);
				} else if (bootverbose) {
					printf("UFS autostarted EA %s\n",
					    dp->d_name);
				}
			}
			dp = (struct dirent *) ((char *)dp + dp->d_reclen);
			if (dp >= edp)
				break;
		}
	}
	free(dirbuf, M_TEMP);
	
	return (0);
}

/*
 * Auto-start of extended attributes, to be executed (optionally) at
 * mount-time.
 */
int
ufs_extattr_autostart(struct mount *mp, struct thread *td)
{
	struct ufsmount *ump;
	int error;

	ump = VFSTOUFS(mp);
	ufs_extattr_uepm_lock(ump);
	error = ufs_extattr_autostart_locked(mp, td);
	ufs_extattr_uepm_unlock(ump);
	return (error);
}

static int
ufs_extattr_autostart_locked(struct mount *mp, struct thread *td)
{
	struct vnode *rvp, *attr_dvp, *attr_system_dvp, *attr_user_dvp;
	struct ufsmount *ump = VFSTOUFS(mp);
	int error;

	/*
	 * UFS_EXTATTR applies only to UFS1, as UFS2 uses native extended
	 * attributes, so don't autostart.
	 */
	if (ump->um_fstype != UFS1)
		return (0);

	/*
	 * Does UFS_EXTATTR_FSROOTSUBDIR exist off the filesystem root?
	 * If so, automatically start EA's.
	 */
	error = VFS_ROOT(mp, LK_EXCLUSIVE, &rvp);
	if (error) {
		printf("ufs_extattr_autostart.VFS_ROOT() returned %d\n",
		    error);
		return (error);
	}

	error = ufs_extattr_lookup(rvp, UE_GETDIR_LOCKPARENT_DONT,
	    UFS_EXTATTR_FSROOTSUBDIR, &attr_dvp, td);
	if (error) {
		/* rvp ref'd but now unlocked */
		vrele(rvp);
		return (error);
	}
	if (rvp == attr_dvp) {
		/* Should never happen. */
		vput(rvp);
		vrele(attr_dvp);
		return (EINVAL);
	}
	vrele(rvp);

	if (attr_dvp->v_type != VDIR) {
		printf("ufs_extattr_autostart: %s != VDIR\n",
		    UFS_EXTATTR_FSROOTSUBDIR);
		goto return_vput_attr_dvp;
	}

	error = ufs_extattr_start_locked(ump, td);
	if (error) {
		printf("ufs_extattr_autostart: ufs_extattr_start failed (%d)\n",
		    error);
		goto return_vput_attr_dvp;
	}

	/*
	 * Look for two subdirectories: UFS_EXTATTR_SUBDIR_SYSTEM,
	 * UFS_EXTATTR_SUBDIR_USER.  For each, iterate over the sub-directory,
	 * and start with appropriate type.  Failures in either don't
	 * result in an over-all failure.  attr_dvp is left locked to
	 * be cleaned up on exit.
	 */
	error = ufs_extattr_lookup(attr_dvp, UE_GETDIR_LOCKPARENT,
	    UFS_EXTATTR_SUBDIR_SYSTEM, &attr_system_dvp, td);
	if (!error) {
		error = ufs_extattr_iterate_directory(VFSTOUFS(mp),
		    attr_system_dvp, EXTATTR_NAMESPACE_SYSTEM, td);
		if (error)
			printf("ufs_extattr_iterate_directory returned %d\n",
			    error);
		vput(attr_system_dvp);
	}

	error = ufs_extattr_lookup(attr_dvp, UE_GETDIR_LOCKPARENT,
	    UFS_EXTATTR_SUBDIR_USER, &attr_user_dvp, td);
	if (!error) {
		error = ufs_extattr_iterate_directory(VFSTOUFS(mp),
		    attr_user_dvp, EXTATTR_NAMESPACE_USER, td);
		if (error)
			printf("ufs_extattr_iterate_directory returned %d\n",
			    error);
		vput(attr_user_dvp);
	}

	/* Mask startup failures in sub-directories. */
	error = 0;

return_vput_attr_dvp:
	vput(attr_dvp);

	return (error);
}
#endif /* !UFS_EXTATTR_AUTOSTART */

/*
 * Stop extended attribute support on an FS.
 */
int
ufs_extattr_stop(struct mount *mp, struct thread *td)
{
	struct ufs_extattr_list_entry *uele;
	struct ufsmount *ump = VFSTOUFS(mp);
	int error = 0;

	ufs_extattr_uepm_lock(ump);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		error = EOPNOTSUPP;
		goto unlock;
	}

	while ((uele = LIST_FIRST(&ump->um_extattr.uepm_list)) != NULL) {
		ufs_extattr_disable(ump, uele->uele_attrnamespace,
		    uele->uele_attrname, td);
	}

	ump->um_extattr.uepm_flags &= ~UFS_EXTATTR_UEPM_STARTED;

	crfree(ump->um_extattr.uepm_ucred);
	ump->um_extattr.uepm_ucred = NULL;

unlock:
	ufs_extattr_uepm_unlock(ump);

	return (error);
}

/*
 * Enable a named attribute on the specified filesystem; provide an
 * unlocked backing vnode to hold the attribute data.
 */
static int
ufs_extattr_enable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct vnode *backing_vnode, struct thread *td)
{
	struct ufs_extattr_list_entry *attribute;
	struct iovec aiov;
	struct uio auio;
	int error = 0;

	if (!ufs_extattr_valid_attrname(attrnamespace, attrname))
		return (EINVAL);
	if (backing_vnode->v_type != VREG)
		return (EINVAL);

	attribute = malloc(sizeof(struct ufs_extattr_list_entry),
	    M_UFS_EXTATTR, M_WAITOK);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		error = EOPNOTSUPP;
		goto free_exit;
	}

	if (ufs_extattr_find_attr(ump, attrnamespace, attrname)) {
		error = EEXIST;
		goto free_exit;
	}

	strncpy(attribute->uele_attrname, attrname,
	    UFS_EXTATTR_MAXEXTATTRNAME);
	attribute->uele_attrnamespace = attrnamespace;
	bzero(&attribute->uele_fileheader,
	    sizeof(struct ufs_extattr_fileheader));
	
	attribute->uele_backing_vnode = backing_vnode;

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t) &attribute->uele_fileheader;
	aiov.iov_len = sizeof(struct ufs_extattr_fileheader);
	auio.uio_resid = sizeof(struct ufs_extattr_fileheader);
	auio.uio_offset = (off_t) 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;

	vn_lock(backing_vnode, LK_SHARED | LK_RETRY);
	error = VOP_READ(backing_vnode, &auio, IO_NODELOCKED,
	    ump->um_extattr.uepm_ucred);

	if (error)
		goto unlock_free_exit;

	if (auio.uio_resid != 0) {
		printf("ufs_extattr_enable: malformed attribute header\n");
		error = EINVAL;
		goto unlock_free_exit;
	}

	if (attribute->uele_fileheader.uef_magic != UFS_EXTATTR_MAGIC) {
		printf("ufs_extattr_enable: invalid attribute header magic\n");
		error = EINVAL;
		goto unlock_free_exit;
	}

	if (attribute->uele_fileheader.uef_version != UFS_EXTATTR_VERSION) {
		printf("ufs_extattr_enable: incorrect attribute header "
		    "version\n");
		error = EINVAL;
		goto unlock_free_exit;
	}

	ASSERT_VOP_LOCKED(backing_vnode, "ufs_extattr_enable");
	LIST_INSERT_HEAD(&ump->um_extattr.uepm_list, attribute,
	    uele_entries);

	VOP_UNLOCK(backing_vnode, 0);
	return (0);

unlock_free_exit:
	VOP_UNLOCK(backing_vnode, 0);

free_exit:
	free(attribute, M_UFS_EXTATTR);
	return (error);
}

/*
 * Disable extended attribute support on an FS.
 */
static int
ufs_extattr_disable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct thread *td)
{
	struct ufs_extattr_list_entry *uele;
	int error = 0;

	if (!ufs_extattr_valid_attrname(attrnamespace, attrname))
		return (EINVAL);

	uele = ufs_extattr_find_attr(ump, attrnamespace, attrname);
	if (!uele)
		return (ENOATTR);

	LIST_REMOVE(uele, uele_entries);

	vn_lock(uele->uele_backing_vnode, LK_SHARED | LK_RETRY);
	ASSERT_VOP_LOCKED(uele->uele_backing_vnode, "ufs_extattr_disable");
	VOP_UNLOCK(uele->uele_backing_vnode, 0);
	error = vn_close(uele->uele_backing_vnode, FREAD|FWRITE,
	    td->td_ucred, td);

	free(uele, M_UFS_EXTATTR);

	return (error);
}

/*
 * VFS call to manage extended attributes in UFS.  If filename_vp is
 * non-NULL, it must be passed in locked, and regardless of errors in
 * processing, will be unlocked.
 */
int
ufs_extattrctl(struct mount *mp, int cmd, struct vnode *filename_vp,
    int attrnamespace, const char *attrname)
{
	struct ufsmount *ump = VFSTOUFS(mp);
	struct thread *td = curthread;
	int error;

	/*
	 * Processes with privilege, but in jail, are not allowed to
	 * configure extended attributes.
	 */
	error = priv_check(td, PRIV_UFS_EXTATTRCTL);
	if (error) {
		if (filename_vp != NULL)
			VOP_UNLOCK(filename_vp, 0);
		return (error);
	}

	/*
	 * We only allow extattrctl(2) on UFS1 file systems, as UFS2 uses
	 * native extended attributes.
	 */
	if (ump->um_fstype != UFS1) {
		if (filename_vp != NULL)
			VOP_UNLOCK(filename_vp, 0);
		return (EOPNOTSUPP);
	}

	switch(cmd) {
	case UFS_EXTATTR_CMD_START:
		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0);
			return (EINVAL);
		}
		if (attrname != NULL)
			return (EINVAL);

		error = ufs_extattr_start(mp, td);

		return (error);
		
	case UFS_EXTATTR_CMD_STOP:
		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0);
			return (EINVAL);
		}
		if (attrname != NULL)
			return (EINVAL);

		error = ufs_extattr_stop(mp, td);

		return (error);

	case UFS_EXTATTR_CMD_ENABLE:

		if (filename_vp == NULL)
			return (EINVAL);
		if (attrname == NULL) {
			VOP_UNLOCK(filename_vp, 0);
			return (EINVAL);
		}

		/*
		 * ufs_extattr_enable_with_open() will always unlock the
		 * vnode, regardless of failure.
		 */
		ufs_extattr_uepm_lock(ump);
		error = ufs_extattr_enable_with_open(ump, filename_vp,
		    attrnamespace, attrname, td);
		ufs_extattr_uepm_unlock(ump);

		return (error);

	case UFS_EXTATTR_CMD_DISABLE:

		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0);
			return (EINVAL);
		}
		if (attrname == NULL)
			return (EINVAL);

		ufs_extattr_uepm_lock(ump);
		error = ufs_extattr_disable(ump, attrnamespace, attrname,
		    td);
		ufs_extattr_uepm_unlock(ump);

		return (error);

	default:
		return (EINVAL);
	}
}

/*
 * Vnode operating to retrieve a named extended attribute.
 */
int
ufs_getextattr(struct vop_getextattr_args *ap)
/*
vop_getextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	OUT size_t *a_size;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	struct mount *mp = ap->a_vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp);
	int error;

	ufs_extattr_uepm_lock(ump);

	error = ufs_extattr_get(ap->a_vp, ap->a_attrnamespace, ap->a_name,
	    ap->a_uio, ap->a_size, ap->a_cred, ap->a_td);

	ufs_extattr_uepm_unlock(ump);

	return (error);
}

/*
 * Real work associated with retrieving a named attribute--assumes that
 * the attribute lock has already been grabbed.
 */
static int
ufs_extattr_get(struct vnode *vp, int attrnamespace, const char *name,
    struct uio *uio, size_t *size, struct ucred *cred, struct thread *td)
{
	struct ufs_extattr_list_entry *attribute;
	struct ufs_extattr_header ueh;
	struct iovec local_aiov;
	struct uio local_aio;
	struct mount *mp = vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct inode *ip = VTOI(vp);
	off_t base_offset;
	size_t len, old_len;
	int error = 0;

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);

	if (strlen(name) == 0)
		return (EINVAL);

	error = extattr_check_cred(vp, attrnamespace, cred, td, VREAD);
	if (error)
		return (error);

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		return (ENOATTR);

	/*
	 * Allow only offsets of zero to encourage the read/replace
	 * extended attribute semantic.  Otherwise we can't guarantee
	 * atomicity, as we don't provide locks for extended attributes.
	 */
	if (uio != NULL && uio->uio_offset != 0)
		return (ENXIO);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Read in the data header to see if the data is defined, and if so
	 * how much.
	 */
	bzero(&ueh, sizeof(struct ufs_extattr_header));
	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_READ;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_td = td;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);
	
	/*
	 * Acquire locks.
	 *
	 * Don't need to get a lock on the backing file if the getattr is
	 * being applied to the backing file, as the lock is already held.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode, LK_SHARED | LK_RETRY);

	error = VOP_READ(attribute->uele_backing_vnode, &local_aio,
	    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	/* Defined? */
	if ((ueh.ueh_flags & UFS_EXTATTR_ATTR_FLAG_INUSE) == 0) {
		error = ENOATTR;
		goto vopunlock_exit;
	}

	/* Valid for the current inode generation? */
	if (ueh.ueh_i_gen != ip->i_gen) {
		/*
		 * The inode itself has a different generation number
		 * than the attribute data.  For now, the best solution
		 * is to coerce this to undefined, and let it get cleaned
		 * up by the next write or extattrctl clean.
		 */
		printf("ufs_extattr_get (%s): inode number inconsistency (%d, %ju)\n",
		    mp->mnt_stat.f_mntonname, ueh.ueh_i_gen, (uintmax_t)ip->i_gen);
		error = ENOATTR;
		goto vopunlock_exit;
	}

	/* Local size consistency check. */
	if (ueh.ueh_len > attribute->uele_fileheader.uef_size) {
		error = ENXIO;
		goto vopunlock_exit;
	}

	/* Return full data size if caller requested it. */
	if (size != NULL)
		*size = ueh.ueh_len;

	/* Return data if the caller requested it. */
	if (uio != NULL) {
		/* Allow for offset into the attribute data. */
		uio->uio_offset = base_offset + sizeof(struct
		    ufs_extattr_header);

		/*
		 * Figure out maximum to transfer -- use buffer size and
		 * local data limit.
		 */
		len = MIN(uio->uio_resid, ueh.ueh_len);
		old_len = uio->uio_resid;
		uio->uio_resid = len;

		error = VOP_READ(attribute->uele_backing_vnode, uio,
		    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
		if (error)
			goto vopunlock_exit;

		uio->uio_resid = old_len - (len - uio->uio_resid);
	}

vopunlock_exit:

	if (uio != NULL)
		uio->uio_offset = 0;

	if (attribute->uele_backing_vnode != vp)
		VOP_UNLOCK(attribute->uele_backing_vnode, 0);

	return (error);
}

/*
 * Vnode operation to remove a named attribute.
 */
int
ufs_deleteextattr(struct vop_deleteextattr_args *ap)
/*
vop_deleteextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	struct mount *mp = ap->a_vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp); 
	int error;

	ufs_extattr_uepm_lock(ump);

	error = ufs_extattr_rm(ap->a_vp, ap->a_attrnamespace, ap->a_name,
	    ap->a_cred, ap->a_td);


	ufs_extattr_uepm_unlock(ump);

	return (error);
}

/*
 * Vnode operation to set a named attribute.
 */
int
ufs_setextattr(struct vop_setextattr_args *ap)
/*
vop_setextattr {
	IN struct vnode *a_vp;
	IN int a_attrnamespace;
	IN const char *a_name;
	INOUT struct uio *a_uio;
	IN struct ucred *a_cred;
	IN struct thread *a_td;
};
*/
{
	struct mount *mp = ap->a_vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp); 
	int error;

	/*
	 * XXX: No longer a supported way to delete extended attributes.
	 */
	if (ap->a_uio == NULL)
		return (EINVAL);

	ufs_extattr_uepm_lock(ump);

	error = ufs_extattr_set(ap->a_vp, ap->a_attrnamespace, ap->a_name,
	    ap->a_uio, ap->a_cred, ap->a_td);

	ufs_extattr_uepm_unlock(ump);

	return (error);
}

/*
 * Real work associated with setting a vnode's extended attributes;
 * assumes that the attribute lock has already been grabbed.
 */
static int
ufs_extattr_set(struct vnode *vp, int attrnamespace, const char *name,
    struct uio *uio, struct ucred *cred, struct thread *td)
{
	struct ufs_extattr_list_entry *attribute;
	struct ufs_extattr_header ueh;
	struct iovec local_aiov;
	struct uio local_aio;
	struct mount *mp = vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct inode *ip = VTOI(vp);
	off_t base_offset;
	int error = 0, ioflag;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);
	if (!ufs_extattr_valid_attrname(attrnamespace, name))
		return (EINVAL);

	error = extattr_check_cred(vp, attrnamespace, cred, td, VWRITE);
	if (error)
		return (error);

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		return (ENOATTR);

	/*
	 * Early rejection of invalid offsets/length.
	 * Reject: any offset but 0 (replace)
	 *	 Any size greater than attribute size limit
 	 */
	if (uio->uio_offset != 0 ||
	    uio->uio_resid > attribute->uele_fileheader.uef_size)
		return (ENXIO);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Write out a data header for the data.
	 */
	ueh.ueh_len = uio->uio_resid;
	ueh.ueh_flags = UFS_EXTATTR_ATTR_FLAG_INUSE;
	ueh.ueh_i_gen = ip->i_gen;
	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_WRITE;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_td = td;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	/*
	 * Acquire locks.
	 *
	 * Don't need to get a lock on the backing file if the setattr is
	 * being applied to the backing file, as the lock is already held.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode, LK_EXCLUSIVE | LK_RETRY);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, &local_aio, ioflag,
	    ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	if (local_aio.uio_resid != 0) {
		error = ENXIO;
		goto vopunlock_exit;
	}

	/*
	 * Write out user data.
	 */
	uio->uio_offset = base_offset + sizeof(struct ufs_extattr_header);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, uio, ioflag,
	    ump->um_extattr.uepm_ucred);

vopunlock_exit:
	uio->uio_offset = 0;

	if (attribute->uele_backing_vnode != vp)
		VOP_UNLOCK(attribute->uele_backing_vnode, 0);

	return (error);
}

/*
 * Real work associated with removing an extended attribute from a vnode.
 * Assumes the attribute lock has already been grabbed.
 */
static int
ufs_extattr_rm(struct vnode *vp, int attrnamespace, const char *name,
    struct ucred *cred, struct thread *td)
{
	struct ufs_extattr_list_entry *attribute;
	struct ufs_extattr_header ueh;
	struct iovec local_aiov;
	struct uio local_aio;
	struct mount *mp = vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp);
	struct inode *ip = VTOI(vp);
	off_t base_offset;
	int error = 0, ioflag;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)  
		return (EROFS);
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);
	if (!ufs_extattr_valid_attrname(attrnamespace, name))
		return (EINVAL);

	error = extattr_check_cred(vp, attrnamespace, cred, td, VWRITE);
	if (error)
		return (error);

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		return (ENOATTR);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Check to see if currently defined.
	 */
	bzero(&ueh, sizeof(struct ufs_extattr_header));

	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_READ;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_td = td;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	/*
	 * Don't need to get the lock on the backing vnode if the vnode we're
	 * modifying is it, as we already hold the lock.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode, LK_EXCLUSIVE | LK_RETRY);

	error = VOP_READ(attribute->uele_backing_vnode, &local_aio,
	    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	/* Defined? */
	if ((ueh.ueh_flags & UFS_EXTATTR_ATTR_FLAG_INUSE) == 0) {
		error = ENOATTR;
		goto vopunlock_exit;
	}

	/* Valid for the current inode generation? */
	if (ueh.ueh_i_gen != ip->i_gen) {
		/*
		 * The inode itself has a different generation number than
		 * the attribute data.  For now, the best solution is to
		 * coerce this to undefined, and let it get cleaned up by
		 * the next write or extattrctl clean.
		 */
		printf("ufs_extattr_rm (%s): inode number inconsistency (%d, %jd)\n",
		    mp->mnt_stat.f_mntonname, ueh.ueh_i_gen, (intmax_t)ip->i_gen);
		error = ENOATTR;
		goto vopunlock_exit;
	}

	/* Flag it as not in use. */
	ueh.ueh_flags = 0;
	ueh.ueh_len = 0;

	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_WRITE;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_td = td;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, &local_aio, ioflag,
	    ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	if (local_aio.uio_resid != 0)
		error = ENXIO;

vopunlock_exit:
	VOP_UNLOCK(attribute->uele_backing_vnode, 0);

	return (error);
}

/*
 * Called by UFS when an inode is no longer active and should have its
 * attributes stripped.
 */
void
ufs_extattr_vnode_inactive(struct vnode *vp, struct thread *td)
{
	struct ufs_extattr_list_entry *uele;
	struct mount *mp = vp->v_mount;
	struct ufsmount *ump = VFSTOUFS(mp);

	/*
	 * In that case, we cannot lock. We should not have any active vnodes
	 * on the fs if this is not yet initialized but is going to be, so
	 * this can go unlocked.
	 */
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED))
		return;

	ufs_extattr_uepm_lock(ump);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		ufs_extattr_uepm_unlock(ump);
		return;
	}

	LIST_FOREACH(uele, &ump->um_extattr.uepm_list, uele_entries)
		ufs_extattr_rm(vp, uele->uele_attrnamespace,
		    uele->uele_attrname, NULL, td);

	ufs_extattr_uepm_unlock(ump);
}

#endif /* !UFS_EXTATTR */
