// SPDX-License-Identifier: GPL-2.0
/*
 * fs/sysfs/group.c - Operations for adding/removing multiple files at once.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2013 Greg Kroah-Hartman
 * Copyright (c) 2013 The Linux Foundation
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/err.h>
#include <linux/fs.h>
#include "sysfs.h"


static void remove_files(struct kernfs_node *parent,
			 const struct attribute_group *grp)
{
	struct attribute *const *attr;
	struct bin_attribute *const *bin_attr;

	if (grp->attrs)
		for (attr = grp->attrs; *attr; attr++)
			kernfs_remove_by_name(parent, (*attr)->name);
	if (grp->bin_attrs)
		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++)
			kernfs_remove_by_name(parent, (*bin_attr)->attr.name);
}

static umode_t __first_visible(const struct attribute_group *grp, struct kobject *kobj)
{
	if (grp->attrs && grp->attrs[0] && grp->is_visible)
		return grp->is_visible(kobj, grp->attrs[0], 0);

	if (grp->bin_attrs && grp->bin_attrs[0] && grp->is_bin_visible)
		return grp->is_bin_visible(kobj, grp->bin_attrs[0], 0);

	return 0;
}

static int create_files(struct kernfs_node *parent, struct kobject *kobj,
			kuid_t uid, kgid_t gid,
			const struct attribute_group *grp, int update)
{
	struct attribute *const *attr;
	struct bin_attribute *const *bin_attr;
	int error = 0, i;

	if (grp->attrs) {
		for (i = 0, attr = grp->attrs; *attr && !error; i++, attr++) {
			umode_t mode = (*attr)->mode;

			/*
			 * In update mode, we're changing the permissions or
			 * visibility.  Do this by first removing then
			 * re-adding (if required) the file.
			 */
			if (update)
				kernfs_remove_by_name(parent, (*attr)->name);
			if (grp->is_visible) {
				mode = grp->is_visible(kobj, *attr, i);
				mode &= ~SYSFS_GROUP_INVISIBLE;
				if (!mode)
					continue;
			}

			WARN(mode & ~(SYSFS_PREALLOC | 0664),
			     "Attribute %s: Invalid permissions 0%o\n",
			     (*attr)->name, mode);

			mode &= SYSFS_PREALLOC | 0664;
			error = sysfs_add_file_mode_ns(parent, *attr, mode, uid,
						       gid, NULL);
			if (unlikely(error))
				break;
		}
		if (error) {
			remove_files(parent, grp);
			goto exit;
		}
	}

	if (grp->bin_attrs) {
		for (i = 0, bin_attr = grp->bin_attrs; *bin_attr; i++, bin_attr++) {
			umode_t mode = (*bin_attr)->attr.mode;

			if (update)
				kernfs_remove_by_name(parent,
						(*bin_attr)->attr.name);
			if (grp->is_bin_visible) {
				mode = grp->is_bin_visible(kobj, *bin_attr, i);
				mode &= ~SYSFS_GROUP_INVISIBLE;
				if (!mode)
					continue;
			}

			WARN(mode & ~(SYSFS_PREALLOC | 0664),
			     "Attribute %s: Invalid permissions 0%o\n",
			     (*bin_attr)->attr.name, mode);

			mode &= SYSFS_PREALLOC | 0664;
			error = sysfs_add_bin_file_mode_ns(parent, *bin_attr,
							   mode, uid, gid,
							   NULL);
			if (error)
				break;
		}
		if (error)
			remove_files(parent, grp);
	}
exit:
	return error;
}


static int internal_create_group(struct kobject *kobj, int update,
				 const struct attribute_group *grp)
{
	struct kernfs_node *kn;
	kuid_t uid;
	kgid_t gid;
	int error;

	if (WARN_ON(!kobj || (!update && !kobj->sd)))
		return -EINVAL;

	/* Updates may happen before the object has been instantiated */
	if (unlikely(update && !kobj->sd))
		return -EINVAL;

	if (!grp->attrs && !grp->bin_attrs) {
		pr_debug("sysfs: (bin_)attrs not set by subsystem for group: %s/%s, skipping\n",
			 kobj->name, grp->name ?: "");
		return 0;
	}

	kobject_get_ownership(kobj, &uid, &gid);
	if (grp->name) {
		umode_t mode = __first_visible(grp, kobj);

		if (mode & SYSFS_GROUP_INVISIBLE)
			mode = 0;
		else
			mode = S_IRWXU | S_IRUGO | S_IXUGO;

		if (update) {
			kn = kernfs_find_and_get(kobj->sd, grp->name);
			if (!kn) {
				pr_debug("attr grp %s/%s not created yet\n",
					 kobj->name, grp->name);
				/* may have been invisible prior to this update */
				update = 0;
			} else if (!mode) {
				sysfs_remove_group(kobj, grp);
				kernfs_put(kn);
				return 0;
			}
		}

		if (!update) {
			if (!mode)
				return 0;
			kn = kernfs_create_dir_ns(kobj->sd, grp->name, mode,
						  uid, gid, kobj, NULL);
			if (IS_ERR(kn)) {
				if (PTR_ERR(kn) == -EEXIST)
					sysfs_warn_dup(kobj->sd, grp->name);
				return PTR_ERR(kn);
			}
		}
	} else {
		kn = kobj->sd;
	}

	kernfs_get(kn);
	error = create_files(kn, kobj, uid, gid, grp, update);
	if (error) {
		if (grp->name)
			kernfs_remove(kn);
	}
	kernfs_put(kn);

	if (grp->name && update)
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
 * Returns 0 on success or error code on failure.
 */
int sysfs_create_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	return internal_create_group(kobj, 0, grp);
}
EXPORT_SYMBOL_GPL(sysfs_create_group);

static int internal_create_groups(struct kobject *kobj, int update,
				  const struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (!groups)
		return 0;

	for (i = 0; groups[i]; i++) {
		error = internal_create_group(kobj, update, groups[i]);
		if (error) {
			while (--i >= 0)
				sysfs_remove_group(kobj, groups[i]);
			break;
		}
	}
	return error;
}

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
 * Returns 0 on success or error code from sysfs_create_group on failure.
 */
int sysfs_create_groups(struct kobject *kobj,
			const struct attribute_group **groups)
{
	return internal_create_groups(kobj, 0, groups);
}
EXPORT_SYMBOL_GPL(sysfs_create_groups);

/**
 * sysfs_update_groups - given a directory kobject, create a bunch of attribute groups
 * @kobj:	The kobject to update the group on
 * @groups:	The attribute groups to update, NULL terminated
 *
 * This function update a bunch of attribute groups.  If an error occurs when
 * updating a group, all previously updated groups will be removed together
 * with already existing (not updated) attributes.
 *
 * Returns 0 on success or error code from sysfs_update_group on failure.
 */
int sysfs_update_groups(struct kobject *kobj,
			const struct attribute_group **groups)
{
	return internal_create_groups(kobj, 1, groups);
}
EXPORT_SYMBOL_GPL(sysfs_update_groups);

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
 * relevant files. Changing a group's name (subdirectory name under
 * kobj's directory in sysfs) is not allowed.
 *
 * The primary use for this function is to call it after making a change
 * that affects group visibility.
 *
 * Returns 0 on success or error code on failure.
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
			pr_debug("sysfs group '%s' not found for kobject '%s'\n",
				 grp->name, kobject_name(kobj));
			return;
		}
	} else {
		kn = parent;
		kernfs_get(kn);
	}

	remove_files(kn, grp);
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
 * sysfs_merge_group - merge files into a pre-existing named attribute group.
 * @kobj:	The kobject containing the group.
 * @grp:	The files to create and the attribute group they belong to.
 *
 * This function returns an error if the group doesn't exist, the .name field is
 * NULL or any of the files already exist in that group, in which case none of
 * the new files are created.
 */
int sysfs_merge_group(struct kobject *kobj,
		       const struct attribute_group *grp)
{
	struct kernfs_node *parent;
	kuid_t uid;
	kgid_t gid;
	int error = 0;
	struct attribute *const *attr;
	int i;

	parent = kernfs_find_and_get(kobj->sd, grp->name);
	if (!parent)
		return -ENOENT;

	kobject_get_ownership(kobj, &uid, &gid);

	for ((i = 0, attr = grp->attrs); *attr && !error; (++i, ++attr))
		error = sysfs_add_file_mode_ns(parent, *attr, (*attr)->mode,
					       uid, gid, NULL);
	if (error) {
		while (--i >= 0)
			kernfs_remove_by_name(parent, (*--attr)->name);
	}
	kernfs_put(parent);

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_merge_group);

/**
 * sysfs_unmerge_group - remove files from a pre-existing named attribute group.
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

/**
 * compat_only_sysfs_link_entry_to_kobj - add a symlink to a kobject pointing
 * to a group or an attribute
 * @kobj:		The kobject containing the group.
 * @target_kobj:	The target kobject.
 * @target_name:	The name of the target group or attribute.
 * @symlink_name:	The name of the symlink file (target_name will be
 *			considered if symlink_name is NULL).
 */
int compat_only_sysfs_link_entry_to_kobj(struct kobject *kobj,
					 struct kobject *target_kobj,
					 const char *target_name,
					 const char *symlink_name)
{
	struct kernfs_node *target;
	struct kernfs_node *entry;
	struct kernfs_node *link;

	/*
	 * We don't own @target_kobj and it may be removed at any time.
	 * Synchronize using sysfs_symlink_target_lock. See sysfs_remove_dir()
	 * for details.
	 */
	spin_lock(&sysfs_symlink_target_lock);
	target = target_kobj->sd;
	if (target)
		kernfs_get(target);
	spin_unlock(&sysfs_symlink_target_lock);
	if (!target)
		return -ENOENT;

	entry = kernfs_find_and_get(target, target_name);
	if (!entry) {
		kernfs_put(target);
		return -ENOENT;
	}

	if (!symlink_name)
		symlink_name = target_name;

	link = kernfs_create_link(kobj->sd, symlink_name, entry);
	if (PTR_ERR(link) == -EEXIST)
		sysfs_warn_dup(kobj->sd, symlink_name);

	kernfs_put(entry);
	kernfs_put(target);
	return PTR_ERR_OR_ZERO(link);
}
EXPORT_SYMBOL_GPL(compat_only_sysfs_link_entry_to_kobj);

static int sysfs_group_attrs_change_owner(struct kernfs_node *grp_kn,
					  const struct attribute_group *grp,
					  struct iattr *newattrs)
{
	struct kernfs_node *kn;
	int error;

	if (grp->attrs) {
		struct attribute *const *attr;

		for (attr = grp->attrs; *attr; attr++) {
			kn = kernfs_find_and_get(grp_kn, (*attr)->name);
			if (!kn)
				return -ENOENT;

			error = kernfs_setattr(kn, newattrs);
			kernfs_put(kn);
			if (error)
				return error;
		}
	}

	if (grp->bin_attrs) {
		struct bin_attribute *const *bin_attr;

		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++) {
			kn = kernfs_find_and_get(grp_kn, (*bin_attr)->attr.name);
			if (!kn)
				return -ENOENT;

			error = kernfs_setattr(kn, newattrs);
			kernfs_put(kn);
			if (error)
				return error;
		}
	}

	return 0;
}

/**
 * sysfs_group_change_owner - change owner of an attribute group.
 * @kobj:	The kobject containing the group.
 * @grp:	The attribute group.
 * @kuid:	new owner's kuid
 * @kgid:	new owner's kgid
 *
 * Returns 0 on success or error code on failure.
 */
int sysfs_group_change_owner(struct kobject *kobj,
			     const struct attribute_group *grp, kuid_t kuid,
			     kgid_t kgid)
{
	struct kernfs_node *grp_kn;
	int error;
	struct iattr newattrs = {
		.ia_valid = ATTR_UID | ATTR_GID,
		.ia_uid = kuid,
		.ia_gid = kgid,
	};

	if (!kobj->state_in_sysfs)
		return -EINVAL;

	if (grp->name) {
		grp_kn = kernfs_find_and_get(kobj->sd, grp->name);
	} else {
		kernfs_get(kobj->sd);
		grp_kn = kobj->sd;
	}
	if (!grp_kn)
		return -ENOENT;

	error = kernfs_setattr(grp_kn, &newattrs);
	if (!error)
		error = sysfs_group_attrs_change_owner(grp_kn, grp, &newattrs);

	kernfs_put(grp_kn);

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_group_change_owner);

/**
 * sysfs_groups_change_owner - change owner of a set of attribute groups.
 * @kobj:	The kobject containing the groups.
 * @groups:	The attribute groups.
 * @kuid:	new owner's kuid
 * @kgid:	new owner's kgid
 *
 * Returns 0 on success or error code on failure.
 */
int sysfs_groups_change_owner(struct kobject *kobj,
			      const struct attribute_group **groups,
			      kuid_t kuid, kgid_t kgid)
{
	int error = 0, i;

	if (!kobj->state_in_sysfs)
		return -EINVAL;

	if (!groups)
		return 0;

	for (i = 0; groups[i]; i++) {
		error = sysfs_group_change_owner(kobj, groups[i], kuid, kgid);
		if (error)
			break;
	}

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_groups_change_owner);
