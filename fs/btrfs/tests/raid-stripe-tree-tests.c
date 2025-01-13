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

#define SZ_48K (SZ_32K + SZ_16K)

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
 * Test creating a range of three extents and then punch a hole in the middle,
 * deleting all of the middle extents and partially deleting the "book ends".
 */
static int test_punch_hole_3extents(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical1 = SZ_1M;
	u64 len1 = SZ_1M;
	u64 logical2 = logical1 + len1;
	u64 len2 = SZ_1M;
	u64 logical3 = logical2 + len2;
	u64 len3 = SZ_1M;
	u64 hole_start = logical1 + SZ_256K;
	u64 hole_len = SZ_2M;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical1, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);

	/* Prepare for the test, 1st create 3 x 1M extents. */
	bioc->map_type = map_type;
	bioc->size = len1;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical1 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	bioc->logical = logical2;
	bioc->size = len2;
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical2 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	bioc->logical = logical3;
	bioc->size = len3;
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical3 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	/*
	 * Delete a range starting at logical1 + 256K and 2M in length. Extent
	 * 1 is truncated to 256k length, extent 2 is completely dropped and
	 * extent 3 is moved 256K to the right.
	 */
	ret = btrfs_delete_raid_extent(trans, hole_start, hole_len);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 hole_start, hole_start + hole_len);
		goto out;
	}

	/* Get the first extent and check its size. */
	ret = btrfs_get_raid_extent_offset(fs_info, logical1, &len1, map_type,
					   0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical1, logical1 + len1);
		goto out;
	}

	if (io_stripe.physical != logical1) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical1, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len1 != SZ_256K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_256K, len1);
		ret = -EINVAL;
		goto out;
	}

	/* Get the second extent and check it's absent. */
	ret = btrfs_get_raid_extent_offset(fs_info, logical2, &len2, map_type,
					   0, &io_stripe);
	if (ret != -ENODATA) {
		test_err("lookup of RAID extent [%llu, %llu] succeeded should fail",
			 logical2, logical2 + len2);
		ret = -EINVAL;
		goto out;
	}

	/* Get the third extent and check its size. */
	logical3 += SZ_256K;
	ret = btrfs_get_raid_extent_offset(fs_info, logical3, &len3, map_type,
					   0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical3, logical3 + len3);
		goto out;
	}

	if (io_stripe.physical != logical3) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical3 + SZ_256K, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len3 != SZ_1M - SZ_256K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_1M - SZ_256K, len3);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical1, len1);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical1, logical1 + len1);
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical3, len3);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical1, logical1 + len1);
		goto out;
	}

out:
	btrfs_put_bioc(bioc);
	return ret;
}

static int test_delete_two_extents(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical1 = SZ_1M;
	u64 len1 = SZ_1M;
	u64 logical2 = logical1 + len1;
	u64 len2 = SZ_1M;
	u64 logical3 = logical2 + len2;
	u64 len3 = SZ_1M;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical1, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);

	/* Prepare for the test, 1st create 3 x 1M extents. */
	bioc->map_type = map_type;
	bioc->size = len1;

	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical1 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	bioc->logical = logical2;
	bioc->size = len2;
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical2 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	bioc->logical = logical3;
	bioc->size = len3;
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical3 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	/*
	 * Delete a range starting at logical1 and 2M in length. Extents 1
	 * and 2 are dropped and extent 3 is kept as is.
	 */
	ret = btrfs_delete_raid_extent(trans, logical1, len1 + len2);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical1, logical1 + len1 + len2);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical1, &len1, map_type,
					   0, &io_stripe);
	if (ret != -ENODATA) {
		test_err("lookup of RAID extent [%llu, %llu] succeeded, should fail",
			 logical1, len1);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical2, &len2, map_type,
					   0, &io_stripe);
	if (ret != -ENODATA) {
		test_err("lookup of RAID extent [%llu, %llu] succeeded, should fail",
			 logical2, len2);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical3, &len3, map_type,
					   0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical3, len3);
		goto out;
	}

	if (io_stripe.physical != logical3) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical3, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len3 != SZ_1M) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_1M, len3);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical3, len3);
out:
	btrfs_put_bioc(bioc);
	return ret;
}

/* Test punching a hole into a single RAID stripe-extent. */
static int test_punch_hole(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical1 = SZ_1M;
	u64 hole_start = logical1 + SZ_32K;
	u64 hole_len = SZ_64K;
	u64 logical2 = hole_start + hole_len;
	u64 len = SZ_1M;
	u64 len1 = SZ_32K;
	u64 len2 = len - len1 - hole_len;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical1, RST_TEST_NUM_DEVICES);
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

		stripe->physical = logical1 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical1, &len, map_type, 0,
					   &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical1,
			 logical1 + len);
		goto out;
	}

	if (io_stripe.physical != logical1) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical1, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_1M) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_1M, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, hole_start, hole_len);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 hole_start, hole_start + hole_len);
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical1, &len1, map_type,
					   0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical1, logical1 + len1);
		goto out;
	}

	if (io_stripe.physical != logical1) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical1, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len1 != SZ_32K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_32K, len1);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical2, &len2, map_type,
					   0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical2,
			 logical2 + len2);
		goto out;
	}

	if (io_stripe.physical != logical2) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical2, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len2 != len - len1 - hole_len) {
		test_err("invalid length, expected %llu, got %llu",
			 len - len1 - hole_len, len2);
		ret = -EINVAL;
		goto out;
	}

	/* Check for the absence of the hole. */
	ret = btrfs_get_raid_extent_offset(fs_info, hole_start, &hole_len,
					   map_type, 0, &io_stripe);
	if (ret != -ENODATA) {
		ret = -EINVAL;
		test_err("lookup of RAID extent [%llu, %llu] succeeded, should fail",
			 hole_start, hole_start + SZ_64K);
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical1, len1);
	if (ret)
		goto out;

	ret = btrfs_delete_raid_extent(trans, logical2, len2);
out:
	btrfs_put_bioc(bioc);
	return ret;
}

/*
 * Test a 1M RST write that spans two adjacent RST items on disk and then
 * delete a portion starting in the first item and spanning into the second
 * item. This is similar to test_front_delete(), but spanning multiple items.
 */
static int test_front_delete_prev_item(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe io_stripe = { 0 };
	u64 map_type = RST_TEST_RAID1_TYPE;
	u64 logical1 = SZ_1M;
	u64 logical2 = SZ_2M;
	u64 len = SZ_1M;
	int ret;

	bioc = alloc_btrfs_io_context(fs_info, logical1, RST_TEST_NUM_DEVICES);
	if (!bioc) {
		test_std_err(TEST_ALLOC_IO_CONTEXT);
		ret = -ENOMEM;
		goto out;
	}

	io_stripe.dev = btrfs_device_by_devid(fs_info->fs_devices, 0);
	bioc->map_type = map_type;
	bioc->size = len;

	/* Insert RAID extent 1. */
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical1 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	bioc->logical = logical2;
	/* Insert RAID extent 2, directly adjacent to it. */
	for (int i = 0; i < RST_TEST_NUM_DEVICES; i++) {
		struct btrfs_io_stripe *stripe = &bioc->stripes[i];

		stripe->dev = btrfs_device_by_devid(fs_info->fs_devices, i);
		if (!stripe->dev) {
			test_err("cannot find device with devid %d", i);
			ret = -EINVAL;
			goto out;
		}

		stripe->physical = logical2 + i * SZ_1G;
	}

	ret = btrfs_insert_one_raid_extent(trans, bioc);
	if (ret) {
		test_err("inserting RAID extent failed: %d", ret);
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical1 + SZ_512K, SZ_1M);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical1 + SZ_512K, (u64)SZ_1M);
		goto out;
	}

	/* Verify item 1 is truncated to 512K. */
	ret = btrfs_get_raid_extent_offset(fs_info, logical1, &len, map_type, 0,
					   &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed", logical1,
			 logical1 + len);
		goto out;
	}

	if (io_stripe.physical != logical1) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical1, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_512K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_512K, len);
		ret = -EINVAL;
		goto out;
	}

	/* Verify item 2's start is moved by 512K. */
	ret = btrfs_get_raid_extent_offset(fs_info, logical2 + SZ_512K, &len,
					   map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical2 + SZ_512K, logical2 + len);
		goto out;
	}

	if (io_stripe.physical != logical2 + SZ_512K) {
		test_err("invalid physical address, expected %llu got %llu",
			 logical2 + SZ_512K, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_512K) {
		test_err("invalid stripe length, expected %llu got %llu",
			 (u64)SZ_512K, len);
		ret = -EINVAL;
		goto out;
	}

	/* Verify there's a hole at [1M+512K, 2M+512K] . */
	len = SZ_1M;
	ret = btrfs_get_raid_extent_offset(fs_info, logical1 + SZ_512K, &len,
					   map_type, 0, &io_stripe);
	if (ret != -ENODATA) {
		test_err("lookup of RAID [%llu, %llu] succeeded, should fail",
			 logical1 + SZ_512K, logical1 + SZ_512K + len);
		goto out;
	}

	/* Clean up after us. */
	ret = btrfs_delete_raid_extent(trans, logical1, SZ_512K);
	if (ret)
		goto out;

	ret = btrfs_delete_raid_extent(trans, logical2 + SZ_512K, SZ_512K);

out:
	btrfs_put_bioc(bioc);
	return ret;
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

	ret = btrfs_delete_raid_extent(trans, logical, SZ_16K);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed", logical,
			 logical + SZ_16K);
		goto out;
	}

	len -= SZ_16K;
	ret = btrfs_get_raid_extent_offset(fs_info, logical + SZ_16K, &len,
					   map_type, 0, &io_stripe);
	if (ret) {
		test_err("lookup of RAID extent [%llu, %llu] failed",
			 logical + SZ_16K, logical + SZ_64K);
		goto out;
	}

	if (io_stripe.physical != logical + SZ_16K) {
		test_err("invalid physical address, expected %llu, got %llu",
			 logical + SZ_16K, io_stripe.physical);
		ret = -EINVAL;
		goto out;
	}

	if (len != SZ_48K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_48K, len);
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_get_raid_extent_offset(fs_info, logical, &len, map_type, 0, &io_stripe);
	if (ret != -ENODATA) {
		ret = -EINVAL;
		test_err("lookup of RAID extent [%llu, %llu] succeeded, should fail",
			 logical, logical + SZ_16K);
		goto out;
	}

	ret = btrfs_delete_raid_extent(trans, logical + SZ_16K, SZ_48K);
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

	ret = btrfs_delete_raid_extent(trans, logical + SZ_48K, SZ_16K);
	if (ret) {
		test_err("deleting RAID extent [%llu, %llu] failed",
			 logical + SZ_48K, logical + SZ_64K);
		goto out;
	}

	len = SZ_48K;
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

	if (len != SZ_48K) {
		test_err("invalid stripe length, expected %llu, got %llu",
			 (u64)SZ_48K, len);
		ret = -EINVAL;
		goto out;
	}

	len = SZ_16K;
	ret = btrfs_get_raid_extent_offset(fs_info, logical + SZ_48K, &len,
					   map_type, 0, &io_stripe);
	if (ret != -ENODATA) {
		test_err("lookup of RAID extent [%llu, %llu] succeeded should fail",
			 logical + SZ_48K, logical + SZ_64K);
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
	test_front_delete_prev_item,
	test_punch_hole,
	test_punch_hole_3extents,
	test_delete_two_extents,
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
	btrfs_set_super_incompat_flags(root->fs_info->super_copy,
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
