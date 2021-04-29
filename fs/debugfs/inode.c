// SPDX-License-Identifier: GPL-2.0
/*
 *  inode.c - part of debugfs, a tiny little debug file system
 *
 *  Copyright (C) 2004,2019 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *  Copyright (C) 2019 Linux Foundation <gregkh@linuxfoundation.org>
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See ./Documentation/core-api/kernel-api.rst for more details.
 */

#define pr_fmt(fmt)	"debugfs: " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/fsnotify.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/security.h>

#include "internal.h"

#define DEBUGFS_DEFAULT_MODE	0700

static struct vfsmount *debugfs_mount;
static int debugfs_mount_count;
static bool debugfs_registered;
static unsigned int debugfs_allow = DEFAULT_DEBUGFS_ALLOW_BITS;

/*
 * Don't allow access attributes to be changed whilst the kernel is locked down
 * so that we can use the file mode as part of a heuristic to determine whether
 * to lock down individual files.
 */
static int debugfs_setattr(struct user_namespace *mnt_userns,
			   struct dentry *dentry, struct iattr *ia)
{
	int ret = security_locked_down(LOCKDOWN_DEBUGFS);

	if (ret && (ia->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID)))
		return ret;
	return simple_setattr(&init_user_ns, dentry, ia);
}

static const struct inode_operations debugfs_file_inode_operations = {
	.setattr	= debugfs_setattr,
};
static const struct inode_operations debugfs_dir_inode_operations = {
	.lookup		= simple_lookup,
	.setattr	= debugfs_setattr,
};
static const struct inode_operations debugfs_symlink_inode_operations = {
	.get_link	= simple_get_link,
	.setattr	= debugfs_setattr,
};

static struct inode *debugfs_get_inode(struct super_block *sb)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_atime = inode->i_mtime =
			inode->i_ctime = current_time(inode);
	}
	return inode;
}

struct debugfs_mount_opts {
	kuid_t uid;
	kgid_t gid;
	umode_t mode;
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

struct debugfs_fs_info {
	struct debugfs_mount_opts mount_opts;
};

static int debugfs_parse_options(char *data, struct debugfs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	kuid_t uid;
	kgid_t gid;
	char *p;

	opts->mode = DEBUGFS_DEFAULT_MODE;

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
		 * but traditionally debugfs has ignored all mount options
		 */
		}
	}

	return 0;
}

static int debugfs_apply_options(struct super_block *sb)
{
	struct debugfs_fs_info *fsi = sb->s_fs_info;
	struct inode *inode = d_inode(sb->s_root);
	struct debugfs_mount_opts *opts = &fsi->mount_opts;

	inode->i_mode &= ~S_IALLUGO;
	inode->i_mode |= opts->mode;

	inode->i_uid = opts->uid;
	inode->i_gid = opts->gid;

	return 0;
}

static int debugfs_remount(struct super_block *sb, int *flags, char *data)
{
	int err;
	struct debugfs_fs_info *fsi = sb->s_fs_info;

	sync_filesystem(sb);
	err = debugfs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	debugfs_apply_options(sb);

fail:
	return err;
}

static int debugfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct debugfs_fs_info *fsi = root->d_sb->s_fs_info;
	struct debugfs_mount_opts *opts = &fsi->mount_opts;

	if (!uid_eq(opts->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, opts->uid));
	if (!gid_eq(opts->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, opts->gid));
	if (opts->mode != DEBUGFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", opts->mode);

	return 0;
}

static void debugfs_free_inode(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	free_inode_nonrcu(inode);
}

static const struct super_operations debugfs_super_operations = {
	.statfs		= simple_statfs,
	.remount_fs	= debugfs_remount,
	.show_options	= debugfs_show_options,
	.free_inode	= debugfs_free_inode,
};

static void debugfs_release_dentry(struct dentry *dentry)
{
	void *fsd = dentry->d_fsdata;

	if (!((unsigned long)fsd & DEBUGFS_FSDATA_IS_REAL_FOPS_BIT))
		kfree(dentry->d_fsdata);
}

static struct vfsmount *debugfs_automount(struct path *path)
{
	debugfs_automount_t f;
	f = (debugfs_automount_t)path->dentry->d_fsdata;
	return f(path->dentry, d_inode(path->dentry)->i_private);
}

static const struct dentry_operations debugfs_dops = {
	.d_delete = always_delete_dentry,
	.d_release = debugfs_release_dentry,
	.d_automount = debugfs_automount,
};

static int debug_fill_super(struct super_block *sb, void *data, int silent)
{
	static const struct tree_descr debug_files[] = {{""}};
	struct debugfs_fs_info *fsi;
	int err;

	fsi = kzalloc(sizeof(struct debugfs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi) {
		err = -ENOMEM;
		goto fail;
	}

	err = debugfs_parse_options(data, &fsi->mount_opts);
	if (err)
		goto fail;

	err  =  simple_fill_super(sb, DEBUGFS_MAGIC, debug_files);
	if (err)
		goto fail;

	sb->s_op = &debugfs_super_operations;
	sb->s_d_op = &debugfs_dops;

	debugfs_apply_options(sb);

	return 0;

fail:
	kfree(fsi);
	sb->s_fs_info = NULL;
	return err;
}

static struct dentry *debug_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	if (!(debugfs_allow & DEBUGFS_ALLOW_API))
		return ERR_PTR(-EPERM);

	return mount_single(fs_type, flags, data, debug_fill_super);
}

static struct file_system_type debug_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"debugfs",
	.mount =	debug_mount,
	.kill_sb =	kill_litter_super,
};
MODULE_ALIAS_FS("debugfs");

/**
 * debugfs_lookup() - look up an existing debugfs file
 * @name: a pointer to a string containing the name of the file to look up.
 * @parent: a pointer to the parent dentry of the file.
 *
 * This function will return a pointer to a dentry if it succeeds.  If the file
 * doesn't exist or an error occurs, %NULL will be returned.  The returned
 * dentry must be passed to dput() when it is no longer needed.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_lookup(const char *name, struct dentry *parent)
{
	struct dentry *dentry;

	if (!debugfs_initialized() || IS_ERR_OR_NULL(name) || IS_ERR(parent))
		return NULL;

	if (!parent)
		parent = debugfs_mount->mnt_root;

	dentry = lookup_positive_unlocked(name, parent, strlen(name));
	if (IS_ERR(dentry))
		return NULL;
	return dentry;
}
EXPORT_SYMBOL_GPL(debugfs_lookup);

static struct dentry *start_creating(const char *name, struct dentry *parent)
{
	struct dentry *dentry;
	int error;

	if (!(debugfs_allow & DEBUGFS_ALLOW_API))
		return ERR_PTR(-EPERM);

	if (!debugfs_initialized())
		return ERR_PTR(-ENOENT);

	pr_debug("creating file '%s'\n", name);

	if (IS_ERR(parent))
		return parent;

	error = simple_pin_fs(&debug_fs_type, &debugfs_mount,
			      &debugfs_mount_count);
	if (error) {
		pr_err("Unable to pin filesystem for file '%s'\n", name);
		return ERR_PTR(error);
	}

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = debugfs_mount->mnt_root;

	inode_lock(d_inode(parent));
	if (unlikely(IS_DEADDIR(d_inode(parent))))
		dentry = ERR_PTR(-ENOENT);
	else
		dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(dentry) && d_really_is_positive(dentry)) {
		if (d_is_dir(dentry))
			pr_err("Directory '%s' with parent '%s' already present!\n",
			       name, parent->d_name.name);
		else
			pr_err("File '%s' in directory '%s' already present!\n",
			       name, parent->d_name.name);
		dput(dentry);
		dentry = ERR_PTR(-EEXIST);
	}

	if (IS_ERR(dentry)) {
		inode_unlock(d_inode(parent));
		simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	}

	return dentry;
}

static struct dentry *failed_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	dput(dentry);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	return ERR_PTR(-ENOMEM);
}

static struct dentry *end_creating(struct dentry *dentry)
{
	inode_unlock(d_inode(dentry->d_parent));
	return dentry;
}

static struct dentry *__debugfs_create_file(const char *name, umode_t mode,
				struct dentry *parent, void *data,
				const struct file_operations *proxy_fops,
				const struct file_operations *real_fops)
{
	struct dentry *dentry;
	struct inode *inode;

	if (!(mode & S_IFMT))
		mode |= S_IFREG;
	BUG_ON(!S_ISREG(mode));
	dentry = start_creating(name, parent);

	if (IS_ERR(dentry))
		return dentry;

	if (!(debugfs_allow & DEBUGFS_ALLOW_API)) {
		failed_creating(dentry);
		return ERR_PTR(-EPERM);
	}

	inode = debugfs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		pr_err("out of free dentries, can not create file '%s'\n",
		       name);
		return failed_creating(dentry);
	}

	inode->i_mode = mode;
	inode->i_private = data;

	inode->i_op = &debugfs_file_inode_operations;
	inode->i_fop = proxy_fops;
	dentry->d_fsdata = (void *)((unsigned long)real_fops |
				DEBUGFS_FSDATA_IS_REAL_FOPS_BIT);

	d_instantiate(dentry, inode);
	fsnotify_create(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}

/**
 * debugfs_create_file - create a file in the debugfs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * This is the basic "create a file" function for debugfs.  It allows for a
 * wide range of flexibility in creating a file, or a directory (if you want
 * to create a directory, the debugfs_create_dir() function is
 * recommended to be used instead.)
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, ERR_PTR(-ERROR) will be
 * returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_file(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{

	return __debugfs_create_file(name, mode, parent, data,
				fops ? &debugfs_full_proxy_file_operations :
					&debugfs_noop_file_operations,
				fops);
}
EXPORT_SYMBOL_GPL(debugfs_create_file);

/**
 * debugfs_create_file_unsafe - create a file in the debugfs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 *
 * debugfs_create_file_unsafe() is completely analogous to
 * debugfs_create_file(), the only difference being that the fops
 * handed it will not get protected against file removals by the
 * debugfs core.
 *
 * It is your responsibility to protect your struct file_operation
 * methods against file removals by means of debugfs_file_get()
 * and debugfs_file_put(). ->open() is still protected by
 * debugfs though.
 *
 * Any struct file_operations defined by means of
 * DEFINE_DEBUGFS_ATTRIBUTE() is protected against file removals and
 * thus, may be used here.
 */
struct dentry *debugfs_create_file_unsafe(const char *name, umode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{

	return __debugfs_create_file(name, mode, parent, data,
				fops ? &debugfs_open_proxy_file_operations :
					&debugfs_noop_file_operations,
				fops);
}
EXPORT_SYMBOL_GPL(debugfs_create_file_unsafe);

/**
 * debugfs_create_file_size - create a file in the debugfs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @data: a pointer to something that the caller will want to get to later
 *        on.  The inode.i_private pointer will point to this value on
 *        the open() call.
 * @fops: a pointer to a struct file_operations that should be used for
 *        this file.
 * @file_size: initial file size
 *
 * This is the basic "create a file" function for debugfs.  It allows for a
 * wide range of flexibility in creating a file, or a directory (if you want
 * to create a directory, the debugfs_create_dir() function is
 * recommended to be used instead.)
 */
void debugfs_create_file_size(const char *name, umode_t mode,
			      struct dentry *parent, void *data,
			      const struct file_operations *fops,
			      loff_t file_size)
{
	struct dentry *de = debugfs_create_file(name, mode, parent, data, fops);

	if (de)
		d_inode(de)->i_size = file_size;
}
EXPORT_SYMBOL_GPL(debugfs_create_file_size);

/**
 * debugfs_create_dir - create a directory in the debugfs filesystem
 * @name: a pointer to a string containing the name of the directory to
 *        create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          directory will be created in the root of the debugfs filesystem.
 *
 * This function creates a directory in debugfs with the given name.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, ERR_PTR(-ERROR) will be
 * returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_dir(const char *name, struct dentry *parent)
{
	struct dentry *dentry = start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return dentry;

	if (!(debugfs_allow & DEBUGFS_ALLOW_API)) {
		failed_creating(dentry);
		return ERR_PTR(-EPERM);
	}

	inode = debugfs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		pr_err("out of free dentries, can not create directory '%s'\n",
		       name);
		return failed_creating(dentry);
	}

	inode->i_mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	inode->i_op = &debugfs_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;

	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}
EXPORT_SYMBOL_GPL(debugfs_create_dir);

/**
 * debugfs_create_automount - create automount point in the debugfs filesystem
 * @name: a pointer to a string containing the name of the file to create.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @f: function to be called when pathname resolution steps on that one.
 * @data: opaque argument to pass to f().
 *
 * @f should return what ->d_automount() would.
 */
struct dentry *debugfs_create_automount(const char *name,
					struct dentry *parent,
					debugfs_automount_t f,
					void *data)
{
	struct dentry *dentry = start_creating(name, parent);
	struct inode *inode;

	if (IS_ERR(dentry))
		return dentry;

	if (!(debugfs_allow & DEBUGFS_ALLOW_API)) {
		failed_creating(dentry);
		return ERR_PTR(-EPERM);
	}

	inode = debugfs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		pr_err("out of free dentries, can not create automount '%s'\n",
		       name);
		return failed_creating(dentry);
	}

	make_empty_dir_inode(inode);
	inode->i_flags |= S_AUTOMOUNT;
	inode->i_private = data;
	dentry->d_fsdata = (void *)f;
	/* directory inodes start off with i_nlink == 2 (for "." entry) */
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	inc_nlink(d_inode(dentry->d_parent));
	fsnotify_mkdir(d_inode(dentry->d_parent), dentry);
	return end_creating(dentry);
}
EXPORT_SYMBOL(debugfs_create_automount);

/**
 * debugfs_create_symlink- create a symbolic link in the debugfs filesystem
 * @name: a pointer to a string containing the name of the symbolic link to
 *        create.
 * @parent: a pointer to the parent dentry for this symbolic link.  This
 *          should be a directory dentry if set.  If this parameter is NULL,
 *          then the symbolic link will be created in the root of the debugfs
 *          filesystem.
 * @target: a pointer to a string containing the path to the target of the
 *          symbolic link.
 *
 * This function creates a symbolic link with the given name in debugfs that
 * links to the given target path.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the symbolic
 * link is to be removed (no automatic cleanup happens if your module is
 * unloaded, you are responsible here.)  If an error occurs, ERR_PTR(-ERROR)
 * will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_create_symlink(const char *name, struct dentry *parent,
				      const char *target)
{
	struct dentry *dentry;
	struct inode *inode;
	char *link = kstrdup(target, GFP_KERNEL);
	if (!link)
		return ERR_PTR(-ENOMEM);

	dentry = start_creating(name, parent);
	if (IS_ERR(dentry)) {
		kfree(link);
		return dentry;
	}

	inode = debugfs_get_inode(dentry->d_sb);
	if (unlikely(!inode)) {
		pr_err("out of free dentries, can not create symlink '%s'\n",
		       name);
		kfree(link);
		return failed_creating(dentry);
	}
	inode->i_mode = S_IFLNK | S_IRWXUGO;
	inode->i_op = &debugfs_symlink_inode_operations;
	inode->i_link = link;
	d_instantiate(dentry, inode);
	return end_creating(dentry);
}
EXPORT_SYMBOL_GPL(debugfs_create_symlink);

static void __debugfs_file_removed(struct dentry *dentry)
{
	struct debugfs_fsdata *fsd;

	/*
	 * Paired with the closing smp_mb() implied by a successful
	 * cmpxchg() in debugfs_file_get(): either
	 * debugfs_file_get() must see a dead dentry or we must see a
	 * debugfs_fsdata instance at ->d_fsdata here (or both).
	 */
	smp_mb();
	fsd = READ_ONCE(dentry->d_fsdata);
	if ((unsigned long)fsd & DEBUGFS_FSDATA_IS_REAL_FOPS_BIT)
		return;
	if (!refcount_dec_and_test(&fsd->active_users))
		wait_for_completion(&fsd->active_users_drained);
}

static void remove_one(struct dentry *victim)
{
        if (d_is_reg(victim))
		__debugfs_file_removed(victim);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}

/**
 * debugfs_remove - recursively removes a directory
 * @dentry: a pointer to a the dentry of the directory to be removed.  If this
 *          parameter is NULL or an error value, nothing will be done.
 *
 * This function recursively removes a directory tree in debugfs that
 * was previously created with a call to another debugfs function
 * (like debugfs_create_file() or variants thereof.)
 *
 * This function is required to be called in order for the file to be
 * removed, no automatic cleanup of files will happen when a module is
 * removed, you are responsible here.
 */
void debugfs_remove(struct dentry *dentry)
{
	if (IS_ERR_OR_NULL(dentry))
		return;

	simple_pin_fs(&debug_fs_type, &debugfs_mount, &debugfs_mount_count);
	simple_recursive_removal(dentry, remove_one);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}
EXPORT_SYMBOL_GPL(debugfs_remove);

/**
 * debugfs_rename - rename a file/directory in the debugfs filesystem
 * @old_dir: a pointer to the parent dentry for the renamed object. This
 *          should be a directory dentry.
 * @old_dentry: dentry of an object to be renamed.
 * @new_dir: a pointer to the parent dentry where the object should be
 *          moved. This should be a directory dentry.
 * @new_name: a pointer to a string containing the target name.
 *
 * This function renames a file/directory in debugfs.  The target must not
 * exist for rename to succeed.
 *
 * This function will return a pointer to old_dentry (which is updated to
 * reflect renaming) if it succeeds. If an error occurs, %NULL will be
 * returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.
 */
struct dentry *debugfs_rename(struct dentry *old_dir, struct dentry *old_dentry,
		struct dentry *new_dir, const char *new_name)
{
	int error;
	struct dentry *dentry = NULL, *trap;
	struct name_snapshot old_name;

	if (IS_ERR(old_dir))
		return old_dir;
	if (IS_ERR(new_dir))
		return new_dir;
	if (IS_ERR_OR_NULL(old_dentry))
		return old_dentry;

	trap = lock_rename(new_dir, old_dir);
	/* Source or destination directories don't exist? */
	if (d_really_is_negative(old_dir) || d_really_is_negative(new_dir))
		goto exit;
	/* Source does not exist, cyclic rename, or mountpoint? */
	if (d_really_is_negative(old_dentry) || old_dentry == trap ||
	    d_mountpoint(old_dentry))
		goto exit;
	dentry = lookup_one_len(new_name, new_dir, strlen(new_name));
	/* Lookup failed, cyclic rename or target exists? */
	if (IS_ERR(dentry) || dentry == trap || d_really_is_positive(dentry))
		goto exit;

	take_dentry_name_snapshot(&old_name, old_dentry);

	error = simple_rename(&init_user_ns, d_inode(old_dir), old_dentry,
			      d_inode(new_dir), dentry, 0);
	if (error) {
		release_dentry_name_snapshot(&old_name);
		goto exit;
	}
	d_move(old_dentry, dentry);
	fsnotify_move(d_inode(old_dir), d_inode(new_dir), &old_name.name,
		d_is_dir(old_dentry),
		NULL, old_dentry);
	release_dentry_name_snapshot(&old_name);
	unlock_rename(new_dir, old_dir);
	dput(dentry);
	return old_dentry;
exit:
	if (dentry && !IS_ERR(dentry))
		dput(dentry);
	unlock_rename(new_dir, old_dir);
	if (IS_ERR(dentry))
		return dentry;
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(debugfs_rename);

/**
 * debugfs_initialized - Tells whether debugfs has been registered
 */
bool debugfs_initialized(void)
{
	return debugfs_registered;
}
EXPORT_SYMBOL_GPL(debugfs_initialized);

static int __init debugfs_kernel(char *str)
{
	if (str) {
		if (!strcmp(str, "on"))
			debugfs_allow = DEBUGFS_ALLOW_API | DEBUGFS_ALLOW_MOUNT;
		else if (!strcmp(str, "no-mount"))
			debugfs_allow = DEBUGFS_ALLOW_API;
		else if (!strcmp(str, "off"))
			debugfs_allow = 0;
	}

	return 0;
}
early_param("debugfs", debugfs_kernel);
static int __init debugfs_init(void)
{
	int retval;

	if (!(debugfs_allow & DEBUGFS_ALLOW_MOUNT))
		return -EPERM;

	retval = sysfs_create_mount_point(kernel_kobj, "debug");
	if (retval)
		return retval;

	retval = register_filesystem(&debug_fs_type);
	if (retval)
		sysfs_remove_mount_point(kernel_kobj, "debug");
	else
		debugfs_registered = true;

	return retval;
}
core_initcall(debugfs_init);
