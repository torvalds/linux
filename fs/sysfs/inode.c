/*
 * fs/sysfs/inode.c - basic sysfs inode and dentry operations
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
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
#include <linux/xattr.h>
#include <linux/security.h>
#include "sysfs.h"

extern struct super_block * sysfs_sb;

static const struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.name		= "sysfs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

static const struct inode_operations sysfs_inode_operations ={
	.setattr	= sysfs_setattr,
	.setxattr	= sysfs_setxattr,
};

int __init sysfs_inode_init(void)
{
	return bdi_init(&sysfs_backing_dev_info);
}

struct sysfs_inode_attrs *sysfs_init_inode_attrs(struct sysfs_dirent *sd)
{
	struct sysfs_inode_attrs *attrs;
	struct iattr *iattrs;

	attrs = kzalloc(sizeof(struct sysfs_inode_attrs), GFP_KERNEL);
	if (!attrs)
		return NULL;
	iattrs = &attrs->ia_iattr;

	/* assign default attributes */
	iattrs->ia_mode = sd->s_mode;
	iattrs->ia_uid = 0;
	iattrs->ia_gid = 0;
	iattrs->ia_atime = iattrs->ia_mtime = iattrs->ia_ctime = CURRENT_TIME;

	return attrs;
}
int sysfs_setattr(struct dentry * dentry, struct iattr * iattr)
{
	struct inode * inode = dentry->d_inode;
	struct sysfs_dirent * sd = dentry->d_fsdata;
	struct sysfs_inode_attrs *sd_attrs;
	struct iattr *iattrs;
	unsigned int ia_valid = iattr->ia_valid;
	int error;

	if (!sd)
		return -EINVAL;

	sd_attrs = sd->s_iattr;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;

	iattr->ia_valid &= ~ATTR_SIZE; /* ignore size changes */

	error = inode_setattr(inode, iattr);
	if (error)
		return error;

	if (!sd_attrs) {
		/* setting attributes for the first time, allocate now */
		sd_attrs = sysfs_init_inode_attrs(sd);
		if (!sd_attrs)
			return -ENOMEM;
		sd->s_iattr = sd_attrs;
	}
	/* attributes were changed at least once in past */
	iattrs = &sd_attrs->ia_iattr;

	if (ia_valid & ATTR_UID)
		iattrs->ia_uid = iattr->ia_uid;
	if (ia_valid & ATTR_GID)
		iattrs->ia_gid = iattr->ia_gid;
	if (ia_valid & ATTR_ATIME)
		iattrs->ia_atime = timespec_trunc(iattr->ia_atime,
			inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MTIME)
		iattrs->ia_mtime = timespec_trunc(iattr->ia_mtime,
			inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_CTIME)
		iattrs->ia_ctime = timespec_trunc(iattr->ia_ctime,
			inode->i_sb->s_time_gran);
	if (ia_valid & ATTR_MODE) {
		umode_t mode = iattr->ia_mode;

		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			mode &= ~S_ISGID;
		iattrs->ia_mode = sd->s_mode = mode;
	}
	return error;
}

int sysfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		size_t size, int flags)
{
	struct sysfs_dirent *sd = dentry->d_fsdata;
	struct sysfs_inode_attrs *iattrs;
	void *secdata;
	int error;
	u32 secdata_len = 0;

	if (!sd)
		return -EINVAL;
	if (!sd->s_iattr)
		sd->s_iattr = sysfs_init_inode_attrs(sd);
	if (!sd->s_iattr)
		return -ENOMEM;

	iattrs = sd->s_iattr;

	if (!strncmp(name, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN)) {
		const char *suffix = name + XATTR_SECURITY_PREFIX_LEN;
		error = security_inode_setsecurity(dentry->d_inode, suffix,
						value, size, flags);
		if (error)
			goto out;
		error = security_inode_getsecctx(dentry->d_inode,
						&secdata, &secdata_len);
		if (error)
			goto out;
		if (iattrs->ia_secdata)
			security_release_secctx(iattrs->ia_secdata,
						iattrs->ia_secdata_len);
		iattrs->ia_secdata = secdata;
		iattrs->ia_secdata_len = secdata_len;

	} else
		return -EINVAL;
out:
	return error;
}

static inline void set_default_inode_attr(struct inode * inode, mode_t mode)
{
	inode->i_mode = mode;
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

static int sysfs_count_nlink(struct sysfs_dirent *sd)
{
	struct sysfs_dirent *child;
	int nr = 0;

	for (child = sd->s_dir.children; child; child = child->s_sibling)
		if (sysfs_type(child) == SYSFS_DIR)
			nr++;

	return nr + 2;
}

static void sysfs_init_inode(struct sysfs_dirent *sd, struct inode *inode)
{
	struct bin_attribute *bin_attr;
	struct sysfs_inode_attrs *iattrs;

	inode->i_private = sysfs_get(sd);
	inode->i_mapping->a_ops = &sysfs_aops;
	inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
	inode->i_op = &sysfs_inode_operations;
	inode->i_ino = sd->s_ino;
	lockdep_set_class(&inode->i_mutex, &sysfs_inode_imutex_key);

	iattrs = sd->s_iattr;
	if (iattrs) {
		/* sysfs_dirent has non-default attributes
		 * get them for the new inode from persistent copy
		 * in sysfs_dirent
		 */
		set_inode_attr(inode, &iattrs->ia_iattr);
		if (iattrs->ia_secdata)
			security_inode_notifysecctx(inode,
						iattrs->ia_secdata,
						iattrs->ia_secdata_len);
	} else
		set_default_inode_attr(inode, sd->s_mode);

	/* initialize inode according to type */
	switch (sysfs_type(sd)) {
	case SYSFS_DIR:
		inode->i_op = &sysfs_dir_inode_operations;
		inode->i_fop = &sysfs_dir_operations;
		inode->i_nlink = sysfs_count_nlink(sd);
		break;
	case SYSFS_KOBJ_ATTR:
		inode->i_size = PAGE_SIZE;
		inode->i_fop = &sysfs_file_operations;
		break;
	case SYSFS_KOBJ_BIN_ATTR:
		bin_attr = sd->s_bin_attr.bin_attr;
		inode->i_size = bin_attr->size;
		inode->i_fop = &bin_fops;
		break;
	case SYSFS_KOBJ_LINK:
		inode->i_op = &sysfs_symlink_inode_operations;
		break;
	default:
		BUG();
	}

	unlock_new_inode(inode);
}

/**
 *	sysfs_get_inode - get inode for sysfs_dirent
 *	@sd: sysfs_dirent to allocate inode for
 *
 *	Get inode for @sd.  If such inode doesn't exist, a new inode
 *	is allocated and basics are initialized.  New inode is
 *	returned locked.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	Pointer to allocated inode on success, NULL on failure.
 */
struct inode * sysfs_get_inode(struct sysfs_dirent *sd)
{
	struct inode *inode;

	inode = iget_locked(sysfs_sb, sd->s_ino);
	if (inode && (inode->i_state & I_NEW))
		sysfs_init_inode(sd, inode);

	return inode;
}

/*
 * The sysfs_dirent serves as both an inode and a directory entry for sysfs.
 * To prevent the sysfs inode numbers from being freed prematurely we take a
 * reference to sysfs_dirent from the sysfs inode.  A
 * super_operations.delete_inode() implementation is needed to drop that
 * reference upon inode destruction.
 */
void sysfs_delete_inode(struct inode *inode)
{
	struct sysfs_dirent *sd  = inode->i_private;

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	sysfs_put(sd);
}

int sysfs_hash_and_remove(struct sysfs_dirent *dir_sd, const char *name)
{
	struct sysfs_addrm_cxt acxt;
	struct sysfs_dirent *sd;

	if (!dir_sd)
		return -ENOENT;

	sysfs_addrm_start(&acxt, dir_sd);

	sd = sysfs_find_dirent(dir_sd, name);
	if (sd)
		sysfs_remove_one(&acxt, sd);

	sysfs_addrm_finish(&acxt);

	if (sd)
		return 0;
	else
		return -ENOENT;
}
