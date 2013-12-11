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

static int sysfs_do_create_link_sd(struct kernfs_node *parent,
				   struct kobject *target_kobj,
				   const char *name, int warn)
{
	struct kernfs_node *kn, *target = NULL;

	BUG_ON(!name || !parent);

	/*
	 * We don't own @target_kobj and it may be removed at any time.
	 * Synchronize using sysfs_symlink_target_lock.  See
	 * sysfs_remove_dir() for details.
	 */
	spin_lock(&sysfs_symlink_target_lock);
	if (target_kobj->sd) {
		target = target_kobj->sd;
		kernfs_get(target);
	}
	spin_unlock(&sysfs_symlink_target_lock);

	if (!target)
		return -ENOENT;

	kn = kernfs_create_link(parent, name, target);
	kernfs_put(target);

	if (!IS_ERR(kn))
		return 0;

	if (warn && PTR_ERR(kn) == -EEXIST)
		sysfs_warn_dup(parent, name);
	return PTR_ERR(kn);
}

/**
 *	sysfs_create_link_sd - create symlink to a given object.
 *	@kn:		directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link_sd(struct kernfs_node *kn, struct kobject *target,
			 const char *name)
{
	return sysfs_do_create_link_sd(kn, target, name, 1);
}

static int sysfs_do_create_link(struct kobject *kobj, struct kobject *target,
				const char *name, int warn)
{
	struct kernfs_node *parent = NULL;

	if (!kobj)
		parent = sysfs_root_kn;
	else
		parent = kobj->sd;

	if (!parent)
		return -EFAULT;

	return sysfs_do_create_link_sd(parent, target, name, warn);
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
	if (targ->sd && kernfs_ns_enabled(kobj->sd))
		ns = targ->sd->ns;
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
	struct kernfs_node *parent = NULL;

	if (!kobj)
		parent = sysfs_root_kn;
	else
		parent = kobj->sd;

	kernfs_remove_by_name(parent, name);
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
	struct kernfs_node *parent, *kn = NULL;
	const void *old_ns = NULL;
	int result;

	if (!kobj)
		parent = sysfs_root_kn;
	else
		parent = kobj->sd;

	if (targ->sd)
		old_ns = targ->sd->ns;

	result = -ENOENT;
	kn = kernfs_find_and_get_ns(parent, old, old_ns);
	if (!kn)
		goto out;

	result = -EINVAL;
	if (kernfs_type(kn) != KERNFS_LINK)
		goto out;
	if (kn->symlink.target_kn->priv != targ)
		goto out;

	result = kernfs_rename_ns(kn, parent, new, new_ns);

out:
	kernfs_put(kn);
	return result;
}
EXPORT_SYMBOL_GPL(sysfs_rename_link_ns);
