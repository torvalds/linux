/*
 * fs/sysfs/symlink.c - sysfs symlink implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/mutex.h>
#include <linux/security.h>

#include "sysfs.h"

static int sysfs_do_create_link(struct kobject *kobj, struct kobject *target,
				const char *name, int warn)
{
	struct sysfs_dirent *parent_sd = NULL;
	struct sysfs_dirent *target_sd = NULL;
	struct sysfs_dirent *sd = NULL;
	struct sysfs_addrm_cxt acxt;
	enum kobj_ns_type ns_type;
	int error;

	BUG_ON(!name);

	if (!kobj)
		parent_sd = &sysfs_root;
	else
		parent_sd = kobj->sd;

	error = -EFAULT;
	if (!parent_sd)
		goto out_put;

	/* target->sd can go away beneath us but is protected with
	 * sysfs_assoc_lock.  Fetch target_sd from it.
	 */
	spin_lock(&sysfs_assoc_lock);
	if (target->sd)
		target_sd = sysfs_get(target->sd);
	spin_unlock(&sysfs_assoc_lock);

	error = -ENOENT;
	if (!target_sd)
		goto out_put;

	error = -ENOMEM;
	sd = sysfs_new_dirent(name, S_IFLNK|S_IRWXUGO, SYSFS_KOBJ_LINK);
	if (!sd)
		goto out_put;

	ns_type = sysfs_ns_type(parent_sd);
	if (ns_type)
		sd->s_ns = target->ktype->namespace(target);
	sd->s_symlink.target_sd = target_sd;
	target_sd = NULL;	/* reference is now owned by the symlink */

	sysfs_addrm_start(&acxt, parent_sd);
	/* Symlinks must be between directories with the same ns_type */
	if (!ns_type ||
	    (ns_type == sysfs_ns_type(sd->s_symlink.target_sd->s_parent))) {
		if (warn)
			error = sysfs_add_one(&acxt, sd);
		else
			error = __sysfs_add_one(&acxt, sd);
	} else {
		error = -EINVAL;
		WARN(1, KERN_WARNING
			"sysfs: symlink across ns_types %s/%s -> %s/%s\n",
			parent_sd->s_name,
			sd->s_name,
			sd->s_symlink.target_sd->s_parent->s_name,
			sd->s_symlink.target_sd->s_name);
	}
	sysfs_addrm_finish(&acxt);

	if (error)
		goto out_put;

	return 0;

 out_put:
	sysfs_put(target_sd);
	sysfs_put(sd);
	return error;
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject *kobj, struct kobject *target,
		      const char *name)
{
	return sysfs_do_create_link(kobj, target, name, 1);
}

/**
 *	sysfs_create_link_nowarn - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 *
 *	This function does the same as sysfs_create_link(), but it
 *	doesn't warn if the link already exists.
 */
int sysfs_create_link_nowarn(struct kobject *kobj, struct kobject *target,
			     const char *name)
{
	return sysfs_do_create_link(kobj, target, name, 0);
}

/**
 *	sysfs_delete_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@targ:	object we're pointing to.
 *	@name:	name of the symlink to remove.
 *
 *	Unlike sysfs_remove_link sysfs_delete_link has enough information
 *	to successfully delete symlinks in tagged directories.
 */
void sysfs_delete_link(struct kobject *kobj, struct kobject *targ,
			const char *name)
{
	const void *ns = NULL;
	spin_lock(&sysfs_assoc_lock);
	if (targ->sd && sysfs_ns_type(kobj->sd))
		ns = targ->sd->s_ns;
	spin_unlock(&sysfs_assoc_lock);
	sysfs_hash_and_remove(kobj->sd, ns, name);
}

/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */

void sysfs_remove_link(struct kobject * kobj, const char * name)
{
	struct sysfs_dirent *parent_sd = NULL;

	if (!kobj)
		parent_sd = &sysfs_root;
	else
		parent_sd = kobj->sd;

	sysfs_hash_and_remove(parent_sd, NULL, name);
}

/**
 *	sysfs_rename_link - rename symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@targ:	object we're pointing to.
 *	@old:	previous name of the symlink.
 *	@new:	new name of the symlink.
 *
 *	A helper function for the common rename symlink idiom.
 */
int sysfs_rename_link(struct kobject *kobj, struct kobject *targ,
			const char *old, const char *new)
{
	struct sysfs_dirent *parent_sd, *sd = NULL;
	const void *old_ns = NULL, *new_ns = NULL;
	int result;

	if (!kobj)
		parent_sd = &sysfs_root;
	else
		parent_sd = kobj->sd;

	if (targ->sd)
		old_ns = targ->sd->s_ns;

	result = -ENOENT;
	sd = sysfs_get_dirent(parent_sd, old_ns, old);
	if (!sd)
		goto out;

	result = -EINVAL;
	if (sysfs_type(sd) != SYSFS_KOBJ_LINK)
		goto out;
	if (sd->s_symlink.target_sd->s_dir.kobj != targ)
		goto out;

	if (sysfs_ns_type(parent_sd))
		new_ns = targ->ktype->namespace(targ);

	result = sysfs_rename(sd, parent_sd, new_ns, new);

out:
	sysfs_put(sd);
	return result;
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

static int sysfs_getlink(struct dentry *dentry, char * path)
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

static void sysfs_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
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


EXPORT_SYMBOL_GPL(sysfs_create_link);
EXPORT_SYMBOL_GPL(sysfs_remove_link);
EXPORT_SYMBOL_GPL(sysfs_rename_link);
