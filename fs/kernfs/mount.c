/*
 * fs/kernfs/mount.c - kernfs mount implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include "kernfs-internal.h"

struct kmem_cache *kernfs_node_cache;

static int kernfs_sop_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct kernfs_root *root = kernfs_info(sb)->root;
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->remount_fs)
		return scops->remount_fs(root, flags, data);
	return 0;
}

static int kernfs_sop_show_options(struct seq_file *sf, struct dentry *dentry)
{
	struct kernfs_root *root = kernfs_root(dentry->d_fsdata);
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->show_options)
		return scops->show_options(sf, root);
	return 0;
}

const struct super_operations kernfs_sops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= kernfs_evict_inode,

	.remount_fs	= kernfs_sop_remount_fs,
	.show_options	= kernfs_sop_show_options,
};

/**
 * kernfs_root_from_sb - determine kernfs_root associated with a super_block
 * @sb: the super_block in question
 *
 * Return the kernfs_root associated with @sb.  If @sb is not a kernfs one,
 * %NULL is returned.
 */
struct kernfs_root *kernfs_root_from_sb(struct super_block *sb)
{
	if (sb->s_op == &kernfs_sops)
		return kernfs_info(sb)->root;
	return NULL;
}

static int kernfs_fill_super(struct super_block *sb, unsigned long magic)
{
	struct kernfs_super_info *info = kernfs_info(sb);
	struct inode *inode;
	struct dentry *root;

	info->sb = sb;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = magic;
	sb->s_op = &kernfs_sops;
	sb->s_time_gran = 1;

	/* get root inode, initialize and unlock it */
	mutex_lock(&kernfs_mutex);
	inode = kernfs_get_inode(sb, info->root->kn);
	mutex_unlock(&kernfs_mutex);
	if (!inode) {
		pr_debug("kernfs: could not get root inode\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_make_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n", __func__);
		return -ENOMEM;
	}
	kernfs_get(info->root->kn);
	root->d_fsdata = info->root->kn;
	sb->s_root = root;
	sb->s_d_op = &kernfs_dops;
	return 0;
}

static int kernfs_test_super(struct super_block *sb, void *data)
{
	struct kernfs_super_info *sb_info = kernfs_info(sb);
	struct kernfs_super_info *info = data;

	return sb_info->root == info->root && sb_info->ns == info->ns;
}

static int kernfs_set_super(struct super_block *sb, void *data)
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
	struct kernfs_super_info *info = kernfs_info(sb);

	return info->ns;
}

/**
 * kernfs_mount_ns - kernfs mount helper
 * @fs_type: file_system_type of the fs being mounted
 * @flags: mount flags specified for the mount
 * @root: kernfs_root of the hierarchy being mounted
 * @magic: file system specific magic number
 * @new_sb_created: tell the caller if we allocated a new superblock
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
				struct kernfs_root *root, unsigned long magic,
				bool *new_sb_created, const void *ns)
{
	struct super_block *sb;
	struct kernfs_super_info *info;
	int error;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->root = root;
	info->ns = ns;

	sb = sget(fs_type, kernfs_test_super, kernfs_set_super, flags, info);
	if (IS_ERR(sb) || sb->s_fs_info != info)
		kfree(info);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (new_sb_created)
		*new_sb_created = !sb->s_root;

	if (!sb->s_root) {
		struct kernfs_super_info *info = kernfs_info(sb);

		error = kernfs_fill_super(sb, magic);
		if (error) {
			deactivate_locked_super(sb);
			return ERR_PTR(error);
		}
		sb->s_flags |= MS_ACTIVE;

		mutex_lock(&kernfs_mutex);
		list_add(&info->node, &root->supers);
		mutex_unlock(&kernfs_mutex);
	}

	return dget(sb->s_root);
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
	struct kernfs_super_info *info = kernfs_info(sb);
	struct kernfs_node *root_kn = sb->s_root->d_fsdata;

	mutex_lock(&kernfs_mutex);
	list_del(&info->node);
	mutex_unlock(&kernfs_mutex);

	/*
	 * Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing kernfs_super_info.
	 */
	kill_anon_super(sb);
	kfree(info);
	kernfs_put(root_kn);
}

void __init kernfs_init(void)
{
	kernfs_node_cache = kmem_cache_create("kernfs_node_cache",
					      sizeof(struct kernfs_node),
					      0, SLAB_PANIC, NULL);
	kernfs_inode_init();
}
