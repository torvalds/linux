/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmfs.c
 *
 * Code which implements the kernel side of a minimal userspace
 * interface to our DLM. This file handles the virtual file system
 * used for communication with userspace. Credit should go to ramfs,
 * which was a template for the fs side of this module.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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
 */

/* Simple VFS hooks based on: */
/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>

#include <asm/uaccess.h>


#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "dlmapi.h"

#include "userdlm.h"

#include "dlmfsver.h"

#define MLOG_MASK_PREFIX ML_DLMFS
#include "cluster/masklog.h"

#include "ocfs2_lockingver.h"

static const struct super_operations dlmfs_ops;
static const struct file_operations dlmfs_file_operations;
static const struct inode_operations dlmfs_dir_inode_operations;
static const struct inode_operations dlmfs_root_inode_operations;
static const struct inode_operations dlmfs_file_inode_operations;
static struct kmem_cache *dlmfs_inode_cache;

struct workqueue_struct *user_dlm_worker;

/*
 * This is the userdlmfs locking protocol version.
 *
 * See fs/ocfs2/dlmglue.c for more details on locking versions.
 */
static const struct dlm_protocol_version user_locking_protocol = {
	.pv_major = OCFS2_LOCKING_PROTOCOL_MAJOR,
	.pv_minor = OCFS2_LOCKING_PROTOCOL_MINOR,
};

/*
 * decodes a set of open flags into a valid lock level and a set of flags.
 * returns < 0 if we have invalid flags
 * flags which mean something to us:
 * O_RDONLY -> PRMODE level
 * O_WRONLY -> EXMODE level
 *
 * O_NONBLOCK -> LKM_NOQUEUE
 */
static int dlmfs_decode_open_flags(int open_flags,
				   int *level,
				   int *flags)
{
	if (open_flags & (O_WRONLY|O_RDWR))
		*level = LKM_EXMODE;
	else
		*level = LKM_PRMODE;

	*flags = 0;
	if (open_flags & O_NONBLOCK)
		*flags |= LKM_NOQUEUE;

	return 0;
}

static int dlmfs_file_open(struct inode *inode,
			   struct file *file)
{
	int status, level, flags;
	struct dlmfs_filp_private *fp = NULL;
	struct dlmfs_inode_private *ip;

	if (S_ISDIR(inode->i_mode))
		BUG();

	mlog(0, "open called on inode %lu, flags 0x%x\n", inode->i_ino,
		file->f_flags);

	status = dlmfs_decode_open_flags(file->f_flags, &level, &flags);
	if (status < 0)
		goto bail;

	/* We don't want to honor O_APPEND at read/write time as it
	 * doesn't make sense for LVB writes. */
	file->f_flags &= ~O_APPEND;

	fp = kmalloc(sizeof(*fp), GFP_NOFS);
	if (!fp) {
		status = -ENOMEM;
		goto bail;
	}
	fp->fp_lock_level = level;

	ip = DLMFS_I(inode);

	status = user_dlm_cluster_lock(&ip->ip_lockres, level, flags);
	if (status < 0) {
		/* this is a strange error to return here but I want
		 * to be able userspace to be able to distinguish a
		 * valid lock request from one that simply couldn't be
		 * granted. */
		if (flags & LKM_NOQUEUE && status == -EAGAIN)
			status = -ETXTBSY;
		kfree(fp);
		goto bail;
	}

	file->private_data = fp;
bail:
	return status;
}

static int dlmfs_file_release(struct inode *inode,
			      struct file *file)
{
	int level, status;
	struct dlmfs_inode_private *ip = DLMFS_I(inode);
	struct dlmfs_filp_private *fp =
		(struct dlmfs_filp_private *) file->private_data;

	if (S_ISDIR(inode->i_mode))
		BUG();

	mlog(0, "close called on inode %lu\n", inode->i_ino);

	status = 0;
	if (fp) {
		level = fp->fp_lock_level;
		if (level != LKM_IVMODE)
			user_dlm_cluster_unlock(&ip->ip_lockres, level);

		kfree(fp);
		file->private_data = NULL;
	}

	return 0;
}

static ssize_t dlmfs_file_read(struct file *filp,
			       char __user *buf,
			       size_t count,
			       loff_t *ppos)
{
	int bytes_left;
	ssize_t readlen;
	char *lvb_buf;
	struct inode *inode = filp->f_path.dentry->d_inode;

	mlog(0, "inode %lu, count = %zu, *ppos = %llu\n",
		inode->i_ino, count, *ppos);

	if (*ppos >= i_size_read(inode))
		return 0;

	if (!count)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	/* don't read past the lvb */
	if ((count + *ppos) > i_size_read(inode))
		readlen = i_size_read(inode) - *ppos;
	else
		readlen = count;

	lvb_buf = kmalloc(readlen, GFP_NOFS);
	if (!lvb_buf)
		return -ENOMEM;

	user_dlm_read_lvb(inode, lvb_buf, readlen);
	bytes_left = __copy_to_user(buf, lvb_buf, readlen);
	readlen -= bytes_left;

	kfree(lvb_buf);

	*ppos = *ppos + readlen;

	mlog(0, "read %zd bytes\n", readlen);
	return readlen;
}

static ssize_t dlmfs_file_write(struct file *filp,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	int bytes_left;
	ssize_t writelen;
	char *lvb_buf;
	struct inode *inode = filp->f_path.dentry->d_inode;

	mlog(0, "inode %lu, count = %zu, *ppos = %llu\n",
		inode->i_ino, count, *ppos);

	if (*ppos >= i_size_read(inode))
		return -ENOSPC;

	if (!count)
		return 0;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;

	/* don't write past the lvb */
	if ((count + *ppos) > i_size_read(inode))
		writelen = i_size_read(inode) - *ppos;
	else
		writelen = count - *ppos;

	lvb_buf = kmalloc(writelen, GFP_NOFS);
	if (!lvb_buf)
		return -ENOMEM;

	bytes_left = copy_from_user(lvb_buf, buf, writelen);
	writelen -= bytes_left;
	if (writelen)
		user_dlm_write_lvb(inode, lvb_buf, writelen);

	kfree(lvb_buf);

	*ppos = *ppos + writelen;
	mlog(0, "wrote %zd bytes\n", writelen);
	return writelen;
}

static void dlmfs_init_once(void *foo)
{
	struct dlmfs_inode_private *ip =
		(struct dlmfs_inode_private *) foo;

	ip->ip_dlm = NULL;
	ip->ip_parent = NULL;

	inode_init_once(&ip->ip_vfs_inode);
}

static struct inode *dlmfs_alloc_inode(struct super_block *sb)
{
	struct dlmfs_inode_private *ip;

	ip = kmem_cache_alloc(dlmfs_inode_cache, GFP_NOFS);
	if (!ip)
		return NULL;

	return &ip->ip_vfs_inode;
}

static void dlmfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(dlmfs_inode_cache, DLMFS_I(inode));
}

static void dlmfs_clear_inode(struct inode *inode)
{
	int status;
	struct dlmfs_inode_private *ip;

	if (!inode)
		return;

	mlog(0, "inode %lu\n", inode->i_ino);

	ip = DLMFS_I(inode);

	if (S_ISREG(inode->i_mode)) {
		status = user_dlm_destroy_lock(&ip->ip_lockres);
		if (status < 0)
			mlog_errno(status);
		iput(ip->ip_parent);
		goto clear_fields;
	}

	mlog(0, "we're a directory, ip->ip_dlm = 0x%p\n", ip->ip_dlm);
	/* we must be a directory. If required, lets unregister the
	 * dlm context now. */
	if (ip->ip_dlm)
		user_dlm_unregister_context(ip->ip_dlm);
clear_fields:
	ip->ip_parent = NULL;
	ip->ip_dlm = NULL;
}

static struct backing_dev_info dlmfs_backing_dev_info = {
	.name		= "ocfs2-dlmfs",
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK,
};

static struct inode *dlmfs_get_root_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	int mode = S_IFDIR | 0755;
	struct dlmfs_inode_private *ip;

	if (inode) {
		ip = DLMFS_I(inode);

		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
		inode->i_mapping->backing_dev_info = &dlmfs_backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inc_nlink(inode);

		inode->i_fop = &simple_dir_operations;
		inode->i_op = &dlmfs_root_inode_operations;
	}

	return inode;
}

static struct inode *dlmfs_get_inode(struct inode *parent,
				     struct dentry *dentry,
				     int mode)
{
	struct super_block *sb = parent->i_sb;
	struct inode * inode = new_inode(sb);
	struct dlmfs_inode_private *ip;

	if (!inode)
		return NULL;

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_mapping->backing_dev_info = &dlmfs_backing_dev_info;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	ip = DLMFS_I(inode);
	ip->ip_dlm = DLMFS_I(parent)->ip_dlm;

	switch (mode & S_IFMT) {
	default:
		/* for now we don't support anything other than
		 * directories and regular files. */
		BUG();
		break;
	case S_IFREG:
		inode->i_op = &dlmfs_file_inode_operations;
		inode->i_fop = &dlmfs_file_operations;

		i_size_write(inode,  DLM_LVB_LEN);

		user_dlm_lock_res_init(&ip->ip_lockres, dentry);

		/* released at clear_inode time, this insures that we
		 * get to drop the dlm reference on each lock *before*
		 * we call the unregister code for releasing parent
		 * directories. */
		ip->ip_parent = igrab(parent);
		BUG_ON(!ip->ip_parent);
		break;
	case S_IFDIR:
		inode->i_op = &dlmfs_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* directory inodes start off with i_nlink ==
		 * 2 (for "." entry) */
		inc_nlink(inode);
		break;
	}

	if (parent->i_mode & S_ISGID) {
		inode->i_gid = parent->i_gid;
		if (S_ISDIR(mode))
			inode->i_mode |= S_ISGID;
	}

	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int dlmfs_mkdir(struct inode * dir,
		       struct dentry * dentry,
		       int mode)
{
	int status;
	struct inode *inode = NULL;
	struct qstr *domain = &dentry->d_name;
	struct dlmfs_inode_private *ip;
	struct dlm_ctxt *dlm;
	struct dlm_protocol_version proto = user_locking_protocol;

	mlog(0, "mkdir %.*s\n", domain->len, domain->name);

	/* verify that we have a proper domain */
	if (domain->len >= O2NM_MAX_NAME_LEN) {
		status = -EINVAL;
		mlog(ML_ERROR, "invalid domain name for directory.\n");
		goto bail;
	}

	inode = dlmfs_get_inode(dir, dentry, mode | S_IFDIR);
	if (!inode) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	ip = DLMFS_I(inode);

	dlm = user_dlm_register_context(domain, &proto);
	if (IS_ERR(dlm)) {
		status = PTR_ERR(dlm);
		mlog(ML_ERROR, "Error %d could not register domain \"%.*s\"\n",
		     status, domain->len, domain->name);
		goto bail;
	}
	ip->ip_dlm = dlm;

	inc_nlink(dir);
	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */

	status = 0;
bail:
	if (status < 0)
		iput(inode);
	return status;
}

static int dlmfs_create(struct inode *dir,
			struct dentry *dentry,
			int mode,
			struct nameidata *nd)
{
	int status = 0;
	struct inode *inode;
	struct qstr *name = &dentry->d_name;

	mlog(0, "create %.*s\n", name->len, name->name);

	/* verify name is valid and doesn't contain any dlm reserved
	 * characters */
	if (name->len >= USER_DLM_LOCK_ID_MAX_LEN ||
	    name->name[0] == '$') {
		status = -EINVAL;
		mlog(ML_ERROR, "invalid lock name, %.*s\n", name->len,
		     name->name);
		goto bail;
	}

	inode = dlmfs_get_inode(dir, dentry, mode | S_IFREG);
	if (!inode) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	d_instantiate(dentry, inode);
	dget(dentry);	/* Extra count - pin the dentry in core */
bail:
	return status;
}

static int dlmfs_unlink(struct inode *dir,
			struct dentry *dentry)
{
	int status;
	struct inode *inode = dentry->d_inode;

	mlog(0, "unlink inode %lu\n", inode->i_ino);

	/* if there are no current holders, or none that are waiting
	 * to acquire a lock, this basically destroys our lockres. */
	status = user_dlm_destroy_lock(&DLMFS_I(inode)->ip_lockres);
	if (status < 0) {
		mlog(ML_ERROR, "unlink %.*s, error %d from destroy\n",
		     dentry->d_name.len, dentry->d_name.name, status);
		goto bail;
	}
	status = simple_unlink(dir, dentry);
bail:
	return status;
}

static int dlmfs_fill_super(struct super_block * sb,
			    void * data,
			    int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = DLMFS_MAGIC;
	sb->s_op = &dlmfs_ops;
	inode = dlmfs_get_root_inode(sb);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static const struct file_operations dlmfs_file_operations = {
	.open		= dlmfs_file_open,
	.release	= dlmfs_file_release,
	.read		= dlmfs_file_read,
	.write		= dlmfs_file_write,
};

static const struct inode_operations dlmfs_dir_inode_operations = {
	.create		= dlmfs_create,
	.lookup		= simple_lookup,
	.unlink		= dlmfs_unlink,
};

/* this way we can restrict mkdir to only the toplevel of the fs. */
static const struct inode_operations dlmfs_root_inode_operations = {
	.lookup		= simple_lookup,
	.mkdir		= dlmfs_mkdir,
	.rmdir		= simple_rmdir,
};

static const struct super_operations dlmfs_ops = {
	.statfs		= simple_statfs,
	.alloc_inode	= dlmfs_alloc_inode,
	.destroy_inode	= dlmfs_destroy_inode,
	.clear_inode	= dlmfs_clear_inode,
	.drop_inode	= generic_delete_inode,
};

static const struct inode_operations dlmfs_file_inode_operations = {
	.getattr	= simple_getattr,
};

static int dlmfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, data, dlmfs_fill_super, mnt);
}

static struct file_system_type dlmfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ocfs2_dlmfs",
	.get_sb		= dlmfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_dlmfs_fs(void)
{
	int status;
	int cleanup_inode = 0, cleanup_worker = 0;

	dlmfs_print_version();

	status = bdi_init(&dlmfs_backing_dev_info);
	if (status)
		return status;

	dlmfs_inode_cache = kmem_cache_create("dlmfs_inode_cache",
				sizeof(struct dlmfs_inode_private),
				0, (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
					SLAB_MEM_SPREAD),
				dlmfs_init_once);
	if (!dlmfs_inode_cache) {
		status = -ENOMEM;
		goto bail;
	}
	cleanup_inode = 1;

	user_dlm_worker = create_singlethread_workqueue("user_dlm");
	if (!user_dlm_worker) {
		status = -ENOMEM;
		goto bail;
	}
	cleanup_worker = 1;

	status = register_filesystem(&dlmfs_fs_type);
bail:
	if (status) {
		if (cleanup_inode)
			kmem_cache_destroy(dlmfs_inode_cache);
		if (cleanup_worker)
			destroy_workqueue(user_dlm_worker);
		bdi_destroy(&dlmfs_backing_dev_info);
	} else
		printk("OCFS2 User DLM kernel interface loaded\n");
	return status;
}

static void __exit exit_dlmfs_fs(void)
{
	unregister_filesystem(&dlmfs_fs_type);

	flush_workqueue(user_dlm_worker);
	destroy_workqueue(user_dlm_worker);

	kmem_cache_destroy(dlmfs_inode_cache);

	bdi_destroy(&dlmfs_backing_dev_info);
}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");

module_init(init_dlmfs_fs)
module_exit(exit_dlmfs_fs)
