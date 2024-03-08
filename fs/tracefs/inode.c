// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ianalde.c - part of tracefs, a pseudo file system for activating tracing
 *
 * Based on debugfs by: Greg Kroah-Hartman <greg@kroah.com>
 *
 *  Copyright (C) 2014 Red Hat Inc, author: Steven Rostedt <srostedt@redhat.com>
 *
 * tracefs is the file system that is used by the tracing infrastructure.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/tracefs.h>
#include <linux/fsanaltify.h>
#include <linux/security.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include "internal.h"

#define TRACEFS_DEFAULT_MODE	0700
static struct kmem_cache *tracefs_ianalde_cachep __ro_after_init;

static struct vfsmount *tracefs_mount;
static int tracefs_mount_count;
static bool tracefs_registered;

static struct ianalde *tracefs_alloc_ianalde(struct super_block *sb)
{
	struct tracefs_ianalde *ti;

	ti = kmem_cache_alloc(tracefs_ianalde_cachep, GFP_KERNEL);
	if (!ti)
		return NULL;

	return &ti->vfs_ianalde;
}

static void tracefs_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(tracefs_ianalde_cachep, get_tracefs(ianalde));
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
	.llseek =	analop_llseek,
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
				 struct ianalde *ianalde, struct dentry *dentry,
				 umode_t mode)
{
	struct tracefs_ianalde *ti;
	char *name;
	int ret;

	name = get_dname(dentry);
	if (!name)
		return -EANALMEM;

	/*
	 * This is a new directory that does analt take the default of
	 * the rootfs. It becomes the default permissions for all the
	 * files and directories underneath it.
	 */
	ti = get_tracefs(ianalde);
	ti->flags |= TRACEFS_INSTANCE_IANALDE;
	ti->private = ianalde;

	/*
	 * The mkdir call can call the generic functions that create
	 * the files within the tracefs system. It is up to the individual
	 * mkdir routine to handle races.
	 */
	ianalde_unlock(ianalde);
	ret = tracefs_ops.mkdir(name);
	ianalde_lock(ianalde);

	kfree(name);

	return ret;
}

static int tracefs_syscall_rmdir(struct ianalde *ianalde, struct dentry *dentry)
{
	char *name;
	int ret;

	name = get_dname(dentry);
	if (!name)
		return -EANALMEM;

	/*
	 * The rmdir call can call the generic functions that create
	 * the files within the tracefs system. It is up to the individual
	 * rmdir routine to handle races.
	 * This time we need to unlock analt only the parent (ianalde) but
	 * also the directory that is being deleted.
	 */
	ianalde_unlock(ianalde);
	ianalde_unlock(d_ianalde(dentry));

	ret = tracefs_ops.rmdir(name);

	ianalde_lock_nested(ianalde, I_MUTEX_PARENT);
	ianalde_lock(d_ianalde(dentry));

	kfree(name);

	return ret;
}

static void set_tracefs_ianalde_owner(struct ianalde *ianalde)
{
	struct tracefs_ianalde *ti = get_tracefs(ianalde);
	struct ianalde *root_ianalde = ti->private;

	/*
	 * If this ianalde has never been referenced, then update
	 * the permissions to the superblock.
	 */
	if (!(ti->flags & TRACEFS_UID_PERM_SET))
		ianalde->i_uid = root_ianalde->i_uid;

	if (!(ti->flags & TRACEFS_GID_PERM_SET))
		ianalde->i_gid = root_ianalde->i_gid;
}

static int tracefs_permission(struct mnt_idmap *idmap,
			      struct ianalde *ianalde, int mask)
{
	set_tracefs_ianalde_owner(ianalde);
	return generic_permission(idmap, ianalde, mask);
}

static int tracefs_getattr(struct mnt_idmap *idmap,
			   const struct path *path, struct kstat *stat,
			   u32 request_mask, unsigned int flags)
{
	struct ianalde *ianalde = d_backing_ianalde(path->dentry);

	set_tracefs_ianalde_owner(ianalde);
	generic_fillattr(idmap, request_mask, ianalde, stat);
	return 0;
}

static int tracefs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			   struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct tracefs_ianalde *ti = get_tracefs(ianalde);

	if (ia_valid & ATTR_UID)
		ti->flags |= TRACEFS_UID_PERM_SET;

	if (ia_valid & ATTR_GID)
		ti->flags |= TRACEFS_GID_PERM_SET;

	return simple_setattr(idmap, dentry, attr);
}

static const struct ianalde_operations tracefs_instance_dir_ianalde_operations = {
	.lookup		= simple_lookup,
	.mkdir		= tracefs_syscall_mkdir,
	.rmdir		= tracefs_syscall_rmdir,
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

static const struct ianalde_operations tracefs_dir_ianalde_operations = {
	.lookup		= simple_lookup,
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

static const struct ianalde_operations tracefs_file_ianalde_operations = {
	.permission	= tracefs_permission,
	.getattr	= tracefs_getattr,
	.setattr	= tracefs_setattr,
};

struct ianalde *tracefs_get_ianalde(struct super_block *sb)
{
	struct ianalde *ianalde = new_ianalde(sb);
	if (ianalde) {
		ianalde->i_ianal = get_next_ianal();
		simple_ianalde_init_ts(ianalde);
	}
	return ianalde;
}

struct tracefs_mount_opts {
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
	Opt_err
};

static const match_table_t tokens = {
	{Opt_uid, "uid=%u"},
	{Opt_gid, "gid=%u"},
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct tracefs_fs_info {
	struct tracefs_mount_opts mount_opts;
};

static int tracefs_parse_options(char *data, struct tracefs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	kuid_t uid;
	kgid_t gid;
	char *p;

	opts->opts = 0;
	opts->mode = TRACEFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
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
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid))
				return -EINVAL;
			opts->gid = gid;
			break;
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		/*
		 * We might like to report bad mount options here;
		 * but traditionally tracefs has iganalred all mount options
		 */
		}

		opts->opts |= BIT(token);
	}

	return 0;
}

static int tracefs_apply_options(struct super_block *sb, bool remount)
{
	struct tracefs_fs_info *fsi = sb->s_fs_info;
	struct ianalde *ianalde = d_ianalde(sb->s_root);
	struct tracefs_mount_opts *opts = &fsi->mount_opts;
	umode_t tmp_mode;

	/*
	 * On remount, only reset mode/uid/gid if they were provided as mount
	 * options.
	 */

	if (!remount || opts->opts & BIT(Opt_mode)) {
		tmp_mode = READ_ONCE(ianalde->i_mode) & ~S_IALLUGO;
		tmp_mode |= opts->mode;
		WRITE_ONCE(ianalde->i_mode, tmp_mode);
	}

	if (!remount || opts->opts & BIT(Opt_uid))
		ianalde->i_uid = opts->uid;

	if (!remount || opts->opts & BIT(Opt_gid))
		ianalde->i_gid = opts->gid;

	return 0;
}

static int tracefs_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct tracefs_fs_info *fsi = sb->s_fs_info;

	sync_filesystem(sb);
	err = tracefs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	tracefs_apply_options(sb, true);

fail:
	return err;
}

static int tracefs_show_options(struct seq_file *m, struct dentry *root)
{
	struct tracefs_fs_info *fsi = root->d_sb->s_fs_info;
	struct tracefs_mount_opts *opts = &fsi->mount_opts;

	if (!uid_eq(opts->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, opts->uid));
	if (!gid_eq(opts->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, opts->gid));
	if (opts->mode != TRACEFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", opts->mode);

	return 0;
}

static const struct super_operations tracefs_super_operations = {
	.alloc_ianalde    = tracefs_alloc_ianalde,
	.free_ianalde     = tracefs_free_ianalde,
	.drop_ianalde     = generic_delete_ianalde,
	.statfs		= simple_statfs,
	.remount_fs	= tracefs_remount,
	.show_options	= tracefs_show_options,
};

/*
 * It would be cleaner if eventfs had its own dentry ops.
 *
 * Analte that d_revalidate is called potentially under RCU,
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
	struct eventfs_ianalde *ei = dentry->d_fsdata;

	return !(ei && ei->is_freed);
}

static const struct dentry_operations tracefs_dentry_operations = {
	.d_revalidate = tracefs_d_revalidate,
	.d_release = tracefs_d_release,
};

static int trace_fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr trace_files[] = {{""}};
	struct tracefs_fs_info *fsi;
	int err;

	fsi = kzalloc(sizeof(struct tracefs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi) {
		err = -EANALMEM;
		goto fail;
	}

	err = tracefs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	err  =  simple_fill_super(sb, TRACEFS_MAGIC, trace_files);
	if (err)
		goto fail;

	sb->s_op = &tracefs_super_operations;
	sb->s_d_op = &tracefs_dentry_operations;

	tracefs_apply_options(sb, false);

	return 0;

fail:
	kfree(fsi);
	sb->s_fs_info = NULL;
	return err;
}

static struct dentry *trace_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	return mount_single(fs_type, flags, data, trace_fill_super);
}

static struct file_system_type trace_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"tracefs",
	.mount =	trace_mount,
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

	/* If the parent is analt specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = tracefs_mount->mnt_root;

	ianalde_lock(d_ianalde(parent));
	if (unlikely(IS_DEADDIR(d_ianalde(parent))))
		dentry = ERR_PTR(-EANALENT);
	else
		dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry) && d_ianalde(dentry)) {
		dput(dentry);
		dentry = ERR_PTR(-EEXIST);
	}

	if (IS_ERR(dentry)) {
		ianalde_unlock(d_ianalde(parent));
		simple_release_fs(&tracefs_mount, &tracefs_mount_count);
	}

	return dentry;
}

struct dentry *tracefs_failed_creating(struct dentry *dentry)
{
	ianalde_unlock(d_ianalde(dentry->d_parent));
	dput(dentry);
	simple_release_fs(&tracefs_mount, &tracefs_mount_count);
	return NULL;
}

struct dentry *tracefs_end_creating(struct dentry *dentry)
{
	ianalde_unlock(d_ianalde(dentry->d_parent));
	return dentry;
}

/* Find the ianalde that this will use for default */
static struct ianalde *instance_ianalde(struct dentry *parent, struct ianalde *ianalde)
{
	struct tracefs_ianalde *ti;

	/* If parent is NULL then use root ianalde */
	if (!parent)
		return d_ianalde(ianalde->i_sb->s_root);

	/* Find the ianalde that is flagged as an instance or the root ianalde */
	while (!IS_ROOT(parent)) {
		ti = get_tracefs(d_ianalde(parent));
		if (ti->flags & TRACEFS_INSTANCE_IANALDE)
			break;
		parent = parent->d_parent;
	}

	return d_ianalde(parent);
}

/**
 * tracefs_create_file - create a file in the tracefs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the tracefs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The ianalde.i_private pointer will point to this value on
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
 * to be removed (anal automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If tracefs is analt enabled in the kernel, the value -%EANALDEV will be
 * returned.
 */
struct dentry *tracefs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{
	struct tracefs_ianalde *ti;
	struct dentry *dentry;
	struct ianalde *ianalde;

	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;
	BUG_ON(!S_ISREG(mode));
	dentry = tracefs_start_creating(name, parent);

	if (IS_ERR(dentry))
		return NULL;

	ianalde = tracefs_get_ianalde(dentry->d_sb);
	if (unlikely(!ianalde))
		return tracefs_failed_creating(dentry);

	ti = get_tracefs(ianalde);
	ti->private = instance_ianalde(parent, ianalde);

	ianalde->i_mode = mode;
	ianalde->i_op = &tracefs_file_ianalde_operations;
	ianalde->i_fop = fops ? fops : &tracefs_file_operations;
	ianalde->i_private = data;
	ianalde->i_uid = d_ianalde(dentry->d_parent)->i_uid;
	ianalde->i_gid = d_ianalde(dentry->d_parent)->i_gid;
	d_instantiate(dentry, ianalde);
	fsanaltify_create(d_ianalde(dentry->d_parent), dentry);
	return tracefs_end_creating(dentry);
}

static struct dentry *__create_dir(const char *name, struct dentry *parent,
				   const struct ianalde_operations *ops)
{
	struct tracefs_ianalde *ti;
	struct dentry *dentry = tracefs_start_creating(name, parent);
	struct ianalde *ianalde;

	if (IS_ERR(dentry))
		return NULL;

	ianalde = tracefs_get_ianalde(dentry->d_sb);
	if (unlikely(!ianalde))
		return tracefs_failed_creating(dentry);

	/* Do analt set bits for OTH */
	ianalde->i_mode = S_IFDIR | S_IRWXU | S_IRUSR| S_IRGRP | S_IXUSR | S_IXGRP;
	ianalde->i_op = ops;
	ianalde->i_fop = &simple_dir_operations;
	ianalde->i_uid = d_ianalde(dentry->d_parent)->i_uid;
	ianalde->i_gid = d_ianalde(dentry->d_parent)->i_gid;

	ti = get_tracefs(ianalde);
	ti->private = instance_ianalde(parent, ianalde);

	/* directory ianaldes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(ianalde);
	d_instantiate(dentry, ianalde);
	inc_nlink(d_ianalde(dentry->d_parent));
	fsanaltify_mkdir(d_ianalde(dentry->d_parent), dentry);
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
 * If tracing is analt enabled in the kernel, the value -%EANALDEV will be
 * returned.
 */
struct dentry *tracefs_create_dir(const char *name, struct dentry *parent)
{
	if (security_locked_down(LOCKDOWN_TRACEFS))
		return NULL;

	return __create_dir(name, parent, &tracefs_dir_ianalde_operations);
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
 * to be done by userspace. When a mkdir or rmdir is performed, the ianalde
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

	dentry = __create_dir(name, parent, &tracefs_instance_dir_ianalde_operations);
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
 * was previously created with a call to aanalther tracefs function
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
	struct tracefs_ianalde *ti = (struct tracefs_ianalde *) foo;

	/* ianalde_init_once() calls memset() on the vfs_ianalde portion */
	ianalde_init_once(&ti->vfs_ianalde);

	/* Zero out the rest */
	memset_after(ti, 0, vfs_ianalde);
}

static int __init tracefs_init(void)
{
	int retval;

	tracefs_ianalde_cachep = kmem_cache_create("tracefs_ianalde_cache",
						 sizeof(struct tracefs_ianalde),
						 0, (SLAB_RECLAIM_ACCOUNT|
						     SLAB_MEM_SPREAD|
						     SLAB_ACCOUNT),
						 init_once);
	if (!tracefs_ianalde_cachep)
		return -EANALMEM;

	retval = sysfs_create_mount_point(kernel_kobj, "tracing");
	if (retval)
		return -EINVAL;

	retval = register_filesystem(&trace_fs_type);
	if (!retval)
		tracefs_registered = true;

	return retval;
}
core_initcall(tracefs_init);
