// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * mount.c - operations for initializing and mounting configfs.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

/* Random magic number */
#define CONFIGFS_MAGIC 0x62656570

static struct vfsmount *configfs_mount = NULL;
struct kmem_cache *configfs_dir_cachep;
static int configfs_mnt_count = 0;


static void configfs_free_inode(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	free_inode_nonrcu(inode);
}

static const struct super_operations configfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.free_inode	= configfs_free_inode,
};

static struct config_group configfs_root_group = {
	.cg_item = {
		.ci_namebuf	= "root",
		.ci_name	= configfs_root_group.cg_item.ci_namebuf,
	},
};

int configfs_is_root(struct config_item *item)
{
	return item == &configfs_root_group.cg_item;
}

static struct configfs_dirent configfs_root = {
	.s_sibling	= LIST_HEAD_INIT(configfs_root.s_sibling),
	.s_children	= LIST_HEAD_INIT(configfs_root.s_children),
	.s_element	= &configfs_root_group.cg_item,
	.s_type		= CONFIGFS_ROOT,
	.s_iattr	= NULL,
};

static int configfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;
	struct dentry *root;

	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = CONFIGFS_MAGIC;
	sb->s_op = &configfs_ops;
	sb->s_time_gran = 1;

	inode = configfs_new_inode(S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
				   &configfs_root, sb);
	if (inode) {
		inode->i_op = &configfs_root_inode_operations;
		inode->i_fop = &configfs_dir_operations;
		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inc_nlink(inode);
	} else {
		pr_debug("could not get root inode\n");
		return -ENOMEM;
	}

	root = d_make_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__func__);
		return -ENOMEM;
	}
	config_group_init(&configfs_root_group);
	configfs_root_group.cg_item.ci_dentry = root;
	root->d_fsdata = &configfs_root;
	sb->s_root = root;
	sb->s_d_op = &configfs_dentry_ops; /* the rest get that */
	return 0;
}

static int configfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, configfs_fill_super);
}

static const struct fs_context_operations configfs_context_ops = {
	.get_tree	= configfs_get_tree,
};

static int configfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &configfs_context_ops;
	return 0;
}

static struct file_system_type configfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "configfs",
	.init_fs_context = configfs_init_fs_context,
	.kill_sb	= kill_litter_super,
};
MODULE_ALIAS_FS("configfs");

struct dentry *configfs_pin_fs(void)
{
	int err = simple_pin_fs(&configfs_fs_type, &configfs_mount,
			     &configfs_mnt_count);
	return err ? ERR_PTR(err) : configfs_mount->mnt_root;
}

void configfs_release_fs(void)
{
	simple_release_fs(&configfs_mount, &configfs_mnt_count);
}


static int __init configfs_init(void)
{
	int err = -ENOMEM;

	configfs_dir_cachep = kmem_cache_create("configfs_dir_cache",
						sizeof(struct configfs_dirent),
						0, 0, NULL);
	if (!configfs_dir_cachep)
		goto out;

	err = sysfs_create_mount_point(kernel_kobj, "config");
	if (err)
		goto out2;

	err = register_filesystem(&configfs_fs_type);
	if (err)
		goto out3;

	return 0;
out3:
	pr_err("Unable to register filesystem!\n");
	sysfs_remove_mount_point(kernel_kobj, "config");
out2:
	kmem_cache_destroy(configfs_dir_cachep);
	configfs_dir_cachep = NULL;
out:
	return err;
}

static void __exit configfs_exit(void)
{
	unregister_filesystem(&configfs_fs_type);
	sysfs_remove_mount_point(kernel_kobj, "config");
	kmem_cache_destroy(configfs_dir_cachep);
	configfs_dir_cachep = NULL;
}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.2");
MODULE_DESCRIPTION("Simple RAM filesystem for user driven kernel subsystem configuration.");

core_initcall(configfs_init);
module_exit(configfs_exit);
