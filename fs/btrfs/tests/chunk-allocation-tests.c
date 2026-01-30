// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Meta.  All rights reserved.
 */

#include <linux/sizes.h>
#include "btrfs-tests.h"
#include "../volumes.h"
#include "../disk-io.h"
#include "../extent-io-tree.h"

/*
 * Tests for chunk allocator pending extent internals.
 * These two functions form the core of searching the chunk allocation pending
 * extent bitmap and have relatively easily definable semantics, so unit
 * testing them can help ensure the correctness of chunk allocation.
 */

/*
 * Describes the inputs to the system and expected results
 * when testing btrfs_find_hole_in_pending_extents().
 */
struct pending_extent_test_case {
	const char *name;
	/* Input range to search. */
	u64 hole_start;
	u64 hole_len;
	/* The size of hole we are searching for. */
	u64 min_hole_size;
	/*
	 * Pending extents to set up (up to 2 for up to 3 holes)
	 * If len == 0, then it is skipped.
	 */
	struct {
		u64 start;
		u64 len;
	} pending_extents[2];
	/* Expected outputs. */
	bool expected_found;
	u64 expected_start;
	u64 expected_len;
};

static const struct pending_extent_test_case find_hole_tests[] = {
	{
		.name = "no pending extents",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = { },
		.expected_found = true,
		.expected_start = 0,
		.expected_len = 10ULL * SZ_1G,
	},
	{
		.name = "pending extent at start of range",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = 0, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = SZ_1G,
		.expected_len = 9ULL * SZ_1G,
	},
	{
		.name = "pending extent overlapping start of range",
		.hole_start = SZ_1G,
		.hole_len = 9ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = 0, .len = SZ_2G },
		},
		.expected_found = true,
		.expected_start = SZ_2G,
		.expected_len = 8ULL * SZ_1G,
	},
	{
		.name = "two holes; first hole is exactly big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = 0,
		.expected_len = SZ_1G,
	},
	{
		.name = "two holes; first hole is big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = SZ_2G, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = 0,
		.expected_len = SZ_2G,
	},
	{
		.name = "two holes; second hole is big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_2G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = SZ_2G,
		.expected_len = 8ULL * SZ_1G,
	},
	{
		.name = "three holes; first hole big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_2G,
		.pending_extents = {
			{ .start = SZ_2G, .len = SZ_1G },
			{ .start = 4ULL * SZ_1G, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = 0,
		.expected_len = SZ_2G,
	},
	{
		.name = "three holes; second hole big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_2G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
			{ .start = 5ULL * SZ_1G, .len = SZ_1G },
		},
		.expected_found = true,
		.expected_start = SZ_2G,
		.expected_len = 3ULL * SZ_1G,
	},
	{
		.name = "three holes; third hole big enough",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_2G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
			{ .start = 3ULL * SZ_1G, .len = 5ULL * SZ_1G },
		},
		.expected_found = true,
		.expected_start = 8ULL * SZ_1G,
		.expected_len = SZ_2G,
	},
	{
		.name = "three holes; all holes too small",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_2G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
			{ .start = 3ULL * SZ_1G, .len = 6ULL * SZ_1G },
		},
		.expected_found = false,
		.expected_start = 0,
		.expected_len = SZ_1G,
	},
	{
		.name = "three holes; all holes too small; first biggest",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = 3ULL * SZ_1G,
		.pending_extents = {
			{ .start = SZ_2G, .len = SZ_1G },
			{ .start = 4ULL * SZ_1G, .len = 5ULL * SZ_1G },
		},
		.expected_found = false,
		.expected_start = 0,
		.expected_len = SZ_2G,
	},
	{
		.name = "three holes; all holes too small; second biggest",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = 3ULL * SZ_1G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
			{ .start = 4ULL * SZ_1G, .len = 5ULL * SZ_1G },
		},
		.expected_found = false,
		.expected_start = SZ_2G,
		.expected_len = SZ_2G,
	},
	{
		.name = "three holes; all holes too small; third biggest",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = 3ULL * SZ_1G,
		.pending_extents = {
			{ .start = SZ_1G, .len = SZ_1G },
			{ .start = 3ULL * SZ_1G, .len = 5ULL * SZ_1G },
		},
		.expected_found = false,
		.expected_start = 8ULL * SZ_1G,
		.expected_len = SZ_2G,
	},
	{
		.name = "hole entirely allocated by pending",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = 0, .len = 10ULL * SZ_1G },
		},
		.expected_found = false,
		.expected_start = 10ULL * SZ_1G,
		.expected_len = 0,
	},
	{
		.name = "pending extent at end of range",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.min_hole_size = SZ_1G,
		.pending_extents = {
			{ .start = 9ULL * SZ_1G, .len = SZ_2G },
		},
		.expected_found = true,
		.expected_start = 0,
		.expected_len = 9ULL * SZ_1G,
	},
	{
		.name = "zero length input",
		.hole_start = SZ_1G,
		.hole_len = 0,
		.min_hole_size = SZ_1G,
		.pending_extents = { },
		.expected_found = false,
		.expected_start = SZ_1G,
		.expected_len = 0,
	},
};

static int test_find_hole_in_pending(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_device *device;
	int ret = 0;

	test_msg("running find_hole_in_pending_extents tests");

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	device = btrfs_alloc_dummy_device(fs_info);
	if (IS_ERR(device)) {
		test_err("failed to allocate dummy device");
		ret = PTR_ERR(device);
		goto out_free_fs_info;
	}
	device->fs_info = fs_info;

	for (int i = 0; i < ARRAY_SIZE(find_hole_tests); i++) {
		const struct pending_extent_test_case *test_case = &find_hole_tests[i];
		u64 hole_start = test_case->hole_start;
		u64 hole_len = test_case->hole_len;
		bool found;

		for (int j = 0; j < ARRAY_SIZE(test_case->pending_extents); j++) {
			u64 start = test_case->pending_extents[j].start;
			u64 len = test_case->pending_extents[j].len;

			if (!len)
				continue;
			btrfs_set_extent_bit(&device->alloc_state,
					     start, start + len - 1,
					     CHUNK_ALLOCATED, NULL);
		}

		mutex_lock(&fs_info->chunk_mutex);
		found = btrfs_find_hole_in_pending_extents(device, &hole_start, &hole_len,
							   test_case->min_hole_size);
		mutex_unlock(&fs_info->chunk_mutex);

		if (found != test_case->expected_found) {
			test_err("%s: expected found=%d, got found=%d",
				 test_case->name, test_case->expected_found, found);
			ret = -EINVAL;
			goto out_clear_pending_extents;
		}
		if (hole_start != test_case->expected_start ||
		    hole_len != test_case->expected_len) {
			test_err("%s: expected [%llu, %llu), got [%llu, %llu)",
				 test_case->name, test_case->expected_start,
				 test_case->expected_start +
					 test_case->expected_len,
				 hole_start, hole_start + hole_len);
			ret = -EINVAL;
			goto out_clear_pending_extents;
		}
out_clear_pending_extents:
		btrfs_clear_extent_bit(&device->alloc_state, 0, (u64)-1,
				       CHUNK_ALLOCATED, NULL);
		if (ret)
			break;
	}

out_free_fs_info:
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

/*
 * Describes the inputs to the system and expected results
 * when testing btrfs_first_pending_extent().
 */
struct first_pending_test_case {
	const char *name;
	/* The range to look for a pending extent in. */
	u64 hole_start;
	u64 hole_len;
	/* The pending extent to look for. */
	struct {
		u64 start;
		u64 len;
	} pending_extent;
	/* Expected outputs. */
	bool expected_found;
	u64 expected_pending_start;
	u64 expected_pending_end;
};

static const struct first_pending_test_case first_pending_tests[] = {
	{
		.name = "no pending extent",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.pending_extent = { 0, 0 },
		.expected_found = false,
	},
	{
		.name = "pending extent at search start",
		.hole_start = SZ_1G,
		.hole_len = 9ULL * SZ_1G,
		.pending_extent = { SZ_1G, SZ_1G },
		.expected_found = true,
		.expected_pending_start = SZ_1G,
		.expected_pending_end = SZ_2G - 1,
	},
	{
		.name = "pending extent overlapping search start",
		.hole_start = SZ_1G,
		.hole_len = 9ULL * SZ_1G,
		.pending_extent = { 0, SZ_2G },
		.expected_found = true,
		.expected_pending_start = 0,
		.expected_pending_end = SZ_2G - 1,
	},
	{
		.name = "pending extent inside search range",
		.hole_start = 0,
		.hole_len = 10ULL * SZ_1G,
		.pending_extent = { SZ_2G, SZ_1G },
		.expected_found = true,
		.expected_pending_start = SZ_2G,
		.expected_pending_end = 3ULL * SZ_1G - 1,
	},
	{
		.name = "pending extent outside search range",
		.hole_start = 0,
		.hole_len = SZ_1G,
		.pending_extent = { SZ_2G, SZ_1G },
		.expected_found = false,
	},
	{
		.name = "pending extent overlapping end of search range",
		.hole_start = 0,
		.hole_len = SZ_2G,
		.pending_extent = { SZ_1G, SZ_2G },
		.expected_found = true,
		.expected_pending_start = SZ_1G,
		.expected_pending_end = 3ULL * SZ_1G - 1,
	},
};

static int test_first_pending_extent(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_device *device;
	int ret = 0;

	test_msg("running first_pending_extent tests");

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	device = btrfs_alloc_dummy_device(fs_info);
	if (IS_ERR(device)) {
		test_err("failed to allocate dummy device");
		ret = PTR_ERR(device);
		goto out_free_fs_info;
	}

	device->fs_info = fs_info;

	for (int i = 0; i < ARRAY_SIZE(first_pending_tests); i++) {
		const struct first_pending_test_case *test_case = &first_pending_tests[i];
		u64 start = test_case->pending_extent.start;
		u64 len = test_case->pending_extent.len;
		u64 pending_start, pending_end;
		bool found;

		if (len) {
			btrfs_set_extent_bit(&device->alloc_state,
					     start, start + len - 1,
					     CHUNK_ALLOCATED, NULL);
		}

		mutex_lock(&fs_info->chunk_mutex);
		found = btrfs_first_pending_extent(device, test_case->hole_start,
						   test_case->hole_len,
						   &pending_start, &pending_end);
		mutex_unlock(&fs_info->chunk_mutex);

		if (found != test_case->expected_found) {
			test_err("%s: expected found=%d, got found=%d",
				 test_case->name, test_case->expected_found, found);
			ret = -EINVAL;
			goto out_clear_pending_extents;
		}
		if (!found)
			goto out_clear_pending_extents;

		if (pending_start != test_case->expected_pending_start ||
		    pending_end != test_case->expected_pending_end) {
			test_err("%s: expected pending [%llu, %llu], got [%llu, %llu]",
				 test_case->name,
				 test_case->expected_pending_start,
				 test_case->expected_pending_end,
				 pending_start, pending_end);
			ret = -EINVAL;
			goto out_clear_pending_extents;
		}

out_clear_pending_extents:
		btrfs_clear_extent_bit(&device->alloc_state, 0, (u64)-1,
				       CHUNK_ALLOCATED, NULL);
		if (ret)
			break;
	}

out_free_fs_info:
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

int btrfs_test_chunk_allocation(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("running chunk allocation tests");

	ret = test_first_pending_extent(sectorsize, nodesize);
	if (ret)
		return ret;

	ret = test_find_hole_in_pending(sectorsize, nodesize);
	if (ret)
		return ret;

	return 0;
}
