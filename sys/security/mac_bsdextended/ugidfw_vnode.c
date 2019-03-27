/*-
 * Copyright (c) 1999-2002, 2007-2008 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005 Tom Rhodes
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 * It was later enhanced by Tom Rhodes for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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
#include <sys/acl.h>
#include <sys/kernel.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/stat.h>

#include <security/mac/mac_policy.h>
#include <security/mac_bsdextended/mac_bsdextended.h>
#include <security/mac_bsdextended/ugidfw_internal.h>

int
ugidfw_vnode_check_access(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	return (ugidfw_check_vp(cred, vp, ugidfw_accmode2mbi(accmode)));
}

int
ugidfw_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	return (ugidfw_check_vp(cred, dvp, MBI_EXEC));
}

int
ugidfw_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	return (ugidfw_check_vp(cred, dvp, MBI_EXEC));
}

int
ugidfw_check_create_vnode(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{

	return (ugidfw_check_vp(cred, dvp, MBI_WRITE));
}

int
ugidfw_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (ugidfw_check_vp(cred, vp, MBI_WRITE));
}

int
ugidfw_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{

	return (ugidfw_check_vp(cred, vp, MBI_READ|MBI_EXEC));
}

int
ugidfw_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	return (ugidfw_check_vp(cred, vp, MBI_STAT));
}

int
ugidfw_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (ugidfw_check_vp(cred, vp, MBI_READ));
}

int
ugidfw_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *label,
    struct componentname *cnp)
{
	int error;

	error = ugidfw_check_vp(cred, dvp, MBI_WRITE);
	if (error)
		return (error);
	error = ugidfw_check_vp(cred, vp, MBI_WRITE);
	if (error)
		return (error);
	return (0);
}

int
ugidfw_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{

	return (ugidfw_check_vp(cred, vp, MBI_READ));
}

int
ugidfw_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{

	return (ugidfw_check_vp(cred, dvp, MBI_EXEC));
}

int
ugidfw_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	return (ugidfw_check_vp(cred, vp, ugidfw_accmode2mbi(accmode)));
}

int
ugidfw_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	return (ugidfw_check_vp(cred, dvp, MBI_READ));
}

int
ugidfw_vnode_check_readdlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (ugidfw_check_vp(cred, vp, MBI_READ));
}

int
ugidfw_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	int error;

	error = ugidfw_check_vp(cred, dvp, MBI_WRITE);
	if (error)
		return (error);
	return (ugidfw_check_vp(cred, vp, MBI_WRITE));
}

int
ugidfw_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{
	int error;

	error = ugidfw_check_vp(cred, dvp, MBI_WRITE);
	if (error)
		return (error);
	if (vp != NULL)
		error = ugidfw_check_vp(cred, vp, MBI_WRITE);
	return (error);
}

int
ugidfw_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_check_setacl_vnode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (ugidfw_check_vp(cred, vp, MBI_WRITE));
}

int
ugidfw_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec utime)
{

	return (ugidfw_check_vp(cred, vp, MBI_ADMIN));
}

int
ugidfw_vnode_check_stat(struct ucred *active_cred,
    struct ucred *file_cred, struct vnode *vp, struct label *vplabel)
{

	return (ugidfw_check_vp(active_cred, vp, MBI_STAT));
}

int
ugidfw_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	int error;

	error = ugidfw_check_vp(cred, dvp, MBI_WRITE);
	if (error)
		return (error);
	return (ugidfw_check_vp(cred, vp, MBI_WRITE));
}
