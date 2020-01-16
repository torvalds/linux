// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/kernfs/symlink.c - kernfs symlink implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 */

#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/namei.h>

#include "kernfs-internal.h"

/**
 * kernfs_create_link - create a symlink
 * @parent: directory to create the symlink in
 * @name: name of the symlink
 * @target: target yesde for the symlink to point to
 *
 * Returns the created yesde on success, ERR_PTR() value on error.
 * Ownership of the link matches ownership of the target.
 */
struct kernfs_yesde *kernfs_create_link(struct kernfs_yesde *parent,
				       const char *name,
				       struct kernfs_yesde *target)
{
	struct kernfs_yesde *kn;
	int error;
	kuid_t uid = GLOBAL_ROOT_UID;
	kgid_t gid = GLOBAL_ROOT_GID;

	if (target->iattr) {
		uid = target->iattr->ia_uid;
		gid = target->iattr->ia_gid;
	}

	kn = kernfs_new_yesde(parent, name, S_IFLNK|S_IRWXUGO, uid, gid,
			     KERNFS_LINK);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	if (kernfs_ns_enabled(parent))
		kn->ns = target->ns;
	kn->symlink.target_kn = target;
	kernfs_get(target);	/* ref owned by symlink */

	error = kernfs_add_one(kn);
	if (!error)
		return kn;

	kernfs_put(kn);
	return ERR_PTR(error);
}

static int kernfs_get_target_path(struct kernfs_yesde *parent,
				  struct kernfs_yesde *target, char *path)
{
	struct kernfs_yesde *base, *kn;
	char *s = path;
	int len = 0;

	/* go up to the root, stop at the base */
	base = parent;
	while (base->parent) {
		kn = target->parent;
		while (kn->parent && base != kn)
			kn = kn->parent;

		if (base == kn)
			break;

		if ((s - path) + 3 >= PATH_MAX)
			return -ENAMETOOLONG;

		strcpy(s, "../");
		s += 3;
		base = base->parent;
	}

	/* determine end of target string for reverse fillup */
	kn = target;
	while (kn->parent && kn != base) {
		len += strlen(kn->name) + 1;
		kn = kn->parent;
	}

	/* check limits */
	if (len < 2)
		return -EINVAL;
	len--;
	if ((s - path) + len >= PATH_MAX)
		return -ENAMETOOLONG;

	/* reverse fillup of target string from target to base */
	kn = target;
	while (kn->parent && kn != base) {
		int slen = strlen(kn->name);

		len -= slen;
		memcpy(s + len, kn->name, slen);
		if (len)
			s[--len] = '/';

		kn = kn->parent;
	}

	return 0;
}

static int kernfs_getlink(struct iyesde *iyesde, char *path)
{
	struct kernfs_yesde *kn = iyesde->i_private;
	struct kernfs_yesde *parent = kn->parent;
	struct kernfs_yesde *target = kn->symlink.target_kn;
	int error;

	mutex_lock(&kernfs_mutex);
	error = kernfs_get_target_path(parent, target, path);
	mutex_unlock(&kernfs_mutex);

	return error;
}

static const char *kernfs_iop_get_link(struct dentry *dentry,
				       struct iyesde *iyesde,
				       struct delayed_call *done)
{
	char *body;
	int error;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	body = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!body)
		return ERR_PTR(-ENOMEM);
	error = kernfs_getlink(iyesde, body);
	if (unlikely(error < 0)) {
		kfree(body);
		return ERR_PTR(error);
	}
	set_delayed_call(done, kfree_link, body);
	return body;
}

const struct iyesde_operations kernfs_symlink_iops = {
	.listxattr	= kernfs_iop_listxattr,
	.get_link	= kernfs_iop_get_link,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.permission	= kernfs_iop_permission,
};
