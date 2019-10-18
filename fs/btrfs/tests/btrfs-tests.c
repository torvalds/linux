// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/magic.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../free-space-cache.h"
#include "../free-space-tree.h"
#include "../transaction.h"
#include "../volumes.h"
#include "../disk-io.h"
#include "../qgroup.h"
#include "../block-group.h"

static struct vfsmount *test_mnt = NULL;

const char *test_error[] = {
	[TEST_ALLOC_FS_INFO]	     = "cannot allocate fs_info",
	[TEST_ALLOC_ROOT]	     = "cannot allocate root",
	[TEST_ALLOC_EXTENT_BUFFER]   = "cannot extent buffer",
	[TEST_ALLOC_PATH]	     = "cannot allocate path",
	[TEST_ALLOC_INODE]	     = "cannot allocate inode",
	[TEST_ALLOC_BLOCK_GROUP]     = "cannot allocate block group",
	[TEST_ALLOC_EXTENT_MAP]      = "cannot allocate extent map",
};

static const struct super_operations btrfs_test_super_ops = {
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_test_destroy_inode,
};


static int btrfs_test_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, BTRFS_TEST_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->ops = &btrfs_test_super_ops;
	return 0;
}

static struct file_system_type test_type = {
	.name		= "btrfs_test_fs",
	.init_fs_context = btrfs_test_init_fs_context,
	.kill_sb	= kill_anon_super,
};

struct inode *btrfs_new_test_inode(void)
{
	struct inode *inode;

	inode = new_inode(test_mnt->mnt_sb);
	if (inode)
		inode_init_owner(inode, NULL, S_IFREG);

	return inode;
}

static int btrfs_init_test_fs(void)
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
		return PTR_ERR(test_mnt);
	}
	return 0;
}

static void btrfs_destroy_test_fs(void)
{
	kern_unmount(test_mnt);
	unregister_filesystem(&test_type);
}

struct btrfs_fs_info *btrfs_alloc_dummy_fs_info(u32 nodesize, u32 sectorsize)
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

	fs_info->nodesize = nodesize;
	fs_info->sectorsize = sectorsize;

	if (init_srcu_struct(&fs_info->subvol_srcu)) {
		kfree(fs_info->fs_devices);
		kfree(fs_info->super_copy);
		kfree(fs_info);
		return NULL;
	}

	spin_lock_init(&fs_info->buffer_lock);
	spin_lock_init(&fs_info->qgroup_lock);
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
	extent_io_tree_init(fs_info, &fs_info->freed_extents[0],
			    IO_TREE_FS_INFO_FREED_EXTENTS0, NULL);
	extent_io_tree_init(fs_info, &fs_info->freed_extents[1],
			    IO_TREE_FS_INFO_FREED_EXTENTS1, NULL);
	fs_info->pinned_extents = &fs_info->freed_extents[0];
	set_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state);

	test_mnt->mnt_sb->s_fs_info = fs_info;

	return fs_info;
}

void btrfs_free_dummy_fs_info(struct btrfs_fs_info *fs_info)
{
	struct radix_tree_iter iter;
	void **slot;

	if (!fs_info)
		return;

	if (WARN_ON(!test_bit(BTRFS_FS_STATE_DUMMY_FS_INFO,
			      &fs_info->fs_state)))
		return;

	test_mnt->mnt_sb->s_fs_info = NULL;

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
		slot = radix_tree_iter_resume(slot, &iter);
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
	/* Will be freed by btrfs_free_fs_roots */
	if (WARN_ON(test_bit(BTRFS_ROOT_IN_RADIX, &root->state)))
		return;
	if (root->node) {
		/* One for allocate_extent_buffer */
		free_extent_buffer(root->node);
	}
	kfree(root);
}

struct btrfs_block_group_cache *
btrfs_alloc_dummy_block_group(struct btrfs_fs_info *fs_info,
			      unsigned long length)
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
	cache->full_stripe_len = fs_info->sectorsize;
	cache->fs_info = fs_info;

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

void btrfs_init_dummy_trans(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info)
{
	memset(trans, 0, sizeof(*trans));
	trans->transid = 1;
	trans->type = __TRANS_DUMMY;
	trans->fs_info = fs_info;
}

int btrfs_run_sanity_tests(void)
{
	int ret, i;
	u32 sectorsize, nodesize;
	u32 test_sectorsize[] = {
		PAGE_SIZE,
	};
	ret = btrfs_init_test_fs();
	if (ret)
		return ret;
	for (i = 0; i < ARRAY_SIZE(test_sectorsize); i++) {
		sectorsize = test_sectorsize[i];
		for (nodesize = sectorsize;
		     nodesize <= BTRFS_MAX_METADATA_BLOCKSIZE;
		     nodesize <<= 1) {
			pr_info("BTRFS: selftest: sectorsize: %u  nodesize: %u\n",
				sectorsize, nodesize);
			ret = btrfs_test_free_space_cache(sectorsize, nodesize);
			if (ret)
				goto out;
			ret = btrfs_test_extent_buffer_operations(sectorsize,
				nodesize);
			if (ret)
				goto out;
			ret = btrfs_test_extent_io(sectorsize, nodesize);
			if (ret)
				goto out;
			ret = btrfs_test_inodes(sectorsize, nodesize);
			if (ret)
				goto out;
			ret = btrfs_test_qgroups(sectorsize, nodesize);
			if (ret)
				goto out;
			ret = btrfs_test_free_space_tree(sectorsize, nodesize);
			if (ret)
				goto out;
		}
	}
	ret = btrfs_test_extent_map();

out:
	btrfs_destroy_test_fs();
	return ret;
}
