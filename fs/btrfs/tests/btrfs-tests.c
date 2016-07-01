/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/magic.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../free-space-cache.h"
#include "../free-space-tree.h"
#include "../transaction.h"
#include "../volumes.h"
#include "../disk-io.h"
#include "../qgroup.h"

static struct vfsmount *test_mnt = NULL;

static const struct super_operations btrfs_test_super_ops = {
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_test_destroy_inode,
};

static struct dentry *btrfs_test_mount(struct file_system_type *fs_type,
				       int flags, const char *dev_name,
				       void *data)
{
	return mount_pseudo(fs_type, "btrfs_test:", &btrfs_test_super_ops,
			    NULL, BTRFS_TEST_MAGIC);
}

static struct file_system_type test_type = {
	.name		= "btrfs_test_fs",
	.mount		= btrfs_test_mount,
	.kill_sb	= kill_anon_super,
};

struct inode *btrfs_new_test_inode(void)
{
	return new_inode(test_mnt->mnt_sb);
}

int btrfs_init_test_fs(void)
{
	int ret;

	ret = register_filesystem(&test_type);
	if (ret) {
		printk(KERN_ERR "btrfs: cannot register test file system\n");
		return ret;
	}

	test_mnt = kern_mount(&test_type);
	if (IS_ERR(test_mnt)) {
		printk(KERN_ERR "btrfs: cannot mount test file system\n");
		unregister_filesystem(&test_type);
		return ret;
	}
	return 0;
}

void btrfs_destroy_test_fs(void)
{
	kern_unmount(test_mnt);
	unregister_filesystem(&test_type);
}

struct btrfs_fs_info *btrfs_alloc_dummy_fs_info(void)
{
	struct btrfs_fs_info *fs_info = kzalloc(sizeof(struct btrfs_fs_info),
						GFP_KERNEL);

	if (!fs_info)
		return fs_info;
	fs_info->fs_devices = kzalloc(sizeof(struct btrfs_fs_devices),
				      GFP_KERNEL);
	if (!fs_info->fs_devices) {
		kfree(fs_info);
		return NULL;
	}
	fs_info->super_copy = kzalloc(sizeof(struct btrfs_super_block),
				      GFP_KERNEL);
	if (!fs_info->super_copy) {
		kfree(fs_info->fs_devices);
		kfree(fs_info);
		return NULL;
	}

	if (init_srcu_struct(&fs_info->subvol_srcu)) {
		kfree(fs_info->fs_devices);
		kfree(fs_info->super_copy);
		kfree(fs_info);
		return NULL;
	}

	spin_lock_init(&fs_info->buffer_lock);
	spin_lock_init(&fs_info->qgroup_lock);
	spin_lock_init(&fs_info->qgroup_op_lock);
	spin_lock_init(&fs_info->super_lock);
	spin_lock_init(&fs_info->fs_roots_radix_lock);
	spin_lock_init(&fs_info->tree_mod_seq_lock);
	mutex_init(&fs_info->qgroup_ioctl_lock);
	mutex_init(&fs_info->qgroup_rescan_lock);
	rwlock_init(&fs_info->tree_mod_log_lock);
	fs_info->running_transaction = NULL;
	fs_info->qgroup_tree = RB_ROOT;
	fs_info->qgroup_ulist = NULL;
	atomic64_set(&fs_info->tree_mod_seq, 0);
	INIT_LIST_HEAD(&fs_info->dirty_qgroups);
	INIT_LIST_HEAD(&fs_info->dead_roots);
	INIT_LIST_HEAD(&fs_info->tree_mod_seq_list);
	INIT_RADIX_TREE(&fs_info->buffer_radix, GFP_ATOMIC);
	INIT_RADIX_TREE(&fs_info->fs_roots_radix, GFP_ATOMIC);
	extent_io_tree_init(&fs_info->freed_extents[0], NULL);
	extent_io_tree_init(&fs_info->freed_extents[1], NULL);
	fs_info->pinned_extents = &fs_info->freed_extents[0];
	return fs_info;
}

static void btrfs_free_dummy_fs_info(struct btrfs_fs_info *fs_info)
{
	struct radix_tree_iter iter;
	void **slot;

	spin_lock(&fs_info->buffer_lock);
	radix_tree_for_each_slot(slot, &fs_info->buffer_radix, &iter, 0) {
		struct extent_buffer *eb;

		eb = radix_tree_deref_slot_protected(slot, &fs_info->buffer_lock);
		if (!eb)
			continue;
		/* Shouldn't happen but that kind of thinking creates CVE's */
		if (radix_tree_exception(eb)) {
			if (radix_tree_deref_retry(eb))
				slot = radix_tree_iter_retry(&iter);
			continue;
		}
		spin_unlock(&fs_info->buffer_lock);
		free_extent_buffer_stale(eb);
		spin_lock(&fs_info->buffer_lock);
	}
	spin_unlock(&fs_info->buffer_lock);

	btrfs_free_qgroup_config(fs_info);
	btrfs_free_fs_roots(fs_info);
	cleanup_srcu_struct(&fs_info->subvol_srcu);
	kfree(fs_info->super_copy);
	kfree(fs_info->fs_devices);
	kfree(fs_info);
}

void btrfs_free_dummy_root(struct btrfs_root *root)
{
	if (!root)
		return;
	if (root->node)
		free_extent_buffer(root->node);
	if (root->fs_info)
		btrfs_free_dummy_fs_info(root->fs_info);
	kfree(root);
}

struct btrfs_block_group_cache *
btrfs_alloc_dummy_block_group(unsigned long length, u32 sectorsize)
{
	struct btrfs_block_group_cache *cache;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return NULL;
	cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
					GFP_KERNEL);
	if (!cache->free_space_ctl) {
		kfree(cache);
		return NULL;
	}

	cache->key.objectid = 0;
	cache->key.offset = length;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	cache->sectorsize = sectorsize;
	cache->full_stripe_len = sectorsize;

	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);
	INIT_LIST_HEAD(&cache->bg_list);
	btrfs_init_free_space_ctl(cache);
	mutex_init(&cache->free_space_lock);

	return cache;
}

void btrfs_free_dummy_block_group(struct btrfs_block_group_cache *cache)
{
	if (!cache)
		return;
	__btrfs_remove_free_space_cache(cache->free_space_ctl);
	kfree(cache->free_space_ctl);
	kfree(cache);
}

void btrfs_init_dummy_trans(struct btrfs_trans_handle *trans)
{
	memset(trans, 0, sizeof(*trans));
	trans->transid = 1;
	INIT_LIST_HEAD(&trans->qgroup_ref_list);
	trans->type = __TRANS_DUMMY;
}
