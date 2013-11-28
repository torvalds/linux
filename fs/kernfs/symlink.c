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
struct sysfs_dirent *kernfs_create_link(struct sysfs_dirent *parent,
					const char *name,
					struct sysfs_dirent *target)
{
	struct sysfs_dirent *sd;
	struct sysfs_addrm_cxt acxt;
	int error;

	sd = sysfs_new_dirent(kernfs_root(parent), name, S_IFLNK|S_IRWXUGO,
			      SYSFS_KOBJ_LINK);
	if (!sd)
		return ERR_PTR(-ENOMEM);

	if (parent->s_flags & SYSFS_FLAG_NS)
		sd->s_ns = target->s_ns;
	sd->s_symlink.target_sd = target;
	kernfs_get(target);	/* ref owned by symlink */

	sysfs_addrm_start(&acxt);
	error = sysfs_add_one(&acxt, sd, parent);
	sysfs_addrm_finish(&acxt);

	if (!error)
		return sd;

	kernfs_put(sd);
	return ERR_PTR(error);
}

static int sysfs_get_target_path(struct sysfs_dirent *parent_sd,
				 struct sysfs_dirent *target_sd, char *path)
{
	struct sysfs_dirent *base, *sd;
	char *s = path;
	int len = 0;

	/* go up to the root, stop at the base */
	base = parent_sd;
	while (base->s_parent) {
		sd = target_sd->s_parent;
		while (sd->s_parent && base != sd)
			sd = sd->s_parent;

		if (base == sd)
			break;

		strcpy(s, "../");
		s += 3;
		base = base->s_parent;
	}

	/* determine end of target string for reverse fillup */
	sd = target_sd;
	while (sd->s_parent && sd != base) {
		len += strlen(sd->s_name) + 1;
		sd = sd->s_parent;
	}

	/* check limits */
	if (len < 2)
		return -EINVAL;
	len--;
	if ((s - path) + len > PATH_MAX)
		return -ENAMETOOLONG;

	/* reverse fillup of target string from target to base */
	sd = target_sd;
	while (sd->s_parent && sd != base) {
		int slen = strlen(sd->s_name);

		len -= slen;
		strncpy(s + len, sd->s_name, slen);
		if (len)
			s[--len] = '/';

		sd = sd->s_parent;
	}

	return 0;
}

static int sysfs_getlink(struct dentry *dentry, char *path)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	struct sysfs_dirent *parent_sd = sd->s_parent;
	struct sysfs_dirent *target_sd = sd->s_symlink.target_sd;
	int error;

	mutex_lock(&sysfs_mutex);
	error = sysfs_get_target_path(parent_sd, target_sd, path);
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
	.readlink	= generic_readlink,
	.follow_link	= sysfs_follow_link,
	.put_link	= sysfs_put_link,
	.setattr	= sysfs_setattr,
	.getattr	= sysfs_getattr,
	.permission	= sysfs_permission,
};
