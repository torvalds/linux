/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015, Joyent, Inc. All rights reserved.
 * Copyright (c) 2016, Lawrence Livermore National Security, LLC.
 */

#ifndef _SYS_POLICY_H
#define	_SYS_POLICY_H

#ifdef _KERNEL

#include <sys/cred.h>
#include <sys/types.h>
#include <sys/xvattr.h>
#include <sys/zpl.h>

int secpolicy_nfs(const cred_t *);
int secpolicy_sys_config(const cred_t *, boolean_t);
int secpolicy_vnode_access2(const cred_t *, struct inode *,
    uid_t, mode_t, mode_t);
int secpolicy_vnode_any_access(const cred_t *, struct inode *, uid_t);
int secpolicy_vnode_chown(const cred_t *, uid_t);
int secpolicy_vnode_create_gid(const cred_t *);
int secpolicy_vnode_remove(const cred_t *);
int secpolicy_vnode_setdac(const cred_t *, uid_t);
int secpolicy_vnode_setid_retain(const cred_t *, boolean_t);
int secpolicy_vnode_setids_setgids(const cred_t *, gid_t);
int secpolicy_zinject(const cred_t *);
int secpolicy_zfs(const cred_t *);
void secpolicy_setid_clear(vattr_t *, cred_t *);
int secpolicy_setid_setsticky_clear(struct inode *, vattr_t *,
    const vattr_t *, cred_t *);
int secpolicy_xvattr(xvattr_t *, uid_t, cred_t *, vtype_t);
int secpolicy_vnode_setattr(cred_t *, struct inode *, struct vattr *,
    const struct vattr *, int, int (void *, int, cred_t *), void *);
int secpolicy_basic_link(const cred_t *);

#endif /* _KERNEL */
#endif /* _SYS_POLICY_H */
