// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dir.c - Operations for configfs directories.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#undef DEBUG

#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

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
	struct configfs_dirent *sd = dentry->d_fsdata;

	if (sd) {
		/* Coordinate with configfs_readdir */
		spin_lock(&configfs_dirent_lock);
		/*
		 * Set sd->s_dentry to null only when this dentry is the one
		 * that is going to be killed.  Otherwise configfs_d_iput may
		 * run just after configfs_attach_attr and set sd->s_dentry to
		 * NULL even it's still in use.
		 */
		if (sd->s_dentry == dentry)
			sd->s_dentry = NULL;

		spin_unlock(&configfs_dirent_lock);
		configfs_put(sd);
	}
	iput(inode);
}

const struct dentry_operations configfs_dentry_ops = {
	.d_iput		= configfs_d_iput,
	.d_delete	= always_delete_dentry,
};

#ifdef CONFIG_LOCKDEP

/*
 * Helpers to make lockdep happy with our recursive locking of default groups'
 * inodes (see configfs_attach_group() and configfs_detach_group()).
 * We put default groups i_mutexes in separate classes according to their depth
 * from the youngest non-default group ancestor.
 *
 * For a non-default group A having default groups A/B, A/C, and A/C/D, default
 * groups A/B and A/C will have their inode's mutex in class
 * default_group_class[0], and default group A/C/D will be in
 * default_group_class[1].
 *
 * The lock classes are declared and assigned in inode.c, according to the
 * s_depth value.
 * The s_depth value is initialized to -1, adjusted to >= 0 when attaching
 * default groups, and reset to -1 when all default groups are attached. During
 * attachment, if configfs_create() sees s_depth > 0, the lock class of the new
 * inode's mutex is set to default_group_class[s_depth - 1].
 */

static void configfs_init_dirent_depth(struct configfs_dirent *sd)
{
	sd->s_depth = -1;
}

static void configfs_set_dir_dirent_depth(struct configfs_dirent *parent_sd,
					  struct configfs_dirent *sd)
{
	int parent_depth = parent_sd->s_depth;

	if (parent_depth >= 0)
		sd->s_depth = parent_depth + 1;
}

static void
configfs_adjust_dir_dirent_depth_before_populate(struct configfs_dirent *sd)
{
	/*
	 * item's i_mutex class is already setup, so s_depth is now only
	 * used to set new sub-directories s_depth, which is always done
	 * with item's i_mutex locked.
	 */
	/*
	 *  sd->s_depth == -1 iff we are a non default group.
	 *  else (we are a default group) sd->s_depth > 0 (see
	 *  create_dir()).
	 */
	if (sd->s_depth == -1)
		/*
		 * We are a non default group and we are going to create
		 * default groups.
		 */
		sd->s_depth = 0;
}

static void
configfs_adjust_dir_dirent_depth_after_populate(struct configfs_dirent *sd)
{
	/* We will not create default groups anymore. */
	sd->s_depth = -1;
}

#else /* CONFIG_LOCKDEP */

static void configfs_init_dirent_depth(struct configfs_dirent *sd)
{
}

static void configfs_set_dir_dirent_depth(struct configfs_dirent *parent_sd,
					  struct configfs_dirent *sd)
{
}

static void
configfs_adjust_dir_dirent_depth_before_populate(struct configfs_dirent *sd)
{
}

static void
configfs_adjust_dir_dirent_depth_after_populate(struct configfs_dirent *sd)
{
}

#endif /* CONFIG_LOCKDEP */

static struct configfs_fragment *new_fragment(void)
{
	struct configfs_fragment *p;

	p = kmalloc(sizeof(struct configfs_fragment), GFP_KERNEL);
	if (p) {
		atomic_set(&p->frag_count, 1);
		init_rwsem(&p->frag_sem);
		p->frag_dead = false;
	}
	return p;
}

void put_fragment(struct configfs_fragment *frag)
{
	if (frag && atomic_dec_and_test(&frag->frag_count))
		kfree(frag);
}

struct configfs_fragment *get_fragment(struct configfs_fragment *frag)
{
	if (likely(frag))
		atomic_inc(&frag->frag_count);
	return frag;
}

/*
 * Allocates a new configfs_dirent and links it to the parent configfs_dirent
 */
static struct configfs_dirent *configfs_new_dirent(struct configfs_dirent *parent_sd,
						   void *element, int type,
						   struct configfs_fragment *frag)
{
	struct configfs_dirent * sd;

	sd = kmem_cache_zalloc(configfs_dir_cachep, GFP_KERNEL);
	if (!sd)
		return ERR_PTR(-ENOMEM);

	atomic_set(&sd->s_count, 1);
	INIT_LIST_HEAD(&sd->s_children);
	sd->s_element = element;
	sd->s_type = type;
	configfs_init_dirent_depth(sd);
	spin_lock(&configfs_dirent_lock);
	if (parent_sd->s_type & CONFIGFS_USET_DROPPING) {
		spin_unlock(&configfs_dirent_lock);
		kmem_cache_free(configfs_dir_cachep, sd);
		return ERR_PTR(-ENOENT);
	}
	sd->s_frag = get_fragment(frag);
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
			 umode_t mode, int type, struct configfs_fragment *frag)
{
	struct configfs_dirent * sd;

	sd = configfs_new_dirent(parent_sd, element, type, frag);
	if (IS_ERR(sd))
		return PTR_ERR(sd);

	sd->s_mode = mode;
	sd->s_dentry = dentry;
	if (dentry)
		dentry->d_fsdata = configfs_get(sd);

	return 0;
}

static void configfs_remove_dirent(struct dentry *dentry)
{
	struct configfs_dirent *sd = dentry->d_fsdata;

	if (!sd)
		return;
	spin_lock(&configfs_dirent_lock);
	list_del_init(&sd->s_sibling);
	spin_unlock(&configfs_dirent_lock);
	configfs_put(sd);
}

/**
 *	configfs_create_dir - create a directory for an config_item.
 *	@item:		config_itemwe're creating directory for.
 *	@dentry:	config_item's dentry.
 *
 *	Note: user-created entries won't be allowed under this new directory
 *	until it is validated by configfs_dir_set_ready()
 */

static int configfs_create_dir(struct config_item *item, struct dentry *dentry,
				struct configfs_fragment *frag)
{
	int error;
	umode_t mode = S_IFDIR| S_IRWXU | S_IRUGO | S_IXUGO;
	struct dentry *p = dentry->d_parent;
	struct inode *inode;

	BUG_ON(!item);

	error = configfs_dirent_exists(p->d_fsdata, dentry->d_name.name);
	if (unlikely(error))
		return error;

	error = configfs_make_dirent(p->d_fsdata, dentry, item, mode,
				     CONFIGFS_DIR | CONFIGFS_USET_CREATING,
				     frag);
	if (unlikely(error))
		return error;

	configfs_set_dir_dirent_depth(p->d_fsdata, dentry->d_fsdata);
	inode = configfs_create(dentry, mode);
	if (IS_ERR(inode))
		goto out_remove;

	inode->i_op = &configfs_dir_inode_operations;
	inode->i_fop = &configfs_dir_operations;
	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	/* already hashed */
	dget(dentry);  /* pin directory dentries in core */
	inc_nlink(d_inode(p));
	item->ci_dentry = dentry;
	return 0;

out_remove:
	configfs_remove_dirent(dentry);
	return PTR_ERR(inode);
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

int configfs_create_link(struct configfs_dirent *target, struct dentry *parent,
		struct dentry *dentry, char *body)
{
	int err = 0;
	umode_t mode = S_IFLNK | S_IRWXUGO;
	struct configfs_dirent *p = parent->d_fsdata;
	struct inode *inode;

	err = configfs_make_dirent(p, dentry, target, mode, CONFIGFS_ITEM_LINK,
			p->s_frag);
	if (err)
		return err;

	inode = configfs_create(dentry, mode);
	if (IS_ERR(inode))
		goto out_remove;

	inode->i_link = body;
	inode->i_op = &configfs_symlink_inode_operations;
	d_instantiate(dentry, inode);
	dget(dentry);  /* pin link dentries in core */
	return 0;

out_remove:
	configfs_remove_dirent(dentry);
	return PTR_ERR(inode);
}

static void remove_dir(struct dentry * d)
{
	struct dentry * parent = dget(d->d_parent);

	configfs_remove_dirent(d);

	if (d_really_is_positive(d))
		simple_rmdir(d_inode(parent),d);

	pr_debug(" o %pd removing done (%d)\n", d, d_count(d));

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
	struct inode *inode;

	spin_lock(&configfs_dirent_lock);
	dentry->d_fsdata = configfs_get(sd);
	sd->s_dentry = dentry;
	spin_unlock(&configfs_dirent_lock);

	inode = configfs_create(dentry, (attr->ca_mode & S_IALLUGO) | S_IFREG);
	if (IS_ERR(inode)) {
		configfs_put(sd);
		return PTR_ERR(inode);
	}
	if (sd->s_type & CONFIGFS_ITEM_BIN_ATTR) {
		inode->i_size = 0;
		inode->i_fop = &configfs_bin_file_operations;
	} else {
		inode->i_size = PAGE_SIZE;
		inode->i_fop = &configfs_file_operations;
	}
	d_add(dentry, inode);
	return 0;
}

static struct dentry * configfs_lookup(struct inode *dir,
				       struct dentry *dentry,
				       unsigned int flags)
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
		if (dentry->d_name.len > NAME_MAX)
			return ERR_PTR(-ENAMETOOLONG);
		d_add(dentry, NULL);
		return NULL;
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
static int configfs_detach_prep(struct dentry *dentry, struct dentry **wait)
{
	struct configfs_dirent *parent_sd = dentry->d_fsdata;
	struct configfs_dirent *sd;
	int ret;

	/* Mark that we're trying to drop the group */
	parent_sd->s_type |= CONFIGFS_USET_DROPPING;

	ret = -EBUSY;
	if (parent_sd->s_links)
		goto out;

	ret = 0;
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element ||
		    (sd->s_type & CONFIGFS_NOT_PINNED))
			continue;
		if (sd->s_type & CONFIGFS_USET_DEFAULT) {
			/* Abort if racing with mkdir() */
			if (sd->s_type & CONFIGFS_USET_IN_MKDIR) {
				if (wait)
					*wait= dget(sd->s_dentry);
				return -EAGAIN;
			}

			/*
			 * Yup, recursive.  If there's a problem, blame
			 * deep nesting of default_groups
			 */
			ret = configfs_detach_prep(sd->s_dentry, wait);
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
	const struct config_item_type *t = item->ci_type;
	struct configfs_attribute *attr;
	struct configfs_bin_attribute *bin_attr;
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
	if (t->ct_bin_attrs) {
		for (i = 0; (bin_attr = t->ct_bin_attrs[i]) != NULL; i++) {
			error = configfs_create_bin_file(item, bin_attr);
			if (error)
				break;
		}
	}

	if (error)
		detach_attrs(item);

	return error;
}

static int configfs_attach_group(struct config_item *parent_item,
				 struct config_item *item,
				 struct dentry *dentry,
				 struct configfs_fragment *frag);
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

		inode_lock(d_inode(child));

		configfs_detach_group(sd->s_element);
		d_inode(child)->i_flags |= S_DEAD;
		dont_mount(child);

		inode_unlock(d_inode(child));

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
				struct config_group *group,
				struct configfs_fragment *frag)
{
	int ret;
	struct configfs_dirent *sd;
	/* We trust the caller holds a reference to parent */
	struct dentry *child, *parent = parent_group->cg_item.ci_dentry;

	if (!group->cg_item.ci_name)
		group->cg_item.ci_name = group->cg_item.ci_namebuf;

	ret = -ENOMEM;
	child = d_alloc_name(parent, group->cg_item.ci_name);
	if (child) {
		d_add(child, NULL);

		ret = configfs_attach_group(&parent_group->cg_item,
					    &group->cg_item, child, frag);
		if (!ret) {
			sd = child->d_fsdata;
			sd->s_type |= CONFIGFS_USET_DEFAULT;
		} else {
			BUG_ON(d_inode(child));
			d_drop(child);
			dput(child);
		}
	}

	return ret;
}

static int populate_groups(struct config_group *group,
			   struct configfs_fragment *frag)
{
	struct config_group *new_group;
	int ret = 0;

	list_for_each_entry(new_group, &group->default_groups, group_entry) {
		ret = create_default_group(group, new_group, frag);
		if (ret) {
			detach_groups(group);
			break;
		}
	}

	return ret;
}

void configfs_remove_default_groups(struct config_group *group)
{
	struct config_group *g, *n;

	list_for_each_entry_safe(g, n, &group->default_groups, group_entry) {
		list_del(&g->group_entry);
		config_item_put(&g->cg_item);
	}
}
EXPORT_SYMBOL(configfs_remove_default_groups);

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
	struct config_group *new_group;

	list_for_each_entry(new_group, &group->default_groups, group_entry)
		unlink_group(new_group);

	group->cg_subsys = NULL;
	unlink_obj(&group->cg_item);
}

static void link_group(struct config_group *parent_group, struct config_group *group)
{
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

	list_for_each_entry(new_group, &group->default_groups, group_entry)
		link_group(group, new_group);
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
				struct dentry *dentry,
				struct configfs_fragment *frag)
{
	int ret;

	ret = configfs_create_dir(item, dentry, frag);
	if (!ret) {
		ret = populate_attrs(item);
		if (ret) {
			/*
			 * We are going to remove an inode and its dentry but
			 * the VFS may already have hit and used them. Thus,
			 * we must lock them as rmdir() would.
			 */
			inode_lock(d_inode(dentry));
			configfs_remove_dir(item);
			d_inode(dentry)->i_flags |= S_DEAD;
			dont_mount(dentry);
			inode_unlock(d_inode(dentry));
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
				 struct dentry *dentry,
				 struct configfs_fragment *frag)
{
	int ret;
	struct configfs_dirent *sd;

	ret = configfs_attach_item(parent_item, item, dentry, frag);
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
		inode_lock_nested(d_inode(dentry), I_MUTEX_CHILD);
		configfs_adjust_dir_dirent_depth_before_populate(sd);
		ret = populate_groups(to_config_group(item), frag);
		if (ret) {
			configfs_detach_item(item);
			d_inode(dentry)->i_flags |= S_DEAD;
			dont_mount(dentry);
		}
		configfs_adjust_dir_dirent_depth_after_populate(sd);
		inode_unlock(d_inode(dentry));
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
	const struct config_item_type *type;

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
	const struct config_item_type *type;

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
	pr_info("%*s\"%s\":\n", level, " ", configfs_get_name(sd));

#define type_print(_type) if (sd->s_type & _type) pr_info("%*s %s\n", level, " ", #_type);
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
 * happens, the item cannot go away until the dependent can live without
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
 * precautions.  We pin the filesystem.  We lock configfs_dirent_lock.
 * If we can find the target item in the
 * configfs tree, it must be part of the subsystem tree as well, so we
 * do not need the subsystem semaphore.  Holding configfs_dirent_lock helps
 * locking out mkdir() and rmdir(), who might be racing us.
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
 * our object.
 * We deliberately ignore items tagged as dropping since they are virtually
 * dead, as well as items in the middle of attachment since they virtually
 * do not exist yet. This completes the locking out of racing mkdir() and
 * rmdir().
 * Note: subdirectories in the middle of attachment start with s_type =
 * CONFIGFS_DIR|CONFIGFS_USET_CREATING set by create_dir().  When
 * CONFIGFS_USET_CREATING is set, we ignore the item.  The actual set of
 * s_type is in configfs_new_dirent(), which has configfs_dirent_lock.
 *
 * If the target is not found, -ENOENT is bubbled up.
 *
 * This adds a requirement that all config_items be unique!
 *
 * This is recursive.  There isn't
 * much on the stack, though, so folks that need this function - be careful
 * about your stack!  Patches will be accepted to make it iterative.
 */
static int configfs_depend_prep(struct dentry *origin,
				struct config_item *target)
{
	struct configfs_dirent *child_sd, *sd;
	int ret = 0;

	BUG_ON(!origin || !origin->d_fsdata);
	sd = origin->d_fsdata;

	if (sd->s_element == target)  /* Boo-yah */
		goto out;

	list_for_each_entry(child_sd, &sd->s_children, s_sibling) {
		if ((child_sd->s_type & CONFIGFS_DIR) &&
		    !(child_sd->s_type & CONFIGFS_USET_DROPPING) &&
		    !(child_sd->s_type & CONFIGFS_USET_CREATING)) {
			ret = configfs_depend_prep(child_sd->s_dentry,
						   target);
			if (!ret)
				goto out;  /* Child path boo-yah */
		}
	}

	/* We looped all our children and didn't find target */
	ret = -ENOENT;

out:
	return ret;
}

static int configfs_do_depend_item(struct dentry *subsys_dentry,
				   struct config_item *target)
{
	struct configfs_dirent *p;
	int ret;

	spin_lock(&configfs_dirent_lock);
	/* Scan the tree, return 0 if found */
	ret = configfs_depend_prep(subsys_dentry, target);
	if (ret)
		goto out_unlock_dirent_lock;

	/*
	 * We are sure that the item is not about to be removed by rmdir(), and
	 * not in the middle of attachment by mkdir().
	 */
	p = target->ci_dentry->d_fsdata;
	p->s_dependent_count += 1;

out_unlock_dirent_lock:
	spin_unlock(&configfs_dirent_lock);

	return ret;
}

static inline struct configfs_dirent *
configfs_find_subsys_dentry(struct configfs_dirent *root_sd,
			    struct config_item *subsys_item)
{
	struct configfs_dirent *p;
	struct configfs_dirent *ret = NULL;

	list_for_each_entry(p, &root_sd->s_children, s_sibling) {
		if (p->s_type & CONFIGFS_DIR &&
		    p->s_element == subsys_item) {
			ret = p;
			break;
		}
	}

	return ret;
}


int configfs_depend_item(struct configfs_subsystem *subsys,
			 struct config_item *target)
{
	int ret;
	struct configfs_dirent *subsys_sd;
	struct config_item *s_item = &subsys->su_group.cg_item;
	struct dentry *root;

	/*
	 * Pin the configfs filesystem.  This means we can safely access
	 * the root of the configfs filesystem.
	 */
	root = configfs_pin_fs();
	if (IS_ERR(root))
		return PTR_ERR(root);

	/*
	 * Next, lock the root directory.  We're going to check that the
	 * subsystem is really registered, and so we need to lock out
	 * configfs_[un]register_subsystem().
	 */
	inode_lock(d_inode(root));

	subsys_sd = configfs_find_subsys_dentry(root->d_fsdata, s_item);
	if (!subsys_sd) {
		ret = -ENOENT;
		goto out_unlock_fs;
	}

	/* Ok, now we can trust subsys/s_item */
	ret = configfs_do_depend_item(subsys_sd->s_dentry, target);

out_unlock_fs:
	inode_unlock(d_inode(root));

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
 * configfs_depend_item() because we know that the client driver is
 * pinned, thus the subsystem is pinned, and therefore configfs is pinned.
 */
void configfs_undepend_item(struct config_item *target)
{
	struct configfs_dirent *sd;

	/*
	 * Since we can trust everything is pinned, we just need
	 * configfs_dirent_lock.
	 */
	spin_lock(&configfs_dirent_lock);

	sd = target->ci_dentry->d_fsdata;
	BUG_ON(sd->s_dependent_count < 1);

	sd->s_dependent_count -= 1;

	/*
	 * After this unlock, we cannot trust the item to stay alive!
	 * DO NOT REFERENCE item after this unlock.
	 */
	spin_unlock(&configfs_dirent_lock);
}
EXPORT_SYMBOL(configfs_undepend_item);

/*
 * caller_subsys is a caller's subsystem not target's. This is used to
 * determine if we should lock root and check subsys or not. When we are
 * in the same subsystem as our target there is no need to do locking as
 * we know that subsys is valid and is not unregistered during this function
 * as we are called from callback of one of his children and VFS holds a lock
 * on some inode. Otherwise we have to lock our root to  ensure that target's
 * subsystem it is not unregistered during this function.
 */
int configfs_depend_item_unlocked(struct configfs_subsystem *caller_subsys,
				  struct config_item *target)
{
	struct configfs_subsystem *target_subsys;
	struct config_group *root, *parent;
	struct configfs_dirent *subsys_sd;
	int ret = -ENOENT;

	/* Disallow this function for configfs root */
	if (configfs_is_root(target))
		return -EINVAL;

	parent = target->ci_group;
	/*
	 * This may happen when someone is trying to depend root
	 * directory of some subsystem
	 */
	if (configfs_is_root(&parent->cg_item)) {
		target_subsys = to_configfs_subsystem(to_config_group(target));
		root = parent;
	} else {
		target_subsys = parent->cg_subsys;
		/* Find a cofnigfs root as we may need it for locking */
		for (root = parent; !configfs_is_root(&root->cg_item);
		     root = root->cg_item.ci_group)
			;
	}

	if (target_subsys != caller_subsys) {
		/*
		 * We are in other configfs subsystem, so we have to do
		 * additional locking to prevent other subsystem from being
		 * unregistered
		 */
		inode_lock(d_inode(root->cg_item.ci_dentry));

		/*
		 * As we are trying to depend item from other subsystem
		 * we have to check if this subsystem is still registered
		 */
		subsys_sd = configfs_find_subsys_dentry(
				root->cg_item.ci_dentry->d_fsdata,
				&target_subsys->su_group.cg_item);
		if (!subsys_sd)
			goto out_root_unlock;
	} else {
		subsys_sd = target_subsys->su_group.cg_item.ci_dentry->d_fsdata;
	}

	/* Now we can execute core of depend item */
	ret = configfs_do_depend_item(subsys_sd->s_dentry, target);

	if (target_subsys != caller_subsys)
out_root_unlock:
		/*
		 * We were called from subsystem other than our target so we
		 * took some locks so now it's time to release them
		 */
		inode_unlock(d_inode(root->cg_item.ci_dentry));

	return ret;
}
EXPORT_SYMBOL(configfs_depend_item_unlocked);

static int configfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int ret = 0;
	int module_got = 0;
	struct config_group *group = NULL;
	struct config_item *item = NULL;
	struct config_item *parent_item;
	struct configfs_subsystem *subsys;
	struct configfs_dirent *sd;
	const struct config_item_type *type;
	struct module *subsys_owner = NULL, *new_item_owner = NULL;
	struct configfs_fragment *frag;
	char *name;

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

	frag = new_fragment();
	if (!frag) {
		ret = -ENOMEM;
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
		ret = configfs_attach_group(parent_item, item, dentry, frag);
	else
		ret = configfs_attach_item(parent_item, item, dentry, frag);

	/* inherit uid/gid from process creating the directory */
	if (!uid_eq(current_fsuid(), GLOBAL_ROOT_UID) ||
	    !gid_eq(current_fsgid(), GLOBAL_ROOT_GID)) {
		struct iattr ia = {
			.ia_uid = current_fsuid(),
			.ia_gid = current_fsgid(),
			.ia_valid = ATTR_UID | ATTR_GID,
		};
		struct inode *inode = d_inode(dentry);
		inode->i_uid = ia.ia_uid;
		inode->i_gid = ia.ia_gid;
		/* the above manual assignments skip the permission checks */
		configfs_setattr(dentry, &ia);
	}

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
	put_fragment(frag);

out:
	return ret;
}

static int configfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct config_item *parent_item;
	struct config_item *item;
	struct configfs_subsystem *subsys;
	struct configfs_dirent *sd;
	struct configfs_fragment *frag;
	struct module *subsys_owner = NULL, *dead_item_owner = NULL;
	int ret;

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

	/* configfs_mkdir() shouldn't have allowed this */
	BUG_ON(!subsys->su_group.cg_item.ci_type);
	subsys_owner = subsys->su_group.cg_item.ci_type->ct_owner;

	/*
	 * Ensure that no racing symlink() will make detach_prep() fail while
	 * the new link is temporarily attached
	 */
	do {
		struct dentry *wait;

		mutex_lock(&configfs_symlink_mutex);
		spin_lock(&configfs_dirent_lock);
		/*
		 * Here's where we check for dependents.  We're protected by
		 * configfs_dirent_lock.
		 * If no dependent, atomically tag the item as dropping.
		 */
		ret = sd->s_dependent_count ? -EBUSY : 0;
		if (!ret) {
			ret = configfs_detach_prep(dentry, &wait);
			if (ret)
				configfs_detach_rollback(dentry);
		}
		spin_unlock(&configfs_dirent_lock);
		mutex_unlock(&configfs_symlink_mutex);

		if (ret) {
			if (ret != -EAGAIN) {
				config_item_put(parent_item);
				return ret;
			}

			/* Wait until the racing operation terminates */
			inode_lock(d_inode(wait));
			inode_unlock(d_inode(wait));
			dput(wait);
		}
	} while (ret == -EAGAIN);

	frag = sd->s_frag;
	if (down_write_killable(&frag->frag_sem)) {
		spin_lock(&configfs_dirent_lock);
		configfs_detach_rollback(dentry);
		spin_unlock(&configfs_dirent_lock);
		config_item_put(parent_item);
		return -EINTR;
	}
	frag->frag_dead = true;
	up_write(&frag->frag_sem);

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

const struct inode_operations configfs_root_inode_operations = {
	.lookup		= configfs_lookup,
	.setattr	= configfs_setattr,
};

static int configfs_dir_open(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct configfs_dirent * parent_sd = dentry->d_fsdata;
	int err;

	inode_lock(d_inode(dentry));
	/*
	 * Fake invisibility if dir belongs to a group/default groups hierarchy
	 * being attached
	 */
	err = -ENOENT;
	if (configfs_dirent_is_ready(parent_sd)) {
		file->private_data = configfs_new_dirent(parent_sd, NULL, 0, NULL);
		if (IS_ERR(file->private_data))
			err = PTR_ERR(file->private_data);
		else
			err = 0;
	}
	inode_unlock(d_inode(dentry));

	return err;
}

static int configfs_dir_close(struct inode *inode, struct file *file)
{
	struct dentry * dentry = file->f_path.dentry;
	struct configfs_dirent * cursor = file->private_data;

	inode_lock(d_inode(dentry));
	spin_lock(&configfs_dirent_lock);
	list_del_init(&cursor->s_sibling);
	spin_unlock(&configfs_dirent_lock);
	inode_unlock(d_inode(dentry));

	release_configfs_dirent(cursor);

	return 0;
}

/* Relationship between s_mode and the DT_xxx types */
static inline unsigned char dt_type(struct configfs_dirent *sd)
{
	return (sd->s_mode >> 12) & 15;
}

static int configfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry *dentry = file->f_path.dentry;
	struct super_block *sb = dentry->d_sb;
	struct configfs_dirent * parent_sd = dentry->d_fsdata;
	struct configfs_dirent *cursor = file->private_data;
	struct list_head *p, *q = &cursor->s_sibling;
	ino_t ino = 0;

	if (!dir_emit_dots(file, ctx))
		return 0;
	spin_lock(&configfs_dirent_lock);
	if (ctx->pos == 2)
		list_move(q, &parent_sd->s_children);
	for (p = q->next; p != &parent_sd->s_children; p = p->next) {
		struct configfs_dirent *next;
		const char *name;
		int len;
		struct inode *inode = NULL;

		next = list_entry(p, struct configfs_dirent, s_sibling);
		if (!next->s_element)
			continue;

		/*
		 * We'll have a dentry and an inode for
		 * PINNED items and for open attribute
		 * files.  We lock here to prevent a race
		 * with configfs_d_iput() clearing
		 * s_dentry before calling iput().
		 *
		 * Why do we go to the trouble?  If
		 * someone has an attribute file open,
		 * the inode number should match until
		 * they close it.  Beyond that, we don't
		 * care.
		 */
		dentry = next->s_dentry;
		if (dentry)
			inode = d_inode(dentry);
		if (inode)
			ino = inode->i_ino;
		spin_unlock(&configfs_dirent_lock);
		if (!inode)
			ino = iunique(sb, 2);

		name = configfs_get_name(next);
		len = strlen(name);

		if (!dir_emit(ctx, name, len, ino, dt_type(next)))
			return 0;

		spin_lock(&configfs_dirent_lock);
		list_move(q, p);
		p = q;
		ctx->pos++;
	}
	spin_unlock(&configfs_dirent_lock);
	return 0;
}

static loff_t configfs_dir_lseek(struct file *file, loff_t offset, int whence)
{
	struct dentry * dentry = file->f_path.dentry;

	switch (whence) {
		case 1:
			offset += file->f_pos;
			fallthrough;
		case 0:
			if (offset >= 0)
				break;
			fallthrough;
		default:
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
	return offset;
}

const struct file_operations configfs_dir_operations = {
	.open		= configfs_dir_open,
	.release	= configfs_dir_close,
	.llseek		= configfs_dir_lseek,
	.read		= generic_read_dir,
	.iterate_shared	= configfs_readdir,
};

/**
 * configfs_register_group - creates a parent-child relation between two groups
 * @parent_group:	parent group
 * @group:		child group
 *
 * link groups, creates dentry for the child and attaches it to the
 * parent dentry.
 *
 * Return: 0 on success, negative errno code on error
 */
int configfs_register_group(struct config_group *parent_group,
			    struct config_group *group)
{
	struct configfs_subsystem *subsys = parent_group->cg_subsys;
	struct dentry *parent;
	struct configfs_fragment *frag;
	int ret;

	frag = new_fragment();
	if (!frag)
		return -ENOMEM;

	mutex_lock(&subsys->su_mutex);
	link_group(parent_group, group);
	mutex_unlock(&subsys->su_mutex);

	parent = parent_group->cg_item.ci_dentry;

	inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
	ret = create_default_group(parent_group, group, frag);
	if (ret)
		goto err_out;

	spin_lock(&configfs_dirent_lock);
	configfs_dir_set_ready(group->cg_item.ci_dentry->d_fsdata);
	spin_unlock(&configfs_dirent_lock);
	inode_unlock(d_inode(parent));
	put_fragment(frag);
	return 0;
err_out:
	inode_unlock(d_inode(parent));
	mutex_lock(&subsys->su_mutex);
	unlink_group(group);
	mutex_unlock(&subsys->su_mutex);
	put_fragment(frag);
	return ret;
}
EXPORT_SYMBOL(configfs_register_group);

/**
 * configfs_unregister_group() - unregisters a child group from its parent
 * @group: parent group to be unregistered
 *
 * Undoes configfs_register_group()
 */
void configfs_unregister_group(struct config_group *group)
{
	struct configfs_subsystem *subsys = group->cg_subsys;
	struct dentry *dentry = group->cg_item.ci_dentry;
	struct dentry *parent = group->cg_item.ci_parent->ci_dentry;
	struct configfs_dirent *sd = dentry->d_fsdata;
	struct configfs_fragment *frag = sd->s_frag;

	down_write(&frag->frag_sem);
	frag->frag_dead = true;
	up_write(&frag->frag_sem);

	inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
	spin_lock(&configfs_dirent_lock);
	configfs_detach_prep(dentry, NULL);
	spin_unlock(&configfs_dirent_lock);

	configfs_detach_group(&group->cg_item);
	d_inode(dentry)->i_flags |= S_DEAD;
	dont_mount(dentry);
	fsnotify_rmdir(d_inode(parent), dentry);
	d_delete(dentry);
	inode_unlock(d_inode(parent));

	dput(dentry);

	mutex_lock(&subsys->su_mutex);
	unlink_group(group);
	mutex_unlock(&subsys->su_mutex);
}
EXPORT_SYMBOL(configfs_unregister_group);

/**
 * configfs_register_default_group() - allocates and registers a child group
 * @parent_group:	parent group
 * @name:		child group name
 * @item_type:		child item type description
 *
 * boilerplate to allocate and register a child group with its parent. We need
 * kzalloc'ed memory because child's default_group is initially empty.
 *
 * Return: allocated config group or ERR_PTR() on error
 */
struct config_group *
configfs_register_default_group(struct config_group *parent_group,
				const char *name,
				const struct config_item_type *item_type)
{
	int ret;
	struct config_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return ERR_PTR(-ENOMEM);
	config_group_init_type_name(group, name, item_type);

	ret = configfs_register_group(parent_group, group);
	if (ret) {
		kfree(group);
		return ERR_PTR(ret);
	}
	return group;
}
EXPORT_SYMBOL(configfs_register_default_group);

/**
 * configfs_unregister_default_group() - unregisters and frees a child group
 * @group:	the group to act on
 */
void configfs_unregister_default_group(struct config_group *group)
{
	configfs_unregister_group(group);
	kfree(group);
}
EXPORT_SYMBOL(configfs_unregister_default_group);

int configfs_register_subsystem(struct configfs_subsystem *subsys)
{
	int err;
	struct config_group *group = &subsys->su_group;
	struct dentry *dentry;
	struct dentry *root;
	struct configfs_dirent *sd;
	struct configfs_fragment *frag;

	frag = new_fragment();
	if (!frag)
		return -ENOMEM;

	root = configfs_pin_fs();
	if (IS_ERR(root)) {
		put_fragment(frag);
		return PTR_ERR(root);
	}

	if (!group->cg_item.ci_name)
		group->cg_item.ci_name = group->cg_item.ci_namebuf;

	sd = root->d_fsdata;
	link_group(to_config_group(sd->s_element), group);

	inode_lock_nested(d_inode(root), I_MUTEX_PARENT);

	err = -ENOMEM;
	dentry = d_alloc_name(root, group->cg_item.ci_name);
	if (dentry) {
		d_add(dentry, NULL);

		err = configfs_attach_group(sd->s_element, &group->cg_item,
					    dentry, frag);
		if (err) {
			BUG_ON(d_inode(dentry));
			d_drop(dentry);
			dput(dentry);
		} else {
			spin_lock(&configfs_dirent_lock);
			configfs_dir_set_ready(dentry->d_fsdata);
			spin_unlock(&configfs_dirent_lock);
		}
	}

	inode_unlock(d_inode(root));

	if (err) {
		unlink_group(group);
		configfs_release_fs();
	}
	put_fragment(frag);

	return err;
}

void configfs_unregister_subsystem(struct configfs_subsystem *subsys)
{
	struct config_group *group = &subsys->su_group;
	struct dentry *dentry = group->cg_item.ci_dentry;
	struct dentry *root = dentry->d_sb->s_root;
	struct configfs_dirent *sd = dentry->d_fsdata;
	struct configfs_fragment *frag = sd->s_frag;

	if (dentry->d_parent != root) {
		pr_err("Tried to unregister non-subsystem!\n");
		return;
	}

	down_write(&frag->frag_sem);
	frag->frag_dead = true;
	up_write(&frag->frag_sem);

	inode_lock_nested(d_inode(root),
			  I_MUTEX_PARENT);
	inode_lock_nested(d_inode(dentry), I_MUTEX_CHILD);
	mutex_lock(&configfs_symlink_mutex);
	spin_lock(&configfs_dirent_lock);
	if (configfs_detach_prep(dentry, NULL)) {
		pr_err("Tried to unregister non-empty subsystem!\n");
	}
	spin_unlock(&configfs_dirent_lock);
	mutex_unlock(&configfs_symlink_mutex);
	configfs_detach_group(&group->cg_item);
	d_inode(dentry)->i_flags |= S_DEAD;
	dont_mount(dentry);
	fsnotify_rmdir(d_inode(root), dentry);
	inode_unlock(d_inode(dentry));

	d_delete(dentry);

	inode_unlock(d_inode(root));

	dput(dentry);

	unlink_group(group);
	configfs_release_fs();
}

EXPORT_SYMBOL(configfs_register_subsystem);
EXPORT_SYMBOL(configfs_unregister_subsystem);
