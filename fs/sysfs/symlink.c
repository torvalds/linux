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
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/security.h>

#include "sysfs.h"

static int sysfs_do_create_link_sd(struct sysfs_dirent *parent_sd,
				   struct kobject *target,
				   const char *name, int warn)
{
	struct sysfs_dirent *sd, *target_sd = NULL;

	BUG_ON(!name || !parent_sd);

	/*
	 * We don't own @target and it may be removed at any time.
	 * Synchronize using sysfs_symlink_target_lock.  See
	 * sysfs_remove_dir() for details.
	 */
	spin_lock(&sysfs_symlink_target_lock);
	if (target->sd) {
		target_sd = target->sd;
		kernfs_get(target_sd);
	}
	spin_unlock(&sysfs_symlink_target_lock);

	if (!target_sd)
		return -ENOENT;

	sd = kernfs_create_link(parent_sd, name, target_sd);
	kernfs_put(target_sd);

	if (!IS_ERR(sd))
		return 0;

	if (warn && PTR_ERR(sd) == -EEXIST)
		sysfs_warn_dup(parent_sd, name);
	return PTR_ERR(sd);
}

/**
 *	sysfs_create_link_sd - create symlink to a given object.
 *	@sd:		directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link_sd(struct sysfs_dirent *sd, struct kobject *target,
			 const char *name)
{
	return sysfs_do_create_link_sd(sd, target, name, 1);
}

static int sysfs_do_create_link(struct kobject *kobj, struct kobject *target,
				const char *name, int warn)
{
	struct sysfs_dirent *parent_sd = NULL;

	if (!kobj)
		parent_sd = sysfs_root_sd;
	else
		parent_sd = kobj->sd;

	if (!parent_sd)
		return -EFAULT;

	return sysfs_do_create_link_sd(parent_sd, target, name, warn);
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
EXPORT_SYMBOL_GPL(sysfs_create_link);

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

	/*
	 * We don't own @target and it may be removed at any time.
	 * Synchronize using sysfs_symlink_target_lock.  See
	 * sysfs_remove_dir() for details.
	 */
	spin_lock(&sysfs_symlink_target_lock);
	if (targ->sd && (kobj->sd->s_flags & SYSFS_FLAG_NS))
		ns = targ->sd->s_ns;
	spin_unlock(&sysfs_symlink_target_lock);
	kernfs_remove_by_name_ns(kobj->sd, name, ns);
}

/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */
void sysfs_remove_link(struct kobject *kobj, const char *name)
{
	struct sysfs_dirent *parent_sd = NULL;

	if (!kobj)
		parent_sd = sysfs_root_sd;
	else
		parent_sd = kobj->sd;

	kernfs_remove_by_name(parent_sd, name);
}
EXPORT_SYMBOL_GPL(sysfs_remove_link);

/**
 *	sysfs_rename_link_ns - rename symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@targ:	object we're pointing to.
 *	@old:	previous name of the symlink.
 *	@new:	new name of the symlink.
 *	@new_ns: new namespace of the symlink.
 *
 *	A helper function for the common rename symlink idiom.
 */
int sysfs_rename_link_ns(struct kobject *kobj, struct kobject *targ,
			 const char *old, const char *new, const void *new_ns)
{
	struct sysfs_dirent *parent_sd, *sd = NULL;
	const void *old_ns = NULL;
	int result;

	if (!kobj)
		parent_sd = sysfs_root_sd;
	else
		parent_sd = kobj->sd;

	if (targ->sd)
		old_ns = targ->sd->s_ns;

	result = -ENOENT;
	sd = kernfs_find_and_get_ns(parent_sd, old, old_ns);
	if (!sd)
		goto out;

	result = -EINVAL;
	if (sysfs_type(sd) != SYSFS_KOBJ_LINK)
		goto out;
	if (sd->s_symlink.target_sd->priv != targ)
		goto out;

	result = kernfs_rename_ns(sd, parent_sd, new, new_ns);

out:
	kernfs_put(sd);
	return result;
}
EXPORT_SYMBOL_GPL(sysfs_rename_link_ns);
