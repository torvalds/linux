// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Western Digital Corporation or its affiliates.
 */

#include <linux/sizes.h>
#include "../fs.h"
#include "../disk-io.h"
#include "../transaction.h"
#include "../volumes.h"
#include "../raid-stripe-tree.h"
#include "btrfs-tests.h"

#define RST_TEST_NUM_DEVICES	(2)
#define RST_TEST_RAID1_TYPE	(BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_RAID1)

typedef int (*test_func_t)(struct btrfs_trans_handle *trans);

static struct btrfs_device *btrfs_device_by_devid(struct btrfs_fs_devices *fs_devices,
						  u64 devid)
{
	struct btrfs_device *dev;

	list_for_each_entry(dev, &fs_devices->devices, dev_list) {
		if (dev->devid == devid)
			return dev;
	}

	return NULL;
}

/*
 * Test a 64K RST write on a 2 disk RAID1 at a logical address of 1M and then
 * delete the 1st 32K, making the new start address 1M+32K.
 */
static int test_front_delete(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical = SZ_1M;
	u64 len = SZ_64K;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	bioc->map_type = map_type;
	bioc->size = len;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_64K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_64K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical, SZ_32K);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed", logical,
			 logical + SZ_32K);
		goto out;
	}

	len = SZ_32K;
	ret = btrfs_get_raid_extent_offset(fs_info, logical + SZ_32K, &len,
					   map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical + SZ_32K, logical + SZ_32K + len);
		goto out;
	}

	if (io_stripe.physical != logical + SZ_32K) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical + SZ_32K, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_32K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_32K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (!ret) {
		ret = -EINVAL;
		test_err("lookup of RAID extent [%llu, %llu] succeeded, should fail",
			 logical, logical + SZ_32K);
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical + SZ_32K, SZ_32K);
out:
	btrfs_put_bioc(bioc);
	return ret;
}

/*
 * Test a 64K RST write on a 2 disk RAID1 at a logical address of 1M and then
 * truncate the stripe extent down to 32K.
 */
static int test_tail_delete(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical = SZ_1M;
	u64 len = SZ_64K;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	bioc->map_type = map_type;
	bioc->size = len;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	if (!io_stripe.dev) {
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_64K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_64K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical + SZ_32K, SZ_32K);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical + SZ_32K, logical + SZ_64K);
		goto out;
	}

	len = SZ_32K;
	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_32K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_32K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical, len);
	if (ret)
		test_err("deleting RAID extent [%llu, %llu] failed", logical,
			 logical + len);

out:
	btrfs_put_bioc(bioc);
	return ret;
}

/*
 * Test a 64K RST write on a 2 disk RAID1 at a logical address of 1M and then
 * overwrite the whole range giving it new physical address at an offset of 1G.
 * The intent of this test is to exercise the 'update_raid_extent_item()'
 * function called be btrfs_insert_one_raid_extent().
 */
static int test_create_update_delete(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical = SZ_1M;
	u64 len = SZ_64K;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	bioc->map_type = map_type;
	bioc->size = len;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	if (!io_stripe.dev) {
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_64K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_64K, len);
		ret = -EINVAL;
		goto out;
	}

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = SZ_1G + logical + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("updating RAID extent failed: %d", ret);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical + SZ_1G) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical + SZ_1G, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_64K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_64K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical, len);
	if (ret)
		test_err("deleting RAID extent [%llu, %llu] failed", logical,
			 logical + len);

out:
	btrfs_put_bioc(bioc);
	return ret;
}

/*
 * Test a simple 64K RST write on a 2 disk RAID1 at a logical address of 1M.
 * The "physical" copy on device 0 is at 1M, on device 1 it is at 1G+1M.
 */
static int test_simple_create_delete(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical = SZ_1M;
	u64 len = SZ_64K;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	bioc->map_type = map_type;
	bioc->size = SZ_64K;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	if (!io_stripe.dev) {
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret)  {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical,
			 logical + len);
		goto out;
	}

	if (io_stripe.physical != logical) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_64K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_64K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical, len);
	if (ret)
		test_err("deleting RAID extent [%llu, %llu] failed", logical,
			 logical + len);

out:
	btrfs_put_bioc(bioc);
	return ret;
}

static const test_func_t tests[] = {
	test_simple_create_delete,
	test_create_update_delete,
	test_tail_delete,
	test_front_delete,
};

static int run_test(test_func_t test, u32 sectorsize, u32 nodesize)
{
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root = NULL;
	int ret;

	fs_info = btrfs_alloc_dummy_fs_info(sectorsize, nodesize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		ret = -ENOMEM;
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(root);
		goto out;
	}
	btrfs_set_super_compat_ro_flags(root->fs_info->super_copy,
					BTRFS_FEATURE_INCOMPAT_RAID_STRIPE_TREE);
	root->root_key.objectid = BTRFS_RAID_STRIPE_TREE_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = 0;
	fs_info->stripe_root = root;
	root->fs_info->tree_root = root;

	root->node = alloc_test_extent_buffer(root->fs_info, nodesize);
	if (IS_ERR(root->node)) {
		test_std_err(TEST_ALLOC_EXTENT_BUFFER);
		ret = PTR_ERR(root->node);
		goto out;
	}
	btrfs_set_header_level(root->node, 0);
	btrfs_set_header_nritems(root->node, 0);
	root->alloc_bytenr += 2 * nodesize;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_device *dev;

		dev = btrfs_alloc_dummy_device(fs_info);
		if (IS_ERR(dev)) {
			test_err("cannot allocate device");
			ret = PTR_ERR(dev);
			goto out;
		}
		dev->devid = i;
	}

	btrfs_init_dummy_trans(&trans, root->fs_info);
	ret = test(&trans);
	if (ret)
		goto out;

out:
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);

	return ret;
}

int btrfs_test_raid_stripe_tree(u32 sectorsize, u32 nodesize)
{
	int ret = 0;

	test_msg("running raid-stripe-tree tests");
	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		ret = run_test(tests[i], sectorsize, nodesize);
		if (ret) {
			test_err("test-case %ps failed with %d\n", tests[i], ret);
			goto out;
		}
	}

out:
	return ret;
}
