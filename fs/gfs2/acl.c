/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
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
#include "trans.h"
#include "util.h"

static const char *gfs2_acl_name(int type)
{
	switch (type) {
	case ACL_TYPE_ACCESS:
		return GFS2_POSIX_ACL_ACCESS;
	case ACL_TYPE_DEFAULT:
		return GFS2_POSIX_ACL_DEFAULT;
	}
	return NULL;
}

static struct posix_acl *gfs2_acl_get(struct gfs2_inode *ip, int type)
{
	struct posix_acl *acl;
	const char *name;
	char *data;
	int len;

	if (!ip->i_eattr)
		return NULL;

	acl = get_cached_acl(&ip->i_inode, type);
	if (acl != ACL_NOT_CACHED)
		return acl;

	name = gfs2_acl_name(type);
	if (name == NULL)
		return ERR_PTR(-EINVAL);

	len = gfs2_xattr_acl_get(ip, name, &data);
	if (len < 0)
		return ERR_PTR(len);
	if (len == 0)
		return NULL;

	acl = posix_acl_from_xattr(data, len);
	kfree(data);
	return acl;
}

struct posix_acl *gfs2_get_acl(struct inode *inode, int type)
{
	return gfs2_acl_get(GFS2_I(inode), type);
}

static int gfs2_set_mode(struct inode *inode, umode_t mode)
{
	int error = 0;

	if (mode != inode->i_mode) {
		struct iattr iattr;

		iattr.ia_valid = ATTR_MODE;
		iattr.ia_mode = mode;

		error = gfs2_setattr_simple(GFS2_I(inode), &iattr);
	}

	return error;
}

static int gfs2_acl_set(struct inode *inode, int type, struct posix_acl *acl)
{
	int error;
	int len;
	char *data;
	const char *name = gfs2_acl_name(type);

	BUG_ON(name == NULL);
	len = posix_acl_to_xattr(acl, NULL, 0);
	if (len == 0)
		return 0;
	data = kmalloc(len, GFP_NOFS);
	if (data == NULL)
		return -ENOMEM;
	error = posix_acl_to_xattr(acl, data, len);
	if (error < 0)
		goto out;
	error = __gfs2_xattr_set(inode, name, data, len, 0, GFS2_EATYPE_SYS);
	if (!error)
		set_cached_acl(inode, type, acl);
out:
	kfree(data);
	return error;
}

int gfs2_acl_create(struct gfs2_inode *dip, struct inode *inode)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct posix_acl *acl;
	umode_t mode = inode->i_mode;
	int error = 0;

	if (!sdp->sd_args.ar_posix_acl)
		return 0;
	if (S_ISLNK(inode->i_mode))
		return 0;

	acl = gfs2_acl_get(dip, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (!acl) {
		mode &= ~current_umask();
		if (mode != inode->i_mode)
			error = gfs2_set_mode(inode, mode);
		return error;
	}

	if (S_ISDIR(inode->i_mode)) {
		error = gfs2_acl_set(inode, ACL_TYPE_DEFAULT, acl);
		if (error)
			goto out;
	}

	error = posix_acl_create(&acl, GFP_NOFS, &mode);
	if (error < 0)
		return error;

	if (error == 0)
		goto munge;

	error = gfs2_acl_set(inode, ACL_TYPE_ACCESS, acl);
	if (error)
		goto out;
munge:
	error = gfs2_set_mode(inode, mode);
out:
	posix_acl_release(acl);
	return error;
}

int gfs2_acl_chmod(struct gfs2_inode *ip, struct iattr *attr)
{
	struct posix_acl *acl;
	char *data;
	unsigned int len;
	int error;

	acl = gfs2_acl_get(ip, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (!acl)
		return gfs2_setattr_simple(ip, attr);

	error = posix_acl_chmod(&acl, GFP_NOFS, attr->ia_mode);
	if (error)
		return error;

	len = posix_acl_to_xattr(acl, NULL, 0);
	data = kmalloc(len, GFP_NOFS);
	error = -ENOMEM;
	if (data == NULL)
		goto out;
	posix_acl_to_xattr(acl, data, len);
	error = gfs2_xattr_acl_chmod(ip, attr, data);
	kfree(data);
	set_cached_acl(&ip->i_inode, ACL_TYPE_ACCESS, acl);

out:
	posix_acl_release(acl);
	return error;
}

static int gfs2_acl_type(const char *name)
{
	if (strcmp(name, GFS2_POSIX_ACL_ACCESS) == 0)
		return ACL_TYPE_ACCESS;
	if (strcmp(name, GFS2_POSIX_ACL_DEFAULT) == 0)
		return ACL_TYPE_DEFAULT;
	return -EINVAL;
}

static int gfs2_xattr_system_get(struct dentry *dentry, const char *name,
				 void *buffer, size_t size, int xtype)
{
	struct inode *inode = dentry->d_inode;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct posix_acl *acl;
	int type;
	int error;

	if (!sdp->sd_args.ar_posix_acl)
		return -EOPNOTSUPP;

	type = gfs2_acl_type(name);
	if (type < 0)
		return type;

	acl = gfs2_acl_get(GFS2_I(inode), type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);

	return error;
}

static int gfs2_xattr_system_set(struct dentry *dentry, const char *name,
				 const void *value, size_t size, int flags,
				 int xtype)
{
	struct inode *inode = dentry->d_inode;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct posix_acl *acl = NULL;
	int error = 0, type;

	if (!sdp->sd_args.ar_posix_acl)
		return -EOPNOTSUPP;

	type = gfs2_acl_type(name);
	if (type < 0)
		return type;
	if (flags & XATTR_CREATE)
		return -EINVAL;
	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return value ? -EACCES : 0;
	if ((current_fsuid() != inode->i_uid) && !capable(CAP_FOWNER))
		return -EPERM;
	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (!value)
		goto set_acl;

	acl = posix_acl_from_xattr(value, size);
	if (!acl) {
		/*
		 * acl_set_file(3) may request that we set default ACLs with
		 * zero length -- defend (gracefully) against that here.
		 */
		goto out;
	}
	if (IS_ERR(acl)) {
		error = PTR_ERR(acl);
		goto out;
	}

	error = posix_acl_valid(acl);
	if (error)
		goto out_release;

	error = -EINVAL;
	if (acl->a_count > GFS2_ACL_MAX_ENTRIES)
		goto out_release;

	if (type == ACL_TYPE_ACCESS) {
		umode_t mode = inode->i_mode;
		error = posix_acl_equiv_mode(acl, &mode);

		if (error <= 0) {
			posix_acl_release(acl);
			acl = NULL;

			if (error < 0)
				return error;
		}

		error = gfs2_set_mode(inode, mode);
		if (error)
			goto out_release;
	}

set_acl:
	error = __gfs2_xattr_set(inode, name, value, size, 0, GFS2_EATYPE_SYS);
	if (!error) {
		if (acl)
			set_cached_acl(inode, type, acl);
		else
			forget_cached_acl(inode, type);
	}
out_release:
	posix_acl_release(acl);
out:
	return error;
}

const struct xattr_handler gfs2_xattr_system_handler = {
	.prefix = XATTR_SYSTEM_PREFIX,
	.flags  = GFS2_EATYPE_SYS,
	.get    = gfs2_xattr_system_get,
	.set    = gfs2_xattr_system_set,
};

