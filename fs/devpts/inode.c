/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/devpts/inode.c
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/tty.h>
#include <linux/mutex.h>
#include <linux/magic.h>
#include <linux/idr.h>
#include <linux/devpts_fs.h>
#include <linux/parser.h>
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
static int pty_count;

static struct ctl_table pty_table[] = {
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
	{}
};

static struct ctl_table pty_kern_table[] = {
	{
		.procname	= "pty",
		.mode		= 0555,
		.child		= pty_table,
	},
	{}
};

static struct ctl_table pty_root_table[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= pty_kern_table,
	},
	{}
};

static DEFINE_MUTEX(allocated_ptys_lock);

static struct vfsmount *devpts_mnt;

struct pts_mount_opts {
	int setuid;
	int setgid;
	kuid_t   uid;
	kgid_t   gid;
	umode_t mode;
	umode_t ptmxmode;
	int newinstance;
	int max;
};

enum {
	Opt_uid, Opt_gid, Opt_mode, Opt_ptmxmode, Opt_newinstance,  Opt_max,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_mode, "mode=%o"},
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	{Opt_ptmxmode, "ptmxmode=%o"},
	{Opt_newinstance, "newinstance"},
	{Opt_max, "max=%d"},
#endif
	{Opt_err, NULL}
};

struct pts_fs_info {
	struct ida allocated_ptys;
	struct pts_mount_opts mount_opts;
	struct dentry *ptmx_dentry;
};

static inline struct pts_fs_info *DEVPTS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct super_block *pts_sb_from_inode(struct inode *inode)
{
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	if (inode->i_sb->s_magic == DEVPTS_SUPER_MAGIC)
		return inode->i_sb;
#endif
	return devpts_mnt->mnt_sb;
}

#define PARSE_MOUNT	0
#define PARSE_REMOUNT	1

/*
 * parse_mount_options():
 * 	Set @opts to mount options specified in @data. If an option is not
 * 	specified in @data, set it to its default value. The exception is
 * 	'newinstance' option which can only be set/cleared on a mount (i.e.
 * 	cannot be changed during remount).
 *
 * Note: @data may be NULL (in which case all options are set to default).
 */
static int parse_mount_options(char *data, int op, struct pts_mount_opts *opts)
{
	char *p;
	kuid_t uid;
	kgid_t gid;

	opts->setuid  = 0;
	opts->setgid  = 0;
	opts->uid     = GLOBAL_ROOT_UID;
	opts->gid     = GLOBAL_ROOT_GID;
	opts->mode    = DEVPTS_DEFAULT_MODE;
	opts->ptmxmode = DEVPTS_DEFAULT_PTMX_MODE;
	opts->max     = NR_UNIX98_PTY_MAX;

	/* newinstance makes sense only on initial mount */
	if (op == PARSE_MOUNT)
		opts->newinstance = 0;

	while ((p = strsep(&data, ",")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;
		int option;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid))
				return -EINVAL;
			opts->uid = uid;
			opts->setuid = 1;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid))
				return -EINVAL;
			opts->gid = gid;
			opts->setgid = 1;
			break;
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
		case Opt_ptmxmode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->ptmxmode = option & S_IALLUGO;
			break;
		case Opt_newinstance:
			/* newinstance makes sense only on initial mount */
			if (op == PARSE_MOUNT)
				opts->newinstance = 1;
			break;
		case Opt_max:
			if (match_int(&args[0], &option) ||
			    option < 0 || option > NR_UNIX98_PTY_MAX)
				return -EINVAL;
			opts->max = option;
			break;
#endif
		default:
			printk(KERN_ERR "devpts: called with bogus options\n");
			return -EINVAL;
		}
	}

	return 0;
}

#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
static int mknod_ptmx(struct super_block *sb)
{
	int mode;
	int rc = -ENOMEM;
	struct dentry *dentry;
	struct inode *inode;
	struct dentry *root = sb->s_root;
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;
	kuid_t root_uid;
	kgid_t root_gid;

	root_uid = make_kuid(current_user_ns(), 0);
	root_gid = make_kgid(current_user_ns(), 0);
	if (!uid_valid(root_uid) || !gid_valid(root_gid))
		return -EINVAL;

	mutex_lock(&root->d_inode->i_mutex);

	/* If we have already created ptmx node, return */
	if (fsi->ptmx_dentry) {
		rc = 0;
		goto out;
	}

	dentry = d_alloc_name(root, "ptmx");
	if (!dentry) {
		printk(KERN_NOTICE "Unable to alloc dentry for ptmx node\n");
		goto out;
	}

	/*
	 * Create a new 'ptmx' node in this mount of devpts.
	 */
	inode = new_inode(sb);
	if (!inode) {
		printk(KERN_ERR "Unable to alloc inode for ptmx node\n");
		dput(dentry);
		goto out;
	}

	inode->i_ino = 2;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	mode = S_IFCHR|opts->ptmxmode;
	init_special_inode(inode, mode, MKDEV(TTYAUX_MAJOR, 2));
	inode->i_uid = root_uid;
	inode->i_gid = root_gid;

	d_add(dentry, inode);

	fsi->ptmx_dentry = dentry;
	rc = 0;
out:
	mutex_unlock(&root->d_inode->i_mutex);
	return rc;
}

static void update_ptmx_mode(struct pts_fs_info *fsi)
{
	struct inode *inode;
	if (fsi->ptmx_dentry) {
		inode = fsi->ptmx_dentry->d_inode;
		inode->i_mode = S_IFCHR|fsi->mount_opts.ptmxmode;
	}
}
#else
static inline void update_ptmx_mode(struct pts_fs_info *fsi)
{
       return;
}
#endif

static int devpts_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;

	err = parse_mount_options(data, PARSE_REMOUNT, opts);

	/*
	 * parse_mount_options() restores options to default values
	 * before parsing and may have changed ptmxmode. So, update the
	 * mode in the inode too. Bogus options don't fail the remount,
	 * so do this even on error return.
	 */
	update_ptmx_mode(fsi);

	return err;
}

static int devpts_show_options(struct seq_file *seq, struct dentry *root)
{
	struct pts_fs_info *fsi = DEVPTS_SB(root->d_sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;

	if (opts->setuid)
		seq_printf(seq, ",uid=%u", from_kuid_munged(&init_user_ns, opts->uid));
	if (opts->setgid)
		seq_printf(seq, ",gid=%u", from_kgid_munged(&init_user_ns, opts->gid));
	seq_printf(seq, ",mode=%03o", opts->mode);
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	seq_printf(seq, ",ptmxmode=%03o", opts->ptmxmode);
	if (opts->max < NR_UNIX98_PTY_MAX)
		seq_printf(seq, ",max=%d", opts->max);
#endif

	return 0;
}

static const struct super_operations devpts_sops = {
	.statfs		= simple_statfs,
	.remount_fs	= devpts_remount,
	.show_options	= devpts_show_options,
};

static void *new_pts_fs_info(void)
{
	struct pts_fs_info *fsi;

	fsi = kzalloc(sizeof(struct pts_fs_info), GFP_KERNEL);
	if (!fsi)
		return NULL;

	ida_init(&fsi->allocated_ptys);
	fsi->mount_opts.mode = DEVPTS_DEFAULT_MODE;
	fsi->mount_opts.ptmxmode = DEVPTS_DEFAULT_PTMX_MODE;

	return fsi;
}

static int
devpts_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode *inode;

	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = DEVPTS_SUPER_MAGIC;
	s->s_op = &devpts_sops;
	s->s_time_gran = 1;

	s->s_fs_info = new_pts_fs_info();
	if (!s->s_fs_info)
		goto fail;

	inode = new_inode(s);
	if (!inode)
		goto fail;
	inode->i_ino = 1;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);

	s->s_root = d_make_root(inode);
	if (s->s_root)
		return 0;

	printk(KERN_ERR "devpts: get root dentry failed\n");

fail:
	return -ENOMEM;
}

#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
static int compare_init_pts_sb(struct super_block *s, void *p)
{
	if (devpts_mnt)
		return devpts_mnt->mnt_sb == s;
	return 0;
}

/*
 * devpts_mount()
 *
 *     If the '-o newinstance' mount option was specified, mount a new
 *     (private) instance of devpts.  PTYs created in this instance are
 *     independent of the PTYs in other devpts instances.
 *
 *     If the '-o newinstance' option was not specified, mount/remount the
 *     initial kernel mount of devpts.  This type of mount gives the
 *     legacy, single-instance semantics.
 *
 *     The 'newinstance' option is needed to support multiple namespace
 *     semantics in devpts while preserving backward compatibility of the
 *     current 'single-namespace' semantics. i.e all mounts of devpts
 *     without the 'newinstance' mount option should bind to the initial
 *     kernel mount, like mount_single().
 *
 *     Mounts with 'newinstance' option create a new, private namespace.
 *
 *     NOTE:
 *
 *     For single-mount semantics, devpts cannot use mount_single(),
 *     because mount_single()/sget() find and use the super-block from
 *     the most recent mount of devpts. But that recent mount may be a
 *     'newinstance' mount and mount_single() would pick the newinstance
 *     super-block instead of the initial super-block.
 */
static struct dentry *devpts_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	int error;
	struct pts_mount_opts opts;
	struct super_block *s;

	error = parse_mount_options(data, PARSE_MOUNT, &opts);
	if (error)
		return ERR_PTR(error);

	/* Require newinstance for all user namespace mounts to ensure
	 * the mount options are not changed.
	 */
	if ((current_user_ns() != &init_user_ns) && !opts.newinstance)
		return ERR_PTR(-EINVAL);

	if (opts.newinstance)
		s = sget(fs_type, NULL, set_anon_super, flags, NULL);
	else
		s = sget(fs_type, compare_init_pts_sb, set_anon_super, flags,
			 NULL);

	if (IS_ERR(s))
		return ERR_CAST(s);

	if (!s->s_root) {
		error = devpts_fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error)
			goto out_undo_sget;
		s->s_flags |= MS_ACTIVE;
	}

	memcpy(&(DEVPTS_SB(s))->mount_opts, &opts, sizeof(opts));

	error = mknod_ptmx(s);
	if (error)
		goto out_undo_sget;

	return dget(s->s_root);

out_undo_sget:
	deactivate_locked_super(s);
	return ERR_PTR(error);
}

#else
/*
 * This supports only the legacy single-instance semantics (no
 * multiple-instance semantics)
 */
static struct dentry *devpts_mount(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, devpts_fill_super);
}
#endif

static void devpts_kill_sb(struct super_block *sb)
{
	struct pts_fs_info *fsi = DEVPTS_SB(sb);

	ida_destroy(&fsi->allocated_ptys);
	kfree(fsi);
	kill_litter_super(sb);
}

static struct file_system_type devpts_fs_type = {
	.name		= "devpts",
	.mount		= devpts_mount,
	.kill_sb	= devpts_kill_sb,
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	.fs_flags	= FS_USERNS_MOUNT | FS_USERNS_DEV_MOUNT,
#endif
};

/*
 * The normal naming convention is simply /dev/pts/<number>; this conforms
 * to the System V naming convention
 */

int devpts_new_index(struct inode *ptmx_inode)
{
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	int index;
	int ida_ret;

retry:
	if (!ida_pre_get(&fsi->allocated_ptys, GFP_KERNEL))
		return -ENOMEM;

	mutex_lock(&allocated_ptys_lock);
	if (pty_count >= pty_limit -
			(fsi->mount_opts.newinstance ? pty_reserve : 0)) {
		mutex_unlock(&allocated_ptys_lock);
		return -ENOSPC;
	}

	ida_ret = ida_get_new(&fsi->allocated_ptys, &index);
	if (ida_ret < 0) {
		mutex_unlock(&allocated_ptys_lock);
		if (ida_ret == -EAGAIN)
			goto retry;
		return -EIO;
	}

	if (index >= fsi->mount_opts.max) {
		ida_remove(&fsi->allocated_ptys, index);
		mutex_unlock(&allocated_ptys_lock);
		return -ENOSPC;
	}
	pty_count++;
	mutex_unlock(&allocated_ptys_lock);
	return index;
}

void devpts_kill_index(struct inode *ptmx_inode, int idx)
{
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);
	struct pts_fs_info *fsi = DEVPTS_SB(sb);

	mutex_lock(&allocated_ptys_lock);
	ida_remove(&fsi->allocated_ptys, idx);
	pty_count--;
	mutex_unlock(&allocated_ptys_lock);
}

/*
 * pty code needs to hold extra references in case of last /dev/tty close
 */

void devpts_add_ref(struct inode *ptmx_inode)
{
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);

	atomic_inc(&sb->s_active);
	ihold(ptmx_inode);
}

void devpts_del_ref(struct inode *ptmx_inode)
{
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);

	iput(ptmx_inode);
	deactivate_super(sb);
}

/**
 * devpts_pty_new -- create a new inode in /dev/pts/
 * @ptmx_inode: inode of the master
 * @device: major+minor of the node to be created
 * @index: used as a name of the node
 * @priv: what's given back by devpts_get_priv
 *
 * The created inode is returned. Remove it from /dev/pts/ by devpts_pty_kill.
 */
struct inode *devpts_pty_new(struct inode *ptmx_inode, dev_t device, int index,
		void *priv)
{
	struct dentry *dentry;
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);
	struct inode *inode;
	struct dentry *root = sb->s_root;
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;
	char s[12];

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = index + 3;
	inode->i_uid = opts->setuid ? opts->uid : current_fsuid();
	inode->i_gid = opts->setgid ? opts->gid : current_fsgid();
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	init_special_inode(inode, S_IFCHR|opts->mode, device);
	inode->i_private = priv;

	sprintf(s, "%d", index);

	mutex_lock(&root->d_inode->i_mutex);

	dentry = d_alloc_name(root, s);
	if (dentry) {
		d_add(dentry, inode);
		fsnotify_create(root->d_inode, dentry);
	} else {
		iput(inode);
		inode = ERR_PTR(-ENOMEM);
	}

	mutex_unlock(&root->d_inode->i_mutex);

	return inode;
}

/**
 * devpts_get_priv -- get private data for a slave
 * @pts_inode: inode of the slave
 *
 * Returns whatever was passed as priv in devpts_pty_new for a given inode.
 */
void *devpts_get_priv(struct inode *pts_inode)
{
	struct dentry *dentry;
	void *priv = NULL;

	BUG_ON(pts_inode->i_rdev == MKDEV(TTYAUX_MAJOR, PTMX_MINOR));

	/* Ensure dentry has not been deleted by devpts_pty_kill() */
	dentry = d_find_alias(pts_inode);
	if (!dentry)
		return NULL;

	if (pts_inode->i_sb->s_magic == DEVPTS_SUPER_MAGIC)
		priv = pts_inode->i_private;

	dput(dentry);

	return priv;
}

/**
 * devpts_pty_kill -- remove inode form /dev/pts/
 * @inode: inode of the slave to be removed
 *
 * This is an inverse operation of devpts_pty_new.
 */
void devpts_pty_kill(struct inode *inode)
{
	struct super_block *sb = pts_sb_from_inode(inode);
	struct dentry *root = sb->s_root;
	struct dentry *dentry;

	BUG_ON(inode->i_rdev == MKDEV(TTYAUX_MAJOR, PTMX_MINOR));

	mutex_lock(&root->d_inode->i_mutex);

	dentry = d_find_alias(inode);

	drop_nlink(inode);
	d_delete(dentry);
	dput(dentry);	/* d_alloc_name() in devpts_pty_new() */
	dput(dentry);		/* d_find_alias above */

	mutex_unlock(&root->d_inode->i_mutex);
}

static int __init init_devpts_fs(void)
{
	int err = register_filesystem(&devpts_fs_type);
	struct ctl_table_header *table;

	if (!err) {
		table = register_sysctl_table(pty_root_table);
		devpts_mnt = kern_mount(&devpts_fs_type);
		if (IS_ERR(devpts_mnt)) {
			err = PTR_ERR(devpts_mnt);
			unregister_filesystem(&devpts_fs_type);
			unregister_sysctl_table(table);
		}
	}
	return err;
}
module_init(init_devpts_fs)
