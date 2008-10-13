/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.c - Operations for configfs directories.
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

#undef DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

DECLARE_RWSEM(configfs_rename_sem);
/*
 * Protects mutations of configfs_dirent linkage together with proper i_mutex
 * Also protects mutations of symlinks linkage to target configfs_dirent
 * Mutators of configfs_dirent linkage must *both* have the proper inode locked
 * and configfs_dirent_lock locked, in that order.
 * This allows one to safely traverse configfs_dirent trees and symlinks without
 * having to lock inodes.
 *
 * Protects setting of CONFIGFS_USET_DROPPING: checking the flag
 * unlocked is not reliable unless in detach_groups() called from
 * rmdir()/unregister() and from configfs_attach_group()
 */
DEFINE_SPINLOCK(configfs_dirent_lock);

static void configfs_d_iput(struct dentry * dentry,
			    struct inode * inode)
{
	struct configfs_dirent * sd = dentry->d_fsdata;

	if (sd) {
		BUG_ON(sd->s_dentry != dentry);
		sd->s_dentry = NULL;
		configfs_put(sd);
	}
	iput(inode);
}

/*
 * We _must_ delete our dentries on last dput, as the chain-to-parent
 * behavior is required to clear the parents of default_groups.
 */
static int configfs_d_delete(struct dentry *dentry)
{
	return 1;
}

static struct dentry_operations configfs_dentry_ops = {
	.d_iput		= configfs_d_iput,
	/* simple_delete_dentry() isn't exported */
	.d_delete	= configfs_d_delete,
};

/*
 * Allocates a new configfs_dirent and links it to the parent configfs_dirent
 */
static struct configfs_dirent *configfs_new_dirent(struct configfs_dirent * parent_sd,
						void * element)
{
	struct configfs_dirent * sd;

	sd = kmem_cache_zalloc(configfs_dir_cachep, GFP_KERNEL);
	if (!sd)
		return ERR_PTR(-ENOMEM);

	atomic_set(&sd->s_count, 1);
	INIT_LIST_HEAD(&sd->s_links);
	INIT_LIST_HEAD(&sd->s_children);
	sd->s_element = element;
	spin_lock(&configfs_dirent_lock);
	if (parent_sd->s_type & CONFIGFS_USET_DROPPING) {
		spin_unlock(&configfs_dirent_lock);
		kmem_cache_free(configfs_dir_cachep, sd);
		return ERR_PTR(-ENOENT);
	}
	list_add(&sd->s_sibling, &parent_sd->s_children);
	spin_unlock(&configfs_dirent_lock);

	return sd;
}

/*
 *
 * Return -EEXIST if there is already a configfs element with the same
 * name for the same parent.
 *
 * called with parent inode's i_mutex held
 */
static int configfs_dirent_exists(struct configfs_dirent *parent_sd,
				  const unsigned char *new)
{
	struct configfs_dirent * sd;

	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (sd->s_element) {
			const unsigned char *existing = configfs_get_name(sd);
			if (strcmp(existing, new))
				continue;
			else
				return -EEXIST;
		}
	}

	return 0;
}


int configfs_make_dirent(struct configfs_dirent * parent_sd,
			 struct dentry * dentry, void * element,
			 umode_t mode, int type)
{
	struct configfs_dirent * sd;

	sd = configfs_new_dirent(parent_sd, element);
	if (IS_ERR(sd))
		return PTR_ERR(sd);

	sd->s_mode = mode;
	sd->s_type = type;
	sd->s_dentry = dentry;
	if (dentry) {
		dentry->d_fsdata = configfs_get(sd);
		dentry->d_op = &configfs_dentry_ops;
	}

	return 0;
}

static int init_dir(struct inode * inode)
{
	inode->i_op = &configfs_dir_inode_operations;
	inode->i_fop = &configfs_dir_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	return 0;
}

static int configfs_init_file(struct inode * inode)
{
	inode->i_size = PAGE_SIZE;
	inode->i_fop = &configfs_file_operations;
	return 0;
}

static int init_symlink(struct inode * inode)
{
	inode->i_op = &configfs_symlink_inode_operations;
	return 0;
}

static int create_dir(struct config_item * k, struct dentry * p,
		      struct dentry * d)
{
	int error;
	umode_t mode = S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO;

	error = configfs_dirent_exists(p->d_fsdata, d->d_name.name);
	if (!error)
		error = configfs_make_dirent(p->d_fsdata, d, k, mode,
					     CONFIGFS_DIR | CONFIGFS_USET_CREATING);
	if (!error) {
		error = configfs_create(d, mode, init_dir);
		if (!error) {
			inc_nlink(p->d_inode);
			(d)->d_op = &configfs_dentry_ops;
		} else {
			struct configfs_dirent *sd = d->d_fsdata;
			if (sd) {
				spin_lock(&configfs_dirent_lock);
				list_del_init(&sd->s_sibling);
				spin_unlock(&configfs_dirent_lock);
				configfs_put(sd);
			}
		}
	}
	return error;
}


/**
 *	configfs_create_dir - create a directory for an config_item.
 *	@item:		config_itemwe're creating directory for.
 *	@dentry:	config_item's dentry.
 *
 *	Note: user-created entries won't be allowed under this new directory
 *	until it is validated by configfs_dir_set_ready()
 */

static int configfs_create_dir(struct config_item * item, struct dentry *dentry)
{
	struct dentry * parent;
	int error = 0;

	BUG_ON(!item);

	if (item->ci_parent)
		parent = item->ci_parent->ci_dentry;
	else if (configfs_mount && configfs_mount->mnt_sb)
		parent = configfs_mount->mnt_sb->s_root;
	else
		return -EFAULT;

	error = create_dir(item,parent,dentry);
	if (!error)
		item->ci_dentry = dentry;
	return error;
}

/*
 * Allow userspace to create new entries under a new directory created with
 * configfs_create_dir(), and under all of its chidlren directories recursively.
 * @sd		configfs_dirent of the new directory to validate
 *
 * Caller must hold configfs_dirent_lock.
 */
static void configfs_dir_set_ready(struct configfs_dirent *sd)
{
	struct configfs_dirent *child_sd;

	sd->s_type &= ~CONFIGFS_USET_CREATING;
	list_for_each_entry(child_sd, &sd->s_children, s_sibling)
		if (child_sd->s_type & CONFIGFS_USET_CREATING)
			configfs_dir_set_ready(child_sd);
}

/*
 * Check that a directory does not belong to a directory hierarchy being
 * attached and not validated yet.
 * @sd		configfs_dirent of the directory to check
 *
 * @return	non-zero iff the directory was validated
 *
 * Note: takes configfs_dirent_lock, so the result may change from false to true
 * in two consecutive calls, but never from true to false.
 */
int configfs_dirent_is_ready(struct configfs_dirent *sd)
{
	int ret;

	spin_lock(&configfs_dirent_lock);
	ret = !(sd->s_type & CONFIGFS_USET_CREATING);
	spin_unlock(&configfs_dirent_lock);

	return ret;
}

int configfs_create_link(struct configfs_symlink *sl,
			 struct dentry *parent,
			 struct dentry *dentry)
{
	int err = 0;
	umode_t mode = S_IFLNK | S_IRWXUGO;

	err = configfs_make_dirent(parent->d_fsdata, dentry, sl, mode,
				   CONFIGFS_ITEM_LINK);
	if (!err) {
		err = configfs_create(dentry, mode, init_symlink);
		if (!err)
			dentry->d_op = &configfs_dentry_ops;
		else {
			struct configfs_dirent *sd = dentry->d_fsdata;
			if (sd) {
				spin_lock(&configfs_dirent_lock);
				list_del_init(&sd->s_sibling);
				spin_unlock(&configfs_dirent_lock);
				configfs_put(sd);
			}
		}
	}
	return err;
}

static void remove_dir(struct dentry * d)
{
	struct dentry * parent = dget(d->d_parent);
	struct configfs_dirent * sd;

	sd = d->d_fsdata;
	spin_lock(&configfs_dirent_lock);
	list_del_init(&sd->s_sibling);
	spin_unlock(&configfs_dirent_lock);
	configfs_put(sd);
	if (d->d_inode)
		simple_rmdir(parent->d_inode,d);

	pr_debug(" o %s removing done (%d)\n",d->d_name.name,
		 atomic_read(&d->d_count));

	dput(parent);
}

/**
 * configfs_remove_dir - remove an config_item's directory.
 * @item:	config_item we're removing.
 *
 * The only thing special about this is that we remove any files in
 * the directory before we remove the directory, and we've inlined
 * what used to be configfs_rmdir() below, instead of calling separately.
 *
 * Caller holds the mutex of the item's inode
 */

static void configfs_remove_dir(struct config_item * item)
{
	struct dentry * dentry = dget(item->ci_dentry);

	if (!dentry)
		return;

	remove_dir(dentry);
	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
}


/* attaches attribute's configfs_dirent to the dentry corresponding to the
 * attribute file
 */
static int configfs_attach_attr(struct configfs_dirent * sd, struct dentry * dentry)
{
	struct configfs_attribute * attr = sd->s_element;
	int error;

	dentry->d_fsdata = configfs_get(sd);
	sd->s_dentry = dentry;
	error = configfs_create(dentry, (attr->ca_mode & S_IALLUGO) | S_IFREG,
				configfs_init_file);
	if (error) {
		configfs_put(sd);
		return error;
	}

	dentry->d_op = &configfs_dentry_ops;
	d_rehash(dentry);

	return 0;
}

static struct dentry * configfs_lookup(struct inode *dir,
				       struct dentry *dentry,
				       struct nameidata *nd)
{
	struct configfs_dirent * parent_sd = dentry->d_parent->d_fsdata;
	struct configfs_dirent * sd;
	int found = 0;
	int err;

	/*
	 * Fake invisibility if dir belongs to a group/default groups hierarchy
	 * being attached
	 *
	 * This forbids userspace to read/write attributes of items which may
	 * not complete their initialization, since the dentries of the
	 * attributes won't be instantiated.
	 */
	err = -ENOENT;
	if (!configfs_dirent_is_ready(parent_sd))
		goto out;

	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (sd->s_type & CONFIGFS_NOT_PINNED) {
			const unsigned char * name = configfs_get_name(sd);

			if (strcmp(name, dentry->d_name.name))
				continue;

			found = 1;
			err = configfs_attach_attr(sd, dentry);
			break;
		}
	}

	if (!found) {
		/*
		 * If it doesn't exist and it isn't a NOT_PINNED item,
		 * it must be negative.
		 */
		return simple_lookup(dir, dentry, nd);
	}

out:
	return ERR_PTR(err);
}

/*
 * Only subdirectories count here.  Files (CONFIGFS_NOT_PINNED) are
 * attributes and are removed by rmdir().  We recurse, setting
 * CONFIGFS_USET_DROPPING on all children that are candidates for
 * default detach.
 * If there is an error, the caller will reset the flags via
 * configfs_detach_rollback().
 */
static int configfs_detach_prep(struct dentry *dentry, struct mutex **wait_mutex)
{
	struct configfs_dirent *parent_sd = dentry->d_fsdata;
	struct configfs_dirent *sd;
	int ret;

	/* Mark that we're trying to drop the group */
	parent_sd->s_type |= CONFIGFS_USET_DROPPING;

	ret = -EBUSY;
	if (!list_empty(&parent_sd->s_links))
		goto out;

	ret = 0;
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element ||
		    (sd->s_type & CONFIGFS_NOT_PINNED))
			continue;
		if (sd->s_type & CONFIGFS_USET_DEFAULT) {
			/* Abort if racing with mkdir() */
			if (sd->s_type & CONFIGFS_USET_IN_MKDIR) {
				if (wait_mutex)
					*wait_mutex = &sd->s_dentry->d_inode->i_mutex;
				return -EAGAIN;
			}

			/*
			 * Yup, recursive.  If there's a problem, blame
			 * deep nesting of default_groups
			 */
			ret = configfs_detach_prep(sd->s_dentry, wait_mutex);
			if (!ret)
				continue;
		} else
			ret = -ENOTEMPTY;

		break;
	}

out:
	return ret;
}

/*
 * Walk the tree, resetting CONFIGFS_USET_DROPPING wherever it was
 * set.
 */
static void configfs_detach_rollback(struct dentry *dentry)
{
	struct configfs_dirent *parent_sd = dentry->d_fsdata;
	struct configfs_dirent *sd;

	parent_sd->s_type &= ~CONFIGFS_USET_DROPPING;

	list_for_each_entry(sd, &parent_sd->s_children, s_sibling)
		if (sd->s_type & CONFIGFS_USET_DEFAULT)
			configfs_detach_rollback(sd->s_dentry);
}

static void detach_attrs(struct config_item * item)
{
	struct dentry * dentry = dget(item->ci_dentry);
	struct configfs_dirent * parent_sd;
	struct configfs_dirent * sd, * tmp;

	if (!dentry)
		return;

	pr_debug("configfs %s: dropping attrs for  dir\n",
		 dentry->d_name.name);

	parent_sd = dentry->d_fsdata;
	list_for_each_entry_safe(sd, tmp, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element || !(sd->s_type & CONFIGFS_NOT_PINNED))
			continue;
		spin_lock(&configfs_dirent_lock);
		list_del_init(&sd->s_sibling);
		spin_unlock(&configfs_dirent_lock);
		configfs_drop_dentry(sd, dentry);
		configfs_put(sd);
	}

	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
}

static int populate_attrs(struct config_item *item)
{
	struct config_item_type *t = item->ci_type;
	struct configfs_attribute *attr;
	int error = 0;
	int i;

	if (!t)
		return -EINVAL;
	if (t->ct_attrs) {
		for (i = 0; (attr = t->ct_attrs[i]) != NULL; i++) {
			if ((error = configfs_create_file(item, attr)))
				break;
		}
	}

	if (error)
		detach_attrs(item);

	return error;
}

static int configfs_attach_group(struct config_item *parent_item,
				 struct config_item *item,
				 struct dentry *dentry);
static void configfs_detach_group(struct config_item *item);

static void detach_groups(struct config_group *group)
{
	struct dentry * dentry = dget(group->cg_item.ci_dentry);
	struct dentry *child;
	struct configfs_dirent *parent_sd;
	struct configfs_dirent *sd, *tmp;

	if (!dentry)
		return;

	parent_sd = dentry->d_fsdata;
	list_for_each_entry_safe(sd, tmp, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element ||
		    !(sd->s_type & CONFIGFS_USET_DEFAULT))
			continue;

		child = sd->s_dentry;

		mutex_lock(&child->d_inode->i_mutex);

		configfs_detach_group(sd->s_element);
		child->d_inode->i_flags |= S_DEAD;

		mutex_unlock(&child->d_inode->i_mutex);

		d_delete(child);
		dput(child);
	}

	/**
	 * Drop reference from dget() on entrance.
	 */
	dput(dentry);
}

/*
 * This fakes mkdir(2) on a default_groups[] entry.  It
 * creates a dentry, attachs it, and then does fixup
 * on the sd->s_type.
 *
 * We could, perhaps, tweak our parent's ->mkdir for a minute and
 * try using vfs_mkdir.  Just a thought.
 */
static int create_default_group(struct config_group *parent_group,
				struct config_group *group)
{
	int ret;
	struct qstr name;
	struct configfs_dirent *sd;
	/* We trust the caller holds a reference to parent */
	struct dentry *child, *parent = parent_group->cg_item.ci_dentry;

	if (!group->cg_item.ci_name)
		group->cg_item.ci_name = group->cg_item.ci_namebuf;
	name.name = group->cg_item.ci_name;
	name.len = strlen(name.name);
	name.hash = full_name_hash(name.name, name.len);

	ret = -ENOMEM;
	child = d_alloc(parent, &name);
	if (child) {
		d_add(child, NULL);

		ret = configfs_attach_group(&parent_group->cg_item,
					    &group->cg_item, child);
		if (!ret) {
			sd = child->d_fsdata;
			sd->s_type |= CONFIGFS_USET_DEFAULT;
		} else {
			d_delete(child);
			dput(child);
		}
	}

	return ret;
}

static int populate_groups(struct config_group *group)
{
	struct config_group *new_group;
	int ret = 0;
	int i;

	if (group->default_groups) {
		for (i = 0; group->default_groups[i]; i++) {
			new_group = group->default_groups[i];

			ret = create_default_group(group, new_group);
			if (ret) {
				detach_groups(group);
				break;
			}
		}
	}

	return ret;
}

/*
 * All of link_obj/unlink_obj/link_group/unlink_group require that
 * subsys->su_mutex is held.
 */

static void unlink_obj(struct config_item *item)
{
	struct config_group *group;

	group = item->ci_group;
	if (group) {
		list_del_init(&item->ci_entry);

		item->ci_group = NULL;
		item->ci_parent = NULL;

		/* Drop the reference for ci_entry */
		config_item_put(item);

		/* Drop the reference for ci_parent */
		config_group_put(group);
	}
}

static void link_obj(struct config_item *parent_item, struct config_item *item)
{
	/*
	 * Parent seems redundant with group, but it makes certain
	 * traversals much nicer.
	 */
	item->ci_parent = parent_item;

	/*
	 * We hold a reference on the parent for the child's ci_parent
	 * link.
	 */
	item->ci_group = config_group_get(to_config_group(parent_item));
	list_add_tail(&item->ci_entry, &item->ci_group->cg_children);

	/*
	 * We hold a reference on the child for ci_entry on the parent's
	 * cg_children
	 */
	config_item_get(item);
}

static void unlink_group(struct config_group *group)
{
	int i;
	struct config_group *new_group;

	if (group->default_groups) {
		for (i = 0; group->default_groups[i]; i++) {
			new_group = group->default_groups[i];
			unlink_group(new_group);
		}
	}

	group->cg_subsys = NULL;
	unlink_obj(&group->cg_item);
}

static void link_group(struct config_group *parent_group, struct config_group *group)
{
	int i;
	struct config_group *new_group;
	struct configfs_subsystem *subsys = NULL; /* gcc is a turd */

	link_obj(&parent_group->cg_item, &group->cg_item);

	if (parent_group->cg_subsys)
		subsys = parent_group->cg_subsys;
	else if (configfs_is_root(&parent_group->cg_item))
		subsys = to_configfs_subsystem(group);
	else
		BUG();
	group->cg_subsys = subsys;

	if (group->default_groups) {
		for (i = 0; group->default_groups[i]; i++) {
			new_group = group->default_groups[i];
			link_group(group, new_group);
		}
	}
}

/*
 * The goal is that configfs_attach_item() (and
 * configfs_attach_group()) can be called from either the VFS or this
 * module.  That is, they assume that the items have been created,
 * the dentry allocated, and the dcache is all ready to go.
 *
 * If they fail, they must clean up after themselves as if they
 * had never been called.  The caller (VFS or local function) will
 * handle cleaning up the dcache bits.
 *
 * configfs_detach_group() and configfs_detach_item() behave similarly on
 * the way out.  They assume that the proper semaphores are held, they
 * clean up the configfs items, and they expect their callers will
 * handle the dcache bits.
 */
static int configfs_attach_item(struct config_item *parent_item,
				struct config_item *item,
				struct dentry *dentry)
{
	int ret;

	ret = configfs_create_dir(item, dentry);
	if (!ret) {
		ret = populate_attrs(item);
		if (ret) {
			/*
			 * We are going to remove an inode and its dentry but
			 * the VFS may already have hit and used them. Thus,
			 * we must lock them as rmdir() would.
			 */
			mutex_lock(&dentry->d_inode->i_mutex);
			configfs_remove_dir(item);
			dentry->d_inode->i_flags |= S_DEAD;
			mutex_unlock(&dentry->d_inode->i_mutex);
			d_delete(dentry);
		}
	}

	return ret;
}

/* Caller holds the mutex of the item's inode */
static void configfs_detach_item(struct config_item *item)
{
	detach_attrs(item);
	configfs_remove_dir(item);
}

static int configfs_attach_group(struct config_item *parent_item,
				 struct config_item *item,
				 struct dentry *dentry)
{
	int ret;
	struct configfs_dirent *sd;

	ret = configfs_attach_item(parent_item, item, dentry);
	if (!ret) {
		sd = dentry->d_fsdata;
		sd->s_type |= CONFIGFS_USET_DIR;

		/*
		 * FYI, we're faking mkdir in populate_groups()
		 * We must lock the group's inode to avoid races with the VFS
		 * which can already hit the inode and try to add/remove entries
		 * under it.
		 *
		 * We must also lock the inode to remove it safely in case of
		 * error, as rmdir() would.
		 */
		mutex_lock_nested(&dentry->d_inode->i_mutex, I_MUTEX_CHILD);
		ret = populate_groups(to_config_group(item));
		if (ret) {
			configfs_detach_item(item);
			dentry->d_inode->i_flags |= S_DEAD;
		}
		mutex_unlock(&dentry->d_inode->i_mutex);
		if (ret)
			d_delete(dentry);
	}

	return ret;
}

/* Caller holds the mutex of the group's inode */
static void configfs_detach_group(struct config_item *item)
{
	detach_groups(to_config_group(item));
	configfs_detach_item(item);
}

/*
 * After the item has been detached from the filesystem view, we are
 * ready to tear it out of the hierarchy.  Notify the client before
 * we do that so they can perform any cleanup that requires
 * navigating the hierarchy.  A client does not need to provide this
 * callback.  The subsystem semaphore MUST be held by the caller, and
 * references must be valid for both items.  It also assumes the
 * caller has validated ci_type.
 */
static void client_disconnect_notify(struct config_item *parent_item,
				     struct config_item *item)
{
	struct config_item_type *type;

	type = parent_item->ci_type;
	BUG_ON(!type);

	if (type->ct_group_ops && type->ct_group_ops->disconnect_notify)
		type->ct_group_ops->disconnect_notify(to_config_group(parent_item),
						      item);
}

/*
 * Drop the initial reference from make_item()/make_group()
 * This function assumes that reference is held on item
 * and that item holds a valid reference to the parent.  Also, it
 * assumes the caller has validated ci_type.
 */
static void client_drop_item(struct config_item *parent_item,
			     struct config_item *item)
{
	struct config_item_type *type;

	type = parent_item->ci_type;
	BUG_ON(!type);

	/*
	 * If ->drop_item() exists, it is responsible for the
	 * config_item_put().
	 */
	if (type->ct_group_ops && type->ct_group_ops->drop_item)
		type->ct_group_ops->drop_item(to_config_group(parent_item),
					      item);
	else
		config_item_put(item);
}

#ifdef DEBUG
static void configfs_dump_one(struct configfs_dirent *sd, int level)
{
	printk(KERN_INFO "%*s\"%s\":\n", level, " ", configfs_get_name(sd));

#define type_print(_type) if (sd->s_type & _type) printk(KERN_INFO "%*s %s\n", level, " ", #_type);
	type_print(CONFIGFS_ROOT);
	type_print(CONFIGFS_DIR);
	type_print(CONFIGFS_ITEM_ATTR);
	type_print(CONFIGFS_ITEM_LINK);
	type_print(CONFIGFS_USET_DIR);
	type_print(CONFIGFS_USET_DEFAULT);
	type_print(CONFIGFS_USET_DROPPING);
#undef type_print
}

static int configfs_dump(struct configfs_dirent *sd, int level)
{
	struct configfs_dirent *child_sd;
	int ret = 0;

	configfs_dump_one(sd, level);

	if (!(sd->s_type & (CONFIGFS_DIR|CONFIGFS_ROOT)))
		return 0;

	list_for_each_entry(child_sd, &sd->s_children, s_sibling) {
		ret = configfs_dump(child_sd, level + 2);
		if (ret)
			break;
	}

	return ret;
}
#endif


/*
 * configfs_depend_item() and configfs_undepend_item()
 *
 * WARNING: Do not call these from a configfs callback!
 *
 * This describes these functions and their helpers.
 *
 * Allow another kernel system to depend on a config_item.  If this
 * happens, the item cannot go away until the dependant can live without
 * it.  The idea is to give client modules as simple an interface as
 * possible.  When a system asks them to depend on an item, they just
 * call configfs_depend_item().  If the item is live and the client
 * driver is in good shape, we'll happily do the work for them.
 *
 * Why is the locking complex?  Because configfs uses the VFS to handle
 * all locking, but this function is called outside the normal
 * VFS->configfs path.  So it must take VFS locks to prevent the
 * VFS->configfs stuff (configfs_mkdir(), configfs_rmdir(), etc).  This is
 * why you can't call these functions underneath configfs callbacks.
 *
 * Note, btw, that this can be called at *any* time, even when a configfs
 * subsystem isn't registered, or when configfs is loading or unloading.
 * Just like configfs_register_subsystem().  So we take the same
 * precautions.  We pin the filesystem.  We lock each i_mutex _in_order_
 * on our way down the tree.  If we can find the target item in the
 * configfs tree, it must be part of the subsystem tree as well, so we
 * do not need the subsystem semaphore.  Holding the i_mutex chain locks
 * out mkdir() and rmdir(), who might be racing us.
 */

/*
 * configfs_depend_prep()
 *
 * Only subdirectories count here.  Files (CONFIGFS_NOT_PINNED) are
 * attributes.  This is similar but not the same to configfs_detach_prep().
 * Note that configfs_detach_prep() expects the parent to be locked when it
 * is called, but we lock the parent *inside* configfs_depend_prep().  We
 * do that so we can unlock it if we find nothing.
 *
 * Here we do a depth-first search of the dentry hierarchy looking for
 * our object.  We take i_mutex on each step of the way down.  IT IS
 * ESSENTIAL THAT i_mutex LOCKING IS ORDERED.  If we come back up a branch,
 * we'll drop the i_mutex.
 *
 * If the target is not found, -ENOENT is bubbled up and we have released
 * all locks.  If the target was found, the locks will be cleared by
 * configfs_depend_rollback().
 *
 * This adds a requirement that all config_items be unique!
 *
 * This is recursive because the locking traversal is tricky.  There isn't
 * much on the stack, though, so folks that need this function - be careful
 * about your stack!  Patches will be accepted to make it iterative.
 */
static int configfs_depend_prep(struct dentry *origin,
				struct config_item *target)
{
	struct configfs_dirent *child_sd, *sd = origin->d_fsdata;
	int ret = 0;

	BUG_ON(!origin || !sd);

	/* Lock this guy on the way down */
	mutex_lock(&sd->s_dentry->d_inode->i_mutex);
	if (sd->s_element == target)  /* Boo-yah */
		goto out;

	list_for_each_entry(child_sd, &sd->s_children, s_sibling) {
		if (child_sd->s_type & CONFIGFS_DIR) {
			ret = configfs_depend_prep(child_sd->s_dentry,
						   target);
			if (!ret)
				goto out;  /* Child path boo-yah */
		}
	}

	/* We looped all our children and didn't find target */
	mutex_unlock(&sd->s_dentry->d_inode->i_mutex);
	ret = -ENOENT;

out:
	return ret;
}

/*
 * This is ONLY called if configfs_depend_prep() did its job.  So we can
 * trust the entire path from item back up to origin.
 *
 * We walk backwards from item, unlocking each i_mutex.  We finish by
 * unlocking origin.
 */
static void configfs_depend_rollback(struct dentry *origin,
				     struct config_item *item)
{
	struct dentry *dentry = item->ci_dentry;

	while (dentry != origin) {
		mutex_unlock(&dentry->d_inode->i_mutex);
		dentry = dentry->d_parent;
	}

	mutex_unlock(&origin->d_inode->i_mutex);
}

int configfs_depend_item(struct configfs_subsystem *subsys,
			 struct config_item *target)
{
	int ret;
	struct configfs_dirent *p, *root_sd, *subsys_sd = NULL;
	struct config_item *s_item = &subsys->su_group.cg_item;

	/*
	 * Pin the configfs filesystem.  This means we can safely access
	 * the root of the configfs filesystem.
	 */
	ret = configfs_pin_fs();
	if (ret)
		return ret;

	/*
	 * Next, lock the root directory.  We're going to check that the
	 * subsystem is really registered, and so we need to lock out
	 * configfs_[un]register_subsystem().
	 */
	mutex_lock(&configfs_sb->s_root->d_inode->i_mutex);

	root_sd = configfs_sb->s_root->d_fsdata;

	list_for_each_entry(p, &root_sd->s_children, s_sibling) {
		if (p->s_type & CONFIGFS_DIR) {
			if (p->s_element == s_item) {
				subsys_sd = p;
				break;
			}
		}
	}

	if (!subsys_sd) {
		ret = -ENOENT;
		goto out_unlock_fs;
	}

	/* Ok, now we can trust subsys/s_item */

	/* Scan the tree, locking i_mutex recursively, return 0 if found */
	ret = configfs_depend_prep(subsys_sd->s_dentry, target);
	if (ret)
		goto out_unlock_fs;

	/* We hold all i_mutexes from the subsystem down to the target */
	p = target->ci_dentry->d_fsdata;
	p->s_dependent_count += 1;

	configfs_depend_rollback(subsys_sd->s_dentry, target);

out_unlock_fs:
	mutex_unlock(&configfs_sb->s_root->d_inode->i_mutex);

	/*
	 * If we succeeded, the fs is pinned via other methods.  If not,
	 * we're done with it anyway.  So release_fs() is always right.
	 */
	configfs_release_fs();

	return ret;
}
EXPORT_SYMBOL(configfs_depend_item);

/*
 * Release the dependent linkage.  This is much simpler than
 * configfs_depend_item() because we know that that the client driver is
 * pinned, thus the subsystem is pinned, and therefore configfs is pinned.
 */
void configfs_undepend_item(struct configfs_subsystem *subsys,
			    struct config_item *target)
{
	struct configfs_dirent *sd;

	/*
	 * Since we can trust everything is pinned, we just need i_mutex
	 * on the item.
	 */
	mutex_lock(&target->ci_dentry->d_inode->i_mutex);

	sd = target->ci_dentry->d_fsdata;
	BUG_ON(sd->s_dependent_count < 1);

	sd->s_dependent_count -= 1;

	/*
	 * After this unlock, we cannot trust the item to stay alive!
	 * DO NOT REFERENCE item after this unlock.
	 */
	mutex_unlock(&target->ci_dentry->d_inode->i_mutex);
}
EXPORT_SYMBOL(configfs_undepend_item);

static int configfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int ret = 0;
	int module_got = 0;
	struct config_group *group = NULL;
	struct config_item *item = NULL;
	struct config_item *parent_item;
	struct configfs_subsystem *subsys;
	struct configfs_dirent *sd;
	struct config_item_type *type;
	struct module *subsys_owner = NULL, *new_item_owner = NULL;
	char *name;

	if (dentry->d_parent == configfs_sb->s_root) {
		ret = -EPERM;
		goto out;
	}

	sd = dentry->d_parent->d_fsdata;

	/*
	 * Fake invisibility if dir belongs to a group/default groups hierarchy
	 * being attached
	 */
	if (!configfs_dirent_is_ready(sd)) {
		ret = -ENOENT;
		goto out;
	}

	if (!(sd->s_type & CONFIGFS_USET_DIR)) {
		ret = -EPERM;
		goto out;
	}

	/* Get a working ref for the duration of this function */
	parent_item = configfs_get_config_item(dentry->d_parent);
	type = parent_item->ci_type;
	subsys = to_config_group(parent_item)->cg_subsys;
	BUG_ON(!subsys);

	if (!type || !type->ct_group_ops ||
	    (!type->ct_group_ops->make_group &&
	     !type->ct_group_ops->make_item)) {
		ret = -EPERM;  /* Lack-of-mkdir returns -EPERM */
		goto out_put;
	}

	/*
	 * The subsystem may belong to a different module than the item
	 * being created.  We don't want to safely pin the new item but
	 * fail to pin the subsystem it sits under.
	 */
	if (!subsys->su_group.cg_item.ci_type) {
		ret = -EINVAL;
		goto out_put;
	}
	subsys_owner = subsys->su_group.cg_item.ci_type->ct_owner;
	if (!try_module_get(subsys_owner)) {
		ret = -EINVAL;
		goto out_put;
	}

	name = kmalloc(dentry->d_name.len + 1, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto out_subsys_put;
	}

	snprintf(name, dentry->d_name.len + 1, "%s", dentry->d_name.name);

	mutex_lock(&subsys->su_mutex);
	if (type->ct_group_ops->make_group) {
		group = type->ct_group_ops->make_group(to_config_group(parent_item), name);
		if (!group)
			group = ERR_PTR(-ENOMEM);
		if (!IS_ERR(group)) {
			link_group(to_config_group(parent_item), group);
			item = &group->cg_item;
		} else
			ret = PTR_ERR(group);
	} else {
		item = type->ct_group_ops->make_item(to_config_group(parent_item), name);
		if (!item)
			item = ERR_PTR(-ENOMEM);
		if (!IS_ERR(item))
			link_obj(parent_item, item);
		else
			ret = PTR_ERR(item);
	}
	mutex_unlock(&subsys->su_mutex);

	kfree(name);
	if (ret) {
		/*
		 * If ret != 0, then link_obj() was never called.
		 * There are no extra references to clean up.
		 */
		goto out_subsys_put;
	}

	/*
	 * link_obj() has been called (via link_group() for groups).
	 * From here on out, errors must clean that up.
	 */

	type = item->ci_type;
	if (!type) {
		ret = -EINVAL;
		goto out_unlink;
	}

	new_item_owner = type->ct_owner;
	if (!try_module_get(new_item_owner)) {
		ret = -EINVAL;
		goto out_unlink;
	}

	/*
	 * I hate doing it this way, but if there is
	 * an error,  module_put() probably should
	 * happen after any cleanup.
	 */
	module_got = 1;

	/*
	 * Make racing rmdir() fail if it did not tag parent with
	 * CONFIGFS_USET_DROPPING
	 * Note: if CONFIGFS_USET_DROPPING is already set, attach_group() will
	 * fail and let rmdir() terminate correctly
	 */
	spin_lock(&configfs_dirent_lock);
	/* This will make configfs_detach_prep() fail */
	sd->s_type |= CONFIGFS_USET_IN_MKDIR;
	spin_unlock(&configfs_dirent_lock);

	if (group)
		ret = configfs_attach_group(parent_item, item, dentry);
	else
		ret = configfs_attach_item(parent_item, item, dentry);

	spin_lock(&configfs_dirent_lock);
	sd->s_type &= ~CONFIGFS_USET_IN_MKDIR;
	if (!ret)
		configfs_dir_set_ready(dentry->d_fsdata);
	spin_unlock(&configfs_dirent_lock);

out_unlink:
	if (ret) {
		/* Tear down everything we built up */
		mutex_lock(&subsys->su_mutex);

		client_disconnect_notify(parent_item, item);
		if (group)
			unlink_group(group);
		else
			unlink_obj(item);
		client_drop_item(parent_item, item);

		mutex_unlock(&subsys->su_mutex);

		if (module_got)
			module_put(new_item_owner);
	}

out_subsys_put:
	if (ret)
		module_put(subsys_owner);

out_put:
	/*
	 * link_obj()/link_group() took a reference from child->parent,
	 * so the parent is safely pinned.  We can drop our working
	 * reference.
	 */
	config_item_put(parent_item);

out:
	return ret;
}

static int configfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct config_item *parent_item;
	struct config_item *item;
	struct configfs_subsystem *subsys;
	struct configfs_dirent *sd;
	struct module *subsys_owner = NULL, *dead_item_owner = NULL;
	int ret;

	if (dentry->d_parent == configfs_sb->s_root)
		return -EPERM;

	sd = dentry->d_fsdata;
	if (sd->s_type & CONFIGFS_USET_DEFAULT)
		return -EPERM;

	/*
	 * Here's where we check for dependents.  We're protected by
	 * i_mutex.
	 */
	if (sd->s_dependent_count)
		return -EBUSY;

	/* Get a working ref until we have the child */
	parent_item = configfs_get_config_item(dentry->d_parent);
	subsys = to_config_group(parent_item)->cg_subsys;
	BUG_ON(!subsys);

	if (!parent_item->ci_type) {
		config_item_put(parent_item);
		return -EINVAL;
	}

	/* configfs_mkdir() shouldn't have allowed this */
	BUG_ON(!subsys->su_group.cg_item.ci_type);
	subsys_owner = subsys->su_group.cg_item.ci_type->ct_owner;

	/*
	 * Ensure that no racing symlink() will make detach_prep() fail while
	 * the new link is temporarily attached
	 */
	do {
		struct mutex *wait_mutex;

		mutex_lock(&configfs_symlink_mutex);
		spin_lock(&configfs_dirent_lock);
		ret = configfs_detach_prep(dentry, &wait_mutex);
		if (ret)
			configfs_detach_rollback(dentry);
		spin_unlock(&configfs_dirent_lock);
		mutex_unlock(&configfs_symlink_mutex);

		if (ret) {
			if (ret != -EAGAIN) {
				config_item_put(parent_item);
				return ret;
			}

			/* Wait until the racing operation terminates */
			mutex_lock(wait_mutex);
			mutex_unlock(wait_mutex);
		}
	} while (ret == -EAGAIN);

	/* Get a working ref for the duration of this function */
	item = configfs_get_config_item(dentry);

	/* Drop reference from above, item already holds one. */
	config_item_put(parent_item);

	if (item->ci_type)
		dead_item_owner = item->ci_type->ct_owner;

	if (sd->s_type & CONFIGFS_USET_DIR) {
		configfs_detach_group(item);

		mutex_lock(&subsys->su_mutex);
		client_disconnect_notify(parent_item, item);
		unlink_group(to_config_group(item));
	} else {
		configfs_detach_item(item);

		mutex_lock(&subsys->su_mutex);
		client_disconnect_notify(parent_item, item);
		unlink_obj(item);
	}

	client_drop_item(parent_item, item);
	mutex_unlock(&subsys->su_mutex);

	/* Drop our reference from above */
	config_item_put(item);

	module_put(dead_item_owner);
	module_put(subsys_owner);

	return 0;
}

const struct inode_operations configfs_dir_inode_operations = {
	.mkdir		= configfs_mkdir,
	.rmdir		= configfs_rmdir,
	.symlink	= configfs_symlink,
	.unlink		= configfs_unlink,
	.lookup		= configfs_lookup,
	.setattr	= configfs_setattr,
};

#if 0
int configfs_rename_dir(struct config_item * item, const char *new_name)
{
	int error = 0;
	struct dentry * new_dentry, * parent;

	if (!strcmp(config_item_name(item), new_name))
		return -EINVAL;

	if (!item->parent)
		return -EINVAL;

	down_write(&configfs_rename_sem);
	parent = item->parent->dentry;

	mutex_lock(&parent->d_inode->i_mutex);

	new_dentry = lookup_one_len(new_name, parent, strlen(new_name));
	if (!IS_ERR(new_dentry)) {
		if (!new_dentry->d_inode) {
			error = config_item_set_name(item, "%s", new_name);
			if (!error) {
				d_add(new_dentry, NULL);
				d_move(item->dentry, new_dentry);
			}
			else
				d_delete(new_dentry);
		} else
			error = -EEXIST;
		dput(new_dentry);
	}
	mutex_unlock(&parent->d_inode->i_mutex);
	up_write(&configfs_rename_sem);

	return error;
}
#endif

static int configfs_dir_open(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct configfs_dirent * parent_sd = dentry->d_fsdata;
	int err;

	mutex_lock(&dentry->d_inode->i_mutex);
	/*
	 * Fake invisibility if dir belongs to a group/default groups hierarchy
	 * being attached
	 */
	err = -ENOENT;
	if (configfs_dirent_is_ready(parent_sd)) {
		file->private_data = configfs_new_dirent(parent_sd, NULL);
		if (IS_ERR(file->private_data))
			err = PTR_ERR(file->private_data);
		else
			err = 0;
	}
	mutex_unlock(&dentry->d_inode->i_mutex);

	return err;
}

static int configfs_dir_close(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct configfs_dirent * cursor = file->private_data;

	mutex_lock(&dentry->d_inode->i_mutex);
	spin_lock(&configfs_dirent_lock);
	list_del_init(&cursor->s_sibling);
	spin_unlock(&configfs_dirent_lock);
	mutex_unlock(&dentry->d_inode->i_mutex);

	release_configfs_dirent(cursor);

	return 0;
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct configfs_dirent *sd)
{
	return (sd->s_mode >> 12) & 15;
}

static int configfs_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct configfs_dirent * parent_sd = dentry->d_fsdata;
	struct configfs_dirent *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->s_sibling;
	ino_t ino;
	int i = filp->f_pos;

	switch (i) {
		case 0:
			ino = dentry->d_inode->i_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			ino = parent_ino(dentry);
			if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		default:
			if (filp->f_pos == 2) {
				spin_lock(&configfs_dirent_lock);
				list_move(q, &parent_sd->s_children);
				spin_unlock(&configfs_dirent_lock);
			}
			for (p=q->next; p!= &parent_sd->s_children; p=p->next) {
				struct configfs_dirent *next;
				const char * name;
				int len;

				next = list_entry(p, struct configfs_dirent,
						   s_sibling);
				if (!next->s_element)
					continue;

				name = configfs_get_name(next);
				len = strlen(name);
				if (next->s_dentry)
					ino = next->s_dentry->d_inode->i_ino;
				else
					ino = iunique(configfs_sb, 2);

				if (filldir(dirent, name, len, filp->f_pos, ino,
						 dt_type(next)) < 0)
					return 0;

				spin_lock(&configfs_dirent_lock);
				list_move(q, p);
				spin_unlock(&configfs_dirent_lock);
				p = q;
				filp->f_pos++;
			}
	}
	return 0;
}

static loff_t configfs_dir_lseek(struct file * file, loff_t offset, int origin)
{
	struct dentry * dentry = file->f_path.dentry;

	mutex_lock(&dentry->d_inode->i_mutex);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct configfs_dirent *sd = dentry->d_fsdata;
			struct configfs_dirent *cursor = file->private_data;
			struct list_head *p;
			loff_t n = file->f_pos - 2;

			spin_lock(&configfs_dirent_lock);
			list_del(&cursor->s_sibling);
			p = sd->s_children.next;
			while (n && p != &sd->s_children) {
				struct configfs_dirent *next;
				next = list_entry(p, struct configfs_dirent,
						   s_sibling);
				if (next->s_element)
					n--;
				p = p->next;
			}
			list_add_tail(&cursor->s_sibling, p);
			spin_unlock(&configfs_dirent_lock);
		}
	}
	mutex_unlock(&dentry->d_inode->i_mutex);
	return offset;
}

const struct file_operations configfs_dir_operations = {
	.open		= configfs_dir_open,
	.release	= configfs_dir_close,
	.llseek		= configfs_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= configfs_readdir,
};

int configfs_register_subsystem(struct configfs_subsystem *subsys)
{
	int err;
	struct config_group *group = &subsys->su_group;
	struct qstr name;
	struct dentry *dentry;
	struct configfs_dirent *sd;

	err = configfs_pin_fs();
	if (err)
		return err;

	if (!group->cg_item.ci_name)
		group->cg_item.ci_name = group->cg_item.ci_namebuf;

	sd = configfs_sb->s_root->d_fsdata;
	link_group(to_config_group(sd->s_element), group);

	mutex_lock_nested(&configfs_sb->s_root->d_inode->i_mutex,
			I_MUTEX_PARENT);

	name.name = group->cg_item.ci_name;
	name.len = strlen(name.name);
	name.hash = full_name_hash(name.name, name.len);

	err = -ENOMEM;
	dentry = d_alloc(configfs_sb->s_root, &name);
	if (dentry) {
		d_add(dentry, NULL);

		err = configfs_attach_group(sd->s_element, &group->cg_item,
					    dentry);
		if (err) {
			d_delete(dentry);
			dput(dentry);
		} else {
			spin_lock(&configfs_dirent_lock);
			configfs_dir_set_ready(dentry->d_fsdata);
			spin_unlock(&configfs_dirent_lock);
		}
	}

	mutex_unlock(&configfs_sb->s_root->d_inode->i_mutex);

	if (err) {
		unlink_group(group);
		configfs_release_fs();
	}

	return err;
}

void configfs_unregister_subsystem(struct configfs_subsystem *subsys)
{
	struct config_group *group = &subsys->su_group;
	struct dentry *dentry = group->cg_item.ci_dentry;

	if (dentry->d_parent != configfs_sb->s_root) {
		printk(KERN_ERR "configfs: Tried to unregister non-subsystem!\n");
		return;
	}

	mutex_lock_nested(&configfs_sb->s_root->d_inode->i_mutex,
			  I_MUTEX_PARENT);
	mutex_lock_nested(&dentry->d_inode->i_mutex, I_MUTEX_CHILD);
	mutex_lock(&configfs_symlink_mutex);
	spin_lock(&configfs_dirent_lock);
	if (configfs_detach_prep(dentry, NULL)) {
		printk(KERN_ERR "configfs: Tried to unregister non-empty subsystem!\n");
	}
	spin_unlock(&configfs_dirent_lock);
	mutex_unlock(&configfs_symlink_mutex);
	configfs_detach_group(&group->cg_item);
	dentry->d_inode->i_flags |= S_DEAD;
	mutex_unlock(&dentry->d_inode->i_mutex);

	d_delete(dentry);

	mutex_unlock(&configfs_sb->s_root->d_inode->i_mutex);

	dput(dentry);

	unlink_group(group);
	configfs_release_fs();
}

EXPORT_SYMBOL(configfs_register_subsystem);
EXPORT_SYMBOL(configfs_unregister_subsystem);
