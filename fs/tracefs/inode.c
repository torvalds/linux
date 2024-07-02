// SPDX-License-Identifier: GPL-2.0-only
/*
 *  inode.c - part of tracefs, a pseudo file system for activating tracing
 *
 * Based on debugfs by: Greg Kroah-Hartman <greg@kroah.com>
 *
 *  Copyright (C) 2014 Red Hat Inc, author: Steven Rostedt <srostedt@redhat.com>
 *
 * tracefs is the file system that is used by the tracing infrastructure.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/tracefs.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include "internal.h"

#define TRACEFS_DEFAULT_MODE	0700
static struct kmem_cache *tracefs_inode_cachep __ro_after_init;

static struct vfsmount *tracefs_mount;
static int tracefs_mount_count;
static bool tracefs_registered;

/*
 * Keep track of all tracefs_inodes in order to update their
 * flags if necessary on a remount.
 */
static DEFINE_SPINLOCK(tracefs_inode_lock);
static LIST_HEAD(tracefs_inodes);

static struct inode *tracefs_alloc_inode(struct super_block *sb)
{
	struct tracefs_inode *ti;
	unsigned long flags;

	ti = kmem_cache_alloc(tracefs_inode_cachep, GFP_KERNEL);
	if (!ti)
		return NULL;

	spin_lock_irqsave(&tracefs_inode_lock, flags);
	list_add_rcu(&ti->list, &tracefs_inodes);
	spin_unlock_irqrestore(&tracefs_inode_lock, flags);

	return &ti->vfs_inode;
}

static void tracefs_free_inode_rcu(struct rcu_head *rcu)
{
	struct tracefs_inode *ti;

	ti = container_of(rcu, struct tracefs_inode, rcu);
	kmem_cache_free(tracefs_inode_cachep, ti);
}

static void tracefs_free_inode(struct inode *inode)
{
	struct tracefs_inode *ti = get_tracefs(inode);
	unsigned long flags;

	spin_lock_irqsave(&tracefs_inode_lock, flags);
	list_del_rcu(&ti->list);
	spin_unlock_irqrestore(&tracefs_inode_lock, flags);

	call_rcu(&ti->rcu, tracefs_free_inode_rcu);
}

static ssize_t default_read_file(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t default_write_file(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return count;
}

static const struct file_operations tracefs_file_operations = {
	.read =		default_read_file,
	.write =	default_write_file,
	.open =		simple_open,
	.llseek =	noop_llseek,
};

static struct tracefs_dir_ops {
	int (*mkdir)(const char *name);
	int (*rmdir)(const char *name);
} tracefs_ops __ro_after_init;

static char *get_dname(struct dentry *dentry)
{
	const char *dname;
	char *name;
	int len = dentry->d_name.len;

	dname = dentry->d_name.name;
	name = kmalloc(len + 1, GFP_KERNEL);
	if (!name)
		return NULL;
	memcpy(name, dname, len);
	name[len] = 0;
	return name;
}

static int tracefs_syscall_mkdir(struct mnt_idmap *idmap,
				 struct inode *inode, struct dentry *dentry,
				 umode_t mode)
{
	struct tracefs_inode *ti;
	char *name;
	int ret;

	name = get_dname(dentry);
	if (!name)
		return -ENOMEM;

	/*
	 * This is a new directory that does not take the default of
	 * the rootfs. It becomes the default permissions for all the
	 * files and directories underneath it.
	 */
	ti = get_tracefs(inode);
	ti->flags |= TRACEFS_INSTANCE_INODE;
	ti->private = inode;

	/*
	 * The mkdir call can call the generic functions that create
	 * the files within the tracefs system. It is up to the individual
	 * mkdir routine to handle races.
	 */
	inode_unlock(inode);
	ret = tracefs_ops.mkdir(name);
	inode_lock(inode);

	kfree(name);

	return ret;
}

static int tracefs_syscall_rmdir(struct inode *inode, struct dentry *dentry)
{
	char *name;
	int ret;

	name = get_dname(dentry);
	if (!name)
		return -ENOMEM;

	/*
	 * The rmdir call can call the generic functions that create
	 * the files within the tracefs system. It is up to the individual
	 * rmdir routine to handle races.
	 * This time we need to unlock not only the parent (inode) but
	 * also the directory that is being deleted.
	 */
	inode_unlock(inode);
	inode_unlock(d_inode(dentry));

	ret = tracefs_ops.rmdir(name);

	inode_lock_nested(inode, I_MUTEX_PARENT);
	inode_lock(d_inode(dentry));

	kfree(name);

	return ret;
}

static void set_tracefs_inode_owner(struct inode *inode)
{
	struct tracefs_inode *ti = get_tracefs(inode);
	struct inode *root_inode = ti->private;
	kuid_t uid;
	kgid_t gid;

	uid = root_inode->i_uid;
	gid = root_inode->i_gid;

	/*
	 * If the root is not the mount point, then check the root's
	 * permissions. If it was never set, then default to the
	 * mount point.
	 */
	if (root_inode != d_inode(root_inode->i_sb->s_root)) {
		struct tracefs_inode *rti;

		rti = get_tracefs(root_inode);
		root_inode = d_inode(root_inode->i_sb->s_root);

		if (!(rti->flags & TRACEFS_UID_PERM_SET))
			uid = root_inode->i_uid;

		if (!(rti->flags & TRACEFS_GID_PERM_SET))
			gid = root_inode->i_gid;
	}

	/*
	 * If this inode has never been referenced, then update
	 * the permissions to the superblock.
	 */
	if (!(ti->flags & TRACEFS_UID_PERM_SET))
		inode->i_uid = uid;

	if (!(ti->flags & TRACEFS_GID_PERM_SET))
		inode->i_gid = gid;
}

static int tracefs_permission(struct mnt_idmap *idmap,
			      struct inode *inode, int mask)
{
	set_tracefs_inode_owner(inode);
	return generic_permission(idmap, inode, mask);
}

static int tracefs_getattr(struct mnt_idmap *idmap,
			   const struct path *path, struct kstat *stat,
			   u32 request_mask, unsigned int flags)
{
	struct inode *inode = d_backing_inode(path->dentry);

	set_tracefs_inode_owner(inode);
	generic_fillattr(idmap, request_mask, inode, stat);
	return 0;
}

static int tracefs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			   struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;
	struct inode *inode = d_inode(dentry);
	struct tracefs_inode *ti = get_tracefs(inode);

	if (ia_valid & ATTR_UID)
		ti->flags |= TRACEFS_UID_PERM_SET;

	if (ia_valid & ATTR_GID)
		ti->flags |= TRACEFS_GID_PERM_SET;

	return simple_setattr(idmap, dentry, attr);
}

static const struct inode_operations tracefs_instance_dir_inode_operations = {
	.lookup		= simple_lookup,
	.mkdir		= tracefs_syscall_mkdir,
	.rmdir		= tracefs_syscall_rmdir,
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

static const struct inode_operations tracefs_dir_inode_operations = {
	.lookup		= simple_lookup,
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

static const struct inode_operations tracefs_file_inode_operations = {
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

struct inode *tracefs_get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		simple_inode_init_ts(inode);
	}
	return inode;
}

struct tracefs_fs_info {
	kuid_t uid;
	kgid_t gid;
	umode_t mode;
	/* Opt_* bitfield. */
	unsigned int opts;
};

enum {
	Opt_uid,
	Opt_gid,
	Opt_mode,
};

static const struct fs_parameter_spec tracefs_param_specs[] = {
	fsparam_u32	("gid",		Opt_gid),
	fsparam_u32oct	("mode",	Opt_mode),
	fsparam_u32	("uid",		Opt_uid),
	{}
};

static int tracefs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct tracefs_fs_info *opts = fc->s_fs_info;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt;

	opt = fs_parse(fc, tracefs_param_specs, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(uid))
			return invalf(fc, "Unknown uid");
		opts->uid = uid;
		break;
	case Opt_gid:
		gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(gid))
			return invalf(fc, "Unknown gid");
		opts->gid = gid;
		break;
	case Opt_mode:
		opts->mode = result.uint_32 & S_IALLUGO;
		break;
	/*
	 * We might like to report bad mount options here;
	 * but traditionally tracefs has ignored all mount options
	 */
	}

	opts->opts |= BIT(opt);

	return 0;
}

static int tracefs_apply_options(struct super_block *sb, bool remount)
{
	struct tracefs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode = d_inode(sb->s_root);
	struct tracefs_inode *ti;
	bool update_uid, update_gid;
	umode_t tmp_mode;

	/*
	 * On remount, only reset mode/uid/gid if they were provided as mount
	 * options.
	 */

	if (!remount || fsi->opts & BIT(Opt_mode)) {
		tmp_mode = READ_ONCE(inode->i_mode) & ~S_IALLUGO;
		tmp_mode |= fsi->mode;
		WRITE_ONCE(inode->i_mode, tmp_mode);
	}

	if (!remount || fsi->opts & BIT(Opt_uid))
		inode->i_uid = fsi->uid;

	if (!remount || fsi->opts & BIT(Opt_gid))
		inode->i_gid = fsi->gid;

	if (remount && (fsi->opts & BIT(Opt_uid) || fsi->opts & BIT(Opt_gid))) {

		update_uid = fsi->opts & BIT(Opt_uid);
		update_gid = fsi->opts & BIT(Opt_gid);

		rcu_read_lock();
		list_for_each_entry_rcu(ti, &tracefs_inodes, list) {
			if (update_uid)
				ti->flags &= ~TRACEFS_UID_PERM_SET;

			if (update_gid)
				ti->flags &= ~TRACEFS_GID_PERM_SET;

			if (ti->flags & TRACEFS_EVENT_INODE)
				eventfs_remount(ti, update_uid, update_gid);
		}
		rcu_read_unlock();
	}

	return 0;
}

static int tracefs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct tracefs_fs_info *sb_opts = sb->s_fs_info;
	struct tracefs_fs_info *new_opts = fc->s_fs_info;

	sync_filesystem(sb);
	/* structure copy of new mount options to sb */
	*sb_opts = *new_opts;

	return tracefs_apply_options(sb, true);
}

static int tracefs_show_options(struct seq_file *m, struct dentry *root)
{
	struct tracefs_fs_info *fsi = root->d_sb->s_fs_info;

	if (!uid_eq(fsi->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, fsi->uid));
	if (!gid_eq(fsi->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, fsi->gid));
	if (fsi->mode != TRACEFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mode);

	return 0;
}

static const struct super_operations tracefs_super_operations = {
	.alloc_inode    = tracefs_alloc_inode,
	.free_inode     = tracefs_free_inode,
	.drop_inode     = generic_delete_inode,
	.statfs		= simple_statfs,
	.show_options	= tracefs_show_options,
};

/*
 * It would be cleaner if eventfs had its own dentry ops.
 *
 * Note that d_revalidate is called potentially under RCU,
 * so it can't take the eventfs mutex etc. It's fine - if
 * we open a file just as it's marked dead, things will
 * still work just fine, and just see the old stale case.
 */
static void tracefs_d_release(struct dentry *dentry)
{
	if (dentry->d_fsdata)
		eventfs_d_release(dentry);
}

static int tracefs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct eventfs_inode *ei = dentry->d_fsdata;

	return !(ei && ei->is_freed);
}

static void tracefs_d_iput(struct dentry *dentry, struct inode *inode)
{
	struct tracefs_inode *ti = get_tracefs(inode);

	/*
	 * This inode is being freed and cannot be used for
	 * eventfs. Clear the flag so that it doesn't call into
	 * eventfs during the remount flag updates. The eventfs_inode
	 * gets freed after an RCU cycle, so the content will still
	 * be safe if the iteration is going on now.
	 */
	ti->flags &= ~TRACEFS_EVENT_INODE;
}

static const struct dentry_operations tracefs_dentry_operations = {
	.d_iput = tracefs_d_iput,
	.d_revalidate = tracefs_d_revalidate,
	.d_release = tracefs_d_release,
};

static int tracefs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr trace_files[] = {{""}};
	int err;

	err = simple_fill_super(sb, TRACEFS_MAGIC, trace_files);
	if (err)
		return err;

	sb->s_op = &tracefs_super_operations;
	sb->s_d_op = &tracefs_dentry_operations;

	tracefs_apply_options(sb, false);

	return 0;
}

static int tracefs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, tracefs_fill_super);
}

static void tracefs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations tracefs_context_ops = {
	.free		= tracefs_free_fc,
	.parse_param	= tracefs_parse_param,
	.get_tree	= tracefs_get_tree,
	.reconfigure	= tracefs_reconfigure,
};

static int tracefs_init_fs_context(struct fs_context *fc)
{
	struct tracefs_fs_info *fsi;

	fsi = kzalloc(sizeof(struct tracefs_fs_info), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;

	fsi->mode = TRACEFS_DEFAULT_MODE;

	fc->s_fs_info = fsi;
	fc->ops = &tracefs_context_ops;
	return 0;
}

static struct file_system_type trace_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"tracefs",
	.init_fs_context = tracefs_init_fs_context,
	.parameters	= tracefs_param_specs,
	.kill_sb =	kill_litter_super,
};
MODULE_ALIAS_FS("tracefs");

struct dentry *tracefs_start_creating(const char *name, struct dentry *parent)
{
	struct dentry *dentry;
	int error;

	pr_debug("tracefs: creating file '%s'\n",name);

	error = simple_pin_fs(&trace_fs_type, &tracefs_mount,
			      &tracefs_mount_count);
	if (error)
		return ERR_PTR(error);

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = tracefs_mount->mnt_root;

	inode_lock(d_inode(parent));
	if (unlikely(IS_DEADDIR(d_inode(parent))))
		dentry = ERR_PTR(-ENOENT);
	else
		dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry) && d_inode(dentry)) {
		dput(dentry);
		dentry = ERR_PTR(-EEXIST);
	}

	if (IS_ERR(dentry)) {
		inode_unlock(d_inode(parent));
		simple_release_fs(&tracefs_mount, &tracefs_mount_count);
	}

	return dentry;
}

struct dentry *tracefs_failed_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	dput(dentry);
	simple_release_fs(&tracefs_mount, &tracefs_mount_count);
	return NULL;
}

struct dentry *tracefs_end_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	return dentry;
}

/* Find the inode that this will use for default */
static struct inode *instance_inode(struct dentry *parent, struct inode *inode)
{
	struct tracefs_inode *ti;

	/* If parent is NULL then use root inode */
	if (!parent)
		return d_inode(inode->i_sb->s_root);

	/* Find the inode that is flagged as an instance or the root inode */
	while (!IS_ROOT(parent)) {
		ti = get_tracefs(d_inode(parent));
		if (ti->flags & TRACEFS_INSTANCE_INODE)
			break;
		parent = parent->d_parent;
	}

	return d_inode(parent);
}

/**
 * tracefs_create_file - create a file in the tracefs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the tracefs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This is the basic "create a file" function for tracefs.  It allows for a
 * wide range of flexibility in creating a file, or a directory (if you want
 * to create a directory, the tracefs_create_dir() function is
 * recommended to be used instead.)
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the tracefs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If tracefs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *tracefs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{
	struct tracefs_inode *ti;
	struct dentry *dentry;
	struct inode *inode;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;
	BUG_ON(!S_ISREG(mode));
	dentry = tracefs_start_creating(name, parent);

	if (IS_ERR(dentry))
		return NULL;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return tracefs_failed_creating(dentry);

	ti = get_tracefs(inode);
	ti->private = instance_inode(parent, inode);

	inode->i_mode = mode;
	inode->i_op = &tracefs_file_inode_operations;
	inode->i_fop = fops ? fops : &tracefs_file_operations;
	inode->i_private = data;
	inode->i_uid = d_inode(dentry->d_parent)->i_uid;
	inode->i_gid = d_inode(dentry->d_parent)->i_gid;
	d_instantiate(dentry, inode);
	fsnotify_create(d_inode(dentry->d_parent), dentry);
	return tracefs_end_creating(dentry);
}

static struct dentry *__create_dir(const char *name, struct dentry *parent,
				   const struct inode_operations *ops)
{
	struct tracefs_inode *ti;
	struct dentry *dentry = tracefs_start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return NULL;

	inode = tracefs_get_inode(dentry->d_sb);
	if (unlikely(!inode))
		return tracefs_failed_creating(dentry);

	/* Do not set bits for OTH */
	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUSR| S_IRGRP | S_IXUSR | S_IXGRP;
	inode->i_op = ops;
	inode->i_fop = &simple_dir_operations;
	inode->i_uid = d_inode(dentry->d_parent)->i_uid;
	inode->i_gid = d_inode(dentry->d_parent)->i_gid;

	ti = get_tracefs(inode);
	ti->private = instance_inode(parent, inode);

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return tracefs_end_creating(dentry);
}

/**
 * tracefs_create_dir - create a directory in the tracefs filesystem
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          directory will be created in the root of the tracefs filesystem.
 *
 * This function creates a directory in tracefs with the given name.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the tracefs_remove() function when the file is
 * to be removed. If an error occurs, %NULL will be returned.
 *
 * If tracing is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *tracefs_create_dir(const char *name, struct dentry *parent)
{
	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	return __create_dir(name, parent, &tracefs_dir_inode_operations);
}

/**
 * tracefs_create_instance_dir - create the tracing instances directory
 * @name: The name of the instances directory to create
 * @parent: The parent directory that the instances directory will exist
 * @mkdir: The function to call when a mkdir is performed.
 * @rmdir: The function to call when a rmdir is performed.
 *
 * Only one instances directory is allowed.
 *
 * The instances directory is special as it allows for mkdir and rmdir
 * to be done by userspace. When a mkdir or rmdir is performed, the inode
 * locks are released and the methods passed in (@mkdir and @rmdir) are
 * called without locks and with the name of the directory being created
 * within the instances directory.
 *
 * Returns the dentry of the instances directory.
 */
__init struct dentry *tracefs_create_instance_dir(const char *name,
					  struct dentry *parent,
					  int (*mkdir)(const char *name),
					  int (*rmdir)(const char *name))
{
	struct dentry *dentry;

	/* Only allow one instance of the instances directory. */
	if (WARN_ON(tracefs_ops.mkdir || tracefs_ops.rmdir))
		return NULL;

	dentry = __create_dir(name, parent, &tracefs_instance_dir_inode_operations);
	if (!dentry)
		return NULL;

	tracefs_ops.mkdir = mkdir;
	tracefs_ops.rmdir = rmdir;

	return dentry;
}

static void remove_one(struct dentry *victim)
{
	simple_release_fs(&tracefs_mount, &tracefs_mount_count);
}

/**
 * tracefs_remove - recursively removes a directory
 * @dentry: a pointer to a the dentry of the directory to be removed.
 *
 * This function recursively removes a directory tree in tracefs that
 * was previously created with a call to another tracefs function
 * (like tracefs_create_file() or variants thereof.)
 */
void tracefs_remove(struct dentry *dentry)
{
	if (IS_ERR_OR_NULL(dentry))
		return;

	simple_pin_fs(&trace_fs_type, &tracefs_mount, &tracefs_mount_count);
	simple_recursive_removal(dentry, remove_one);
	simple_release_fs(&tracefs_mount, &tracefs_mount_count);
}

/**
 * tracefs_initialized - Tells whether tracefs has been registered
 */
bool tracefs_initialized(void)
{
	return tracefs_registered;
}

static void init_once(void *foo)
{
	struct tracefs_inode *ti = (struct tracefs_inode *) foo;

	/* inode_init_once() calls memset() on the vfs_inode portion */
	inode_init_once(&ti->vfs_inode);

	/* Zero out the rest */
	memset_after(ti, 0, vfs_inode);
}

static int __init tracefs_init(void)
{
	int retval;

	tracefs_inode_cachep = kmem_cache_create("tracefs_inode_cache",
						 sizeof(struct tracefs_inode),
						 0, (SLAB_RECLAIM_ACCOUNT|
						     SLAB_ACCOUNT),
						 init_once);
	if (!tracefs_inode_cachep)
		return -ENOMEM;

	retval = sysfs_create_mount_point(kernel_kobj, "tracing");
	if (retval)
		return -EINVAL;

	retval = register_filesystem(&trace_fs_type);
	if (!retval)
		tracefs_registered = true;

	return retval;
}
core_initcall(tracefs_init);
