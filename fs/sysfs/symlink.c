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

static int sysfs_add_link(struct sysfs_dirent * parent_sd, const char * name,
			  struct sysfs_dirent * target_sd)
{
	struct sysfs_dirent * sd;

	sd = sysfs_new_dirent(name, S_IFLNK|S_IRWXUGO, SYSFS_KOBJ_LINK);
	if (!sd)
		return -ENOMEM;

	sd->s_elem.symlink.target_sd = target_sd;
	sysfs_attach_dirent(sd, parent_sd, NULL);
	return 0;
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject * kobj, struct kobject * target, const char * name)
{
	struct dentry *dentry = NULL;
	struct sysfs_dirent *parent_sd = NULL;
	struct sysfs_dirent *target_sd = NULL;
	int error = -EEXIST;

	BUG_ON(!name);

	if (!kobj) {
		if (sysfs_mount && sysfs_mount->mnt_sb)
			dentry = sysfs_mount->mnt_sb->s_root;
	} else
		dentry = kobj->dentry;

	if (!dentry)
		return -EFAULT;
	parent_sd = dentry->d_fsdata;

	/* target->dentry can go away beneath us but is protected with
	 * kobj_sysfs_assoc_lock.  Fetch target_sd from it.
	 */
	spin_lock(&kobj_sysfs_assoc_lock);
	if (target->dentry)
		target_sd = sysfs_get(target->dentry->d_fsdata);
	spin_unlock(&kobj_sysfs_assoc_lock);

	if (!target_sd)
		return -ENOENT;

	mutex_lock(&dentry->d_inode->i_mutex);
	if (!sysfs_dirent_exist(dentry->d_fsdata, name))
		error = sysfs_add_link(parent_sd, name, target_sd);
	mutex_unlock(&dentry->d_inode->i_mutex);

	if (error)
		sysfs_put(target_sd);

	return error;
}


/**
 *	sysfs_remove_link - remove symlink in object's directory.
 *	@kobj:	object we're acting for.
 *	@name:	name of the symlink to remove.
 */

void sysfs_remove_link(struct kobject * kobj, const char * name)
{
	sysfs_hash_and_remove(kobj->dentry,name);
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

	down_read(&sysfs_rename_sem);
	error = sysfs_get_target_path(parent_sd, target_sd, path);
	up_read(&sysfs_rename_sem);

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
