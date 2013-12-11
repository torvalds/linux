/*
 * fs/sysfs/group.c - Operations for adding/removing multiple files at once.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2013 Greg Kroah-Hartman
 * Copyright (c) 2013 The Linux Foundation
 *
 * This file is released undert the GPL v2.
 *
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/err.h>
#include "sysfs.h"


static void remove_files(struct kernfs_node *parent, struct kobject *kobj,
			 const struct attribute_group *grp)
{
	struct attribute *const *attr;
	struct bin_attribute *const *bin_attr;

	if (grp->attrs)
		for (attr = grp->attrs; *attr; attr++)
			kernfs_remove_by_name(parent, (*attr)->name);
	if (grp->bin_attrs)
		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++)
			sysfs_remove_bin_file(kobj, *bin_attr);
}

static int create_files(struct kernfs_node *parent, struct kobject *kobj,
			const struct attribute_group *grp, int update)
{
	struct attribute *const *attr;
	struct bin_attribute *const *bin_attr;
	int error = 0, i;

	if (grp->attrs) {
		for (i = 0, attr = grp->attrs; *attr && !error; i++, attr++) {
			umode_t mode = 0;

			/*
			 * In update mode, we're changing the permissions or
			 * visibility.  Do this by first removing then
			 * re-adding (if required) the file.
			 */
			if (update)
				kernfs_remove_by_name(parent, (*attr)->name);
			if (grp->is_visible) {
				mode = grp->is_visible(kobj, *attr, i);
				if (!mode)
					continue;
			}
			error = sysfs_add_file_mode_ns(parent, *attr, false,
						       (*attr)->mode | mode,
						       NULL);
			if (unlikely(error))
				break;
		}
		if (error) {
			remove_files(parent, kobj, grp);
			goto exit;
		}
	}

	if (grp->bin_attrs) {
		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++) {
			if (update)
				sysfs_remove_bin_file(kobj, *bin_attr);
			error = sysfs_create_bin_file(kobj, *bin_attr);
			if (error)
				break;
		}
		if (error)
			remove_files(parent, kobj, grp);
	}
exit:
	return error;
}


static int internal_create_group(struct kobject *kobj, int update,
				 const struct attribute_group *grp)
{
	struct kernfs_node *kn;
	int error;

	BUG_ON(!kobj || (!update && !kobj->sd));

	/* Updates may happen before the object has been instantiated */
	if (unlikely(update && !kobj->sd))
		return -EINVAL;
	if (!grp->attrs && !grp->bin_attrs) {
		WARN(1, "sysfs: (bin_)attrs not set by subsystem for group: %s/%s\n",
			kobj->name, grp->name ? "" : grp->name);
		return -EINVAL;
	}
	if (grp->name) {
		kn = kernfs_create_dir(kobj->sd, grp->name, kobj);
		if (IS_ERR(kn)) {
			if (PTR_ERR(kn) == -EEXIST)
				sysfs_warn_dup(kobj->sd, grp->name);
			return PTR_ERR(kn);
		}
	} else
		kn = kobj->sd;
	kernfs_get(kn);
	error = create_files(kn, kobj, grp, update);
	if (error) {
		if (grp->name)
			kernfs_remove(kn);
	}
	kernfs_put(kn);
	return error;
}

/**
 * sysfs_create_group - given a directory kobject, create an attribute group
 * @kobj:	The kobject to create the group on
 * @grp:	The attribute group to create
 *
 * This function creates a group for the first time.  It will explicitly
 * warn and error if any of the attribute files being created already exist.
 *
 * Returns 0 on success or error.
 */
int sysfs_create_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	return internal_create_group(kobj, 0, grp);
}
EXPORT_SYMBOL_GPL(sysfs_create_group);

/**
 * sysfs_create_groups - given a directory kobject, create a bunch of attribute groups
 * @kobj:	The kobject to create the group on
 * @groups:	The attribute groups to create, NULL terminated
 *
 * This function creates a bunch of attribute groups.  If an error occurs when
 * creating a group, all previously created groups will be removed, unwinding
 * everything back to the original state when this function was called.
 * It will explicitly warn and error if any of the attribute files being
 * created already exist.
 *
 * Returns 0 on success or error code from sysfs_create_group on error.
 */
int sysfs_create_groups(struct kobject *kobj,
			const struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (!groups)
		return 0;

	for (i = 0; groups[i]; i++) {
		error = sysfs_create_group(kobj, groups[i]);
		if (error) {
			while (--i >= 0)
				sysfs_remove_group(kobj, groups[i]);
			break;
		}
	}
	return error;
}
EXPORT_SYMBOL_GPL(sysfs_create_groups);

/**
 * sysfs_update_group - given a directory kobject, update an attribute group
 * @kobj:	The kobject to update the group on
 * @grp:	The attribute group to update
 *
 * This function updates an attribute group.  Unlike
 * sysfs_create_group(), it will explicitly not warn or error if any
 * of the attribute files being created already exist.  Furthermore,
 * if the visibility of the files has changed through the is_visible()
 * callback, it will update the permissions and add or remove the
 * relevant files.
 *
 * The primary use for this function is to call it after making a change
 * that affects group visibility.
 *
 * Returns 0 on success or error.
 */
int sysfs_update_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	return internal_create_group(kobj, 1, grp);
}
EXPORT_SYMBOL_GPL(sysfs_update_group);

/**
 * sysfs_remove_group: remove a group from a kobject
 * @kobj:	kobject to remove the group from
 * @grp:	group to remove
 *
 * This function removes a group of attributes from a kobject.  The attributes
 * previously have to have been created for this group, otherwise it will fail.
 */
void sysfs_remove_group(struct kobject *kobj,
			const struct attribute_group *grp)
{
	struct kernfs_node *parent = kobj->sd;
	struct kernfs_node *kn;

	if (grp->name) {
		kn = kernfs_find_and_get(parent, grp->name);
		if (!kn) {
			WARN(!kn, KERN_WARNING
			     "sysfs group %p not found for kobject '%s'\n",
			     grp, kobject_name(kobj));
			return;
		}
	} else {
		kn = parent;
		kernfs_get(kn);
	}

	remove_files(kn, kobj, grp);
	if (grp->name)
		kernfs_remove(kn);

	kernfs_put(kn);
}
EXPORT_SYMBOL_GPL(sysfs_remove_group);

/**
 * sysfs_remove_groups - remove a list of groups
 *
 * @kobj:	The kobject for the groups to be removed from
 * @groups:	NULL terminated list of groups to be removed
 *
 * If groups is not NULL, remove the specified groups from the kobject.
 */
void sysfs_remove_groups(struct kobject *kobj,
			 const struct attribute_group **groups)
{
	int i;

	if (!groups)
		return;
	for (i = 0; groups[i]; i++)
		sysfs_remove_group(kobj, groups[i]);
}
EXPORT_SYMBOL_GPL(sysfs_remove_groups);

/**
 * sysfs_merge_group - merge files into a pre-existing attribute group.
 * @kobj:	The kobject containing the group.
 * @grp:	The files to create and the attribute group they belong to.
 *
 * This function returns an error if the group doesn't exist or any of the
 * files already exist in that group, in which case none of the new files
 * are created.
 */
int sysfs_merge_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	struct kernfs_node *parent;
	int error = 0;
	struct attribute *const *attr;
	int i;

	parent = kernfs_find_and_get(kobj->sd, grp->name);
	if (!parent)
		return -ENOENT;

	for ((i = 0, attr = grp->attrs); *attr && !error; (++i, ++attr))
		error = sysfs_add_file(parent, *attr, false);
	if (error) {
		while (--i >= 0)
			kernfs_remove_by_name(parent, (*--attr)->name);
	}
	kernfs_put(parent);

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_merge_group);

/**
 * sysfs_unmerge_group - remove files from a pre-existing attribute group.
 * @kobj:	The kobject containing the group.
 * @grp:	The files to remove and the attribute group they belong to.
 */
void sysfs_unmerge_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	struct kernfs_node *parent;
	struct attribute *const *attr;

	parent = kernfs_find_and_get(kobj->sd, grp->name);
	if (parent) {
		for (attr = grp->attrs; *attr; ++attr)
			kernfs_remove_by_name(parent, (*attr)->name);
		kernfs_put(parent);
	}
}
EXPORT_SYMBOL_GPL(sysfs_unmerge_group);

/**
 * sysfs_add_link_to_group - add a symlink to an attribute group.
 * @kobj:	The kobject containing the group.
 * @group_name:	The name of the group.
 * @target:	The target kobject of the symlink to create.
 * @link_name:	The name of the symlink to create.
 */
int sysfs_add_link_to_group(struct kobject *kobj, const char *group_name,
			    struct kobject *target, const char *link_name)
{
	struct kernfs_node *parent;
	int error = 0;

	parent = kernfs_find_and_get(kobj->sd, group_name);
	if (!parent)
		return -ENOENT;

	error = sysfs_create_link_sd(parent, target, link_name);
	kernfs_put(parent);

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_add_link_to_group);

/**
 * sysfs_remove_link_from_group - remove a symlink from an attribute group.
 * @kobj:	The kobject containing the group.
 * @group_name:	The name of the group.
 * @link_name:	The name of the symlink to remove.
 */
void sysfs_remove_link_from_group(struct kobject *kobj, const char *group_name,
				  const char *link_name)
{
	struct kernfs_node *parent;

	parent = kernfs_find_and_get(kobj->sd, group_name);
	if (parent) {
		kernfs_remove_by_name(parent, link_name);
		kernfs_put(parent);
	}
}
EXPORT_SYMBOL_GPL(sysfs_remove_link_from_group);
