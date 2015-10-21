/*
 * fs/kernfs/symlink.c - kernfs symlink implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
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
 * Returns the created node on success, ERR_PTR() value on error.
 */
struct kernfs_node *kernfs_create_link(struct kernfs_node *parent,
				       const char *name,
				       struct kernfs_node *target)
{
	struct kernfs_node *kn;
	int error;

	kn = kernfs_new_node(parent, name, S_IFLNK|S_IRWXUGO, KERNFS_LINK);
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
	while (base->parent) {
		kn = target->parent;
		while (kn->parent && base != kn)
			kn = kn->parent;

		if (base == kn)
			break;

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
	if ((s - path) + len > PATH_MAX)
		return -ENAMETOOLONG;

	/* reverse fillup of target string from target to base */
	kn = target;
	while (kn->parent && kn != base) {
		int slen = strlen(kn->name);

		len -= slen;
		strncpy(s + len, kn->name, slen);
		if (len)
			s[--len] = '/';

		kn = kn->parent;
	}

	return 0;
}

static int kernfs_getlink(struct dentry *dentry, char *path)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_node *parent = kn->parent;
	struct kernfs_node *target = kn->symlink.target_kn;
	int error;

	mutex_lock(&kernfs_mutex);
	error = kernfs_get_target_path(parent, target, path);
	mutex_unlock(&kernfs_mutex);

	return error;
}

static const char *kernfs_iop_follow_link(struct dentry *dentry, void **cookie)
{
	int error = -ENOMEM;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return ERR_PTR(-ENOMEM);
	error = kernfs_getlink(dentry, (char *)page);
	if (unlikely(error < 0)) {
		free_page((unsigned long)page);
		return ERR_PTR(error);
	}
	return *cookie = (char *)page;
}

const struct inode_operations kernfs_symlink_iops = {
	.setxattr	= kernfs_iop_setxattr,
	.removexattr	= kernfs_iop_removexattr,
	.getxattr	= kernfs_iop_getxattr,
	.listxattr	= kernfs_iop_listxattr,
	.readlink	= generic_readlink,
	.follow_link	= kernfs_iop_follow_link,
	.put_link	= free_page_put_link,
	.setattr	= kernfs_iop_setattr,
	.getattr	= kernfs_iop_getattr,
	.permission	= kernfs_iop_permission,
};
