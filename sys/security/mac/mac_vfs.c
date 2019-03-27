/*-
 * Copyright (c) 1999-2002, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2005 McAfee, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc. 
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/extattr.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/sdt.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <fs/devfs/devfs.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

/*
 * Warn about EA transactions only the first time they happen.  No locking on
 * this variable.
 */
static int	ea_warn_once = 0;

static int	mac_vnode_setlabel_extattr(struct ucred *cred,
		    struct vnode *vp, struct label *intlabel);

static struct label *
mac_devfs_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(devfs_init_label, label);
	return (label);
}

void
mac_devfs_init(struct devfs_dirent *de)
{

	if (mac_labeled & MPC_OBJECT_DEVFS)
		de->de_label = mac_devfs_label_alloc();
	else
		de->de_label = NULL;
}

static struct label *
mac_mount_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(mount_init_label, label);
	return (label);
}

void
mac_mount_init(struct mount *mp)
{

	if (mac_labeled & MPC_OBJECT_MOUNT)
		mp->mnt_label = mac_mount_label_alloc();
	else
		mp->mnt_label = NULL;
}

struct label *
mac_vnode_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(vnode_init_label, label);
	return (label);
}

void
mac_vnode_init(struct vnode *vp)
{

	if (mac_labeled & MPC_OBJECT_VNODE)
		vp->v_label = mac_vnode_label_alloc();
	else
		vp->v_label = NULL;
}

static void
mac_devfs_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_devfs_destroy(struct devfs_dirent *de)
{

	if (de->de_label != NULL) {
		mac_devfs_label_free(de->de_label);
		de->de_label = NULL;
	}
}

static void
mac_mount_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(mount_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_mount_destroy(struct mount *mp)
{

	if (mp->mnt_label != NULL) {
		mac_mount_label_free(mp->mnt_label);
		mp->mnt_label = NULL;
	}
}

void
mac_vnode_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(vnode_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_vnode_destroy(struct vnode *vp)
{

	if (vp->v_label != NULL) {
		mac_vnode_label_free(vp->v_label);
		vp->v_label = NULL;
	}
}

void
mac_vnode_copy_label(struct label *src, struct label *dest)
{

	MAC_POLICY_PERFORM_NOSLEEP(vnode_copy_label, src, dest);
}

int
mac_vnode_externalize_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen)
{
	int error;

	MAC_POLICY_EXTERNALIZE(vnode, label, elements, outbuf, outbuflen);

	return (error);
}

int
mac_vnode_internalize_label(struct label *label, char *string)
{
	int error;

	MAC_POLICY_INTERNALIZE(vnode, label, string);

	return (error);
}

void
mac_devfs_update(struct mount *mp, struct devfs_dirent *de, struct vnode *vp)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_update, mp, de, de->de_label, vp,
	    vp->v_label);
}

void
mac_devfs_vnode_associate(struct mount *mp, struct devfs_dirent *de,
    struct vnode *vp)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_vnode_associate, mp, mp->mnt_label,
	    de, de->de_label, vp, vp->v_label);
}

int
mac_vnode_associate_extattr(struct mount *mp, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_associate_extattr");

	MAC_POLICY_CHECK(vnode_associate_extattr, mp, mp->mnt_label, vp,
	    vp->v_label);

	return (error);
}

void
mac_vnode_associate_singlelabel(struct mount *mp, struct vnode *vp)
{

	MAC_POLICY_PERFORM_NOSLEEP(vnode_associate_singlelabel, mp,
	    mp->mnt_label, vp, vp->v_label);
}

/*
 * Functions implementing extended-attribute backed labels for file systems
 * that support it.
 *
 * Where possible, we use EA transactions to make writes to multiple
 * attributes across difference policies mutually atomic.  We allow work to
 * continue on file systems not supporting EA transactions, but generate a
 * printf warning.
 */
int
mac_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct vnode *dvp, struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_create_extattr");
	ASSERT_VOP_LOCKED(vp, "mac_vnode_create_extattr");

	error = VOP_OPENEXTATTR(vp, cred, curthread);
	if (error == EOPNOTSUPP) {
		if (ea_warn_once == 0) {
			printf("Warning: transactions not supported "
			    "in EA write.\n");
			ea_warn_once = 1;
		}
	} else if (error)
		return (error);

	MAC_POLICY_CHECK(vnode_create_extattr, cred, mp, mp->mnt_label, dvp,
	    dvp->v_label, vp, vp->v_label, cnp);

	if (error) {
		VOP_CLOSEEXTATTR(vp, 0, NOCRED, curthread);
		return (error);
	}

	error = VOP_CLOSEEXTATTR(vp, 1, NOCRED, curthread);
	if (error == EOPNOTSUPP)
		error = 0;

	return (error);
}

static int
mac_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *intlabel)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_setlabel_extattr");

	error = VOP_OPENEXTATTR(vp, cred, curthread);
	if (error == EOPNOTSUPP) {
		if (ea_warn_once == 0) {
			printf("Warning: transactions not supported "
			    "in EA write.\n");
			ea_warn_once = 1;
		}
	} else if (error)
		return (error);

	MAC_POLICY_CHECK(vnode_setlabel_extattr, cred, vp, vp->v_label,
	    intlabel);

	if (error) {
		VOP_CLOSEEXTATTR(vp, 0, NOCRED, curthread);
		return (error);
	}

	error = VOP_CLOSEEXTATTR(vp, 1, NOCRED, curthread);
	if (error == EOPNOTSUPP)
		error = 0;

	return (error);
}

void
mac_vnode_execve_transition(struct ucred *old, struct ucred *new,
    struct vnode *vp, struct label *interpvplabel, struct image_params *imgp)
{

	ASSERT_VOP_LOCKED(vp, "mac_vnode_execve_transition");

	MAC_POLICY_PERFORM(vnode_execve_transition, old, new, vp,
	    vp->v_label, interpvplabel, imgp, imgp->execlabel);
}

int
mac_vnode_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *interpvplabel, struct image_params *imgp)
{
	int result;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_execve_will_transition");

	result = 0;
	/* No sleeping since the process lock will be held by the caller. */
	MAC_POLICY_BOOLEAN_NOSLEEP(vnode_execve_will_transition, ||, old, vp,
	    vp->v_label, interpvplabel, imgp, imgp->execlabel);

	return (result);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_access, "struct ucred *",
    "struct vnode *", "accmode_t");

int
mac_vnode_check_access(struct ucred *cred, struct vnode *vp, accmode_t accmode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_access");

	MAC_POLICY_CHECK(vnode_check_access, cred, vp, vp->v_label, accmode);
	MAC_CHECK_PROBE3(vnode_check_access, error, cred, vp, accmode);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(vnode_check_chdir, "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_chdir(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_chdir");

	MAC_POLICY_CHECK(vnode_check_chdir, cred, dvp, dvp->v_label);
	MAC_CHECK_PROBE2(vnode_check_chdir, error, cred, dvp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(vnode_check_chroot, "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_chroot(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_chroot");

	MAC_POLICY_CHECK(vnode_check_chroot, cred, dvp, dvp->v_label);
	MAC_CHECK_PROBE2(vnode_check_chroot, error, cred, dvp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_create, "struct ucred *",
    "struct vnode *", "struct componentname *", "struct vattr *");

int
mac_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct componentname *cnp, struct vattr *vap)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_create");

	MAC_POLICY_CHECK(vnode_check_create, cred, dvp, dvp->v_label, cnp,
	    vap);
	MAC_CHECK_PROBE4(vnode_check_create, error, cred, dvp, cnp, vap);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_deleteacl, "struct ucred *",
    "struct vnode *", "acl_type_t");

int
mac_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    acl_type_t type)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_deleteacl");

	MAC_POLICY_CHECK(vnode_check_deleteacl, cred, vp, vp->v_label, type);
	MAC_CHECK_PROBE3(vnode_check_deleteacl, error, cred, vp, type);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_deleteextattr, "struct ucred *",
    "struct vnode *", "int", "const char *");

int
mac_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_deleteextattr");

	MAC_POLICY_CHECK(vnode_check_deleteextattr, cred, vp, vp->v_label,
	    attrnamespace, name);
	MAC_CHECK_PROBE4(vnode_check_deleteextattr, error, cred, vp,
	    attrnamespace, name);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_exec, "struct ucred *", "struct vnode *",
    "struct image_params *");

int
mac_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct image_params *imgp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_exec");

	MAC_POLICY_CHECK(vnode_check_exec, cred, vp, vp->v_label, imgp,
	    imgp->execlabel);
	MAC_CHECK_PROBE3(vnode_check_exec, error, cred, vp, imgp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_getacl, "struct ucred *",
    "struct vnode *", "acl_type_t");

int
mac_vnode_check_getacl(struct ucred *cred, struct vnode *vp, acl_type_t type)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_getacl");

	MAC_POLICY_CHECK(vnode_check_getacl, cred, vp, vp->v_label, type);
	MAC_CHECK_PROBE3(vnode_check_getacl, error, cred, vp, type);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_getextattr, "struct ucred *",
    "struct vnode *", "int", "const char *");

int
mac_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_getextattr");

	MAC_POLICY_CHECK(vnode_check_getextattr, cred, vp, vp->v_label,
	    attrnamespace, name);
	MAC_CHECK_PROBE4(vnode_check_getextattr, error, cred, vp,
	    attrnamespace, name);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_link, "struct ucred *", "struct vnode *",
    "struct vnode *", "struct componentname *");

int
mac_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_link");
	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_link");

	MAC_POLICY_CHECK(vnode_check_link, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	MAC_CHECK_PROBE4(vnode_check_link, error, cred, dvp, vp, cnp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_listextattr, "struct ucred *",
    "struct vnode *", "int");

int
mac_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_listextattr");

	MAC_POLICY_CHECK(vnode_check_listextattr, cred, vp, vp->v_label,
	    attrnamespace);
	MAC_CHECK_PROBE3(vnode_check_listextattr, error, cred, vp,
	    attrnamespace);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_lookup, "struct ucred *",
    "struct vnode *", "struct componentname *");

int
mac_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_lookup");

	MAC_POLICY_CHECK(vnode_check_lookup, cred, dvp, dvp->v_label, cnp);
	MAC_CHECK_PROBE3(vnode_check_lookup, error, cred, dvp, cnp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_mmap, "struct ucred *", "struct vnode *",
    "int", "int");

int
mac_vnode_check_mmap(struct ucred *cred, struct vnode *vp, int prot,
    int flags)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_mmap");

	MAC_POLICY_CHECK(vnode_check_mmap, cred, vp, vp->v_label, prot, flags);
	MAC_CHECK_PROBE4(vnode_check_mmap, error, cred, vp, prot, flags);

	return (error);
}

void
mac_vnode_check_mmap_downgrade(struct ucred *cred, struct vnode *vp,
    int *prot)
{
	int result = *prot;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_mmap_downgrade");

	MAC_POLICY_PERFORM(vnode_check_mmap_downgrade, cred, vp, vp->v_label,
	    &result);

	*prot = result;
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_mprotect, "struct ucred *",
    "struct vnode *", "int");

int
mac_vnode_check_mprotect(struct ucred *cred, struct vnode *vp, int prot)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_mprotect");

	MAC_POLICY_CHECK(vnode_check_mprotect, cred, vp, vp->v_label, prot);
	MAC_CHECK_PROBE3(vnode_check_mprotect, error, cred, vp, prot);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_open, "struct ucred *", "struct vnode *",
    "accmode_t");

int
mac_vnode_check_open(struct ucred *cred, struct vnode *vp, accmode_t accmode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_open");

	MAC_POLICY_CHECK(vnode_check_open, cred, vp, vp->v_label, accmode);
	MAC_CHECK_PROBE3(vnode_check_open, error, cred, vp, accmode);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_poll, "struct ucred *", "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_poll");

	MAC_POLICY_CHECK(vnode_check_poll, active_cred, file_cred, vp,
	    vp->v_label);
	MAC_CHECK_PROBE3(vnode_check_poll, error, active_cred, file_cred,
	    vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_read, "struct ucred *", "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_read");

	MAC_POLICY_CHECK(vnode_check_read, active_cred, file_cred, vp,
	    vp->v_label);
	MAC_CHECK_PROBE3(vnode_check_read, error, active_cred, file_cred,
	    vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(vnode_check_readdir, "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_readdir(struct ucred *cred, struct vnode *dvp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_readdir");

	MAC_POLICY_CHECK(vnode_check_readdir, cred, dvp, dvp->v_label);
	MAC_CHECK_PROBE2(vnode_check_readdir, error, cred, dvp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(vnode_check_readlink, "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_readlink(struct ucred *cred, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_readlink");

	MAC_POLICY_CHECK(vnode_check_readlink, cred, vp, vp->v_label);
	MAC_CHECK_PROBE2(vnode_check_readlink, error, cred, vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_relabel, "struct ucred *",
    "struct vnode *", "struct label *");

static int
mac_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *newlabel)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_relabel");

	MAC_POLICY_CHECK(vnode_check_relabel, cred, vp, vp->v_label, newlabel);
	MAC_CHECK_PROBE3(vnode_check_relabel, error, cred, vp, newlabel);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_rename_from, "struct ucred *",
    "struct vnode *", "struct vnode *", "struct componentname *");

int
mac_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_rename_from");
	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_rename_from");

	MAC_POLICY_CHECK(vnode_check_rename_from, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	MAC_CHECK_PROBE4(vnode_check_rename_from, error, cred, dvp, vp, cnp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_rename_to, "struct ucred *",
    "struct vnode *", "struct vnode *", "struct componentname *");

int
mac_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, int samedir, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_rename_to");
	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_rename_to");

	MAC_POLICY_CHECK(vnode_check_rename_to, cred, dvp, dvp->v_label, vp,
	    vp != NULL ? vp->v_label : NULL, samedir, cnp);
	MAC_CHECK_PROBE4(vnode_check_rename_to, error, cred, dvp, vp, cnp);
	return (error);
}

MAC_CHECK_PROBE_DEFINE2(vnode_check_revoke, "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_revoke(struct ucred *cred, struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_revoke");

	MAC_POLICY_CHECK(vnode_check_revoke, cred, vp, vp->v_label);
	MAC_CHECK_PROBE2(vnode_check_revoke, error, cred, vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_setacl, "struct ucred *",
    "struct vnode *", "acl_type_t", "struct acl *");

int
mac_vnode_check_setacl(struct ucred *cred, struct vnode *vp, acl_type_t type,
    struct acl *acl)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setacl");

	MAC_POLICY_CHECK(vnode_check_setacl, cred, vp, vp->v_label, type, acl);
	MAC_CHECK_PROBE4(vnode_check_setacl, error, cred, vp, type, acl);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_setextattr, "struct ucred *",
    "struct vnode *", "int", "const char *");

int
mac_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    int attrnamespace, const char *name)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setextattr");

	MAC_POLICY_CHECK(vnode_check_setextattr, cred, vp, vp->v_label,
	    attrnamespace, name);
	MAC_CHECK_PROBE4(vnode_check_setextattr, error, cred, vp,
	    attrnamespace, name);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_setflags, "struct ucred *",
    "struct vnode *", "u_long");

int
mac_vnode_check_setflags(struct ucred *cred, struct vnode *vp, u_long flags)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setflags");

	MAC_POLICY_CHECK(vnode_check_setflags, cred, vp, vp->v_label, flags);
	MAC_CHECK_PROBE3(vnode_check_setflags, error, cred, vp, flags);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_setmode, "struct ucred *",
    "struct vnode *", "mode_t");

int
mac_vnode_check_setmode(struct ucred *cred, struct vnode *vp, mode_t mode)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setmode");

	MAC_POLICY_CHECK(vnode_check_setmode, cred, vp, vp->v_label, mode);
	MAC_CHECK_PROBE3(vnode_check_setmode, error, cred, vp, mode);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_setowner, "struct ucred *",
    "struct vnode *", "uid_t", "gid_t");

int
mac_vnode_check_setowner(struct ucred *cred, struct vnode *vp, uid_t uid,
    gid_t gid)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setowner");

	MAC_POLICY_CHECK(vnode_check_setowner, cred, vp, vp->v_label, uid, gid);
	MAC_CHECK_PROBE4(vnode_check_setowner, error, cred, vp, uid, gid);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_setutimes, "struct ucred *",
    "struct vnode *", "struct timespec *", "struct timespec *");

int
mac_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct timespec atime, struct timespec mtime)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_setutimes");

	MAC_POLICY_CHECK(vnode_check_setutimes, cred, vp, vp->v_label, atime,
	    mtime);
	MAC_CHECK_PROBE4(vnode_check_setutimes, error, cred, vp, &atime,
	    &mtime);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_stat, "struct ucred *", "struct ucred *",
    "struct vnode *");

int
mac_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_stat");

	MAC_POLICY_CHECK(vnode_check_stat, active_cred, file_cred, vp,
	    vp->v_label);
	MAC_CHECK_PROBE3(vnode_check_stat, error, active_cred, file_cred,
	    vp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(vnode_check_unlink, "struct ucred *",
    "struct vnode *", "struct vnode *", "struct componentname *");

int
mac_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct vnode *vp, struct componentname *cnp)
{
	int error;

	ASSERT_VOP_LOCKED(dvp, "mac_vnode_check_unlink");
	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_unlink");

	MAC_POLICY_CHECK(vnode_check_unlink, cred, dvp, dvp->v_label, vp,
	    vp->v_label, cnp);
	MAC_CHECK_PROBE4(vnode_check_unlink, error, cred, dvp, vp, cnp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(vnode_check_write, "struct ucred *",
    "struct ucred *", "struct vnode *");

int
mac_vnode_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp)
{
	int error;

	ASSERT_VOP_LOCKED(vp, "mac_vnode_check_write");

	MAC_POLICY_CHECK(vnode_check_write, active_cred, file_cred, vp,
	    vp->v_label);
	MAC_CHECK_PROBE3(vnode_check_write, error, active_cred, file_cred,
	    vp);

	return (error);
}

void
mac_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *newlabel)
{

	MAC_POLICY_PERFORM(vnode_relabel, cred, vp, vp->v_label, newlabel);
}

void
mac_mount_create(struct ucred *cred, struct mount *mp)
{

	MAC_POLICY_PERFORM(mount_create, cred, mp, mp->mnt_label);
}

MAC_CHECK_PROBE_DEFINE2(mount_check_stat, "struct ucred *",
    "struct mount *");

int
mac_mount_check_stat(struct ucred *cred, struct mount *mount)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(mount_check_stat, cred, mount, mount->mnt_label);
	MAC_CHECK_PROBE2(mount_check_stat, error, cred, mount);

	return (error);
}

void
mac_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_create_device, cred, mp, dev, de,
	    de->de_label);
}

void
mac_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct devfs_dirent *de)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_create_symlink, cred, mp, dd,
	    dd->de_label, de, de->de_label);
}

void
mac_devfs_create_directory(struct mount *mp, char *dirname, int dirnamelen,
    struct devfs_dirent *de)
{

	MAC_POLICY_PERFORM_NOSLEEP(devfs_create_directory, mp, dirname,
	    dirnamelen, de, de->de_label);
}

/*
 * Implementation of VOP_SETLABEL() that relies on extended attributes to
 * store label data.  Can be referenced by filesystems supporting extended
 * attributes.
 */
int
vop_stdsetlabel_ea(struct vop_setlabel_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct label *intlabel = ap->a_label;
	int error;

	ASSERT_VOP_LOCKED(vp, "vop_stdsetlabel_ea");

	if ((vp->v_mount->mnt_flag & MNT_MULTILABEL) == 0)
		return (EOPNOTSUPP);

	error = mac_vnode_setlabel_extattr(ap->a_cred, vp, intlabel);
	if (error)
		return (error);

	mac_vnode_relabel(ap->a_cred, vp, intlabel);

	return (0);
}

int
vn_setlabel(struct vnode *vp, struct label *intlabel, struct ucred *cred)
{
	int error;

	if (vp->v_mount == NULL) {
		/* printf("vn_setlabel: null v_mount\n"); */
		if (vp->v_type != VNON)
			printf("vn_setlabel: null v_mount with non-VNON\n");
		return (EBADF);
	}

	if ((vp->v_mount->mnt_flag & MNT_MULTILABEL) == 0)
		return (EOPNOTSUPP);

	/*
	 * Multi-phase commit.  First check the policies to confirm the
	 * change is OK.  Then commit via the filesystem.  Finally, update
	 * the actual vnode label.
	 *
	 * Question: maybe the filesystem should update the vnode at the end
	 * as part of VOP_SETLABEL()?
	 */
	error = mac_vnode_check_relabel(cred, vp, intlabel);
	if (error)
		return (error);

	/*
	 * VADMIN provides the opportunity for the filesystem to make
	 * decisions about who is and is not able to modify labels and
	 * protections on files.  This might not be right.  We can't assume
	 * VOP_SETLABEL() will do it, because we might implement that as part
	 * of vop_stdsetlabel_ea().
	 */
	error = VOP_ACCESS(vp, VADMIN, cred, curthread);
	if (error)
		return (error);

	error = VOP_SETLABEL(vp, intlabel, cred, curthread);
	if (error)
		return (error);

	return (0);
}
