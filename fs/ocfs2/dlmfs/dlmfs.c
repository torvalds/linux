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
#include <linux/poll.h>

#include <linux/uaccess.h>

#include "stackglue.h"
#include "userdlm.h"

#define MLOG_MASK_PREFIX ML_DLMFS
#include "cluster/masklog.h"


static const struct super_operations dlmfs_ops;
static const struct file_operations dlmfs_file_operations;
static const struct inode_operations dlmfs_dir_inode_operations;
static const struct inode_operations dlmfs_root_inode_operations;
static const struct inode_operations dlmfs_file_inode_operations;
static struct kmem_cache *dlmfs_inode_cache;

struct workqueue_struct *user_dlm_worker;



/*
 * These are the ABI capabilities of dlmfs.
 *
 * Over time, dlmfs has added some features that were not part of the
 * initial ABI.  Unfortunately, some of these features are not detectable
 * via standard usage.  For example, Linux's default poll always returns
 * EPOLLIN, so there is no way for a caller of poll(2) to know when dlmfs
 * added poll support.  Instead, we provide this list of new capabilities.
 *
 * Capabilities is a read-only attribute.  We do it as a module parameter
 * so we can discover it whether dlmfs is built in, loaded, or even not
 * loaded.
 *
 * The ABI features are local to this machine's dlmfs mount.  This is
 * distinct from the locking protocol, which is concerned with inter-node
 * interaction.
 *
 * Capabilities:
 * - bast	: EPOLLIN against the file descriptor of a held lock
 *		  signifies a bast fired on the lock.
 */
#define DLMFS_CAPABILITIES "bast stackglue"
static int param_set_dlmfs_capabilities(const char *val,
					const struct kernel_param *kp)
{
	printk(KERN_ERR "%s: readonly parameter\n", kp->name);
	return -EINVAL;
}
static int param_get_dlmfs_capabilities(char *buffer,
					const struct kernel_param *kp)
{
	return strlcpy(buffer, DLMFS_CAPABILITIES,
		       strlen(DLMFS_CAPABILITIES) + 1);
}
module_param_call(capabilities, param_set_dlmfs_capabilities,
		  param_get_dlmfs_capabilities, NULL, 0444);
MODULE_PARM_DESC(capabilities, DLMFS_CAPABILITIES);


/*
 * decodes a set of open flags into a valid lock level and a set of flags.
 * returns < 0 if we have invalid flags
 * flags which mean something to us:
 * O_RDONLY -> PRMODE level
 * O_WRONLY -> EXMODE level
 *
 * O_NONBLOCK -> NOQUEUE
 */
static int dlmfs_decode_open_flags(int open_flags,
				   int *level,
				   int *flags)
{
	if (open_flags & (O_WRONLY|O_RDWR))
		*level = DLM_LOCK_EX;
	else
		*level = DLM_LOCK_PR;

	*flags = 0;
	if (open_flags & O_NONBLOCK)
		*flags |= DLM_LKF_NOQUEUE;

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
		if (flags & DLM_LKF_NOQUEUE && status == -EAGAIN)
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
	int level;
	struct dlmfs_inode_private *ip = DLMFS_I(inode);
	struct dlmfs_filp_private *fp = file->private_data;

	if (S_ISDIR(inode->i_mode))
		BUG();

	mlog(0, "close called on inode %lu\n", inode->i_ino);

	if (fp) {
		level = fp->fp_lock_level;
		if (level != DLM_LOCK_IV)
			user_dlm_cluster_unlock(&ip->ip_lockres, level);

		kfree(fp);
		file->private_data = NULL;
	}

	return 0;
}

/*
 * We do ->setattr() just to override size changes.  Our size is the size
 * of the LVB and nothing else.
 */
static int dlmfs_file_setattr(struct dentry *dentry, struct iattr *attr)
{
	int error;
	struct inode *inode = d_inode(dentry);

	attr->ia_valid &= ~ATTR_SIZE;
	error = setattr_prepare(dentry, attr);
	if (error)
		return error;

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static __poll_t dlmfs_file_poll(struct file *file, poll_table *wait)
{
	__poll_t event = 0;
	struct inode *inode = file_inode(file);
	struct dlmfs_inode_private *ip = DLMFS_I(inode);

	poll_wait(file, &ip->ip_lockres.l_event, wait);

	spin_lock(&ip->ip_lockres.l_lock);
	if (ip->ip_lockres.l_flags & USER_LOCK_BLOCKED)
		event = EPOLLIN | EPOLLRDNORM;
	spin_unlock(&ip->ip_lockres.l_lock);

	return event;
}

static ssize_t dlmfs_file_read(struct file *filp,
			       char __user *buf,
			       size_t count,
			       loff_t *ppos)
{
	int bytes_left;
	ssize_t readlen, got;
	char *lvb_buf;
	struct inode *inode = file_inode(filp);

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

	got = user_dlm_read_lvb(inode, lvb_buf, readlen);
	if (got) {
		BUG_ON(got != readlen);
		bytes_left = __copy_to_user(buf, lvb_buf, readlen);
		readlen -= bytes_left;
	} else
		readlen = 0;

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
	struct inode *inode = file_inode(filp);

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

	ip->ip_conn = NULL;
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

static void dlmfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(dlmfs_inode_cache, DLMFS_I(inode));
}

static void dlmfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, dlmfs_i_callback);
}

static void dlmfs_evict_inode(struct inode *inode)
{
	int status;
	struct dlmfs_inode_private *ip;

	clear_inode(inode);

	mlog(0, "inode %lu\n", inode->i_ino);

	ip = DLMFS_I(inode);

	if (S_ISREG(inode->i_mode)) {
		status = user_dlm_destroy_lock(&ip->ip_lockres);
		if (status < 0)
			mlog_errno(status);
		iput(ip->ip_parent);
		goto clear_fields;
	}

	mlog(0, "we're a directory, ip->ip_conn = 0x%p\n", ip->ip_conn);
	/* we must be a directory. If required, lets unregister the
	 * dlm context now. */
	if (ip->ip_conn)
		user_dlm_unregister(ip->ip_conn);
clear_fields:
	ip->ip_parent = NULL;
	ip->ip_conn = NULL;
}

static struct inode *dlmfs_get_root_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	umode_t mode = S_IFDIR | 0755;

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, NULL, mode);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inc_nlink(inode);

		inode->i_fop = &simple_dir_operations;
		inode->i_op = &dlmfs_root_inode_operations;
	}

	return inode;
}

static struct inode *dlmfs_get_inode(struct inode *parent,
				     struct dentry *dentry,
				     umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct inode * inode = new_inode(sb);
	struct dlmfs_inode_private *ip;

	if (!inode)
		return NULL;

	inode->i_ino = get_next_ino();
	inode_init_owner(inode, parent, mode);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

	ip = DLMFS_I(inode);
	ip->ip_conn = DLMFS_I(parent)->ip_conn;

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
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int dlmfs_mkdir(struct inode * dir,
		       struct dentry * dentry,
		       umode_t mode)
{
	int status;
	struct inode *inode = NULL;
	const struct qstr *domain = &dentry->d_name;
	struct dlmfs_inode_private *ip;
	struct ocfs2_cluster_connection *conn;

	mlog(0, "mkdir %.*s\n", domain->len, domain->name);

	/* verify that we have a proper domain */
	if (domain->len >= GROUP_NAME_MAX) {
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

	conn = user_dlm_register(domain);
	if (IS_ERR(conn)) {
		status = PTR_ERR(conn);
		mlog(ML_ERROR, "Error %d could not register domain \"%.*s\"\n",
		     status, domain->len, domain->name);
		goto bail;
	}
	ip->ip_conn = conn;

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
			umode_t mode,
			bool excl)
{
	int status = 0;
	struct inode *inode;
	const struct qstr *name = &dentry->d_name;

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
	struct inode *inode = d_inode(dentry);

	mlog(0, "unlink inode %lu\n", inode->i_ino);

	/* if there are no current holders, or none that are waiting
	 * to acquire a lock, this basically destroys our lockres. */
	status = user_dlm_destroy_lock(&DLMFS_I(inode)->ip_lockres);
	if (status < 0) {
		mlog(ML_ERROR, "unlink %pd, error %d from destroy\n",
		     dentry, status);
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
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = DLMFS_MAGIC;
	sb->s_op = &dlmfs_ops;
	sb->s_root = d_make_root(dlmfs_get_root_inode(sb));
	if (!sb->s_root)
		return -ENOMEM;
	return 0;
}

static const struct file_operations dlmfs_file_operations = {
	.open		= dlmfs_file_open,
	.release	= dlmfs_file_release,
	.poll		= dlmfs_file_poll,
	.read		= dlmfs_file_read,
	.write		= dlmfs_file_write,
	.llseek		= default_llseek,
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
	.evict_inode	= dlmfs_evict_inode,
	.drop_inode	= generic_delete_inode,
};

static const struct inode_operations dlmfs_file_inode_operations = {
	.getattr	= simple_getattr,
	.setattr	= dlmfs_file_setattr,
};

static struct dentry *dlmfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, dlmfs_fill_super);
}

static struct file_system_type dlmfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ocfs2_dlmfs",
	.mount		= dlmfs_mount,
	.kill_sb	= kill_litter_super,
};
MODULE_ALIAS_FS("ocfs2_dlmfs");

static int __init init_dlmfs_fs(void)
{
	int status;
	int cleanup_inode = 0, cleanup_worker = 0;

	dlmfs_inode_cache = kmem_cache_create("dlmfs_inode_cache",
				sizeof(struct dlmfs_inode_private),
				0, (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
					SLAB_MEM_SPREAD|SLAB_ACCOUNT),
				dlmfs_init_once);
	if (!dlmfs_inode_cache) {
		status = -ENOMEM;
		goto bail;
	}
	cleanup_inode = 1;

	user_dlm_worker = alloc_workqueue("user_dlm", WQ_MEM_RECLAIM, 0);
	if (!user_dlm_worker) {
		status = -ENOMEM;
		goto bail;
	}
	cleanup_worker = 1;

	user_dlm_set_locking_protocol();
	status = register_filesystem(&dlmfs_fs_type);
bail:
	if (status) {
		if (cleanup_inode)
			kmem_cache_destroy(dlmfs_inode_cache);
		if (cleanup_worker)
			destroy_workqueue(user_dlm_worker);
	} else
		printk("OCFS2 User DLM kernel interface loaded\n");
	return status;
}

static void __exit exit_dlmfs_fs(void)
{
	unregister_filesystem(&dlmfs_fs_type);

	destroy_workqueue(user_dlm_worker);

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(dlmfs_inode_cache);

}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OCFS2 DLM-Filesystem");

module_init(init_dlmfs_fs)
module_exit(exit_dlmfs_fs)
