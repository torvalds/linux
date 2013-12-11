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
	struct sysfs_addrm_cxt acxt;
	int error;

	kn = sysfs_new_dirent(kernfs_root(parent), name, S_IFLNK|S_IRWXUGO,
			      SYSFS_KOBJ_LINK);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	if (kernfs_ns_enabled(parent))
		kn->s_ns = target->s_ns;
	kn->s_symlink.target_kn = target;
	kernfs_get(target);	/* ref owned by symlink */

	sysfs_addrm_start(&acxt);
	error = sysfs_add_one(&acxt, kn, parent);
	sysfs_addrm_finish(&acxt);

	if (!error)
		return kn;

	kernfs_put(kn);
	return ERR_PTR(error);
}

static int sysfs_get_target_path(struct kernfs_node *parent,
				 struct kernfs_node *target, char *path)
{
	struct kernfs_node *base, *kn;
	char *s = path;
	int len = 0;

	/* go up to the root, stop at the base */
	base = parent;
	while (base->s_parent) {
		kn = target->s_parent;
		while (kn->s_parent && base != kn)
			kn = kn->s_parent;

		if (base == kn)
			break;

		strcpy(s, "../");
		s += 3;
		base = base->s_parent;
	}

	/* determine end of target string for reverse fillup */
	kn = target;
	while (kn->s_parent && kn != base) {
		len += strlen(kn->s_name) + 1;
		kn = kn->s_parent;
	}

	/* check limits */
	if (len < 2)
		return -EINVAL;
	len--;
	if ((s - path) + len > PATH_MAX)
		return -ENAMETOOLONG;

	/* reverse fillup of target string from target to base */
	kn = target;
	while (kn->s_parent && kn != base) {
		int slen = strlen(kn->s_name);

		len -= slen;
		strncpy(s + len, kn->s_name, slen);
		if (len)
			s[--len] = '/';

		kn = kn->s_parent;
	}

	return 0;
}

static int sysfs_getlink(struct dentry *dentry, char *path)
{
	struct kernfs_node *kn = dentry->d_fsdata;
	struct kernfs_node *parent = kn->s_parent;
	struct kernfs_node *target = kn->s_symlink.target_kn;
	int error;

	mutex_lock(&sysfs_mutex);
	error = sysfs_get_target_path(parent, target, path);
	mutex_unlock(&sysfs_mutex);

	return error;
}

static void *sysfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int error = -ENOMEM;
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	if (page) {
		error = sysfs_getlink(dentry, (char *) page);
		if (error < 0)
			free_page((unsigned long)page);
	}
	nd_set_link(nd, error ? ERR_PTR(error) : (char *)page);
	return NULL;
}

static void sysfs_put_link(struct dentry *dentry, struct nameidata *nd,
			   void *cookie)
{
	char *page = nd_get_link(nd);
	if (!IS_ERR(page))
		free_page((unsigned long)page);
}

const struct inode_operations sysfs_symlink_inode_operations = {
	.setxattr	= sysfs_setxattr,
	.removexattr	= sysfs_removexattr,
	.getxattr	= sysfs_getxattr,
	.listxattr	= sysfs_listxattr,
	.readlink	= generic_readlink,
	.follow_link	= sysfs_follow_link,
	.put_link	= sysfs_put_link,
	.setattr	= sysfs_setattr,
	.getattr	= sysfs_getattr,
	.permission	= sysfs_permission,
};
