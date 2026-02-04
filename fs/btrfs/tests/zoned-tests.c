// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Western Digital.  All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/sizes.h>

#include "btrfs-tests.h"
#include "../space-info.h"
#include "../volumes.h"
#include "../zoned.h"

#define WP_MISSING_DEV				((u64)-1)
#define WP_CONVENTIONAL				((u64)-2)
#define ZONE_SIZE				SZ_256M

#define HALF_STRIPE_LEN				(BTRFS_STRIPE_LEN >> 1)

struct load_zone_info_test_vector {
	u64 raid_type;
	u64 num_stripes;
	u64 alloc_offsets[8];
	u64 last_alloc;
	u64 bg_length;
	bool degraded;

	int expected_result;
	u64 expected_alloc_offset;

	const char *description;
};

struct zone_info {
	u64 physical;
	u64 capacity;
	u64 alloc_offset;
};

static int test_load_zone_info(struct btrfs_fs_info *fs_info,
			       const struct load_zone_info_test_vector *test)
{
	struct btrfs_block_group *bg __free(btrfs_free_dummy_block_group) = NULL;
	struct btrfs_chunk_map *map __free(btrfs_free_chunk_map) = NULL;
	struct zone_info AUTO_KFREE(zone_info);
	unsigned long AUTO_KFREE(active);
	int ret;

	bg = btrfs_alloc_dummy_block_group(fs_info, test->bg_length);
	if (!bg) {
		test_std_err(TEST_ALLOC_BLOCK_GROUP);
		return -ENOMEM;
	}

	map = btrfs_alloc_chunk_map(test->num_stripes, GFP_KERNEL);
	if (!map) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	zone_info = kcalloc(test->num_stripes, sizeof(*zone_info), GFP_KERNEL);
	if (!zone_info) {
		test_err("cannot allocate zone info");
		return -ENOMEM;
	}

	active = bitmap_zalloc(test->num_stripes, GFP_KERNEL);
	if (!zone_info) {
		test_err("cannot allocate active bitmap");
		return -ENOMEM;
	}

	map->type = test->raid_type;
	map->num_stripes = test->num_stripes;
	if (test->raid_type == BTRFS_BLOCK_GROUP_RAID10)
		map->sub_stripes = 2;
	for (int i = 0; i < test->num_stripes; i++) {
		zone_info[i].physical = 0;
		zone_info[i].alloc_offset = test->alloc_offsets[i];
		zone_info[i].capacity = ZONE_SIZE;
		if (zone_info[i].alloc_offset && zone_info[i].alloc_offset < ZONE_SIZE)
			__set_bit(i, active);
	}
	if (test->degraded)
		btrfs_set_opt(fs_info->mount_opt, DEGRADED);
	else
		btrfs_clear_opt(fs_info->mount_opt, DEGRADED);

	ret = btrfs_load_block_group_by_raid_type(bg, map, zone_info, active,
						  test->last_alloc);

	if (ret != test->expected_result) {
		test_err("unexpected return value: ret %d expected %d", ret,
			 test->expected_result);
		return -EINVAL;
	}

	if (!ret && bg->alloc_offset != test->expected_alloc_offset) {
		test_err("unexpected alloc_offset: alloc_offset %llu expected %llu",
			 bg->alloc_offset, test->expected_alloc_offset);
		return -EINVAL;
	}

	return 0;
}

static const struct load_zone_info_test_vector load_zone_info_tests[] = {
	/* SINGLE */
	{
		.description = "SINGLE: load write pointer from sequential zone",
		.raid_type = 0,
		.num_stripes = 1,
		.alloc_offsets = {
			SZ_1M,
		},
		.expected_alloc_offset = SZ_1M,
	},
	/*
	 * SINGLE block group on a conventional zone sets last_alloc outside of
	 * btrfs_load_block_group_*(). Do not test that case.
	 */

	/* DUP */
	/* Normal case */
	{
		.description = "DUP: having matching write pointers",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, SZ_1M,
		},
		.expected_alloc_offset = SZ_1M,
	},
	/*
	 * One sequential zone and one conventional zone, having matching
	 * last_alloc.
	 */
	{
		.description = "DUP: seq zone and conv zone, matching last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_1M,
		.expected_alloc_offset = SZ_1M,
	},
	/*
	 * One sequential and one conventional zone, but having smaller
	 * last_alloc than write pointer.
	 */
	{
		.description = "DUP: seq zone and conv zone, smaller last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = 0,
		.expected_alloc_offset = SZ_1M,
	},
	/* Error case: having different write pointers. */
	{
		.description = "DUP: fail: different write pointers",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, SZ_2M,
		},
		.expected_result = -EIO,
	},
	/* Error case: partial missing device should not happen on DUP. */
	{
		.description = "DUP: fail: missing device",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_MISSING_DEV,
		},
		.expected_result = -EIO,
	},
	/*
	 * Error case: one sequential and one conventional zone, but having larger
	 * last_alloc than write pointer.
	 */
	{
		.description = "DUP: fail: seq zone and conv zone, larger last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_DUP,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M,
		.expected_result = -EIO,
	},

	/* RAID1 */
	/* Normal case */
	{
		.description = "RAID1: having matching write pointers",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, SZ_1M,
		},
		.expected_alloc_offset = SZ_1M,
	},
	/*
	 * One sequential zone and one conventional zone, having matching
	 * last_alloc.
	 */
	{
		.description = "RAID1: seq zone and conv zone, matching last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_1M,
		.expected_alloc_offset = SZ_1M,
	},
	/*
	 * One sequential and one conventional zone, but having smaller
	 * last_alloc than write pointer.
	 */
	{
		.description = "RAID1: seq zone and conv zone, smaller last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = 0,
		.expected_alloc_offset = SZ_1M,
	},
	/* Partial missing device should be recovered on DEGRADED mount */
	{
		.description = "RAID1: fail: missing device on DEGRADED",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_MISSING_DEV,
		},
		.degraded = true,
		.expected_alloc_offset = SZ_1M,
	},
	/* Error case: having different write pointers. */
	{
		.description = "RAID1: fail: different write pointers",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, SZ_2M,
		},
		.expected_result = -EIO,
	},
	/*
	 * Partial missing device is not allowed on non-DEGRADED mount never happen
	 * as it is rejected beforehand.
	 */
	/*
	 * Error case: one sequential and one conventional zone, but having larger
	 * last_alloc than write pointer.
	 */
	{
		.description = "RAID1: fail: seq zone and conv zone, larger last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_RAID1,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M,
		.expected_result = -EIO,
	},

	/* RAID0 */
	/* Normal case */
	{
		.description = "RAID0: initial partial write",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			HALF_STRIPE_LEN, 0, 0, 0,
		},
		.expected_alloc_offset = HALF_STRIPE_LEN,
	},
	{
		.description = "RAID0: while in second stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN + HALF_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 5 + HALF_STRIPE_LEN,
	},
	{
		.description = "RAID0: one stripe advanced",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M + BTRFS_STRIPE_LEN, SZ_1M,
		},
		.expected_alloc_offset = SZ_2M + BTRFS_STRIPE_LEN,
	},
	/* Error case: having different write pointers. */
	{
		.description = "RAID0: fail: disordered stripes",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN * 2,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_result = -EIO,
	},
	{
		.description = "RAID0: fail: far distance",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 3, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_result = -EIO,
	},
	{
		.description = "RAID0: fail: too many partial write",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			HALF_STRIPE_LEN, HALF_STRIPE_LEN, 0, 0,
		},
		.expected_result = -EIO,
	},
	/*
	 * Error case: Partial missing device is not allowed even on non-DEGRADED
	 * mount.
	 */
	{
		.description = "RAID0: fail: missing device on DEGRADED",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_MISSING_DEV,
		},
		.degraded = true,
		.expected_result = -EIO,
	},

	/*
	 * One sequential zone and one conventional zone, having matching
	 * last_alloc.
	 */
	{
		.description = "RAID0: seq zone and conv zone, partially written stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M - SZ_4K,
		.expected_alloc_offset = SZ_2M - SZ_4K,
	},
	{
		.description = "RAID0: conv zone and seq zone, partially written stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 2,
		.alloc_offsets = {
			WP_CONVENTIONAL, SZ_1M,
		},
		.last_alloc = SZ_2M + SZ_4K,
		.expected_alloc_offset = SZ_2M + SZ_4K,
	},
	/*
	 * Error case: one sequential and one conventional zone, but having larger
	 * last_alloc than write pointer.
	 */
	{
		.description = "RAID0: fail: seq zone and conv zone, larger last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 2,
		.alloc_offsets = {
			SZ_1M, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M + BTRFS_STRIPE_LEN * 2,
		.expected_result = -EIO,
	},

	/* RAID0, 4 stripes with seq zones and conv zones. */
	{
		.description = "RAID0: stripes [2, 2, ?, ?] last_alloc = 6",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 6,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 6,
	},
	{
		.description = "RAID0: stripes [2, 2, ?, ?] last_alloc = 7.5",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 7 + HALF_STRIPE_LEN,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 7 + HALF_STRIPE_LEN,
	},
	{
		.description = "RAID0: stripes [3, ?, ?, ?] last_alloc = 1",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 3, WP_CONVENTIONAL,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 9,
	},
	{
		.description = "RAID0: stripes [2, ?, 1, ?] last_alloc = 5",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, WP_CONVENTIONAL,
			BTRFS_STRIPE_LEN, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 5,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 5,
	},
	{
		.description = "RAID0: fail: stripes [2, ?, 1, ?] last_alloc = 7",
		.raid_type = BTRFS_BLOCK_GROUP_RAID0,
		.num_stripes = 4,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, WP_CONVENTIONAL,
			BTRFS_STRIPE_LEN, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 7,
		.expected_result = -EIO,
	},

	/* RAID10 */
	/* Normal case */
	{
		.description = "RAID10: initial partial write",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			HALF_STRIPE_LEN, HALF_STRIPE_LEN, 0, 0,
		},
		.expected_alloc_offset = HALF_STRIPE_LEN,
	},
	{
		.description = "RAID10: while in second stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			BTRFS_STRIPE_LEN + HALF_STRIPE_LEN,
			BTRFS_STRIPE_LEN + HALF_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 5 + HALF_STRIPE_LEN,
	},
	{
		.description = "RAID10: one stripe advanced",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			SZ_1M + BTRFS_STRIPE_LEN, SZ_1M + BTRFS_STRIPE_LEN,
			SZ_1M, SZ_1M,
		},
		.expected_alloc_offset = SZ_2M + BTRFS_STRIPE_LEN,
	},
	{
		.description = "RAID10: one stripe advanced, with conventional zone",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			SZ_1M + BTRFS_STRIPE_LEN, WP_CONVENTIONAL,
			WP_CONVENTIONAL, SZ_1M,
		},
		.expected_alloc_offset = SZ_2M + BTRFS_STRIPE_LEN,
	},
	/* Error case: having different write pointers. */
	{
		.description = "RAID10: fail: disordered stripes",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_result = -EIO,
	},
	{
		.description = "RAID10: fail: far distance",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 3, BTRFS_STRIPE_LEN * 3,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
		},
		.expected_result = -EIO,
	},
	{
		.description = "RAID10: fail: too many partial write",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			HALF_STRIPE_LEN, HALF_STRIPE_LEN,
			HALF_STRIPE_LEN, HALF_STRIPE_LEN,
			0, 0, 0, 0,
		},
		.expected_result = -EIO,
	},
	/*
	 * Error case: Partial missing device in RAID0 level is not allowed even on
	 * non-DEGRADED mount.
	 */
	{
		.description = "RAID10: fail: missing device on DEGRADED",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			SZ_1M, SZ_1M,
			WP_MISSING_DEV, WP_MISSING_DEV,
		},
		.degraded = true,
		.expected_result = -EIO,
	},

	/*
	 * One sequential zone and one conventional zone, having matching
	 * last_alloc.
	 */
	{
		.description = "RAID10: seq zone and conv zone, partially written stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			SZ_1M, SZ_1M,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M - SZ_4K,
		.expected_alloc_offset = SZ_2M - SZ_4K,
	},
	{
		.description = "RAID10: conv zone and seq zone, partially written stripe",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			SZ_1M, SZ_1M,
		},
		.last_alloc = SZ_2M + SZ_4K,
		.expected_alloc_offset = SZ_2M + SZ_4K,
	},
	/*
	 * Error case: one sequential and one conventional zone, but having larger
	 * last_alloc than write pointer.
	 */
	{
		.description = "RAID10: fail: seq zone and conv zone, larger last_alloc",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 4,
		.alloc_offsets = {
			SZ_1M, SZ_1M,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = SZ_2M + BTRFS_STRIPE_LEN * 2,
		.expected_result = -EIO,
	},

	/* RAID10, 4 stripes with seq zones and conv zones. */
	{
		.description = "RAID10: stripes [2, 2, ?, ?] last_alloc = 6",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 6,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 6,
	},
	{
		.description = "RAID10: stripes [2, 2, ?, ?] last_alloc = 7.5",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 7 + HALF_STRIPE_LEN,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 7 + HALF_STRIPE_LEN,
	},
	{
		.description = "RAID10: stripes [3, ?, ?, ?] last_alloc = 1",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 3, BTRFS_STRIPE_LEN * 3,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 9,
	},
	{
		.description = "RAID10: stripes [2, ?, 1, ?] last_alloc = 5",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 5,
		.expected_alloc_offset = BTRFS_STRIPE_LEN * 5,
	},
	{
		.description = "RAID10: fail: stripes [2, ?, 1, ?] last_alloc = 7",
		.raid_type = BTRFS_BLOCK_GROUP_RAID10,
		.num_stripes = 8,
		.alloc_offsets = {
			BTRFS_STRIPE_LEN * 2, BTRFS_STRIPE_LEN * 2,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
			BTRFS_STRIPE_LEN, BTRFS_STRIPE_LEN,
			WP_CONVENTIONAL, WP_CONVENTIONAL,
		},
		.last_alloc = BTRFS_STRIPE_LEN * 7,
		.expected_result = -EIO,
	},
};

int btrfs_test_zoned(void)
{
	struct btrfs_fs_info *fs_info __free(btrfs_free_dummy_fs_info) = NULL;
	int ret;

	test_msg("running zoned tests (error messages are expected)");

	fs_info = btrfs_alloc_dummy_fs_info(PAGE_SIZE, PAGE_SIZE);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	for (int i = 0; i < ARRAY_SIZE(load_zone_info_tests); i++) {
		ret = test_load_zone_info(fs_info, &load_zone_info_tests[i]);
		if (ret) {
			test_err("test case \"%s\" failed", load_zone_info_tests[i].description);
			return ret;
		}
	}

	return 0;
}
