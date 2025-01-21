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
#include "../fs.h"

static struct vfsmount *test_mnt = NULL;

const char *test_error[] = {
	[TEST_ALLOC_FS_INFO]	     = "cannot allocate fs_info",
	[TEST_ALLOC_ROOT]	     = "cannot allocate root",
	[TEST_ALLOC_EXTENT_BUFFER]   = "cannot extent buffer",
	[TEST_ALLOC_PATH]	     = "cannot allocate path",
	[TEST_ALLOC_INODE]	     = "cannot allocate inode",
	[TEST_ALLOC_BLOCK_GROUP]     = "cannot allocate block group",
	[TEST_ALLOC_EXTENT_MAP]      = "cannot allocate extent map",
	[TEST_ALLOC_CHUNK_MAP]       = "cannot allocate chunk map",
	[TEST_ALLOC_IO_CONTEXT]	     = "cannot allocate io context",
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
	if (!inode)
		return NULL;

	inode->i_mode = S_IFREG;
	btrfs_set_inode_number(BTRFS_I(inode), BTRFS_FIRST_FREE_OBJECTID);
	inode_init_owner(&nop_mnt_idmap, inode, NULL, S_IFREG);

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

struct btrfs_device *btrfs_alloc_dummy_device(struct btrfs_fs_info *fs_info)
{
	struct btrfs_device *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	extent_io_tree_init(fs_info, &dev->alloc_state, 0);
	INIT_LIST_HEAD(&dev->dev_list);
	list_add(&dev->dev_list, &fs_info->fs_devices->devices);

	return dev;
}

static void btrfs_free_dummy_device(struct btrfs_device *dev)
{
	extent_io_tree_release(&dev->alloc_state);
	kfree(dev);
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
	INIT_LIST_HEAD(&fs_info->fs_devices->devices);

	fs_info->super_copy = kzalloc(sizeof(struct btrfs_super_block),
				      GFP_KERNEL);
	if (!fs_info->super_copy) {
		kfree(fs_info->fs_devices);
		kfree(fs_info);
		return NULL;
	}

	btrfs_init_fs_info(fs_info);

	fs_info->nodesize = nodesize;
	fs_info->sectorsize = sectorsize;
	fs_info->sectorsize_bits = ilog2(sectorsize);
	set_bit(BTRFS_FS_STATE_DUMMY_FS_INFO, &fs_info->fs_state);

	test_mnt->mnt_sb->s_fs_info = fs_info;

	return fs_info;
}

void btrfs_free_dummy_fs_info(struct btrfs_fs_info *fs_info)
{
	struct radix_tree_iter iter;
	void **slot;
	struct btrfs_device *dev, *tmp;

	if (!fs_info)
		return;

	if (WARN_ON(!btrfs_is_testing(fs_info)))
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

	btrfs_mapping_tree_free(fs_info);
	list_for_each_entry_safe(dev, tmp, &fs_info->fs_devices->devices,
				 dev_list) {
		btrfs_free_dummy_device(dev);
	}
	btrfs_free_qgroup_config(fs_info);
	btrfs_free_fs_roots(fs_info);
	kfree(fs_info->super_copy);
	btrfs_check_leaked_roots(fs_info);
	btrfs_extent_buffer_leak_debug_check(fs_info);
	kfree(fs_info->fs_devices);
	kfree(fs_info);
}

void btrfs_free_dummy_root(struct btrfs_root *root)
{
	if (IS_ERR_OR_NULL(root))
		return;
	/* Will be freed by btrfs_free_fs_roots */
	if (WARN_ON(test_bit(BTRFS_ROOT_IN_RADIX, &root->state)))
		return;
	btrfs_global_root_delete(root);
	btrfs_put_root(root);
}

struct btrfs_block_group *
btrfs_alloc_dummy_block_group(struct btrfs_fs_info *fs_info,
			      unsigned long length)
{
	struct btrfs_block_group *cache;

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return NULL;
	cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
					GFP_KERNEL);
	if (!cache->free_space_ctl) {
		kfree(cache);
		return NULL;
	}

	cache->start = 0;
	cache->length = length;
	cache->full_stripe_len = fs_info->sectorsize;
	cache->fs_info = fs_info;

	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);
	INIT_LIST_HEAD(&cache->bg_list);
	btrfs_init_free_space_ctl(cache, cache->free_space_ctl);
	mutex_init(&cache->free_space_lock);

	return cache;
}

void btrfs_free_dummy_block_group(struct btrfs_block_group *cache)
{
	if (!cache)
		return;
	btrfs_remove_free_space_cache(cache);
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
			ret = btrfs_test_raid_stripe_tree(sectorsize, nodesize);
			if (ret)
				goto out;
		}
	}
	ret = btrfs_test_extent_map();

out:
	btrfs_destroy_test_fs();
	return ret;
}
