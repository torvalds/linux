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

extern struct posix_acl *gfs2_get_acl(struct inode *inode, int type);
extern int gfs2_set_acl(struct inode *inode, struct posix_acl *acl, int type);

#endif /* __ACL_DOT_H__ */
