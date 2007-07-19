/*
 * symlink.c - operations for sysfs symlinks.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <asm/semaphore.h>

#include "sysfs.h"

static int object_depth(struct sysfs_dirent *sd)
{
	int depth = 0;

	for (; sd->s_parent; sd = sd->s_parent)
		depth++;

	return depth;
}

static int object_path_length(struct sysfs_dirent * sd)
{
	int length = 1;

	for (; sd->s_parent; sd = sd->s_parent)
		length += strlen(sd->s_name) + 1;

	return length;
}

static void fill_object_path(struct sysfs_dirent *sd, char *buffer, int length)
{
	--length;
	for (; sd->s_parent; sd = sd->s_parent) {
		int cur = strlen(sd->s_name);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(buffer + length, sd->s_name, cur);
		*(buffer + --length) = '/';
	}
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject * kobj, struct kobject * target, const char * name)
{
	struct sysfs_dirent *parent_sd = NULL;
	struct sysfs_dirent *target_sd = NULL;
	struct sysfs_dirent *sd = NULL;
	struct sysfs_addrm_cxt acxt;
	int error;

	BUG_ON(!name);

	if (!kobj) {
		if (sysfs_mount && sysfs_mount->mnt_sb)
			parent_sd = sysfs_mount->mnt_sb->s_root->d_fsdata;
	} else
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
	sd->s_elem.symlink.target_sd = target_sd;

	sysfs_addrm_start(&acxt, parent_sd);

	if (!sysfs_find_dirent(parent_sd, name)) {
		sysfs_add_one(&acxt, sd);
		sysfs_link_sibling(sd);
	}

	if (sysfs_addrm_finish(&acxt))
		return 0;

	error = -EEXIST;
	/* fall through */
 out_put:
	sysfs_put(target_sd);
	sysfs_put(sd);
	return error;
}


/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */

void sysfs_remove_link(struct kobject * kobj, const char * name)
{
	sysfs_hash_and_remove(kobj->sd, name);
}

static int sysfs_get_target_path(struct sysfs_dirent * parent_sd,
				 struct sysfs_dirent * target_sd, char *path)
{
	char * s;
	int depth, size;

	depth = object_depth(parent_sd);
	size = object_path_length(target_sd) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;

	pr_debug("%s: depth = %d, size = %d\n", __FUNCTION__, depth, size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_object_path(target_sd, path, size);
	pr_debug("%s: path = '%s'\n", __FUNCTION__, path);

	return 0;
}

static int sysfs_getlink(struct dentry *dentry, char * path)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	struct sysfs_dirent *parent_sd = sd->s_parent;
	struct sysfs_dirent *target_sd = sd->s_elem.symlink.target_sd;
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
	if (page)
		error = sysfs_getlink(dentry, (char *) page); 
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
	.readlink = generic_readlink,
	.follow_link = sysfs_follow_link,
	.put_link = sysfs_put_link,
};


EXPORT_SYMBOL_GPL(sysfs_create_link);
EXPORT_SYMBOL_GPL(sysfs_remove_link);
