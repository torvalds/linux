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


static struct vfsmount *sysfs_mount;
struct kmem_cache *sysfs_dir_cachep;

static const struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.delete_inode	= sysfs_delete_inode,
};

struct sysfs_dirent sysfs_root = {
	.s_name		= "",
	.s_count	= ATOMIC_INIT(1),
	.s_flags	= SYSFS_DIR | (KOBJ_NS_TYPE_NONE << SYSFS_NS_TYPE_SHIFT),
	.s_mode		= S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
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
	root = d_alloc_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__func__);
		iput(inode);
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

static int sysfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct sysfs_super_info *info;
	enum kobj_ns_type type;
	struct super_block *sb;
	int error;

	error = -ENOMEM;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		goto out;

	for (type = KOBJ_NS_TYPE_NONE; type < KOBJ_NS_TYPES; type++)
		info->ns[type] = kobj_ns_current(type);

	sb = sget(fs_type, sysfs_test_super, sysfs_set_super, info);
	if (IS_ERR(sb) || sb->s_fs_info != info)
		kfree(info);
	if (IS_ERR(sb)) {
		error = PTR_ERR(sb);
		goto out;
	}
	if (!sb->s_root) {
		sb->s_flags = flags;
		error = sysfs_fill_super(sb, data, flags & MS_SILENT ? 1 : 0);
		if (error) {
			deactivate_locked_super(sb);
			goto out;
		}
		sb->s_flags |= MS_ACTIVE;
	}

	simple_set_mnt(mnt, sb);
	error = 0;
out:
	return error;
}

static void sysfs_kill_sb(struct super_block *sb)
{
	struct sysfs_super_info *info = sysfs_info(sb);

	/* Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing sysfs_super_info.
	 */
	kill_anon_super(sb);
	kfree(info);
}

static struct file_system_type sysfs_fs_type = {
	.name		= "sysfs",
	.get_sb		= sysfs_get_sb,
	.kill_sb	= sysfs_kill_sb,
};

void sysfs_exit_ns(enum kobj_ns_type type, const void *ns)
{
	struct super_block *sb;

	mutex_lock(&sysfs_mutex);
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &sysfs_fs_type.fs_supers, s_instances) {
		struct sysfs_super_info *info = sysfs_info(sb);
		/*
		 * If we see a superblock on the fs_supers/s_instances
		 * list the unmount has not completed and sb->s_fs_info
		 * points to a valid struct sysfs_super_info.
		 */
		/* Ignore superblocks with the wrong ns */
		if (info->ns[type] != ns)
			continue;
		info->ns[type] = NULL;
	}
	spin_unlock(&sb_lock);
	mutex_unlock(&sysfs_mutex);
}

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
		sysfs_mount = kern_mount(&sysfs_fs_type);
		if (IS_ERR(sysfs_mount)) {
			printk(KERN_ERR "sysfs: could not mount!\n");
			err = PTR_ERR(sysfs_mount);
			sysfs_mount = NULL;
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
