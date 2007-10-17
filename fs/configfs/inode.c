/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * inode.c - basic inode and dentry operations.
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
 *
 * Please see Documentation/filesystems/configfs.txt for more information.
 */

#undef DEBUG

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/sched.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

extern struct super_block * configfs_sb;

static const struct address_space_operations configfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
};

static struct backing_dev_info configfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static const struct inode_operations configfs_inode_operations ={
	.setattr	= configfs_setattr,
};

int configfs_setattr(struct dentry * dentry, struct iattr * iattr)
{
	struct inode * inode = dentry->d_inode;
	struct configfs_dirent * sd = dentry->d_fsdata;
	struct iattr * sd_iattr;
	unsigned int ia_valid = iattr->ia_valid;
	int error;

	if (!sd)
		return -EINVAL;

	sd_iattr = sd->s_iattr;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;

	error = inode_setattr(inode, iattr);
	if (error)
		return error;

	if (!sd_iattr) {
		/* setting attributes for the first time, allocate now */
		sd_iattr = kzalloc(sizeof(struct iattr), GFP_KERNEL);
		if (!sd_iattr)
			return -ENOMEM;
		/* assign default attributes */
		sd_iattr->ia_mode = sd->s_mode;
		sd_iattr->ia_uid = 0;
		sd_iattr->ia_gid = 0;
		sd_iattr->ia_atime = sd_iattr->ia_mtime = sd_iattr->ia_ctime = CURRENT_TIME;
		sd->s_iattr = sd_iattr;
	}

	/* attributes were changed atleast once in past */

	if (ia_valid & ATTR_UID)
		sd_iattr->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		sd_iattr->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		sd_iattr->ia_atime = timespec_trunc(iattr->ia_atime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MTIME)
		sd_iattr->ia_mtime = timespec_trunc(iattr->ia_mtime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_CTIME)
		sd_iattr->ia_ctime = timespec_trunc(iattr->ia_ctime,
						inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		sd_iattr->ia_mode = sd->s_mode = mode;
	}

	return error;
}

static inline void set_default_inode_attr(struct inode * inode, mode_t mode)
{
	inode->i_mode = mode;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
}

static inline void set_inode_attr(struct inode * inode, struct iattr * iattr)
{
	inode->i_mode = iattr->ia_mode;
	inode->i_uid = iattr->ia_uid;
	inode->i_gid = iattr->ia_gid;
	inode->i_atime = iattr->ia_atime;
	inode->i_mtime = iattr->ia_mtime;
	inode->i_ctime = iattr->ia_ctime;
}

struct inode * configfs_new_inode(mode_t mode, struct configfs_dirent * sd)
{
	struct inode * inode = new_inode(configfs_sb);
	if (inode) {
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &configfs_aops;
		inode->i_mapping->backing_dev_info = &configfs_backing_dev_info;
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

int configfs_create(struct dentry * dentry, int mode, int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
			struct configfs_dirent *sd = dentry->d_fsdata;
			if ((inode = configfs_new_inode(mode, sd))) {
				if (dentry->d_parent && dentry->d_parent->d_inode) {
					struct inode *p_inode = dentry->d_parent->d_inode;
					p_inode->i_mtime = p_inode->i_ctime = CURRENT_TIME;
				}
				goto Proceed;
			}
			else
				error = -ENOMEM;
		} else
			error = -EEXIST;
	} else
		error = -ENOENT;
	goto Done;

 Proceed:
	if (init)
		error = init(inode);
	if (!error) {
		d_instantiate(dentry, inode);
		if (S_ISDIR(mode) || S_ISLNK(mode))
			dget(dentry);  /* pin link and directory dentries in core */
	} else
		iput(inode);
 Done:
	return error;
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

	if (sd->s_type & CONFIGFS_ITEM_ATTR) {
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
		spin_lock(&dcache_lock);
		spin_lock(&dentry->d_lock);
		if (!(d_unhashed(dentry) && dentry->d_inode)) {
			dget_locked(dentry);
			__d_drop(dentry);
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dcache_lock);
			simple_unlink(parent->d_inode, dentry);
		} else {
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dcache_lock);
		}
	}
}

void configfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct configfs_dirent * sd;
	struct configfs_dirent * parent_sd = dir->d_fsdata;

	if (dir->d_inode == NULL)
		/* no inode means this hasn't been made visible yet */
		return;

	mutex_lock(&dir->d_inode->i_mutex);
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element)
			continue;
		if (!strcmp(configfs_get_name(sd), name)) {
			list_del_init(&sd->s_sibling);
			configfs_drop_dentry(sd, dir);
			configfs_put(sd);
			break;
		}
	}
	mutex_unlock(&dir->d_inode->i_mutex);
}

int __init configfs_inode_init(void)
{
	return bdi_init(&configfs_backing_dev_info);
}

void __exit configfs_inode_exit(void)
{
	bdi_destroy(&configfs_backing_dev_info);
}
