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
#define GFS2_POSIX_ACL_ACCESS_LEN	16
#define GFS2_POSIX_ACL_DEFAULT		"posix_acl_default"
#define GFS2_POSIX_ACL_DEFAULT_LEN	17

#define GFS2_ACL_IS_ACCESS(name, len) \
         ((len) == GFS2_POSIX_ACL_ACCESS_LEN && \
         !memcmp(GFS2_POSIX_ACL_ACCESS, (name), (len)))

#define GFS2_ACL_IS_DEFAULT(name, len) \
         ((len) == GFS2_POSIX_ACL_DEFAULT_LEN && \
         !memcmp(GFS2_POSIX_ACL_DEFAULT, (name), (len)))

struct gfs2_ea_request;

int gfs2_acl_validate_set(struct gfs2_inode *ip, int access,
			  struct gfs2_ea_request *er,
			  int *remove, mode_t *mode);
int gfs2_acl_validate_remove(struct gfs2_inode *ip, int access);
int gfs2_check_acl_locked(struct inode *inode, int mask);
int gfs2_check_acl(struct inode *inode, int mask);
int gfs2_acl_create(struct gfs2_inode *dip, struct gfs2_inode *ip);
int gfs2_acl_chmod(struct gfs2_inode *ip, struct iattr *attr);

#endif /* __ACL_DOT_H__ */
