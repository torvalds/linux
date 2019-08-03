// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * symlink.c - operations for configfs symlinks.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

/* Protects attachments of new symlinks */
DEFINE_MUTEX(configfs_symlink_mutex);

static int item_depth(struct config_item * item)
{
	struct config_item * p = item;
	int depth = 0;
	do { depth++; } while ((p = p->ci_parent) && !configfs_is_root(p));
	return depth;
}

static int item_path_length(struct config_item * item)
{
	struct config_item * p = item;
	int length = 1;
	do {
		length += strlen(config_item_name(p)) + 1;
		p = p->ci_parent;
	} while (p && !configfs_is_root(p));
	return length;
}

static void fill_item_path(struct config_item * item, char * buffer, int length)
{
	struct config_item * p;

	--length;
	for (p = item; p && !configfs_is_root(p); p = p->ci_parent) {
		int cur = strlen(config_item_name(p));

		/* back up enough to print this bus id with '/' */
		length -= cur;
		memcpy(buffer + length, config_item_name(p), cur);
		*(buffer + --length) = '/';
	}
}

static int create_link(struct config_item *parent_item,
		       struct config_item *item,
		       struct dentry *dentry)
{
	struct configfs_dirent *target_sd = item->ci_dentry->d_fsdata;
	struct configfs_symlink *sl;
	int ret;

	ret = -ENOENT;
	if (!configfs_dirent_is_ready(target_sd))
		goto out;
	ret = -ENOMEM;
	sl = kmalloc(sizeof(struct configfs_symlink), GFP_KERNEL);
	if (sl) {
		spin_lock(&configfs_dirent_lock);
		if (target_sd->s_type & CONFIGFS_USET_DROPPING) {
			spin_unlock(&configfs_dirent_lock);
			kfree(sl);
			return -ENOENT;
		}
		sl->sl_target = config_item_get(item);
		list_add(&sl->sl_list, &target_sd->s_links);
		spin_unlock(&configfs_dirent_lock);
		ret = configfs_create_link(sl, parent_item->ci_dentry,
					   dentry);
		if (ret) {
			spin_lock(&configfs_dirent_lock);
			list_del_init(&sl->sl_list);
			spin_unlock(&configfs_dirent_lock);
			config_item_put(item);
			kfree(sl);
		}
	}

out:
	return ret;
}


static int get_target(const char *symname, struct path *path,
		      struct config_item **target, struct super_block *sb)
{
	int ret;

	ret = kern_path(symname, LOOKUP_FOLLOW|LOOKUP_DIRECTORY, path);
	if (!ret) {
		if (path->dentry->d_sb == sb) {
			*target = configfs_get_config_item(path->dentry);
			if (!*target) {
				ret = -ENOENT;
				path_put(path);
			}
		} else {
			ret = -EPERM;
			path_put(path);
		}
	}

	return ret;
}


int configfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int ret;
	struct path path;
	struct configfs_dirent *sd;
	struct config_item *parent_item;
	struct config_item *target_item = NULL;
	const struct config_item_type *type;

	sd = dentry->d_parent->d_fsdata;
	/*
	 * Fake invisibility if dir belongs to a group/default groups hierarchy
	 * being attached
	 */
	ret = -ENOENT;
	if (!configfs_dirent_is_ready(sd))
		goto out;

	parent_item = configfs_get_config_item(dentry->d_parent);
	type = parent_item->ci_type;

	ret = -EPERM;
	if (!type || !type->ct_item_ops ||
	    !type->ct_item_ops->allow_link)
		goto out_put;

	/*
	 * This is really sick.  What they wanted was a hybrid of
	 * link(2) and symlink(2) - they wanted the target resolved
	 * at syscall time (as link(2) would've done), be a directory
	 * (which link(2) would've refused to do) *AND* be a deep
	 * fucking magic, making the target busy from rmdir POV.
	 * symlink(2) is nothing of that sort, and the locking it
	 * gets matches the normal symlink(2) semantics.  Without
	 * attempts to resolve the target (which might very well
	 * not even exist yet) done prior to locking the parent
	 * directory.  This perversion, OTOH, needs to resolve
	 * the target, which would lead to obvious deadlocks if
	 * attempted with any directories locked.
	 *
	 * Unfortunately, that garbage is userland ABI and we should've
	 * said "no" back in 2005.  Too late now, so we get to
	 * play very ugly games with locking.
	 *
	 * Try *ANYTHING* of that sort in new code, and you will
	 * really regret it.  Just ask yourself - what could a BOFH
	 * do to me and do I want to find it out first-hand?
	 *
	 *  AV, a thoroughly annoyed bastard.
	 */
	inode_unlock(dir);
	ret = get_target(symname, &path, &target_item, dentry->d_sb);
	inode_lock(dir);
	if (ret)
		goto out_put;

	if (dentry->d_inode || d_unhashed(dentry))
		ret = -EEXIST;
	else
		ret = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (!ret)
		ret = type->ct_item_ops->allow_link(parent_item, target_item);
	if (!ret) {
		mutex_lock(&configfs_symlink_mutex);
		ret = create_link(parent_item, target_item, dentry);
		mutex_unlock(&configfs_symlink_mutex);
		if (ret && type->ct_item_ops->drop_link)
			type->ct_item_ops->drop_link(parent_item,
						     target_item);
	}

	config_item_put(target_item);
	path_put(&path);

out_put:
	config_item_put(parent_item);

out:
	return ret;
}

int configfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct configfs_dirent *sd = dentry->d_fsdata;
	struct configfs_symlink *sl;
	struct config_item *parent_item;
	const struct config_item_type *type;
	int ret;

	ret = -EPERM;  /* What lack-of-symlink returns */
	if (!(sd->s_type & CONFIGFS_ITEM_LINK))
		goto out;

	sl = sd->s_element;

	parent_item = configfs_get_config_item(dentry->d_parent);
	type = parent_item->ci_type;

	spin_lock(&configfs_dirent_lock);
	list_del_init(&sd->s_sibling);
	spin_unlock(&configfs_dirent_lock);
	configfs_drop_dentry(sd, dentry->d_parent);
	dput(dentry);
	configfs_put(sd);

	/*
	 * drop_link() must be called before
	 * list_del_init(&sl->sl_list), so that the order of
	 * drop_link(this, target) and drop_item(target) is preserved.
	 */
	if (type && type->ct_item_ops &&
	    type->ct_item_ops->drop_link)
		type->ct_item_ops->drop_link(parent_item,
					       sl->sl_target);

	spin_lock(&configfs_dirent_lock);
	list_del_init(&sl->sl_list);
	spin_unlock(&configfs_dirent_lock);

	/* Put reference from create_link() */
	config_item_put(sl->sl_target);
	kfree(sl);

	config_item_put(parent_item);

	ret = 0;

out:
	return ret;
}

static int configfs_get_target_path(struct config_item * item, struct config_item * target,
				   char *path)
{
	char * s;
	int depth, size;

	depth = item_depth(item);
	size = item_path_length(target) + depth * 3 - 1;
	if (size > PATH_MAX)
		return -ENAMETOOLONG;

	pr_debug("%s: depth = %d, size = %d\n", __func__, depth, size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_item_path(target, path, size);
	pr_debug("%s: path = '%s'\n", __func__, path);

	return 0;
}

static int configfs_getlink(struct dentry *dentry, char * path)
{
	struct config_item *item, *target_item;
	int error = 0;

	item = configfs_get_config_item(dentry->d_parent);
	if (!item)
		return -EINVAL;

	target_item = configfs_get_config_item(dentry);
	if (!target_item) {
		config_item_put(item);
		return -EINVAL;
	}

	down_read(&configfs_rename_sem);
	error = configfs_get_target_path(item, target_item, path);
	up_read(&configfs_rename_sem);

	config_item_put(item);
	config_item_put(target_item);
	return error;

}

static const char *configfs_get_link(struct dentry *dentry,
				     struct inode *inode,
				     struct delayed_call *done)
{
	char *body;
	int error;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	body = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!body)
		return ERR_PTR(-ENOMEM);

	error = configfs_getlink(dentry, body);
	if (!error) {
		set_delayed_call(done, kfree_link, body);
		return body;
	}

	kfree(body);
	return ERR_PTR(error);
}

const struct inode_operations configfs_symlink_inode_operations = {
	.get_link = configfs_get_link,
	.setattr = configfs_setattr,
};

