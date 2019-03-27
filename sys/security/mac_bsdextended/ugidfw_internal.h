/*-
 * Copyright (c) 2008 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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

#ifndef _SYS_SECURITY_MAC_BSDEXTENDED_UGIDFW_INTERNAL_H
#define	_SYS_SECURITY_MAC_BSDEXTENDED_UGIDFW_INTERNAL_H

/*
 * Central access control routines used by object-specific checks.
 */
int	ugidfw_accmode2mbi(accmode_t accmode);
int	ugidfw_check(struct ucred *cred, struct vnode *vp, struct vattr *vap,
	    int acc_mode);
int	ugidfw_check_vp(struct ucred *cred, struct vnode *vp, int acc_mode);

/*
 * System access control checks.
 */
int	ugidfw_system_check_acct(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel);
int	ugidfw_system_check_auditctl(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel);
int	ugidfw_system_check_swapon(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel);

/*
 * Vnode access control checks.
 */
int	ugidfw_vnode_check_access(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, accmode_t accmode);
int	ugidfw_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel);
int	ugidfw_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel);
int	ugidfw_check_create_vnode(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct componentname *cnp,
	    struct vattr *vap);
int	ugidfw_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, acl_type_t type);
int	ugidfw_vnode_check_deleteextattr(struct ucred *cred,
	    struct vnode *vp, struct label *vplabel, int attrnamespace,
	    const char *name);
int	ugidfw_vnode_check_exec(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, struct image_params *imgp,
	    struct label *execlabel);
int	ugidfw_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, acl_type_t type);
int	ugidfw_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, int attrnamespace, const char *name);
int	ugidfw_vnode_check_link(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct vnode *vp, struct label *label,
	    struct componentname *cnp);
int	ugidfw_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, int attrnamespace);
int	ugidfw_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct componentname *cnp);
int	ugidfw_vnode_check_open(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, accmode_t accmode);
int	ugidfw_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel);
int	ugidfw_vnode_check_readdlink(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel);
int	ugidfw_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
	    struct componentname *cnp);
int	ugidfw_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
	    int samedir, struct componentname *cnp);
int	ugidfw_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel);
int	ugidfw_check_setacl_vnode(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, acl_type_t type, struct acl *acl);
int	ugidfw_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, int attrnamespace, const char *name);
int	ugidfw_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, u_long flags);
int	ugidfw_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, mode_t mode);
int	ugidfw_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, uid_t uid, gid_t gid);
int	ugidfw_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
	    struct label *vplabel, struct timespec atime,
	    struct timespec utime);
int	ugidfw_vnode_check_stat(struct ucred *active_cred,
	    struct ucred *file_cred, struct vnode *vp, struct label *vplabel);
int	ugidfw_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
	    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
	    struct componentname *cnp);

#endif /* _SYS_SECURITY_MAC_BSDEXTENDED_UGIDFW_INTERNAL_H */
