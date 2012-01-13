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
#include <linux/mount.h>
#include <linux/pid_namespace.h>

#include "internal.h"

static int proc_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}

static int proc_set_super(struct super_block *sb, void *data)
{
	int err = set_anon_super(sb, NULL);
	if (!err) {
		struct pid_namespace *ns = (struct pid_namespace *)data;
		sb->s_fs_info = get_pid_ns(ns);
	}
	return err;
}

static struct dentry *proc_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	int err;
	struct super_block *sb;
	struct pid_namespace *ns;
	struct proc_inode *ei;

	if (flags & MS_KERNMOUNT)
		ns = (struct pid_namespace *)data;
	else
		ns = current->nsproxy->pid_ns;

	sb = sget(fs_type, proc_test_super, proc_set_super, ns);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (!sb->s_root) {
		sb->s_flags = flags;
		err = proc_fill_super(sb);
		if (err) {
			deactivate_locked_super(sb);
			return ERR_PTR(err);
		}

		sb->s_flags |= MS_ACTIVE;
	}

	ei = PROC_I(sb->s_root->d_inode);
	if (!ei->pid) {
		rcu_read_lock();
		ei->pid = get_pid(find_pid_ns(1, ns));
		rcu_read_unlock();
	}

	return dget(sb->s_root);
}

static void proc_kill_sb(struct super_block *sb)
{
	struct pid_namespace *ns;

	ns = (struct pid_namespace *)sb->s_fs_info;
	kill_anon_super(sb);
	put_pid_ns(ns);
}

static struct file_system_type proc_fs_type = {
	.name		= "proc",
	.mount		= proc_mount,
	.kill_sb	= proc_kill_sb,
};

void __init proc_root_init(void)
{
	int err;

	proc_init_inodecache();
	err = register_filesystem(&proc_fs_type);
	if (err)
		return;
	err = pid_ns_prepare_proc(&init_pid_ns);
	if (err) {
		unregister_filesystem(&proc_fs_type);
		return;
	}

	proc_symlink("mounts", NULL, "self/mounts");

	proc_net_init();

#ifdef CONFIG_SYSVIPC
	proc_mkdir("sysvipc", NULL);
#endif
	proc_mkdir("fs", NULL);
	proc_mkdir("driver", NULL);
	proc_mkdir("fs/nfsd", NULL); /* somewhere for the nfsd filesystem to be mounted */
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
	/* just give it a mountpoint */
	proc_mkdir("openprom", NULL);
#endif
	proc_tty_init();
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif
	proc_mkdir("bus", NULL);
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

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}

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
	.llseek		= default_llseek,
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
	.mode		= S_IFDIR | S_IRUGO | S_IXUGO, 
	.nlink		= 2, 
	.count		= ATOMIC_INIT(1),
	.proc_iops	= &proc_root_inode_operations, 
	.proc_fops	= &proc_root_operations,
	.parent		= &proc_root,
	.name		= "/proc",
};

int pid_ns_prepare_proc(struct pid_namespace *ns)
{
	struct vfsmount *mnt;

	mnt = kern_mount_data(&proc_fs_type, ns);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	ns->proc_mnt = mnt;
	return 0;
}

void pid_ns_release_proc(struct pid_namespace *ns)
{
	kern_unmount(ns->proc_mnt);
}
