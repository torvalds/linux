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

#include "sysfs.h"


static struct vfsmount *sysfs_mnt;
struct kmem_cache *sysfs_dir_cachep;

static const struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= sysfs_evict_inode,
};

struct sysfs_dirent sysfs_root = {
	.s_name		= "",
	.s_count	= ATOMIC_INIT(1),
	.s_flags	= SYSFS_DIR | (KOBJ_NS_TYPE_NONE << SYSFS_NS_TYPE_SHIFT),
	.s_mode		= S_IFDIR | S_IRUGO | S_IXUGO,
	.s_ino		= 1,
};

static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SYSFS_MAGIC;
	sb->s_op = &sysfs_ops;
	sb->s_time_gran = 1;

	/* get root inode, initialize and unlock it */
	mutex_lock(&sysfs_mutex);
	inode = sysfs_get_inode(sb, &sysfs_root);
	mutex_unlock(&sysfs_mutex);
	if (!inode) {
		pr_debug("sysfs: could not get root inode\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_make_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__func__);
		return -ENOMEM;
	}
	root->d_fsdata = &sysfs_root;
	sb->s_root = root;
	return 0;
}

static int sysfs_test_super(struct super_block *sb, void *data)
{
	struct sysfs_super_info *sb_info = sysfs_info(sb);
	struct sysfs_super_info *info = data;
	enum kobj_ns_type type;
	int found = 1;

	for (type = KOBJ_NS_TYPE_NONE; type < KOBJ_NS_TYPES; type++) {
		if (sb_info->ns[type] != info->ns[type])
			found = 0;
	}
	return found;
}

static int sysfs_set_super(struct super_block *sb, void *data)
{
	int error;
	error = set_anon_super(sb, data);
	if (!error)
		sb->s_fs_info = data;
	return error;
}

static void free_sysfs_super_info(struct sysfs_super_info *info)
{
	int type;
	for (type = KOBJ_NS_TYPE_NONE; type < KOBJ_NS_TYPES; type++)
		kobj_ns_drop(type, info->ns[type]);
	kfree(info);
}

static struct dentry *sysfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	struct sysfs_super_info *info;
	enum kobj_ns_type type;
	struct super_block *sb;
	int error;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	for (type = KOBJ_NS_TYPE_NONE; type < KOBJ_NS_TYPES; type++)
		info->ns[type] = kobj_ns_grab_current(type);

	sb = sget(fs_type, sysfs_test_super, sysfs_set_super, info);
	if (IS_ERR(sb) || sb->s_fs_info != info)
		free_sysfs_super_info(info);
	if (IS_ERR(sb))
		return ERR_CAST(sb);
	if (!sb->s_root) {
		sb->s_flags = flags;
		error = sysfs_fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
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
	struct sysfs_super_info *info = sysfs_info(sb);
	/* Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing sysfs_super_info.
	 */
	kill_anon_super(sb);
	free_sysfs_super_info(info);
}

static struct file_system_type sysfs_fs_type = {
	.name		= "sysfs",
	.mount		= sysfs_mount,
	.kill_sb	= sysfs_kill_sb,
};

int __init sysfs_init(void)
{
	int err = -ENOMEM;

	sysfs_dir_cachep = kmem_cache_create("sysfs_dir_cache",
					      sizeof(struct sysfs_dirent),
					      0, 0, NULL);
	if (!sysfs_dir_cachep)
		goto out;

	err = sysfs_inode_init();
	if (err)
		goto out_err;

	err = register_filesystem(&sysfs_fs_type);
	if (!err) {
		sysfs_mnt = kern_mount(&sysfs_fs_type);
		if (IS_ERR(sysfs_mnt)) {
			printk(KERN_ERR "sysfs: could not mount!\n");
			err = PTR_ERR(sysfs_mnt);
			sysfs_mnt = NULL;
			unregister_filesystem(&sysfs_fs_type);
			goto out_err;
		}
	} else
		goto out_err;
out:
	return err;
out_err:
	kmem_cache_destroy(sysfs_dir_cachep);
	sysfs_dir_cachep = NULL;
	goto out;
}

#undef sysfs_get
struct sysfs_dirent *sysfs_get(struct sysfs_dirent *sd)
{
	return __sysfs_get(sd);
}
EXPORT_SYMBOL_GPL(sysfs_get);

#undef sysfs_put
void sysfs_put(struct sysfs_dirent *sd)
{
	__sysfs_put(sd);
}
EXPORT_SYMBOL_GPL(sysfs_put);
