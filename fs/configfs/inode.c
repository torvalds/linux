// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * inode.c - basic inode and dentry operations.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * Please see Documentation/filesystems/configfs.rst for more
 * information.
 */

#undef DEBUG

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <linux/lockdep.h>
#include <linux/slab.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

#ifdef CONFIG_LOCKDEP
static struct lock_class_key default_group_class[MAX_LOCK_DEPTH];
#endif

static const struct inode_operations configfs_inode_operations ={
	.setattr	= configfs_setattr,
};

int configfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		     struct iattr *iattr)
{
	struct inode * inode = d_inode(dentry);
	struct configfs_dirent * sd = dentry->d_fsdata;
	struct iattr * sd_iattr;
	unsigned int ia_valid = iattr->ia_valid;
	int error;

	if (!sd)
		return -EINVAL;

	sd_iattr = sd->s_iattr;
	if (!sd_iattr) {
		/* setting attributes for the first time, allocate now */
		sd_iattr = kzalloc(sizeof(struct iattr), GFP_KERNEL);
		if (!sd_iattr)
			return -ENOMEM;
		/* assign default attributes */
		sd_iattr->ia_mode = sd->s_mode;
		sd_iattr->ia_uid = GLOBAL_ROOT_UID;
		sd_iattr->ia_gid = GLOBAL_ROOT_GID;
		sd_iattr->ia_atime = sd_iattr->ia_mtime =
			sd_iattr->ia_ctime = current_time(inode);
		sd->s_iattr = sd_iattr;
	}
	/* attributes were changed atleast once in past */

	error = simple_setattr(idmap, dentry, iattr);
	if (error)
		return error;

	if (ia_valid & ATTR_UID)
		sd_iattr->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		sd_iattr->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		sd_iattr->ia_atime = iattr->ia_atime;
	if (ia_valid & ATTR_MTIME)
		sd_iattr->ia_mtime = iattr->ia_mtime;
	if (ia_valid & ATTR_CTIME)
		sd_iattr->ia_ctime = iattr->ia_ctime;
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		sd_iattr->ia_mode = sd->s_mode = mode;
	}

	return error;
}

static inline void set_default_inode_attr(struct inode * inode, umode_t mode)
{
	inode->i_mode = mode;
	simple_inode_init_ts(inode);
}

static inline void set_inode_attr(struct inode * inode, struct iattr * iattr)
{
	inode->i_mode = iattr->ia_mode;
	inode->i_uid = iattr->ia_uid;
	inode->i_gid = iattr->ia_gid;
	inode_set_atime_to_ts(inode, iattr->ia_atime);
	inode_set_mtime_to_ts(inode, iattr->ia_mtime);
	inode_set_ctime_to_ts(inode, iattr->ia_ctime);
}

struct inode *configfs_new_inode(umode_t mode, struct configfs_dirent *sd,
				 struct super_block *s)
{
	struct inode * inode = new_inode(s);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mapping->a_ops = &ram_aops;
		inode->i_op = &configfs_inode_operations;

		if (sd->s_iattr) {
			/* sysfs_dirent has non-default attributes
			 * get them for the new inode from persistent copy
			 * in sysfs_dirent
			 */
			set_inode_attr(inode, sd->s_iattr);
		} else
			set_default_inode_attr(inode, mode);
	}
	return inode;
}

#ifdef CONFIG_LOCKDEP

static void configfs_set_inode_lock_class(struct configfs_dirent *sd,
					  struct inode *inode)
{
	int depth = sd->s_depth;

	if (depth > 0) {
		if (depth <= ARRAY_SIZE(default_group_class)) {
			lockdep_set_class(&inode->i_rwsem,
					  &default_group_class[depth - 1]);
		} else {
			/*
			 * In practice the maximum level of locking depth is
			 * already reached. Just inform about possible reasons.
			 */
			pr_info("Too many levels of inodes for the locking correctness validator.\n");
			pr_info("Spurious warnings may appear.\n");
		}
	}
}

#else /* CONFIG_LOCKDEP */

static void configfs_set_inode_lock_class(struct configfs_dirent *sd,
					  struct inode *inode)
{
}

#endif /* CONFIG_LOCKDEP */

struct inode *configfs_create(struct dentry *dentry, umode_t mode)
{
	struct inode *inode = NULL;
	struct configfs_dirent *sd;
	struct inode *p_inode;

	if (!dentry)
		return ERR_PTR(-ENOENT);

	if (d_really_is_positive(dentry))
		return ERR_PTR(-EEXIST);

	sd = dentry->d_fsdata;
	inode = configfs_new_inode(mode, sd, dentry->d_sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	p_inode = d_inode(dentry->d_parent);
	inode_set_mtime_to_ts(p_inode, inode_set_ctime_current(p_inode));
	configfs_set_inode_lock_class(sd, inode);
	return inode;
}

/*
 * Get the name for corresponding element represented by the given configfs_dirent
 */
const unsigned char * configfs_get_name(struct configfs_dirent *sd)
{
	struct configfs_attribute *attr;

	BUG_ON(!sd || !sd->s_element);

	/* These always have a dentry, so use that */
	if (sd->s_type & (CONFIGFS_DIR | CONFIGFS_ITEM_LINK))
		return sd->s_dentry->d_name.name;

	if (sd->s_type & (CONFIGFS_ITEM_ATTR | CONFIGFS_ITEM_BIN_ATTR)) {
		attr = sd->s_element;
		return attr->ca_name;
	}
	return NULL;
}


/*
 * Unhashes the dentry corresponding to given configfs_dirent
 * Called with parent inode's i_mutex held.
 */
void configfs_drop_dentry(struct configfs_dirent * sd, struct dentry * parent)
{
	struct dentry * dentry = sd->s_dentry;

	if (dentry) {
		spin_lock(&dentry->d_lock);
		if (simple_positive(dentry)) {
			dget_dlock(dentry);
			__d_drop(dentry);
			spin_unlock(&dentry->d_lock);
			simple_unlink(d_inode(parent), dentry);
		} else
			spin_unlock(&dentry->d_lock);
	}
}
