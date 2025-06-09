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
 * @target: target node for the symlink to point to
 *
 * Return: the created node on success, ERR_PTR() value on error.
 * Ownership of the link matches ownership of the target.
 */
struct kernfs_node *kernfs_create_link(struct kernfs_node *parent,
				       const char *name,
				       struct kernfs_node *target)
{
	struct kernfs_node *kn;
	int error;
	kuid_t uid = GLOBAL_ROOT_UID;
	kgid_t gid = GLOBAL_ROOT_GID;

	if (target->iattr) {
		uid = target->iattr->ia_uid;
		gid = target->iattr->ia_gid;
	}

	kn = kernfs_new_node(parent, name, S_IFLNK|0777, uid, gid, KERNFS_LINK);
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

static int kernfs_get_target_path(struct kernfs_node *parent,
				  struct kernfs_node *target, char *path)
{
	struct kernfs_node *base, *kn;
	char *s = path;
	int len = 0;

	/* go up to the root, stop at the base */
	base = parent;
	while (kernfs_parent(base)) {
		kn = kernfs_parent(target);
		while (kernfs_parent(kn) && base != kn)
			kn = kernfs_parent(kn);

		if (base == kn)
			break;

		if ((s - path) + 3 >= PATH_MAX)
			return -ENAMETOOLONG;

		strcpy(s, "../");
		s += 3;
		base = kernfs_parent(base);
	}

	/* determine end of target string for reverse fillup */
	kn = target;
	while (kernfs_parent(kn) && kn != base) {
		len += strlen(kernfs_rcu_name(kn)) + 1;
		kn = kernfs_parent(kn);
	}

	/* check limits */
	if (len < 2)
		return -EINVAL;
	len--;
	if ((s - path) + len >= PATH_MAX)
		return -ENAMETOOLONG;

	/* reverse fillup of target string from target to base */
	kn = target;
	while (kernfs_parent(kn) && kn != base) {
		const char *name = kernfs_rcu_name(kn);
		int slen = strlen(name);

		len -= slen;
		memcpy(s + len, name, slen);
		if (len)
			s[--len] = '/';

		kn = kernfs_parent(kn);
	}

	return 0;
}

static int kernfs_getlink(struct inode *inode, char *path)
{
	struct kernfs_node *kn = inode->i_private;
	struct kernfs_node *parent;
	struct kernfs_node *target = kn->symlink.target_kn;
	struct kernfs_root *root = kernfs_root(kn);
	int error;

	down_read(&root->kernfs_rwsem);
	parent = kernfs_parent(kn);
	error = kernfs_get_target_path(parent, target, path);
	up_read(&root->kernfs_rwsem);

	return error;
}

static const char *kernfs_iop_get_link(struct dentry *dentry,
				       struct inode *inode,
				       struct delayed_call *done)
{
	char *body;
	int error;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	body = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!body)
		return ERR_PTR(-ENOMEM);
	error = kernfs_getlink(inode, body);
	if (unlikely(error < 0)) {
		kfree(body);
		return ERR_PTR(error);
	}
	set_delayed_call(done, kfree_link, body);
	return body;
}

const struct inode_operations kernfs_symlink_iops = {
	.listxattr	= kernfs_iop_listxattr,
	.get_link	= kernfs_iop_get_link,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.permission	= kernfs_iop_permission,
};
