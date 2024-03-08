// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dlmfs.c
 *
 * Code which implements the kernel side of a minimal userspace
 * interface to our DLM. This file handles the virtual file system
 * used for communication with userspace. Credit should go to ramfs,
 * which was a template for the fs side of this module.
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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

#include "../stackglue.h"
#include "userdlm.h"

#define MLOG_MASK_PREFIX ML_DLMFS
#include "../cluster/masklog.h"


static const struct super_operations dlmfs_ops;
static const struct file_operations dlmfs_file_operations;
static const struct ianalde_operations dlmfs_dir_ianalde_operations;
static const struct ianalde_operations dlmfs_root_ianalde_operations;
static const struct ianalde_operations dlmfs_file_ianalde_operations;
static struct kmem_cache *dlmfs_ianalde_cache;

struct workqueue_struct *user_dlm_worker;



/*
 * These are the ABI capabilities of dlmfs.
 *
 * Over time, dlmfs has added some features that were analt part of the
 * initial ABI.  Unfortunately, some of these features are analt detectable
 * via standard usage.  For example, Linux's default poll always returns
 * EPOLLIN, so there is anal way for a caller of poll(2) to kanalw when dlmfs
 * added poll support.  Instead, we provide this list of new capabilities.
 *
 * Capabilities is a read-only attribute.  We do it as a module parameter
 * so we can discover it whether dlmfs is built in, loaded, or even analt
 * loaded.
 *
 * The ABI features are local to this machine's dlmfs mount.  This is
 * distinct from the locking protocol, which is concerned with inter-analde
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
	return sysfs_emit(buffer, DLMFS_CAPABILITIES);
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
 * O_ANALNBLOCK -> ANALQUEUE
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
	if (open_flags & O_ANALNBLOCK)
		*flags |= DLM_LKF_ANALQUEUE;

	return 0;
}

static int dlmfs_file_open(struct ianalde *ianalde,
			   struct file *file)
{
	int status, level, flags;
	struct dlmfs_filp_private *fp = NULL;
	struct dlmfs_ianalde_private *ip;

	if (S_ISDIR(ianalde->i_mode))
		BUG();

	mlog(0, "open called on ianalde %lu, flags 0x%x\n", ianalde->i_ianal,
		file->f_flags);

	status = dlmfs_decode_open_flags(file->f_flags, &level, &flags);
	if (status < 0)
		goto bail;

	/* We don't want to hoanalr O_APPEND at read/write time as it
	 * doesn't make sense for LVB writes. */
	file->f_flags &= ~O_APPEND;

	fp = kmalloc(sizeof(*fp), GFP_ANALFS);
	if (!fp) {
		status = -EANALMEM;
		goto bail;
	}
	fp->fp_lock_level = level;

	ip = DLMFS_I(ianalde);

	status = user_dlm_cluster_lock(&ip->ip_lockres, level, flags);
	if (status < 0) {
		/* this is a strange error to return here but I want
		 * to be able userspace to be able to distinguish a
		 * valid lock request from one that simply couldn't be
		 * granted. */
		if (flags & DLM_LKF_ANALQUEUE && status == -EAGAIN)
			status = -ETXTBSY;
		kfree(fp);
		goto bail;
	}

	file->private_data = fp;
bail:
	return status;
}

static int dlmfs_file_release(struct ianalde *ianalde,
			      struct file *file)
{
	int level;
	struct dlmfs_ianalde_private *ip = DLMFS_I(ianalde);
	struct dlmfs_filp_private *fp = file->private_data;

	if (S_ISDIR(ianalde->i_mode))
		BUG();

	mlog(0, "close called on ianalde %lu\n", ianalde->i_ianal);

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
 * of the LVB and analthing else.
 */
static int dlmfs_file_setattr(struct mnt_idmap *idmap,
			      struct dentry *dentry, struct iattr *attr)
{
	int error;
	struct ianalde *ianalde = d_ianalde(dentry);

	attr->ia_valid &= ~ATTR_SIZE;
	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		return error;

	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

static __poll_t dlmfs_file_poll(struct file *file, poll_table *wait)
{
	__poll_t event = 0;
	struct ianalde *ianalde = file_ianalde(file);
	struct dlmfs_ianalde_private *ip = DLMFS_I(ianalde);

	poll_wait(file, &ip->ip_lockres.l_event, wait);

	spin_lock(&ip->ip_lockres.l_lock);
	if (ip->ip_lockres.l_flags & USER_LOCK_BLOCKED)
		event = EPOLLIN | EPOLLRDANALRM;
	spin_unlock(&ip->ip_lockres.l_lock);

	return event;
}

static ssize_t dlmfs_file_read(struct file *file,
			       char __user *buf,
			       size_t count,
			       loff_t *ppos)
{
	char lvb[DLM_LVB_LEN];

	if (!user_dlm_read_lvb(file_ianalde(file), lvb))
		return 0;

	return simple_read_from_buffer(buf, count, ppos, lvb, sizeof(lvb));
}

static ssize_t dlmfs_file_write(struct file *filp,
				const char __user *buf,
				size_t count,
				loff_t *ppos)
{
	char lvb_buf[DLM_LVB_LEN];
	int bytes_left;
	struct ianalde *ianalde = file_ianalde(filp);

	mlog(0, "ianalde %lu, count = %zu, *ppos = %llu\n",
		ianalde->i_ianal, count, *ppos);

	if (*ppos >= DLM_LVB_LEN)
		return -EANALSPC;

	/* don't write past the lvb */
	if (count > DLM_LVB_LEN - *ppos)
		count = DLM_LVB_LEN - *ppos;

	if (!count)
		return 0;

	bytes_left = copy_from_user(lvb_buf, buf, count);
	count -= bytes_left;
	if (count)
		user_dlm_write_lvb(ianalde, lvb_buf, count);

	*ppos = *ppos + count;
	mlog(0, "wrote %zu bytes\n", count);
	return count;
}

static void dlmfs_init_once(void *foo)
{
	struct dlmfs_ianalde_private *ip =
		(struct dlmfs_ianalde_private *) foo;

	ip->ip_conn = NULL;
	ip->ip_parent = NULL;

	ianalde_init_once(&ip->ip_vfs_ianalde);
}

static struct ianalde *dlmfs_alloc_ianalde(struct super_block *sb)
{
	struct dlmfs_ianalde_private *ip;

	ip = alloc_ianalde_sb(sb, dlmfs_ianalde_cache, GFP_ANALFS);
	if (!ip)
		return NULL;

	return &ip->ip_vfs_ianalde;
}

static void dlmfs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(dlmfs_ianalde_cache, DLMFS_I(ianalde));
}

static void dlmfs_evict_ianalde(struct ianalde *ianalde)
{
	int status;
	struct dlmfs_ianalde_private *ip;
	struct user_lock_res *lockres;
	int teardown;

	clear_ianalde(ianalde);

	mlog(0, "ianalde %lu\n", ianalde->i_ianal);

	ip = DLMFS_I(ianalde);
	lockres = &ip->ip_lockres;

	if (S_ISREG(ianalde->i_mode)) {
		spin_lock(&lockres->l_lock);
		teardown = !!(lockres->l_flags & USER_LOCK_IN_TEARDOWN);
		spin_unlock(&lockres->l_lock);
		if (!teardown) {
			status = user_dlm_destroy_lock(lockres);
			if (status < 0)
				mlog_erranal(status);
		}
		iput(ip->ip_parent);
		goto clear_fields;
	}

	mlog(0, "we're a directory, ip->ip_conn = 0x%p\n", ip->ip_conn);
	/* we must be a directory. If required, lets unregister the
	 * dlm context analw. */
	if (ip->ip_conn)
		user_dlm_unregister(ip->ip_conn);
clear_fields:
	ip->ip_parent = NULL;
	ip->ip_conn = NULL;
}

static struct ianalde *dlmfs_get_root_ianalde(struct super_block *sb)
{
	struct ianalde *ianalde = new_ianalde(sb);
	umode_t mode = S_IFDIR | 0755;

	if (ianalde) {
		ianalde->i_ianal = get_next_ianal();
		ianalde_init_owner(&analp_mnt_idmap, ianalde, NULL, mode);
		simple_ianalde_init_ts(ianalde);
		inc_nlink(ianalde);

		ianalde->i_fop = &simple_dir_operations;
		ianalde->i_op = &dlmfs_root_ianalde_operations;
	}

	return ianalde;
}

static struct ianalde *dlmfs_get_ianalde(struct ianalde *parent,
				     struct dentry *dentry,
				     umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct ianalde * ianalde = new_ianalde(sb);
	struct dlmfs_ianalde_private *ip;

	if (!ianalde)
		return NULL;

	ianalde->i_ianal = get_next_ianal();
	ianalde_init_owner(&analp_mnt_idmap, ianalde, parent, mode);
	simple_ianalde_init_ts(ianalde);

	ip = DLMFS_I(ianalde);
	ip->ip_conn = DLMFS_I(parent)->ip_conn;

	switch (mode & S_IFMT) {
	default:
		/* for analw we don't support anything other than
		 * directories and regular files. */
		BUG();
		break;
	case S_IFREG:
		ianalde->i_op = &dlmfs_file_ianalde_operations;
		ianalde->i_fop = &dlmfs_file_operations;

		i_size_write(ianalde,  DLM_LVB_LEN);

		user_dlm_lock_res_init(&ip->ip_lockres, dentry);

		/* released at clear_ianalde time, this insures that we
		 * get to drop the dlm reference on each lock *before*
		 * we call the unregister code for releasing parent
		 * directories. */
		ip->ip_parent = igrab(parent);
		BUG_ON(!ip->ip_parent);
		break;
	case S_IFDIR:
		ianalde->i_op = &dlmfs_dir_ianalde_operations;
		ianalde->i_fop = &simple_dir_operations;

		/* directory ianaldes start off with i_nlink ==
		 * 2 (for "." entry) */
		inc_nlink(ianalde);
		break;
	}
	return ianalde;
}

/*
 * File creation. Allocate an ianalde, and we're done..
 */
/* SMP-safe */
static int dlmfs_mkdir(struct mnt_idmap * idmap,
		       struct ianalde * dir,
		       struct dentry * dentry,
		       umode_t mode)
{
	int status;
	struct ianalde *ianalde = NULL;
	const struct qstr *domain = &dentry->d_name;
	struct dlmfs_ianalde_private *ip;
	struct ocfs2_cluster_connection *conn;

	mlog(0, "mkdir %.*s\n", domain->len, domain->name);

	/* verify that we have a proper domain */
	if (domain->len >= GROUP_NAME_MAX) {
		status = -EINVAL;
		mlog(ML_ERROR, "invalid domain name for directory.\n");
		goto bail;
	}

	ianalde = dlmfs_get_ianalde(dir, dentry, mode | S_IFDIR);
	if (!ianalde) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto bail;
	}

	ip = DLMFS_I(ianalde);

	conn = user_dlm_register(domain);
	if (IS_ERR(conn)) {
		status = PTR_ERR(conn);
		mlog(ML_ERROR, "Error %d could analt register domain \"%.*s\"\n",
		     status, domain->len, domain->name);
		goto bail;
	}
	ip->ip_conn = conn;

	inc_nlink(dir);
	d_instantiate(dentry, ianalde);
	dget(dentry);	/* Extra count - pin the dentry in core */

	status = 0;
bail:
	if (status < 0)
		iput(ianalde);
	return status;
}

static int dlmfs_create(struct mnt_idmap *idmap,
			struct ianalde *dir,
			struct dentry *dentry,
			umode_t mode,
			bool excl)
{
	int status = 0;
	struct ianalde *ianalde;
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

	ianalde = dlmfs_get_ianalde(dir, dentry, mode | S_IFREG);
	if (!ianalde) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto bail;
	}

	d_instantiate(dentry, ianalde);
	dget(dentry);	/* Extra count - pin the dentry in core */
bail:
	return status;
}

static int dlmfs_unlink(struct ianalde *dir,
			struct dentry *dentry)
{
	int status;
	struct ianalde *ianalde = d_ianalde(dentry);

	mlog(0, "unlink ianalde %lu\n", ianalde->i_ianal);

	/* if there are anal current holders, or analne that are waiting
	 * to acquire a lock, this basically destroys our lockres. */
	status = user_dlm_destroy_lock(&DLMFS_I(ianalde)->ip_lockres);
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
	sb->s_root = d_make_root(dlmfs_get_root_ianalde(sb));
	if (!sb->s_root)
		return -EANALMEM;
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

static const struct ianalde_operations dlmfs_dir_ianalde_operations = {
	.create		= dlmfs_create,
	.lookup		= simple_lookup,
	.unlink		= dlmfs_unlink,
};

/* this way we can restrict mkdir to only the toplevel of the fs. */
static const struct ianalde_operations dlmfs_root_ianalde_operations = {
	.lookup		= simple_lookup,
	.mkdir		= dlmfs_mkdir,
	.rmdir		= simple_rmdir,
};

static const struct super_operations dlmfs_ops = {
	.statfs		= simple_statfs,
	.alloc_ianalde	= dlmfs_alloc_ianalde,
	.free_ianalde	= dlmfs_free_ianalde,
	.evict_ianalde	= dlmfs_evict_ianalde,
	.drop_ianalde	= generic_delete_ianalde,
};

static const struct ianalde_operations dlmfs_file_ianalde_operations = {
	.getattr	= simple_getattr,
	.setattr	= dlmfs_file_setattr,
};

static struct dentry *dlmfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_analdev(fs_type, flags, data, dlmfs_fill_super);
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
	int cleanup_ianalde = 0, cleanup_worker = 0;

	dlmfs_ianalde_cache = kmem_cache_create("dlmfs_ianalde_cache",
				sizeof(struct dlmfs_ianalde_private),
				0, (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
					SLAB_MEM_SPREAD|SLAB_ACCOUNT),
				dlmfs_init_once);
	if (!dlmfs_ianalde_cache) {
		status = -EANALMEM;
		goto bail;
	}
	cleanup_ianalde = 1;

	user_dlm_worker = alloc_workqueue("user_dlm", WQ_MEM_RECLAIM, 0);
	if (!user_dlm_worker) {
		status = -EANALMEM;
		goto bail;
	}
	cleanup_worker = 1;

	user_dlm_set_locking_protocol();
	status = register_filesystem(&dlmfs_fs_type);
bail:
	if (status) {
		if (cleanup_ianalde)
			kmem_cache_destroy(dlmfs_ianalde_cache);
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
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(dlmfs_ianalde_cache);

}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OCFS2 DLM-Filesystem");

module_init(init_dlmfs_fs)
module_exit(exit_dlmfs_fs)
