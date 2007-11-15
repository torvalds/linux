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
#include <linux/pid_namespace.h>

#include "internal.h"

struct proc_dir_entry *proc_bus, *proc_root_fs, *proc_root_driver;

static int proc_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}

static int proc_set_super(struct super_block *sb, void *data)
{
	struct pid_namespace *ns;

	ns = (struct pid_namespace *)data;
	sb->s_fs_info = get_pid_ns(ns);
	return set_anon_super(sb, NULL);
}

static int proc_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	int err;
	struct super_block *sb;
	struct pid_namespace *ns;
	struct proc_inode *ei;

	if (proc_mnt) {
		/* Seed the root directory with a pid so it doesn't need
		 * to be special in base.c.  I would do this earlier but
		 * the only task alive when /proc is mounted the first time
		 * is the init_task and it doesn't have any pids.
		 */
		ei = PROC_I(proc_mnt->mnt_sb->s_root->d_inode);
		if (!ei->pid)
			ei->pid = find_get_pid(1);
	}

	if (flags & MS_KERNMOUNT)
		ns = (struct pid_namespace *)data;
	else
		ns = current->nsproxy->pid_ns;

	sb = sget(fs_type, proc_test_super, proc_set_super, ns);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		sb->s_flags = flags;
		err = proc_fill_super(sb);
		if (err) {
			up_write(&sb->s_umount);
			deactivate_super(sb);
			return err;
		}

		ei = PROC_I(sb->s_root->d_inode);
		if (!ei->pid) {
			rcu_read_lock();
			ei->pid = get_pid(find_pid_ns(1, ns));
			rcu_read_unlock();
		}

		sb->s_flags |= MS_ACTIVE;
		ns->proc_mnt = mnt;
	}

	return simple_set_mnt(mnt, sb);
}

static void proc_kill_sb(struct super_block *sb)
{
	struct pid_namespace *ns;

	ns = (struct pid_namespace *)sb->s_fs_info;
	kill_anon_super(sb);
	put_pid_ns(ns);
}

struct file_system_type proc_fs_type = {
	.name		= "proc",
	.get_sb		= proc_get_sb,
	.kill_sb	= proc_kill_sb,
};

void __init proc_root_init(void)
{
	int err = proc_init_inodecache();
	if (err)
		return;
	err = register_filesystem(&proc_fs_type);
	if (err)
		return;
	proc_mnt = kern_mount_data(&proc_fs_type, &init_pid_ns);
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

int pid_ns_prepare_proc(struct pid_namespace *ns)
{
	struct vfsmount *mnt;

	mnt = kern_mount_data(&proc_fs_type, ns);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	return 0;
}

void pid_ns_release_proc(struct pid_namespace *ns)
{
	mntput(ns->proc_mnt);
}

EXPORT_SYMBOL(proc_symlink);
EXPORT_SYMBOL(proc_mkdir);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_root_fs);
EXPORT_SYMBOL(proc_bus);
EXPORT_SYMBOL(proc_root_driver);
