// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "xattr.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

static const char *gfs2_acl_name(int type)
{
	switch (type) {
	case ACL_TYPE_ACCESS:
		return XATTR_POSIX_ACL_ACCESS;
	case ACL_TYPE_DEFAULT:
		return XATTR_POSIX_ACL_DEFAULT;
	}
	return NULL;
}

static struct posix_acl *__gfs2_get_acl(struct inode *inode, int type)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct posix_acl *acl;
	const char *name;
	char *data;
	int len;

	if (!ip->i_eattr)
		return NULL;

	name = gfs2_acl_name(type);
	len = gfs2_xattr_acl_get(ip, name, &data);
	if (len <= 0)
		return ERR_PTR(len);
	acl = posix_acl_from_xattr(&init_user_ns, data, len);
	kfree(data);
	return acl;
}

struct posix_acl *gfs2_get_acl(struct inode *inode, int type)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	bool need_unlock = false;
	struct posix_acl *acl;

	if (!gfs2_glock_is_locked_by_me(ip->i_gl)) {
		int ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED,
					     LM_FLAG_ANY, &gh);
		if (ret)
			return ERR_PTR(ret);
		need_unlock = true;
	}
	acl = __gfs2_get_acl(inode, type);
	if (need_unlock)
		gfs2_glock_dq_uninit(&gh);
	return acl;
}

int __gfs2_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int error;
	size_t len;
	char *data;
	const char *name = gfs2_acl_name(type);

	if (acl) {
		len = posix_acl_xattr_size(acl->a_count);
		data = kmalloc(len, GFP_NOFS);
		if (data == NULL)
			return -ENOMEM;
		error = posix_acl_to_xattr(&init_user_ns, acl, data, len);
		if (error < 0)
			goto out;
	} else {
		data = NULL;
		len = 0;
	}

	error = __gfs2_xattr_set(inode, name, data, len, 0, GFS2_EATYPE_SYS);
	if (error)
		goto out;
	set_cached_acl(inode, type, acl);
out:
	kfree(data);
	return error;
}

int gfs2_set_acl(struct user_namespace *mnt_userns, struct inode *inode,
		 struct posix_acl *acl, int type)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	bool need_unlock = false;
	int ret;
	umode_t mode;

	if (acl && acl->a_count > GFS2_ACL_MAX_ENTRIES(GFS2_SB(inode)))
		return -E2BIG;

	ret = gfs2_qa_get(ip);
	if (ret)
		return ret;

	if (!gfs2_glock_is_locked_by_me(ip->i_gl)) {
		ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
		if (ret)
			goto out;
		need_unlock = true;
	}

	mode = inode->i_mode;
	if (type == ACL_TYPE_ACCESS && acl) {
		ret = posix_acl_update_mode(&init_user_ns, inode, &mode, &acl);
		if (ret)
			goto unlock;
	}

	ret = __gfs2_set_acl(inode, acl, type);
	if (!ret && mode != inode->i_mode) {
		inode->i_ctime = current_time(inode);
		inode->i_mode = mode;
		mark_inode_dirty(inode);
	}
unlock:
	if (need_unlock)
		gfs2_glock_dq_uninit(&gh);
out:
	gfs2_qa_put(ip);
	return ret;
}
