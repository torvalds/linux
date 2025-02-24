// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/devpts/inode.c
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * ------------------------------------------------------------------------- */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/magic.h>
#include <linux/idr.h>
#include <linux/devpts_fs.h>
#include <linux/fsnotify.h>
#include <linux/seq_file.h>

#define DEVPTS_DEFAULT_MODE 0600
/*
 * ptmx is a new node in /dev/pts and will be unused in legacy (single-
 * instance) mode. To prevent surprises in user space, set permissions of
 * ptmx to 0. Use 'chmod' or remount with '-o ptmxmode' to set meaningful
 * permissions.
 */
#define DEVPTS_DEFAULT_PTMX_MODE 0000
#define PTMX_MINOR	2

/*
 * sysctl support for setting limits on the number of Unix98 ptys allocated.
 * Otherwise one can eat up all kernel memory by opening /dev/ptmx repeatedly.
 */
static int pty_limit = NR_UNIX98_PTY_DEFAULT;
static int pty_reserve = NR_UNIX98_PTY_RESERVE;
static int pty_limit_min;
static int pty_limit_max = INT_MAX;
static atomic_t pty_count = ATOMIC_INIT(0);

static const struct ctl_table pty_table[] = {
	{
		.procname	= "max",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.data		= &pty_limit,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &pty_limit_min,
		.extra2		= &pty_limit_max,
	}, {
		.procname	= "reserve",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.data		= &pty_reserve,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &pty_limit_min,
		.extra2		= &pty_limit_max,
	}, {
		.procname	= "nr",
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.data		= &pty_count,
		.proc_handler	= proc_dointvec,
	},
};

struct pts_mount_opts {
	int setuid;
	int setgid;
	kuid_t   uid;
	kgid_t   gid;
	umode_t mode;
	umode_t ptmxmode;
	int reserve;
	int max;
};

enum {
	Opt_uid, Opt_gid, Opt_mode, Opt_ptmxmode, Opt_newinstance,  Opt_max,
	Opt_err
};

static const struct fs_parameter_spec devpts_param_specs[] = {
	fsparam_gid	("gid",		Opt_gid),
	fsparam_s32	("max",		Opt_max),
	fsparam_u32oct	("mode",	Opt_mode),
	fsparam_flag	("newinstance",	Opt_newinstance),
	fsparam_u32oct	("ptmxmode",	Opt_ptmxmode),
	fsparam_uid	("uid",		Opt_uid),
	{}
};

struct pts_fs_info {
	struct ida allocated_ptys;
	struct pts_mount_opts mount_opts;
	struct super_block *sb;
	struct dentry *ptmx_dentry;
};

static inline struct pts_fs_info *DEVPTS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static int devpts_ptmx_path(struct path *path)
{
	struct super_block *sb;
	int err;

	/* Is a devpts filesystem at "pts" in the same directory? */
	err = path_pts(path);
	if (err)
		return err;

	/* Is the path the root of a devpts filesystem? */
	sb = path->mnt->mnt_sb;
	if ((sb->s_magic != DEVPTS_SUPER_MAGIC) ||
	    (path->mnt->mnt_root != sb->s_root))
		return -ENODEV;

	return 0;
}

/*
 * Try to find a suitable devpts filesystem. We support the following
 * scenarios:
 * - The ptmx device node is located in the same directory as the devpts
 *   mount where the pts device nodes are located.
 *   This is e.g. the case when calling open on the /dev/pts/ptmx device
 *   node when the devpts filesystem is mounted at /dev/pts.
 * - The ptmx device node is located outside the devpts filesystem mount
 *   where the pts device nodes are located. For example, the ptmx device
 *   is a symlink, separate device node, or bind-mount.
 *   A supported scenario is bind-mounting /dev/pts/ptmx to /dev/ptmx and
 *   then calling open on /dev/ptmx. In this case a suitable pts
 *   subdirectory can be found in the common parent directory /dev of the
 *   devpts mount and the ptmx bind-mount, after resolving the /dev/ptmx
 *   bind-mount.
 *   If no suitable pts subdirectory can be found this function will fail.
 *   This is e.g. the case when bind-mounting /dev/pts/ptmx to /ptmx.
 */
struct vfsmount *devpts_mntget(struct file *filp, struct pts_fs_info *fsi)
{
	struct path path;
	int err = 0;

	path = filp->f_path;
	path_get(&path);

	/* Walk upward while the start point is a bind mount of
	 * a single file.
	 */
	while (path.mnt->mnt_root == path.dentry)
		if (follow_up(&path) == 0)
			break;

	/* devpts_ptmx_path() finds a devpts fs or returns an error. */
	if ((path.mnt->mnt_sb->s_magic != DEVPTS_SUPER_MAGIC) ||
	    (DEVPTS_SB(path.mnt->mnt_sb) != fsi))
		err = devpts_ptmx_path(&path);
	dput(path.dentry);
	if (!err) {
		if (DEVPTS_SB(path.mnt->mnt_sb) == fsi)
			return path.mnt;

		err = -ENODEV;
	}

	mntput(path.mnt);
	return ERR_PTR(err);
}

struct pts_fs_info *devpts_acquire(struct file *filp)
{
	struct pts_fs_info *result;
	struct path path;
	struct super_block *sb;

	path = filp->f_path;
	path_get(&path);

	/* Has the devpts filesystem already been found? */
	if (path.mnt->mnt_sb->s_magic != DEVPTS_SUPER_MAGIC) {
		int err;

		err = devpts_ptmx_path(&path);
		if (err) {
			result = ERR_PTR(err);
			goto out;
		}
	}

	/*
	 * pty code needs to hold extra references in case of last /dev/tty close
	 */
	sb = path.mnt->mnt_sb;
	atomic_inc(&sb->s_active);
	result = DEVPTS_SB(sb);

out:
	path_put(&path);
	return result;
}

void devpts_release(struct pts_fs_info *fsi)
{
	deactivate_super(fsi->sb);
}

/*
 * devpts_parse_param - Parse mount parameters
 */
static int devpts_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct pts_fs_info *fsi = fc->s_fs_info;
	struct pts_mount_opts *opts = &fsi->mount_opts;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, devpts_param_specs, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		opts->uid = result.uid;
		opts->setuid = 1;
		break;
	case Opt_gid:
		opts->gid = result.gid;
		opts->setgid = 1;
		break;
	case Opt_mode:
		opts->mode = result.uint_32 & S_IALLUGO;
		break;
	case Opt_ptmxmode:
		opts->ptmxmode = result.uint_32 & S_IALLUGO;
		break;
	case Opt_newinstance:
		break;
	case Opt_max:
		if (result.uint_32 > NR_UNIX98_PTY_MAX)
			return invalf(fc, "max out of range");
		opts->max = result.uint_32;
		break;
	}

	return 0;
}

static int mknod_ptmx(struct super_block *sb, struct fs_context *fc)
{
	int mode;
	int rc = -ENOMEM;
	struct dentry *dentry;
	struct inode *inode;
	struct dentry *root = sb->s_root;
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;
	kuid_t ptmx_uid = current_fsuid();
	kgid_t ptmx_gid = current_fsgid();

	inode_lock(d_inode(root));

	/* If we have already created ptmx node, return */
	if (fsi->ptmx_dentry) {
		rc = 0;
		goto out;
	}

	dentry = d_alloc_name(root, "ptmx");
	if (!dentry) {
		pr_err("Unable to alloc dentry for ptmx node\n");
		goto out;
	}

	/*
	 * Create a new 'ptmx' node in this mount of devpts.
	 */
	inode = new_inode(sb);
	if (!inode) {
		pr_err("Unable to alloc inode for ptmx node\n");
		dput(dentry);
		goto out;
	}

	inode->i_ino = 2;
	simple_inode_init_ts(inode);

	mode = S_IFCHR|opts->ptmxmode;
	init_special_inode(inode, mode, MKDEV(TTYAUX_MAJOR, 2));
	inode->i_uid = ptmx_uid;
	inode->i_gid = ptmx_gid;

	d_add(dentry, inode);

	fsi->ptmx_dentry = dentry;
	rc = 0;
out:
	inode_unlock(d_inode(root));
	return rc;
}

static void update_ptmx_mode(struct pts_fs_info *fsi)
{
	struct inode *inode;
	if (fsi->ptmx_dentry) {
		inode = d_inode(fsi->ptmx_dentry);
		inode->i_mode = S_IFCHR|fsi->mount_opts.ptmxmode;
	}
}

static int devpts_reconfigure(struct fs_context *fc)
{
	struct pts_fs_info *fsi = DEVPTS_SB(fc->root->d_sb);
	struct pts_fs_info *new = fc->s_fs_info;

	/* Apply the revised options.  We don't want to change ->reserve.
	 * Ideally, we'd update each option conditionally on it having been
	 * explicitly changed, but the default is to reset everything so that
	 * would break UAPI...
	 */
	fsi->mount_opts.setuid		= new->mount_opts.setuid;
	fsi->mount_opts.setgid		= new->mount_opts.setgid;
	fsi->mount_opts.uid		= new->mount_opts.uid;
	fsi->mount_opts.gid		= new->mount_opts.gid;
	fsi->mount_opts.mode		= new->mount_opts.mode;
	fsi->mount_opts.ptmxmode	= new->mount_opts.ptmxmode;
	fsi->mount_opts.max		= new->mount_opts.max;

	/*
	 * parse_mount_options() restores options to default values
	 * before parsing and may have changed ptmxmode. So, update the
	 * mode in the inode too. Bogus options don't fail the remount,
	 * so do this even on error return.
	 */
	update_ptmx_mode(fsi);

	return 0;
}

static int devpts_show_options(struct seq_file *seq, struct dentry *root)
{
	struct pts_fs_info *fsi = DEVPTS_SB(root->d_sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;

	if (opts->setuid)
		seq_printf(seq, ",uid=%u",
			   from_kuid_munged(&init_user_ns, opts->uid));
	if (opts->setgid)
		seq_printf(seq, ",gid=%u",
			   from_kgid_munged(&init_user_ns, opts->gid));
	seq_printf(seq, ",mode=%03o", opts->mode);
	seq_printf(seq, ",ptmxmode=%03o", opts->ptmxmode);
	if (opts->max < NR_UNIX98_PTY_MAX)
		seq_printf(seq, ",max=%d", opts->max);

	return 0;
}

static const struct super_operations devpts_sops = {
	.statfs		= simple_statfs,
	.show_options	= devpts_show_options,
};

static int devpts_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct pts_fs_info *fsi = DEVPTS_SB(s);
	struct inode *inode;

	s->s_iflags &= ~SB_I_NODEV;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = DEVPTS_SUPER_MAGIC;
	s->s_op = &devpts_sops;
	s->s_d_flags = DCACHE_DONTCACHE;
	s->s_time_gran = 1;
	fsi->sb = s;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	inode->i_ino = 1;
	simple_inode_init_ts(inode);
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);

	s->s_root = d_make_root(inode);
	if (!s->s_root) {
		pr_err("get root dentry failed\n");
		return -ENOMEM;
	}

	return mknod_ptmx(s, fc);
}

/*
 * devpts_get_tree()
 *
 *     Mount a new (private) instance of devpts.  PTYs created in this
 *     instance are independent of the PTYs in other devpts instances.
 */
static int devpts_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, devpts_fill_super);
}

static void devpts_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations devpts_context_ops = {
	.free		= devpts_free_fc,
	.parse_param	= devpts_parse_param,
	.get_tree	= devpts_get_tree,
	.reconfigure	= devpts_reconfigure,
};

/*
 * Set up the filesystem mount context.
 */
static int devpts_init_fs_context(struct fs_context *fc)
{
	struct pts_fs_info *fsi;

	fsi = kzalloc(sizeof(struct pts_fs_info), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	ida_init(&fsi->allocated_ptys);
	fsi->mount_opts.uid     = GLOBAL_ROOT_UID;
	fsi->mount_opts.gid     = GLOBAL_ROOT_GID;
	fsi->mount_opts.mode    = DEVPTS_DEFAULT_MODE;
	fsi->mount_opts.ptmxmode = DEVPTS_DEFAULT_PTMX_MODE;
	fsi->mount_opts.max     = NR_UNIX98_PTY_MAX;

	if (fc->purpose == FS_CONTEXT_FOR_MOUNT &&
	    current->nsproxy->mnt_ns == init_task.nsproxy->mnt_ns)
		fsi->mount_opts.reserve = true;

	fc->s_fs_info = fsi;
	fc->ops = &devpts_context_ops;
	return 0;
}

static void devpts_kill_sb(struct super_block *sb)
{
	struct pts_fs_info *fsi = DEVPTS_SB(sb);

	if (fsi)
		ida_destroy(&fsi->allocated_ptys);
	kfree(fsi);
	kill_litter_super(sb);
}

static struct file_system_type devpts_fs_type = {
	.name		= "devpts",
	.init_fs_context = devpts_init_fs_context,
	.parameters	= devpts_param_specs,
	.kill_sb	= devpts_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

/*
 * The normal naming convention is simply /dev/pts/<number>; this conforms
 * to the System V naming convention
 */

int devpts_new_index(struct pts_fs_info *fsi)
{
	int index = -ENOSPC;

	if (atomic_inc_return(&pty_count) >= (pty_limit -
			  (fsi->mount_opts.reserve ? 0 : pty_reserve)))
		goto out;

	index = ida_alloc_max(&fsi->allocated_ptys, fsi->mount_opts.max - 1,
			GFP_KERNEL);

out:
	if (index < 0)
		atomic_dec(&pty_count);
	return index;
}

void devpts_kill_index(struct pts_fs_info *fsi, int idx)
{
	ida_free(&fsi->allocated_ptys, idx);
	atomic_dec(&pty_count);
}

/**
 * devpts_pty_new -- create a new inode in /dev/pts/
 * @fsi: Filesystem info for this instance.
 * @index: used as a name of the node
 * @priv: what's given back by devpts_get_priv
 *
 * The dentry for the created inode is returned.
 * Remove it from /dev/pts/ with devpts_pty_kill().
 */
struct dentry *devpts_pty_new(struct pts_fs_info *fsi, int index, void *priv)
{
	struct dentry *dentry;
	struct super_block *sb = fsi->sb;
	struct inode *inode;
	struct dentry *root;
	struct pts_mount_opts *opts;
	char s[12];

	root = sb->s_root;
	opts = &fsi->mount_opts;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = index + 3;
	inode->i_uid = opts->setuid ? opts->uid : current_fsuid();
	inode->i_gid = opts->setgid ? opts->gid : current_fsgid();
	simple_inode_init_ts(inode);
	init_special_inode(inode, S_IFCHR|opts->mode, MKDEV(UNIX98_PTY_SLAVE_MAJOR, index));

	sprintf(s, "%d", index);

	dentry = d_alloc_name(root, s);
	if (dentry) {
		dentry->d_fsdata = priv;
		d_add(dentry, inode);
		fsnotify_create(d_inode(root), dentry);
	} else {
		iput(inode);
		dentry = ERR_PTR(-ENOMEM);
	}

	return dentry;
}

/**
 * devpts_get_priv -- get private data for a slave
 * @dentry: dentry of the slave
 *
 * Returns whatever was passed as priv in devpts_pty_new for a given inode.
 */
void *devpts_get_priv(struct dentry *dentry)
{
	if (dentry->d_sb->s_magic != DEVPTS_SUPER_MAGIC)
		return NULL;
	return dentry->d_fsdata;
}

/**
 * devpts_pty_kill -- remove inode form /dev/pts/
 * @dentry: dentry of the slave to be removed
 *
 * This is an inverse operation of devpts_pty_new.
 */
void devpts_pty_kill(struct dentry *dentry)
{
	WARN_ON_ONCE(dentry->d_sb->s_magic != DEVPTS_SUPER_MAGIC);

	dentry->d_fsdata = NULL;
	drop_nlink(dentry->d_inode);
	d_drop(dentry);
	fsnotify_unlink(d_inode(dentry->d_parent), dentry);
	dput(dentry);	/* d_alloc_name() in devpts_pty_new() */
}

static int __init init_devpts_fs(void)
{
	int err = register_filesystem(&devpts_fs_type);
	if (!err) {
		register_sysctl("kernel/pty", pty_table);
	}
	return err;
}
module_init(init_devpts_fs)
