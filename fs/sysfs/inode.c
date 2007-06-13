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
#include <linux/errno.h>
#include <linux/sched.h>
#include <asm/semaphore.h>
#include "sysfs.h"

extern struct super_block * sysfs_sb;

static const struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

static const struct inode_operations sysfs_inode_operations ={
	.setattr	= sysfs_setattr,
};

void sysfs_delete_inode(struct inode *inode)
{
	/* Free the shadowed directory inode operations */
	if (sysfs_is_shadowed_inode(inode)) {
		kfree(inode->i_op);
		inode->i_op = NULL;
	}
	return generic_delete_inode(inode);
}

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


/*
 * sysfs has a different i_mutex lock order behavior for i_mutex than other
 * filesystems; sysfs i_mutex is called in many places with subsystem locks
 * held. At the same time, many of the VFS locking rules do not apply to
 * sysfs at all (cross directory rename for example). To untangle this mess
 * (which gives false positives in lockdep), we're giving sysfs inodes their
 * own class for i_mutex.
 */
static struct lock_class_key sysfs_inode_imutex_key;

struct inode * sysfs_new_inode(mode_t mode, struct sysfs_dirent * sd)
{
	struct inode * inode = new_inode(sysfs_sb);
	if (inode) {
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = &sysfs_aops;
		inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
		inode->i_op = &sysfs_inode_operations;
		inode->i_ino = sd->s_ino;
		lockdep_set_class(&inode->i_mutex, &sysfs_inode_imutex_key);

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

int sysfs_create(struct sysfs_dirent *sd, struct dentry *dentry, int mode,
		 int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
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

/**
 *	sysfs_drop_dentry - drop dentry for the specified sysfs_dirent
 *	@sd: target sysfs_dirent
 *
 *	Drop dentry for @sd.  @sd must have been unlinked from its
 *	parent on entry to this function such that it can't be looked
 *	up anymore.
 *
 *	@sd->s_dentry which is protected with sysfs_lock points to the
 *	currently associated dentry but we're not holding a reference
 *	to it and racing with dput().  Grab dcache_lock and verify
 *	dentry before dropping it.  If @sd->s_dentry is NULL or dput()
 *	beats us, no need to bother.
 */
void sysfs_drop_dentry(struct sysfs_dirent *sd)
{
	struct dentry *dentry = NULL, *parent = NULL;
	struct inode *dir;
	struct timespec curtime;

	/* We're not holding a reference to ->s_dentry dentry but the
	 * field will stay valid as long as sysfs_lock is held.
	 */
	spin_lock(&sysfs_lock);
	spin_lock(&dcache_lock);

	if (sd->s_dentry && sd->s_dentry->d_inode) {
		/* get dentry if it's there and dput() didn't kill it yet */
		dentry = dget_locked(sd->s_dentry);
		parent = dentry->d_parent;
	} else if (sd->s_parent->s_dentry->d_inode) {
		/* We need to update the parent even if dentry for the
		 * victim itself doesn't exist.
		 */
		parent = dget_locked(sd->s_parent->s_dentry);
	}

	/* drop */
	if (dentry) {
		spin_lock(&dentry->d_lock);
		__d_drop(dentry);
		spin_unlock(&dentry->d_lock);
	}

	spin_unlock(&dcache_lock);
	spin_unlock(&sysfs_lock);

	/* nothing to do if the parent isn't in dcache */
	if (!parent)
		return;

	/* adjust nlink and update timestamp */
	dir = parent->d_inode;
	mutex_lock(&dir->i_mutex);

	curtime = CURRENT_TIME;

	dir->i_ctime = dir->i_mtime = curtime;

	if (dentry) {
		dentry->d_inode->i_ctime = curtime;
		drop_nlink(dentry->d_inode);
		if (sd->s_type & SYSFS_DIR) {
			drop_nlink(dentry->d_inode);
			drop_nlink(dir);
			/* XXX: unpin if directory, this will go away soon */
			dput(dentry);
		}
	}

	mutex_unlock(&dir->i_mutex);

	/* bye bye */
	if (dentry)
		dput(dentry);
	else
		dput(parent);
}

int sysfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct sysfs_dirent * sd;
	struct sysfs_dirent * parent_sd;
	int found = 0;

	if (!dir)
		return -ENOENT;

	if (dir->d_inode == NULL)
		/* no inode means this hasn't been made visible yet */
		return -ENOENT;

	parent_sd = dir->d_fsdata;
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_type)
			continue;
		if (!strcmp(sd->s_name, name)) {
			list_del_init(&sd->s_sibling);
			found = 1;
			break;
		}
	}
	mutex_unlock(&dir->d_inode->i_mutex);

	if (!found)
		return -ENOENT;

	sysfs_drop_dentry(sd);
	sysfs_deactivate(sd);
	sysfs_put(sd);

	return 0;
}
