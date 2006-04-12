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

#include <linux/configfs.h>
#include "configfs_internal.h"

DECLARE_RWSEM(configfs_rename_sem);

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

	sd = kmem_cache_alloc(configfs_dir_cachep, GFP_KERNEL);
	if (!sd)
		return NULL;

	memset(sd, 0, sizeof(*sd));
	atomic_set(&sd->s_count, 1);
	INIT_LIST_HEAD(&sd->s_links);
	INIT_LIST_HEAD(&sd->s_children);
	list_add(&sd->s_sibling, &parent_sd->s_children);
	sd->s_element = element;

	return sd;
}

int configfs_make_dirent(struct configfs_dirent * parent_sd,
			 struct dentry * dentry, void * element,
			 umode_t mode, int type)
{
	struct configfs_dirent * sd;

	sd = configfs_new_dirent(parent_sd, element);
	if (!sd)
		return -ENOMEM;

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
	inode->i_nlink++;
	return 0;
}

static int init_file(struct inode * inode)
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

	error = configfs_make_dirent(p->d_fsdata, d, k, mode,
				     CONFIGFS_DIR);
	if (!error) {
		error = configfs_create(d, mode, init_dir);
		if (!error) {
			p->d_inode->i_nlink++;
			(d)->d_op = &configfs_dentry_ops;
		} else {
			struct configfs_dirent *sd = d->d_fsdata;
			if (sd) {
				list_del_init(&sd->s_sibling);
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
				list_del_init(&sd->s_sibling);
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
 	list_del_init(&sd->s_sibling);
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
	error = configfs_create(dentry, (attr->ca_mode & S_IALLUGO) | S_IFREG, init_file);
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
	int err = 0;

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

	return ERR_PTR(err);
}

/*
 * Only subdirectories count here.  Files (CONFIGFS_NOT_PINNED) are
 * attributes and are removed by rmdir().  We recurse, taking i_mutex
 * on all children that are candidates for default detach.  If the
 * result is clean, then configfs_detach_group() will handle dropping
 * i_mutex.  If there is an error, the caller will clean up the i_mutex
 * holders via configfs_detach_rollback().
 */
static int configfs_detach_prep(struct dentry *dentry)
{
	struct configfs_dirent *parent_sd = dentry->d_fsdata;
	struct configfs_dirent *sd;
	int ret;

	ret = -EBUSY;
	if (!list_empty(&parent_sd->s_links))
		goto out;

	ret = 0;
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (sd->s_type & CONFIGFS_NOT_PINNED)
			continue;
		if (sd->s_type & CONFIGFS_USET_DEFAULT) {
			mutex_lock(&sd->s_dentry->d_inode->i_mutex);
			/* Mark that we've taken i_mutex */
			sd->s_type |= CONFIGFS_USET_DROPPING;

			ret = configfs_detach_prep(sd->s_dentry);
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
 * Walk the tree, dropping i_mutex wherever CONFIGFS_USET_DROPPING is
 * set.
 */
static void configfs_detach_rollback(struct dentry *dentry)
{
	struct configfs_dirent *parent_sd = dentry->d_fsdata;
	struct configfs_dirent *sd;

	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (sd->s_type & CONFIGFS_USET_DEFAULT) {
			configfs_detach_rollback(sd->s_dentry);

			if (sd->s_type & CONFIGFS_USET_DROPPING) {
				sd->s_type &= ~CONFIGFS_USET_DROPPING;
				mutex_unlock(&sd->s_dentry->d_inode->i_mutex);
			}
		}
	}
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
		list_del_init(&sd->s_sibling);
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

		configfs_detach_group(sd->s_element);
		child->d_inode->i_flags |= S_DEAD;

		/*
		 * From rmdir/unregister, a configfs_detach_prep() pass
		 * has taken our i_mutex for us.  Drop it.
		 * From mkdir/register cleanup, there is no sem held.
		 */
		if (sd->s_type & CONFIGFS_USET_DROPPING)
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
	struct dentry *dentry = group->cg_item.ci_dentry;
	int ret = 0;
	int i;

	if (group->default_groups) {
		/*
		 * FYI, we're faking mkdir here
		 * I'm not sure we need this semaphore, as we're called
		 * from our parent's mkdir.  That holds our parent's
		 * i_mutex, so afaik lookup cannot continue through our
		 * parent to find us, let alone mess with our tree.
		 * That said, taking our i_mutex is closer to mkdir
		 * emulation, and shouldn't hurt.
		 */
		mutex_lock(&dentry->d_inode->i_mutex);

		for (i = 0; group->default_groups[i]; i++) {
			new_group = group->default_groups[i];

			ret = create_default_group(group, new_group);
			if (ret)
				break;
		}

		mutex_unlock(&dentry->d_inode->i_mutex);
	}

	if (ret)
		detach_groups(group);

	return ret;
}

/*
 * All of link_obj/unlink_obj/link_group/unlink_group require that
 * subsys->su_sem is held.
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
			configfs_remove_dir(item);
			d_delete(dentry);
		}
	}

	return ret;
}

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

		ret = populate_groups(to_config_group(item));
		if (ret) {
			configfs_detach_item(item);
			d_delete(dentry);
		}
	}

	return ret;
}

static void configfs_detach_group(struct config_item *item)
{
	detach_groups(to_config_group(item));
	configfs_detach_item(item);
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


static int configfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int ret, module_got = 0;
	struct config_group *group;
	struct config_item *item;
	struct config_item *parent_item;
	struct configfs_subsystem *subsys;
	struct configfs_dirent *sd;
	struct config_item_type *type;
	struct module *owner = NULL;
	char *name;

	if (dentry->d_parent == configfs_sb->s_root) {
		ret = -EPERM;
		goto out;
	}

	sd = dentry->d_parent->d_fsdata;
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

	name = kmalloc(dentry->d_name.len + 1, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto out_put;
	}

	snprintf(name, dentry->d_name.len + 1, "%s", dentry->d_name.name);

	down(&subsys->su_sem);
	group = NULL;
	item = NULL;
	if (type->ct_group_ops->make_group) {
		group = type->ct_group_ops->make_group(to_config_group(parent_item), name);
		if (group) {
			link_group(to_config_group(parent_item), group);
			item = &group->cg_item;
		}
	} else {
		item = type->ct_group_ops->make_item(to_config_group(parent_item), name);
		if (item)
			link_obj(parent_item, item);
	}
	up(&subsys->su_sem);

	kfree(name);
	if (!item) {
		/*
		 * If item == NULL, then link_obj() was never called.
		 * There are no extra references to clean up.
		 */
		ret = -ENOMEM;
		goto out_put;
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

	owner = type->ct_owner;
	if (!try_module_get(owner)) {
		ret = -EINVAL;
		goto out_unlink;
	}

	/*
	 * I hate doing it this way, but if there is
	 * an error,  module_put() probably should
	 * happen after any cleanup.
	 */
	module_got = 1;

	if (group)
		ret = configfs_attach_group(parent_item, item, dentry);
	else
		ret = configfs_attach_item(parent_item, item, dentry);

out_unlink:
	if (ret) {
		/* Tear down everything we built up */
		down(&subsys->su_sem);
		if (group)
			unlink_group(group);
		else
			unlink_obj(item);
		client_drop_item(parent_item, item);
		up(&subsys->su_sem);

		if (module_got)
			module_put(owner);
	}

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
	struct module *owner = NULL;
	int ret;

	if (dentry->d_parent == configfs_sb->s_root)
		return -EPERM;

	sd = dentry->d_fsdata;
	if (sd->s_type & CONFIGFS_USET_DEFAULT)
		return -EPERM;

	/* Get a working ref until we have the child */
	parent_item = configfs_get_config_item(dentry->d_parent);
	subsys = to_config_group(parent_item)->cg_subsys;
	BUG_ON(!subsys);

	if (!parent_item->ci_type) {
		config_item_put(parent_item);
		return -EINVAL;
	}

	ret = configfs_detach_prep(dentry);
	if (ret) {
		configfs_detach_rollback(dentry);
		config_item_put(parent_item);
		return ret;
	}

	/* Get a working ref for the duration of this function */
	item = configfs_get_config_item(dentry);

	/* Drop reference from above, item already holds one. */
	config_item_put(parent_item);

	if (item->ci_type)
		owner = item->ci_type->ct_owner;

	if (sd->s_type & CONFIGFS_USET_DIR) {
		configfs_detach_group(item);

		down(&subsys->su_sem);
		unlink_group(to_config_group(item));
	} else {
		configfs_detach_item(item);

		down(&subsys->su_sem);
		unlink_obj(item);
	}

	client_drop_item(parent_item, item);
	up(&subsys->su_sem);

	/* Drop our reference from above */
	config_item_put(item);

	module_put(owner);

	return 0;
}

struct inode_operations configfs_dir_inode_operations = {
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
	struct dentry * dentry = file->f_dentry;
	struct configfs_dirent * parent_sd = dentry->d_fsdata;

	mutex_lock(&dentry->d_inode->i_mutex);
	file->private_data = configfs_new_dirent(parent_sd, NULL);
	mutex_unlock(&dentry->d_inode->i_mutex);

	return file->private_data ? 0 : -ENOMEM;

}

static int configfs_dir_close(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_dentry;
	struct configfs_dirent * cursor = file->private_data;

	mutex_lock(&dentry->d_inode->i_mutex);
	list_del_init(&cursor->s_sibling);
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
	struct dentry *dentry = filp->f_dentry;
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
				list_del(q);
				list_add(q, &parent_sd->s_children);
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

				list_del(q);
				list_add(q, p);
				p = q;
				filp->f_pos++;
			}
	}
	return 0;
}

static loff_t configfs_dir_lseek(struct file * file, loff_t offset, int origin)
{
	struct dentry * dentry = file->f_dentry;

	mutex_lock(&dentry->d_inode->i_mutex);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			mutex_unlock(&file->f_dentry->d_inode->i_mutex);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct configfs_dirent *sd = dentry->d_fsdata;
			struct configfs_dirent *cursor = file->private_data;
			struct list_head *p;
			loff_t n = file->f_pos - 2;

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

	mutex_lock(&configfs_sb->s_root->d_inode->i_mutex);

	name.name = group->cg_item.ci_name;
	name.len = strlen(name.name);
	name.hash = full_name_hash(name.name, name.len);

	err = -ENOMEM;
	dentry = d_alloc(configfs_sb->s_root, &name);
	if (!dentry)
		goto out_release;

	d_add(dentry, NULL);

	err = configfs_attach_group(sd->s_element, &group->cg_item,
				    dentry);
	if (!err)
		dentry = NULL;
	else
		d_delete(dentry);

	mutex_unlock(&configfs_sb->s_root->d_inode->i_mutex);

	if (dentry) {
	    dput(dentry);
out_release:
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

	mutex_lock(&configfs_sb->s_root->d_inode->i_mutex);
	mutex_lock(&dentry->d_inode->i_mutex);
	if (configfs_detach_prep(dentry)) {
		printk(KERN_ERR "configfs: Tried to unregister non-empty subsystem!\n");
	}
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
