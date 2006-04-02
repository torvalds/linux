/*
 * symlink.c - operations for sysfs symlinks.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/namei.h>

#include "sysfs.h"

static int object_depth(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int depth = 0;
	do { depth++; } while ((p = p->parent));
	return depth;
}

static int object_path_length(struct kobject * kobj)
{
	struct kobject * p = kobj;
	int length = 1;
	do {
		length += strlen(kobject_name(p)) + 1;
		p = p->parent;
	} while (p);
	return length;
}

static void fill_object_path(struct kobject * kobj, char * buffer, int length)
{
	struct kobject * p;

	--length;
	for (p = kobj; p; p = p->parent) {
		int cur = strlen(kobject_name(p));

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(buffer + length,kobject_name(p),cur);
		*(buffer + --length) = '/';
	}
}

static int sysfs_add_link(struct dentry * parent, const char * name, struct kobject * target)
{
	struct sysfs_dirent * parent_sd = parent->d_fsdata;
	struct sysfs_symlink * sl;
	int error = 0;

	error = -ENOMEM;
	sl = kmalloc(sizeof(*sl), GFP_KERNEL);
	if (!sl)
		goto exit1;

	sl->link_name = kmalloc(strlen(name) + 1, GFP_KERNEL);
	if (!sl->link_name)
		goto exit2;

	strcpy(sl->link_name, name);
	sl->target_kobj = kobject_get(target);

	error = sysfs_make_dirent(parent_sd, NULL, sl, S_IFLNK|S_IRWXUGO,
				SYSFS_KOBJ_LINK);
	if (!error)
		return 0;

	kobject_put(target);
	kfree(sl->link_name);
exit2:
	kfree(sl);
exit1:
	return error;
}

/**
 *	sysfs_create_link - create symlink between two objects.
 *	@kobj:	object whose directory we're creating the link in.
 *	@target:	object we're pointing to.
 *	@name:		name of the symlink.
 */
int sysfs_create_link(struct kobject * kobj, struct kobject * target, const char * name)
{
	struct dentry * dentry = kobj->dentry;
	int error = -EEXIST;

	BUG_ON(!kobj || !kobj->dentry || !name);

	mutex_lock(&dentry->d_inode->i_mutex);
	if (!sysfs_dirent_exist(dentry->d_fsdata, name))
		error = sysfs_add_link(dentry, name, target);
	mutex_unlock(&dentry->d_inode->i_mutex);
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

static int sysfs_get_target_path(struct kobject * kobj, struct kobject * target,
				 char *path)
{
	char * s;
	int depth, size;

	depth = object_depth(kobj);
	size = object_path_length(target) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;

	pr_debug("%s: depth = %d, size = %d\n", __FUNCTION__, depth, size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_object_path(target, path, size);
	pr_debug("%s: path = '%s'\n", __FUNCTION__, path);

	return 0;
}

static int sysfs_getlink(struct dentry *dentry, char * path)
{
	struct kobject *kobj, *target_kobj;
	int error = 0;

	kobj = sysfs_get_kobject(dentry->d_parent);
	if (!kobj)
		return -EINVAL;

	target_kobj = sysfs_get_kobject(dentry);
	if (!target_kobj) {
		kobject_put(kobj);
		return -EINVAL;
	}

	down_read(&sysfs_rename_sem);
	error = sysfs_get_target_path(kobj, target_kobj, path);
	up_read(&sysfs_rename_sem);
	
	kobject_put(kobj);
	kobject_put(target_kobj);
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

struct inode_operations sysfs_symlink_inode_operations = {
	.readlink = generic_readlink,
	.follow_link = sysfs_follow_link,
	.put_link = sysfs_put_link,
};


EXPORT_SYMBOL_GPL(sysfs_create_link);
EXPORT_SYMBOL_GPL(sysfs_remove_link);
