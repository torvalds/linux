// SPDX-License-Identifier: GPL-2.0
/*
 * fs/sysfs/symlink.c - operations for initializing and mounting sysfs
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <net/net_namespace.h>

#include "sysfs.h"

static struct kernfs_root *sysfs_root;
struct kernfs_node *sysfs_root_kn;

static int sysfs_get_tree(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;
	int ret;

	ret = kernfs_get_tree(fc);
	if (ret)
		return ret;

	if (kfc->new_sb_created)
		fc->root->d_sb->s_iflags |= SB_I_USERNS_VISIBLE;
	return 0;
}

static void sysfs_fs_context_free(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;

	if (kfc->ns_tag)
		kobj_ns_drop(KOBJ_NS_TYPE_NET, kfc->ns_tag);
	kernfs_free_fs_context(fc);
	kfree(kfc);
}

static const struct fs_context_operations sysfs_fs_context_ops = {
	.free		= sysfs_fs_context_free,
	.get_tree	= sysfs_get_tree,
};

static int sysfs_init_fs_context(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc;
	struct net *netns;

	if (!(fc->sb_flags & SB_KERNMOUNT)) {
		if (!kobj_ns_current_may_mount(KOBJ_NS_TYPE_NET))
			return -EPERM;
	}

	kfc = kzalloc(sizeof(struct kernfs_fs_context), GFP_KERNEL);
	if (!kfc)
		return -ENOMEM;

	kfc->ns_tag = netns = kobj_ns_grab_current(KOBJ_NS_TYPE_NET);
	kfc->root = sysfs_root;
	kfc->magic = SYSFS_MAGIC;
	fc->fs_private = kfc;
	fc->ops = &sysfs_fs_context_ops;
	if (fc->user_ns)
		put_user_ns(fc->user_ns);
	fc->user_ns = get_user_ns(netns->user_ns);
	fc->global = true;
	return 0;
}

static void sysfs_kill_sb(struct super_block *sb)
{
	void *ns = (void *)kernfs_super_ns(sb);

	kernfs_kill_sb(sb);
	kobj_ns_drop(KOBJ_NS_TYPE_NET, ns);
}

static struct file_system_type sysfs_fs_type = {
	.name			= "sysfs",
	.init_fs_context	= sysfs_init_fs_context,
	.kill_sb		= sysfs_kill_sb,
	.fs_flags		= FS_USERNS_MOUNT,
};

int __init sysfs_init(void)
{
	int err;

	sysfs_root = kernfs_create_root(NULL, KERNFS_ROOT_EXTRA_OPEN_PERM_CHECK,
					NULL);
	if (IS_ERR(sysfs_root))
		return PTR_ERR(sysfs_root);

	sysfs_root_kn = sysfs_root->kn;

	err = register_filesystem(&sysfs_fs_type);
	if (err) {
		kernfs_destroy_root(sysfs_root);
		return err;
	}

	return 0;
}
