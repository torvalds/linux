/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * symlink.c - operations for configfs symlinks.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/namei.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

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
		strncpy(buffer + length,config_item_name(p),cur);
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

	ret = -ENOMEM;
	sl = kmalloc(sizeof(struct configfs_symlink), GFP_KERNEL);
	if (sl) {
		sl->sl_target = config_item_get(item);
		/* FIXME: needs a lock, I'd bet */
		list_add(&sl->sl_list, &target_sd->s_links);
		ret = configfs_create_link(sl, parent_item->ci_dentry,
					   dentry);
		if (ret) {
			list_del_init(&sl->sl_list);
			config_item_put(item);
			kfree(sl);
		}
	}

	return ret;
}


static int get_target(const char *symname, struct nameidata *nd,
		      struct config_item **target)
{
	int ret;

	ret = path_lookup(symname, LOOKUP_FOLLOW|LOOKUP_DIRECTORY, nd);
	if (!ret) {
		if (nd->dentry->d_sb == configfs_sb) {
			*target = configfs_get_config_item(nd->dentry);
			if (!*target) {
				ret = -ENOENT;
				path_release(nd);
			}
		} else
			ret = -EPERM;
	}

	return ret;
}


int configfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	int ret;
	struct nameidata nd;
	struct config_item *parent_item;
	struct config_item *target_item;
	struct config_item_type *type;

	ret = -EPERM;  /* What lack-of-symlink returns */
	if (dentry->d_parent == configfs_sb->s_root)
		goto out;

	parent_item = configfs_get_config_item(dentry->d_parent);
	type = parent_item->ci_type;

	if (!type || !type->ct_item_ops ||
	    !type->ct_item_ops->allow_link)
		goto out_put;

	ret = get_target(symname, &nd, &target_item);
	if (ret)
		goto out_put;

	ret = type->ct_item_ops->allow_link(parent_item, target_item);
	if (!ret)
		ret = create_link(parent_item, target_item, dentry);

	config_item_put(target_item);
	path_release(&nd);

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
	struct config_item_type *type;
	int ret;

	ret = -EPERM;  /* What lack-of-symlink returns */
	if (!(sd->s_type & CONFIGFS_ITEM_LINK))
		goto out;

	BUG_ON(dentry->d_parent == configfs_sb->s_root);

	sl = sd->s_element;

	parent_item = configfs_get_config_item(dentry->d_parent);
	type = parent_item->ci_type;

	list_del_init(&sd->s_sibling);
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

	/* FIXME: Needs lock */
	list_del_init(&sl->sl_list);

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

	pr_debug("%s: depth = %d, size = %d\n", __FUNCTION__, depth, size);

	for (s = path; depth--; s += 3)
		strcpy(s,"../");

	fill_item_path(target, path, size);
	pr_debug("%s: path = '%s'\n", __FUNCTION__, path);

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

static void *configfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	int error = -ENOMEM;
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	if (page) {
		error = configfs_getlink(dentry, (char *)page);
		if (!error) {
			nd_set_link(nd, (char *)page);
			return (void *)page;
		}
	}

	nd_set_link(nd, ERR_PTR(error));
	return NULL;
}

static void configfs_put_link(struct dentry *dentry, struct nameidata *nd,
			      void *cookie)
{
	if (cookie) {
		unsigned long page = (unsigned long)cookie;
		free_page(page);
	}
}

struct inode_operations configfs_symlink_inode_operations = {
	.follow_link = configfs_follow_link,
	.readlink = generic_readlink,
	.put_link = configfs_put_link,
	.setattr = configfs_setattr,
};

