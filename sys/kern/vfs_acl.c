/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2006, 2016-2017 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * Portions of this software were developed by BAE Systems, the University of
 * Cambridge Computer Laboratory, and Memorial University under DARPA/AFRL
 * contract FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent
 * Computing (TC) research program.
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
 */
/*
 * Developed by the TrustedBSD Project.
 *
 * ACL system calls and other functions common across different ACL types.
 * Type-specific routines go into subr_acl_<type>.c.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/sysent.h>
#include <sys/acl.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

CTASSERT(ACL_MAX_ENTRIES >= OLDACL_MAX_ENTRIES);

MALLOC_DEFINE(M_ACL, "acl", "Access Control Lists");


static int	kern___acl_aclcheck_path(struct thread *td, const char *path,
		    acl_type_t type, struct acl *aclp, int follow);
static int	kern___acl_delete_path(struct thread *td, const char *path,
		    acl_type_t type, int follow);
static int	kern___acl_get_path(struct thread *td, const char *path,
		    acl_type_t type, struct acl *aclp, int follow);
static int	kern___acl_set_path(struct thread *td, const char *path,
		    acl_type_t type, const struct acl *aclp, int follow);
static int	vacl_set_acl(struct thread *td, struct vnode *vp,
		    acl_type_t type, const struct acl *aclp);
static int	vacl_get_acl(struct thread *td, struct vnode *vp,
		    acl_type_t type, struct acl *aclp);
static int	vacl_aclcheck(struct thread *td, struct vnode *vp,
		    acl_type_t type, const struct acl *aclp);

int
acl_copy_oldacl_into_acl(const struct oldacl *source, struct acl *dest)
{
	int i;

	if (source->acl_cnt < 0 || source->acl_cnt > OLDACL_MAX_ENTRIES)
		return (EINVAL);
	
	bzero(dest, sizeof(*dest));

	dest->acl_cnt = source->acl_cnt;
	dest->acl_maxcnt = ACL_MAX_ENTRIES;

	for (i = 0; i < dest->acl_cnt; i++) {
		dest->acl_entry[i].ae_tag = source->acl_entry[i].ae_tag;
		dest->acl_entry[i].ae_id = source->acl_entry[i].ae_id;
		dest->acl_entry[i].ae_perm = source->acl_entry[i].ae_perm;
	}

	return (0);
}

int
acl_copy_acl_into_oldacl(const struct acl *source, struct oldacl *dest)
{
	int i;

	if (source->acl_cnt > OLDACL_MAX_ENTRIES)
		return (EINVAL);

	bzero(dest, sizeof(*dest));

	dest->acl_cnt = source->acl_cnt;

	for (i = 0; i < dest->acl_cnt; i++) {
		dest->acl_entry[i].ae_tag = source->acl_entry[i].ae_tag;
		dest->acl_entry[i].ae_id = source->acl_entry[i].ae_id;
		dest->acl_entry[i].ae_perm = source->acl_entry[i].ae_perm;
	}

	return (0);
}

/*
 * At one time, "struct ACL" was extended in order to add support for NFSv4
 * ACLs.  Instead of creating compatibility versions of all the ACL-related
 * syscalls, they were left intact.  It's possible to find out what the code
 * calling these syscalls (libc) expects basing on "type" argument - if it's
 * either ACL_TYPE_ACCESS_OLD or ACL_TYPE_DEFAULT_OLD (which previously were
 * known as ACL_TYPE_ACCESS and ACL_TYPE_DEFAULT), then it's the "struct
 * oldacl".  If it's something else, then it's the new "struct acl".  In the
 * latter case, the routines below just copyin/copyout the contents.  In the
 * former case, they copyin the "struct oldacl" and convert it to the new
 * format.
 */
static int
acl_copyin(const void *user_acl, struct acl *kernel_acl, acl_type_t type)
{
	int error;
	struct oldacl old;

	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
	case ACL_TYPE_DEFAULT_OLD:
		error = copyin(user_acl, &old, sizeof(old));
		if (error != 0)
			break;
		acl_copy_oldacl_into_acl(&old, kernel_acl);
		break;

	default:
		error = copyin(user_acl, kernel_acl, sizeof(*kernel_acl));
		if (kernel_acl->acl_maxcnt != ACL_MAX_ENTRIES)
			return (EINVAL);
	}

	return (error);
}

static int
acl_copyout(const struct acl *kernel_acl, void *user_acl, acl_type_t type)
{
	uint32_t am;
	int error;
	struct oldacl old;

	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
	case ACL_TYPE_DEFAULT_OLD:
		error = acl_copy_acl_into_oldacl(kernel_acl, &old);
		if (error != 0)
			break;

		error = copyout(&old, user_acl, sizeof(old));
		break;

	default:
		error = fueword32((char *)user_acl +
		    offsetof(struct acl, acl_maxcnt), &am);
		if (error == -1)
			return (EFAULT);
		if (am != ACL_MAX_ENTRIES)
			return (EINVAL);

		error = copyout(kernel_acl, user_acl, sizeof(*kernel_acl));
	}

	return (error);
}

/*
 * Convert "old" type - ACL_TYPE_{ACCESS,DEFAULT}_OLD - into its "new"
 * counterpart.  It's required for old (pre-NFSv4 ACLs) libc to work
 * with new kernel.  Fixing 'type' for old binaries with new libc
 * is being done in lib/libc/posix1e/acl_support.c:_acl_type_unold().
 */
static int
acl_type_unold(int type)
{
	switch (type) {
	case ACL_TYPE_ACCESS_OLD:
		return (ACL_TYPE_ACCESS);

	case ACL_TYPE_DEFAULT_OLD:
		return (ACL_TYPE_DEFAULT);

	default:
		return (type);
	}
}

/*
 * These calls wrap the real vnode operations, and are called by the syscall
 * code once the syscall has converted the path or file descriptor to a vnode
 * (unlocked).  The aclp pointer is assumed still to point to userland, so
 * this should not be consumed within the kernel except by syscall code.
 * Other code should directly invoke VOP_{SET,GET}ACL.
 */

/*
 * Given a vnode, set its ACL.
 */
static int
vacl_set_acl(struct thread *td, struct vnode *vp, acl_type_t type,
    const struct acl *aclp)
{
	struct acl *inkernelacl;
	struct mount *mp;
	int error;

	AUDIT_ARG_VALUE(type);
	inkernelacl = acl_alloc(M_WAITOK);
	error = acl_copyin(aclp, inkernelacl, type);
	if (error != 0)
		goto out;
	error = vn_start_write(vp, &mp, V_WAIT | PCATCH);
	if (error != 0)
		goto out;
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
#ifdef MAC
	error = mac_vnode_check_setacl(td->td_ucred, vp, type, inkernelacl);
	if (error != 0)
		goto out_unlock;
#endif
	error = VOP_SETACL(vp, acl_type_unold(type), inkernelacl,
	    td->td_ucred, td);
#ifdef MAC
out_unlock:
#endif
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
out:
	acl_free(inkernelacl);
	return (error);
}

/*
 * Given a vnode, get its ACL.
 */
static int
vacl_get_acl(struct thread *td, struct vnode *vp, acl_type_t type,
    struct acl *aclp)
{
	struct acl *inkernelacl;
	int error;

	AUDIT_ARG_VALUE(type);
	inkernelacl = acl_alloc(M_WAITOK | M_ZERO);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
#ifdef MAC
	error = mac_vnode_check_getacl(td->td_ucred, vp, type);
	if (error != 0)
		goto out;
#endif
	error = VOP_GETACL(vp, acl_type_unold(type), inkernelacl,
	    td->td_ucred, td);

#ifdef MAC
out:
#endif
	VOP_UNLOCK(vp, 0);
	if (error == 0)
		error = acl_copyout(inkernelacl, aclp, type);
	acl_free(inkernelacl);
	return (error);
}

/*
 * Given a vnode, delete its ACL.
 */
static int
vacl_delete(struct thread *td, struct vnode *vp, acl_type_t type)
{
	struct mount *mp;
	int error;

	AUDIT_ARG_VALUE(type);
	error = vn_start_write(vp, &mp, V_WAIT | PCATCH);
	if (error != 0)
		return (error);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	AUDIT_ARG_VNODE1(vp);
#ifdef MAC
	error = mac_vnode_check_deleteacl(td->td_ucred, vp, type);
	if (error != 0)
		goto out;
#endif
	error = VOP_SETACL(vp, acl_type_unold(type), 0, td->td_ucred, td);
#ifdef MAC
out:
#endif
	VOP_UNLOCK(vp, 0);
	vn_finished_write(mp);
	return (error);
}

/*
 * Given a vnode, check whether an ACL is appropriate for it
 *
 * XXXRW: No vnode lock held so can't audit vnode state...?
 */
static int
vacl_aclcheck(struct thread *td, struct vnode *vp, acl_type_t type,
    const struct acl *aclp)
{
	struct acl *inkernelacl;
	int error;

	inkernelacl = acl_alloc(M_WAITOK);
	error = acl_copyin(aclp, inkernelacl, type);
	if (error != 0)
		goto out;
	error = VOP_ACLCHECK(vp, acl_type_unold(type), inkernelacl,
	    td->td_ucred, td);
out:
	acl_free(inkernelacl);
	return (error);
}

/*
 * syscalls -- convert the path/fd to a vnode, and call vacl_whatever.  Don't
 * need to lock, as the vacl_ code will get/release any locks required.
 */

/*
 * Given a file path, get an ACL for it
 */
int
sys___acl_get_file(struct thread *td, struct __acl_get_file_args *uap)
{

	return (kern___acl_get_path(td, uap->path, uap->type, uap->aclp,
	    FOLLOW));
}

/*
 * Given a file path, get an ACL for it; don't follow links.
 */
int
sys___acl_get_link(struct thread *td, struct __acl_get_link_args *uap)
{

	return(kern___acl_get_path(td, uap->path, uap->type, uap->aclp,
	    NOFOLLOW));
}

static int
kern___acl_get_path(struct thread *td, const char *path, acl_type_t type,
    struct acl *aclp, int follow)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, follow | AUDITVNODE1, UIO_USERSPACE, path, td);
	error = namei(&nd);
	if (error == 0) {
		error = vacl_get_acl(td, nd.ni_vp, type, aclp);
		NDFREE(&nd, 0);
	}
	return (error);
}

/*
 * Given a file path, set an ACL for it.
 */
int
sys___acl_set_file(struct thread *td, struct __acl_set_file_args *uap)
{

	return(kern___acl_set_path(td, uap->path, uap->type, uap->aclp,
	    FOLLOW));
}

/*
 * Given a file path, set an ACL for it; don't follow links.
 */
int
sys___acl_set_link(struct thread *td, struct __acl_set_link_args *uap)
{

	return(kern___acl_set_path(td, uap->path, uap->type, uap->aclp,
	    NOFOLLOW));
}

static int
kern___acl_set_path(struct thread *td, const char *path,
    acl_type_t type, const struct acl *aclp, int follow)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, follow | AUDITVNODE1, UIO_USERSPACE, path, td);
	error = namei(&nd);
	if (error == 0) {
		error = vacl_set_acl(td, nd.ni_vp, type, aclp);
		NDFREE(&nd, 0);
	}
	return (error);
}

/*
 * Given a file descriptor, get an ACL for it.
 */
int
sys___acl_get_fd(struct thread *td, struct __acl_get_fd_args *uap)
{
	struct file *fp;
	cap_rights_t rights;
	int error;

	AUDIT_ARG_FD(uap->filedes);
	error = getvnode(td, uap->filedes,
	    cap_rights_init(&rights, CAP_ACL_GET), &fp);
	if (error == 0) {
		error = vacl_get_acl(td, fp->f_vnode, uap->type, uap->aclp);
		fdrop(fp, td);
	}
	return (error);
}

/*
 * Given a file descriptor, set an ACL for it.
 */
int
sys___acl_set_fd(struct thread *td, struct __acl_set_fd_args *uap)
{
	struct file *fp;
	cap_rights_t rights;
	int error;

	AUDIT_ARG_FD(uap->filedes);
	error = getvnode(td, uap->filedes,
	    cap_rights_init(&rights, CAP_ACL_SET), &fp);
	if (error == 0) {
		error = vacl_set_acl(td, fp->f_vnode, uap->type, uap->aclp);
		fdrop(fp, td);
	}
	return (error);
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_file(struct thread *td, struct __acl_delete_file_args *uap)
{

	return (kern___acl_delete_path(td, uap->path, uap->type, FOLLOW));
}

/*
 * Given a file path, delete an ACL from it; don't follow links.
 */
int
sys___acl_delete_link(struct thread *td, struct __acl_delete_link_args *uap)
{

	return (kern___acl_delete_path(td, uap->path, uap->type, NOFOLLOW));
}

static int
kern___acl_delete_path(struct thread *td, const char *path,
    acl_type_t type, int follow)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, follow, UIO_USERSPACE, path, td);
	error = namei(&nd);
	if (error == 0) {
		error = vacl_delete(td, nd.ni_vp, type);
		NDFREE(&nd, 0);
	}
	return (error);
}

/*
 * Given a file path, delete an ACL from it.
 */
int
sys___acl_delete_fd(struct thread *td, struct __acl_delete_fd_args *uap)
{
	struct file *fp;
	cap_rights_t rights;
	int error;

	AUDIT_ARG_FD(uap->filedes);
	error = getvnode(td, uap->filedes,
	    cap_rights_init(&rights, CAP_ACL_DELETE), &fp);
	if (error == 0) {
		error = vacl_delete(td, fp->f_vnode, uap->type);
		fdrop(fp, td);
	}
	return (error);
}

/*
 * Given a file path, check an ACL for it.
 */
int
sys___acl_aclcheck_file(struct thread *td, struct __acl_aclcheck_file_args *uap)
{

	return (kern___acl_aclcheck_path(td, uap->path, uap->type, uap->aclp,
	    FOLLOW));
}

/*
 * Given a file path, check an ACL for it; don't follow links.
 */
int
sys___acl_aclcheck_link(struct thread *td, struct __acl_aclcheck_link_args *uap)
{
	return (kern___acl_aclcheck_path(td, uap->path, uap->type, uap->aclp,
	    NOFOLLOW));
}

static int
kern___acl_aclcheck_path(struct thread *td, const char *path, acl_type_t type,
    struct acl *aclp, int follow)
{
	struct nameidata nd;
	int error;

	NDINIT(&nd, LOOKUP, follow, UIO_USERSPACE, path, td);
	error = namei(&nd);
	if (error == 0) {
		error = vacl_aclcheck(td, nd.ni_vp, type, aclp);
		NDFREE(&nd, 0);
	}
	return (error);
}

/*
 * Given a file descriptor, check an ACL for it.
 */
int
sys___acl_aclcheck_fd(struct thread *td, struct __acl_aclcheck_fd_args *uap)
{
	struct file *fp;
	cap_rights_t rights;
	int error;

	AUDIT_ARG_FD(uap->filedes);
	error = getvnode(td, uap->filedes,
	    cap_rights_init(&rights, CAP_ACL_CHECK), &fp);
	if (error == 0) {
		error = vacl_aclcheck(td, fp->f_vnode, uap->type, uap->aclp);
		fdrop(fp, td);
	}
	return (error);
}

struct acl *
acl_alloc(int flags)
{
	struct acl *aclp;

	aclp = malloc(sizeof(*aclp), M_ACL, flags);
	if (aclp == NULL)
		return (NULL);

	aclp->acl_maxcnt = ACL_MAX_ENTRIES;

	return (aclp);
}

void
acl_free(struct acl *aclp)
{

	free(aclp, M_ACL);
}
