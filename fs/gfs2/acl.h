/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __ACL_DOT_H__
#define __ACL_DOT_H__

#include "incore.h"

#define GFS2_POSIX_ACL_ACCESS		"posix_acl_access"
#define GFS2_POSIX_ACL_DEFAULT		"posix_acl_default"
#define GFS2_ACL_MAX_ENTRIES		25

extern int gfs2_check_acl(struct inode *inode, int mask);
extern int gfs2_acl_create(struct gfs2_inode *dip, struct inode *inode);
extern int gfs2_acl_chmod(struct gfs2_inode *ip, struct iattr *attr);
extern struct xattr_handler gfs2_xattr_system_handler;

#endif /* __ACL_DOT_H__ */
