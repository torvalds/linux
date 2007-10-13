/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/smp_lock.h>
#include <linux/mount.h>

#include "internal.h"

struct proc_dir_entry *proc_bus, *proc_root_fs, *proc_root_driver;

static int proc_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	if (proc_mnt) {
		/* Seed the root directory with a pid so it doesn't need
		 * to be special in base.c.  I would do this earlier but
		 * the only task alive when /proc is mounted the first time
		 * is the init_task and it doesn't have any pids.
		 */
		struct proc_inode *ei;
		ei = PROC_I(proc_mnt->mnt_sb->s_root->d_inode);
		if (!ei->pid)
			ei->pid = find_get_pid(1);
	}
	return get_sb_single(fs_type, flags, data, proc_fill_super, mnt);
}

static struct file_system_type proc_fs_type = {
	.name		= "proc",
	.get_sb		= proc_get_sb,
	.kill_sb	= kill_anon_super,
};

void __init proc_root_init(void)
{
	int err = proc_init_inodecache();
	if (err)
		return;
	err = register_filesystem(&proc_fs_type);
	if (err)
		return;
	proc_mnt = kern_mount(&proc_fs_type);
	err = PTR_ERR(proc_mnt);
	if (IS_ERR(proc_mnt)) {
		unregister_filesystem(&proc_fs_type);
		return;
	}
	proc_misc_init();

	proc_net_init();

#ifdef CONFIG_SYSVIPC
	proc_mkdir("sysvipc", NULL);
#endif
	proc_root_fs = proc_mkdir("fs", NULL);
	proc_root_driver = proc_mkdir("driver", NULL);
	proc_mkdir("fs/nfsd", NULL); /* somewhere for the nfsd filesystem to be mounted */
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
	/* just give it a mountpoint */
	proc_mkdir("openprom", NULL);
#endif
	proc_tty_init();
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif
	proc_bus = proc_mkdir("bus", NULL);
	proc_sys_init();
}

static int proc_root_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat
)
{
	generic_fillattr(dentry->d_inode, stat);
	stat->nlink = proc_root.nlink + nr_processes();
	return 0;
}

static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry, struct nameidata *nd)
{
	if (!proc_lookup(dir, dentry, nd)) {
		return NULL;
	}
	
	return proc_pid_lookup(dir, dentry, nd);
}

static int proc_root_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos;
	int ret;

	lock_kernel();

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0) {
			unlock_kernel();
			return error;
		}
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}
	unlock_kernel();

	ret = proc_pid_readdir(filp, dirent, filldir);
	return ret;
}

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static const struct file_operations proc_root_operations = {
	.read		 = generic_read_dir,
	.readdir	 = proc_root_readdir,
};

/*
 * proc root can do almost nothing..
 */
static const struct inode_operations proc_root_inode_operations = {
	.lookup		= proc_root_lookup,
	.getattr	= proc_root_getattr,
};

/*
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	.low_ino	= PROC_ROOT_INO, 
	.namelen	= 5, 
	.name		= "/proc",
	.mode		= S_IFDIR | S_IRUGO | S_IXUGO, 
	.nlink		= 2, 
	.proc_iops	= &proc_root_inode_operations, 
	.proc_fops	= &proc_root_operations,
	.parent		= &proc_root,
};

EXPORT_SYMBOL(proc_symlink);
EXPORT_SYMBOL(proc_mkdir);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_root_fs);
EXPORT_SYMBOL(proc_bus);
EXPORT_SYMBOL(proc_root_driver);
