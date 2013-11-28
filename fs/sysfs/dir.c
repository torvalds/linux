/*
 * fs/sysfs/dir.c - sysfs core and dir operation implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include "sysfs.h"

DEFINE_SPINLOCK(sysfs_symlink_target_lock);

/**
 *	sysfs_pathname - return full path to sysfs dirent
 *	@sd: sysfs_dirent whose path we want
 *	@path: caller allocated buffer of size PATH_MAX
 *
 *	Gives the name "/" to the sysfs_root entry; any path returned
 *	is relative to wherever sysfs is mounted.
 */
static char *sysfs_pathname(struct sysfs_dirent *sd, char *path)
{
	if (sd->s_parent) {
		sysfs_pathname(sd->s_parent, path);
		strlcat(path, "/", PATH_MAX);
	}
	strlcat(path, sd->s_name, PATH_MAX);
	return path;
}

void sysfs_warn_dup(struct sysfs_dirent *parent, const char *name)
{
	char *path;

	path = kzalloc(PATH_MAX, GFP_KERNEL);
	if (path) {
		sysfs_pathname(parent, path);
		strlcat(path, "/", PATH_MAX);
		strlcat(path, name, PATH_MAX);
	}

	WARN(1, KERN_WARNING "sysfs: cannot create duplicate filename '%s'\n",
	     path ? path : name);

	kfree(path);
}

/**
 * sysfs_create_dir_ns - create a directory for an object with a namespace tag
 * @kobj: object we're creating directory for
 * @ns: the namespace tag to use
 */
int sysfs_create_dir_ns(struct kobject *kobj, const void *ns)
{
	struct sysfs_dirent *parent_sd, *sd;

	BUG_ON(!kobj);

	if (kobj->parent)
		parent_sd = kobj->parent->sd;
	else
		parent_sd = &sysfs_root;

	if (!parent_sd)
		return -ENOENT;

	sd = kernfs_create_dir_ns(parent_sd, kobject_name(kobj), kobj, ns);
	if (IS_ERR(sd)) {
		if (PTR_ERR(sd) == -EEXIST)
			sysfs_warn_dup(parent_sd, kobject_name(kobj));
		return PTR_ERR(sd);
	}

	kobj->sd = sd;
	return 0;
}

/**
 *	sysfs_remove_dir - remove an object's directory.
 *	@kobj:	object.
 *
 *	The only thing special about this is that we remove any files in
 *	the directory before we remove the directory, and we've inlined
 *	what used to be sysfs_rmdir() below, instead of calling separately.
 */
void sysfs_remove_dir(struct kobject *kobj)
{
	struct sysfs_dirent *sd = kobj->sd;

	/*
	 * In general, kboject owner is responsible for ensuring removal
	 * doesn't race with other operations and sysfs doesn't provide any
	 * protection; however, when @kobj is used as a symlink target, the
	 * symlinking entity usually doesn't own @kobj and thus has no
	 * control over removal.  @kobj->sd may be removed anytime and
	 * symlink code may end up dereferencing an already freed sd.
	 *
	 * sysfs_symlink_target_lock synchronizes @kobj->sd disassociation
	 * against symlink operations so that symlink code can safely
	 * dereference @kobj->sd.
	 */
	spin_lock(&sysfs_symlink_target_lock);
	kobj->sd = NULL;
	spin_unlock(&sysfs_symlink_target_lock);

	if (sd) {
		WARN_ON_ONCE(sysfs_type(sd) != SYSFS_DIR);
		kernfs_remove(sd);
	}
}

int sysfs_rename_dir_ns(struct kobject *kobj, const char *new_name,
			const void *new_ns)
{
	struct sysfs_dirent *parent_sd = kobj->sd->s_parent;

	return kernfs_rename_ns(kobj->sd, parent_sd, new_name, new_ns);
}

int sysfs_move_dir_ns(struct kobject *kobj, struct kobject *new_parent_kobj,
		      const void *new_ns)
{
	struct sysfs_dirent *sd = kobj->sd;
	struct sysfs_dirent *new_parent_sd;

	BUG_ON(!sd->s_parent);
	new_parent_sd = new_parent_kobj && new_parent_kobj->sd ?
		new_parent_kobj->sd : &sysfs_root;

	return kernfs_rename_ns(sd, new_parent_sd, sd->s_name, new_ns);
}
