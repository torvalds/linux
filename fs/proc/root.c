// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <linux/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <linux/mount.h>
#include <linux/pid_namespace.h>
#include <linux/fs_parser.h>
#include <linux/cred.h>
#include <linux/magic.h>
#include <linux/slab.h>

#include "internal.h"

struct proc_fs_context {
	struct pid_namespace	*pid_ns;
	unsigned int		mask;
	int			hidepid;
	int			gid;
};

enum proc_param {
	Opt_gid,
	Opt_hidepid,
};

static const struct fs_parameter_spec proc_param_specs[] = {
	fsparam_u32("gid",	Opt_gid),
	fsparam_u32("hidepid",	Opt_hidepid),
	{}
};

static const struct fs_parameter_description proc_fs_parameters = {
	.name		= "proc",
	.specs		= proc_param_specs,
};

static int proc_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct proc_fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, &proc_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_gid:
		ctx->gid = result.uint_32;
		break;

	case Opt_hidepid:
		ctx->hidepid = result.uint_32;
		if (ctx->hidepid < HIDEPID_OFF ||
		    ctx->hidepid > HIDEPID_INVISIBLE)
			return invalf(fc, "proc: hidepid value must be between 0 and 2.\n");
		break;

	default:
		return -EINVAL;
	}

	ctx->mask |= 1 << opt;
	return 0;
}

static void proc_apply_options(struct super_block *s,
			       struct fs_context *fc,
			       struct pid_namespace *pid_ns,
			       struct user_namespace *user_ns)
{
	struct proc_fs_context *ctx = fc->fs_private;

	if (ctx->mask & (1 << Opt_gid))
		pid_ns->pid_gid = make_kgid(user_ns, ctx->gid);
	if (ctx->mask & (1 << Opt_hidepid))
		pid_ns->hide_pid = ctx->hidepid;
}

static int proc_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct pid_namespace *pid_ns = get_pid_ns(s->s_fs_info);
	struct inode *root_inode;
	int ret;

	proc_apply_options(s, fc, pid_ns, current_user_ns());

	/* User space would break if executables or devices appear on proc */
	s->s_iflags |= SB_I_USERNS_VISIBLE | SB_I_NOEXEC | SB_I_NODEV;
	s->s_flags |= SB_NODIRATIME | SB_NOSUID | SB_NOEXEC;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = PROC_SUPER_MAGIC;
	s->s_op = &proc_sops;
	s->s_time_gran = 1;

	/*
	 * procfs isn't actually a stacking filesystem; however, there is
	 * too much magic going on inside it to permit stacking things on
	 * top of it
	 */
	s->s_stack_depth = FILESYSTEM_MAX_STACK_DEPTH;
	
	/* procfs dentries and inodes don't require IO to create */
	s->s_shrink.seeks = 0;

	pde_get(&proc_root);
	root_inode = proc_get_inode(s, &proc_root);
	if (!root_inode) {
		pr_err("proc_fill_super: get root inode failed\n");
		return -ENOMEM;
	}

	s->s_root = d_make_root(root_inode);
	if (!s->s_root) {
		pr_err("proc_fill_super: allocate dentry failed\n");
		return -ENOMEM;
	}

	ret = proc_setup_self(s);
	if (ret) {
		return ret;
	}
	return proc_setup_thread_self(s);
}

static int proc_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct pid_namespace *pid = sb->s_fs_info;

	sync_filesystem(sb);

	proc_apply_options(sb, fc, pid, current_user_ns());
	return 0;
}

static int proc_get_tree(struct fs_context *fc)
{
	struct proc_fs_context *ctx = fc->fs_private;

	fc->s_fs_info = ctx->pid_ns;
	return vfs_get_super(fc, vfs_get_keyed_super, proc_fill_super);
}

static void proc_fs_context_free(struct fs_context *fc)
{
	struct proc_fs_context *ctx = fc->fs_private;

	put_pid_ns(ctx->pid_ns);
	kfree(ctx);
}

static const struct fs_context_operations proc_fs_context_ops = {
	.free		= proc_fs_context_free,
	.parse_param	= proc_parse_param,
	.get_tree	= proc_get_tree,
	.reconfigure	= proc_reconfigure,
};

static int proc_init_fs_context(struct fs_context *fc)
{
	struct proc_fs_context *ctx;

	ctx = kzalloc(sizeof(struct proc_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->pid_ns = get_pid_ns(task_active_pid_ns(current));
	put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(ctx->pid_ns->user_ns);
	fc->fs_private = ctx;
	fc->ops = &proc_fs_context_ops;
	return 0;
}

static void proc_kill_sb(struct super_block *sb)
{
	struct pid_namespace *ns;

	ns = (struct pid_namespace *)sb->s_fs_info;
	if (ns->proc_self)
		dput(ns->proc_self);
	if (ns->proc_thread_self)
		dput(ns->proc_thread_self);
	kill_anon_super(sb);
	put_pid_ns(ns);
}

static struct file_system_type proc_fs_type = {
	.name			= "proc",
	.init_fs_context	= proc_init_fs_context,
	.parameters		= &proc_fs_parameters,
	.kill_sb		= proc_kill_sb,
	.fs_flags		= FS_USERNS_MOUNT | FS_DISALLOW_NOTIFY_PERM,
};

void __init proc_root_init(void)
{
	proc_init_kmemcache();
	set_proc_pid_nlink();
	proc_self_init();
	proc_thread_self_init();
	proc_symlink("mounts", NULL, "self/mounts");

	proc_net_init();
	proc_mkdir("fs", NULL);
	proc_mkdir("driver", NULL);
	proc_create_mount_point("fs/nfsd"); /* somewhere for the nfsd filesystem to be mounted */
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
	/* just give it a mountpoint */
	proc_create_mount_point("openprom");
#endif
	proc_tty_init();
	proc_mkdir("bus", NULL);
	proc_sys_init();

	register_filesystem(&proc_fs_type);
}

static int proc_root_getattr(const struct path *path, struct kstat *stat,
			     u32 request_mask, unsigned int query_flags)
{
	generic_fillattr(d_inode(path->dentry), stat);
	stat->nlink = proc_root.nlink + nr_processes();
	return 0;
}

static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry, unsigned int flags)
{
	if (!proc_pid_lookup(dentry, flags))
		return NULL;

	return proc_lookup(dir, dentry, flags);
}

static int proc_root_readdir(struct file *file, struct dir_context *ctx)
{
	if (ctx->pos < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(file, ctx);
		if (unlikely(error <= 0))
			return error;
		ctx->pos = FIRST_PROCESS_ENTRY;
	}

	return proc_pid_readdir(file, ctx);
}

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static const struct file_operations proc_root_operations = {
	.read		 = generic_read_dir,
	.iterate_shared	 = proc_root_readdir,
	.llseek		= generic_file_llseek,
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
	.refcnt		= REFCOUNT_INIT(1),
	.proc_iops	= &proc_root_inode_operations, 
	.proc_fops	= &proc_root_operations,
	.parent		= &proc_root,
	.subdir		= RB_ROOT,
	.name		= "/proc",
};

int pid_ns_prepare_proc(struct pid_namespace *ns)
{
	struct proc_fs_context *ctx;
	struct fs_context *fc;
	struct vfsmount *mnt;

	fc = fs_context_for_mount(&proc_fs_type, SB_KERNMOUNT);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	if (fc->user_ns != ns->user_ns) {
		put_user_ns(fc->user_ns);
		fc->user_ns = get_user_ns(ns->user_ns);
	}

	ctx = fc->fs_private;
	if (ctx->pid_ns != ns) {
		put_pid_ns(ctx->pid_ns);
		get_pid_ns(ns);
		ctx->pid_ns = ns;
	}

	mnt = fc_mount(fc);
	put_fs_context(fc);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	ns->proc_mnt = mnt;
	return 0;
}

void pid_ns_release_proc(struct pid_namespace *ns)
{
	kern_unmount(ns->proc_mnt);
}
