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

extern int pty_limit;			/* Config limit on Unix98 ptys */
static DEFINE_MUTEX(allocated_ptys_lock);

static struct vfsmount *devpts_mnt;

struct pts_mount_opts {
	int setuid;
	int setgid;
	uid_t   uid;
	gid_t   gid;
	umode_t mode;
	umode_t ptmxmode;
	int newinstance;
};

enum {
	Opt_uid, Opt_gid, Opt_mode, Opt_ptmxmode, Opt_newinstance,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_mode, "mode=%o"},
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	{Opt_ptmxmode, "ptmxmode=%o"},
	{Opt_newinstance, "newinstance"},
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

	opts->setuid  = 0;
	opts->setgid  = 0;
	opts->uid     = 0;
	opts->gid     = 0;
	opts->mode    = DEVPTS_DEFAULT_MODE;
	opts->ptmxmode = DEVPTS_DEFAULT_PTMX_MODE;

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
			opts->uid = option;
			opts->setuid = 1;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->gid = option;
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

static int devpts_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct pts_fs_info *fsi = DEVPTS_SB(vfs->mnt_sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;

	if (opts->setuid)
		seq_printf(seq, ",uid=%u", opts->uid);
	if (opts->setgid)
		seq_printf(seq, ",gid=%u", opts->gid);
	seq_printf(seq, ",mode=%03o", opts->mode);
#ifdef CONFIG_DEVPTS_MULTIPLE_INSTANCES
	seq_printf(seq, ",ptmxmode=%03o", opts->ptmxmode);
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
		goto free_fsi;
	inode->i_ino = 1;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inode->i_nlink = 2;

	s->s_root = d_alloc_root(inode);
	if (s->s_root)
		return 0;

	printk(KERN_ERR "devpts: get root dentry failed\n");
	iput(inode);

free_fsi:
	kfree(s->s_fs_info);
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
 * devpts_get_sb()
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
 *     kernel mount, like get_sb_single().
 *
 *     Mounts with 'newinstance' option create a new, private namespace.
 *
 *     NOTE:
 *
 *     For single-mount semantics, devpts cannot use get_sb_single(),
 *     because get_sb_single()/sget() find and use the super-block from
 *     the most recent mount of devpts. But that recent mount may be a
 *     'newinstance' mount and get_sb_single() would pick the newinstance
 *     super-block instead of the initial super-block.
 */
static int devpts_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	int error;
	struct pts_mount_opts opts;
	struct super_block *s;

	error = parse_mount_options(data, PARSE_MOUNT, &opts);
	if (error)
		return error;

	if (opts.newinstance)
		s = sget(fs_type, NULL, set_anon_super, NULL);
	else
		s = sget(fs_type, compare_init_pts_sb, set_anon_super, NULL);

	if (IS_ERR(s))
		return PTR_ERR(s);

	if (!s->s_root) {
		s->s_flags = flags;
		error = devpts_fill_super(s, data, flags & MS_SILENT ? 1 : 0);
		if (error)
			goto out_undo_sget;
		s->s_flags |= MS_ACTIVE;
	}

	simple_set_mnt(mnt, s);

	memcpy(&(DEVPTS_SB(s))->mount_opts, &opts, sizeof(opts));

	error = mknod_ptmx(s);
	if (error)
		goto out_dput;

	return 0;

out_dput:
	dput(s->s_root); /* undo dget() in simple_set_mnt() */

out_undo_sget:
	deactivate_locked_super(s);
	return error;
}

#else
/*
 * This supports only the legacy single-instance semantics (no
 * multiple-instance semantics)
 */
static int devpts_get_sb(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, devpts_fill_super, mnt);
}
#endif

static void devpts_kill_sb(struct super_block *sb)
{
	struct pts_fs_info *fsi = DEVPTS_SB(sb);

	kfree(fsi);
	kill_litter_super(sb);
}

static struct file_system_type devpts_fs_type = {
	.name		= "devpts",
	.get_sb		= devpts_get_sb,
	.kill_sb	= devpts_kill_sb,
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
	ida_ret = ida_get_new(&fsi->allocated_ptys, &index);
	if (ida_ret < 0) {
		mutex_unlock(&allocated_ptys_lock);
		if (ida_ret == -EAGAIN)
			goto retry;
		return -EIO;
	}

	if (index >= pty_limit) {
		ida_remove(&fsi->allocated_ptys, index);
		mutex_unlock(&allocated_ptys_lock);
		return -EIO;
	}
	mutex_unlock(&allocated_ptys_lock);
	return index;
}

void devpts_kill_index(struct inode *ptmx_inode, int idx)
{
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);
	struct pts_fs_info *fsi = DEVPTS_SB(sb);

	mutex_lock(&allocated_ptys_lock);
	ida_remove(&fsi->allocated_ptys, idx);
	mutex_unlock(&allocated_ptys_lock);
}

int devpts_pty_new(struct inode *ptmx_inode, struct tty_struct *tty)
{
	/* tty layer puts index from devpts_new_index() in here */
	int number = tty->index;
	struct tty_driver *driver = tty->driver;
	dev_t device = MKDEV(driver->major, driver->minor_start+number);
	struct dentry *dentry;
	struct super_block *sb = pts_sb_from_inode(ptmx_inode);
	struct inode *inode = new_inode(sb);
	struct dentry *root = sb->s_root;
	struct pts_fs_info *fsi = DEVPTS_SB(sb);
	struct pts_mount_opts *opts = &fsi->mount_opts;
	char s[12];

	/* We're supposed to be given the slave end of a pty */
	BUG_ON(driver->type != TTY_DRIVER_TYPE_PTY);
	BUG_ON(driver->subtype != PTY_TYPE_SLAVE);

	if (!inode)
		return -ENOMEM;

	inode->i_ino = number + 3;
	inode->i_uid = opts->setuid ? opts->uid : current_fsuid();
	inode->i_gid = opts->setgid ? opts->gid : current_fsgid();
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	init_special_inode(inode, S_IFCHR|opts->mode, device);
	inode->i_private = tty;
	tty->driver_data = inode;

	sprintf(s, "%d", number);

	mutex_lock(&root->d_inode->i_mutex);

	dentry = d_alloc_name(root, s);
	if (!IS_ERR(dentry)) {
		d_add(dentry, inode);
		fsnotify_create(root->d_inode, dentry);
	}

	mutex_unlock(&root->d_inode->i_mutex);

	return 0;
}

struct tty_struct *devpts_get_tty(struct inode *pts_inode, int number)
{
	struct dentry *dentry;
	struct tty_struct *tty;

	BUG_ON(pts_inode->i_rdev == MKDEV(TTYAUX_MAJOR, PTMX_MINOR));

	/* Ensure dentry has not been deleted by devpts_pty_kill() */
	dentry = d_find_alias(pts_inode);
	if (!dentry)
		return NULL;

	tty = NULL;
	if (pts_inode->i_sb->s_magic == DEVPTS_SUPER_MAGIC)
		tty = (struct tty_struct *)pts_inode->i_private;

	dput(dentry);

	return tty;
}

void devpts_pty_kill(struct tty_struct *tty)
{
	struct inode *inode = tty->driver_data;
	struct super_block *sb = pts_sb_from_inode(inode);
	struct dentry *root = sb->s_root;
	struct dentry *dentry;

	BUG_ON(inode->i_rdev == MKDEV(TTYAUX_MAJOR, PTMX_MINOR));

	mutex_lock(&root->d_inode->i_mutex);

	dentry = d_find_alias(inode);
	if (IS_ERR(dentry))
		goto out;

	if (dentry) {
		inode->i_nlink--;
		d_delete(dentry);
		dput(dentry);	/* d_alloc_name() in devpts_pty_new() */
	}

	dput(dentry);		/* d_find_alias above */
out:
	mutex_unlock(&root->d_inode->i_mutex);
}

static int __init init_devpts_fs(void)
{
	int err = register_filesystem(&devpts_fs_type);
	if (!err) {
		devpts_mnt = kern_mount(&devpts_fs_type);
		if (IS_ERR(devpts_mnt)) {
			err = PTR_ERR(devpts_mnt);
			unregister_filesystem(&devpts_fs_type);
		}
	}
	return err;
}
module_init(init_devpts_fs)
