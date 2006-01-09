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

#include <linux/configfs.h>
#include "configfs_internal.h"

extern struct super_block * configfs_sb;

static struct address_space_operations configfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct backing_dev_info configfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

struct inode * configfs_new_inode(mode_t mode)
{
	struct inode * inode = new_inode(configfs_sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->a_ops = &configfs_aops;
		inode->i_mapping->backing_dev_info = &configfs_backing_dev_info;
	}
	return inode;
}

int configfs_create(struct dentry * dentry, int mode, int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
			if ((inode = configfs_new_inode(mode))) {
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
	struct attribute * attr;

	if (!sd || !sd->s_element)
		BUG();

	/* These always have a dentry, so use that */
	if (sd->s_type & (CONFIGFS_DIR | CONFIGFS_ITEM_LINK))
		return sd->s_dentry->d_name.name;

	if (sd->s_type & CONFIGFS_ITEM_ATTR) {
		attr = sd->s_element;
		return attr->name;
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
		if (!(d_unhashed(dentry) && dentry->d_inode)) {
			dget_locked(dentry);
			__d_drop(dentry);
			spin_unlock(&dcache_lock);
			simple_unlink(parent->d_inode, dentry);
		} else
			spin_unlock(&dcache_lock);
	}
}

void configfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct configfs_dirent * sd;
	struct configfs_dirent * parent_sd = dir->d_fsdata;

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


