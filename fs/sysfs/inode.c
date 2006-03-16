/*
 * inode.c - basic inode and dentry operations.
 *
 * sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG 

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include "sysfs.h"

extern struct super_block * sysfs_sb;

static struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static struct inode_operations sysfs_inode_operations ={
	.setattr	= sysfs_setattr,
};

int sysfs_setattr(struct dentry * dentry, struct iattr * iattr)
{
	struct inode * inode = dentry->d_inode;
	struct sysfs_dirent * sd = dentry->d_fsdata;
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
		sd_iattr = kmalloc(sizeof(struct iattr), GFP_KERNEL);
		if (!sd_iattr)
			return -ENOMEM;
		/* assign default attributes */
		memset(sd_iattr, 0, sizeof(struct iattr));
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

struct inode * sysfs_new_inode(mode_t mode, struct sysfs_dirent * sd)
{
	struct inode * inode = new_inode(sysfs_sb);
	if (inode) {
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &sysfs_aops;
		inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
		inode->i_op = &sysfs_inode_operations;

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

int sysfs_create(struct dentry * dentry, int mode, int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
			struct sysfs_dirent * sd = dentry->d_fsdata;
			if ((inode = sysfs_new_inode(mode, sd))) {
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
		if (S_ISDIR(mode))
			dget(dentry);  /* pin only directory dentry in core */
	} else
		iput(inode);
 Done:
	return error;
}

/*
 * Get the name for corresponding element represented by the given sysfs_dirent
 */
const unsigned char * sysfs_get_name(struct sysfs_dirent *sd)
{
	struct attribute * attr;
	struct bin_attribute * bin_attr;
	struct sysfs_symlink  * sl;

	if (!sd || !sd->s_element)
		BUG();

	switch (sd->s_type) {
		case SYSFS_DIR:
			/* Always have a dentry so use that */
			return sd->s_dentry->d_name.name;

		case SYSFS_KOBJ_ATTR:
			attr = sd->s_element;
			return attr->name;

		case SYSFS_KOBJ_BIN_ATTR:
			bin_attr = sd->s_element;
			return bin_attr->attr.name;

		case SYSFS_KOBJ_LINK:
			sl = sd->s_element;
			return sl->link_name;
	}
	return NULL;
}


/*
 * Unhashes the dentry corresponding to given sysfs_dirent
 * Called with parent inode's i_mutex held.
 */
void sysfs_drop_dentry(struct sysfs_dirent * sd, struct dentry * parent)
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

void sysfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct sysfs_dirent * sd;
	struct sysfs_dirent * parent_sd;

	if (!dir)
		return;

	if (dir->d_inode == NULL)
		/* no inode means this hasn't been made visible yet */
		return;

	parent_sd = dir->d_fsdata;
	mutex_lock(&dir->d_inode->i_mutex);
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element)
			continue;
		if (!strcmp(sysfs_get_name(sd), name)) {
			list_del_init(&sd->s_sibling);
			sysfs_drop_dentry(sd, dir);
			sysfs_put(sd);
			break;
		}
	}
	mutex_unlock(&dir->d_inode->i_mutex);
}
