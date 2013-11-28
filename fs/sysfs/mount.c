/*
 * fs/sysfs/symlink.c - operations for initializing and mounting sysfs
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#define DEBUG

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>

#include "sysfs.h"


struct kmem_cache *sysfs_dir_cachep;

static const struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= sysfs_evict_inode,
};

static struct kernfs_root *sysfs_root;
struct sysfs_dirent *sysfs_root_sd;

static int sysfs_fill_super(struct super_block *sb)
{
	struct sysfs_super_info *info = sysfs_info(sb);
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SYSFS_MAGIC;
	sb->s_op = &sysfs_ops;
	sb->s_time_gran = 1;

	/* get root inode, initialize and unlock it */
	mutex_lock(&sysfs_mutex);
	inode = sysfs_get_inode(sb, info->root->sd);
	mutex_unlock(&sysfs_mutex);
	if (!inode) {
		pr_debug("sysfs: could not get root inode\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_make_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n", __func__);
		return -ENOMEM;
	}
	kernfs_get(info->root->sd);
	root->d_fsdata = info->root->sd;
	sb->s_root = root;
	sb->s_d_op = &sysfs_dentry_ops;
	return 0;
}

static int sysfs_test_super(struct super_block *sb, void *data)
{
	struct sysfs_super_info *sb_info = sysfs_info(sb);
	struct sysfs_super_info *info = data;

	return sb_info->root == info->root && sb_info->ns == info->ns;
}

static int sysfs_set_super(struct super_block *sb, void *data)
{
	int error;
	error = set_anon_super(sb, data);
	if (!error)
		sb->s_fs_info = data;
	return error;
}

/**
 * kernfs_super_ns - determine the namespace tag of a kernfs super_block
 * @sb: super_block of interest
 *
 * Return the namespace tag associated with kernfs super_block @sb.
 */
const void *kernfs_super_ns(struct super_block *sb)
{
	struct sysfs_super_info *info = sysfs_info(sb);

	return info->ns;
}

static struct dentry *sysfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	struct dentry *root;
	void *ns;

	if (!(flags & MS_KERNMOUNT)) {
		if (!capable(CAP_SYS_ADMIN) && !fs_fully_visible(fs_type))
			return ERR_PTR(-EPERM);

		if (!kobj_ns_current_may_mount(KOBJ_NS_TYPE_NET))
			return ERR_PTR(-EPERM);
	}

	ns = kobj_ns_grab_current(KOBJ_NS_TYPE_NET);
	root = kernfs_mount_ns(fs_type, flags, sysfs_root, ns);
	if (IS_ERR(root))
		kobj_ns_drop(KOBJ_NS_TYPE_NET, ns);
	return root;
}

/**
 * kernfs_mount_ns - kernfs mount helper
 * @fs_type: file_system_type of the fs being mounted
 * @flags: mount flags specified for the mount
 * @root: kernfs_root of the hierarchy being mounted
 * @ns: optional namespace tag of the mount
 *
 * This is to be called from each kernfs user's file_system_type->mount()
 * implementation, which should pass through the specified @fs_type and
 * @flags, and specify the hierarchy and namespace tag to mount via @root
 * and @ns, respectively.
 *
 * The return value can be passed to the vfs layer verbatim.
 */
struct dentry *kernfs_mount_ns(struct file_system_type *fs_type, int flags,
			       struct kernfs_root *root, const void *ns)
{
	struct super_block *sb;
	struct sysfs_super_info *info;
	int error;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->root = root;
	info->ns = ns;

	sb = sget(fs_type, sysfs_test_super, sysfs_set_super, flags, info);
	if (IS_ERR(sb) || sb->s_fs_info != info)
		kfree(info);
	if (IS_ERR(sb))
		return ERR_CAST(sb);
	if (!sb->s_root) {
		error = sysfs_fill_super(sb);
		if (error) {
			deactivate_locked_super(sb);
			return ERR_PTR(error);
		}
		sb->s_flags |= MS_ACTIVE;
	}

	return dget(sb->s_root);
}

static void sysfs_kill_sb(struct super_block *sb)
{
	kernfs_kill_sb(sb);
	kobj_ns_drop(KOBJ_NS_TYPE_NET, (void *)kernfs_super_ns(sb));
}

/**
 * kernfs_kill_sb - kill_sb for kernfs
 * @sb: super_block being killed
 *
 * This can be used directly for file_system_type->kill_sb().  If a kernfs
 * user needs extra cleanup, it can implement its own kill_sb() and call
 * this function at the end.
 */
void kernfs_kill_sb(struct super_block *sb)
{
	struct sysfs_super_info *info = sysfs_info(sb);
	struct sysfs_dirent *root_sd = sb->s_root->d_fsdata;

	/*
	 * Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing sysfs_super_info.
	 */
	kill_anon_super(sb);
	kfree(info);
	kernfs_put(root_sd);
}

static struct file_system_type sysfs_fs_type = {
	.name		= "sysfs",
	.mount		= sysfs_mount,
	.kill_sb	= sysfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

void __init kernfs_init(void)
{
	sysfs_dir_cachep = kmem_cache_create("sysfs_dir_cache",
					      sizeof(struct sysfs_dirent),
					      0, SLAB_PANIC, NULL);
	sysfs_inode_init();
}

int __init sysfs_init(void)
{
	int err;

	sysfs_root = kernfs_create_root(NULL);
	if (IS_ERR(sysfs_root))
		return PTR_ERR(sysfs_root);

	sysfs_root_sd = sysfs_root->sd;

	err = register_filesystem(&sysfs_fs_type);
	if (err) {
		kernfs_destroy_root(sysfs_root);
		return err;
	}

	return 0;
}
