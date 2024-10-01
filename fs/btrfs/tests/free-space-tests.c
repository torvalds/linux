// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#include <linux/slab.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../disk-io.h"
#include "../free-space-cache.h"
#include "../block-group.h"

#define BITS_PER_BITMAP		(PAGE_SIZE * 8UL)

/*
 * This test just does basic sanity checking, making sure we can add an extent
 * entry and remove space from either end and the middle, and make sure we can
 * remove space that covers adjacent extent entries.
 */
static int test_extents(struct btrfs_block_group *cache)
{
	int ret = 0;

	test_msg("running extent only tests");

	/* First just make sure we can remove an entire entry */
	ret = btrfs_add_free_space(cache, 0, SZ_4M);
	if (ret) {
		test_err("error adding initial extents %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, SZ_4M);
	if (ret) {
		test_err("error removing extent %d", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, SZ_4M)) {
		test_err("full remove left some lingering space");
		return -1;
	}

	/* Ok edge and middle cases now */
	ret = btrfs_add_free_space(cache, 0, SZ_4M);
	if (ret) {
		test_err("error adding half extent %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 3 * SZ_1M, SZ_1M);
	if (ret) {
		test_err("error removing tail end %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, SZ_1M);
	if (ret) {
		test_err("error removing front end %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, SZ_2M, 4096);
	if (ret) {
		test_err("error removing middle piece %d", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, SZ_1M)) {
		test_err("still have space at the front");
		return -1;
	}

	if (test_check_exists(cache, SZ_2M, 4096)) {
		test_err("still have space in the middle");
		return -1;
	}

	if (test_check_exists(cache, 3 * SZ_1M, SZ_1M)) {
		test_err("still have space at the end");
		return -1;
	}

	/* Cleanup */
	btrfs_remove_free_space_cache(cache);

	return 0;
}

static int test_bitmaps(struct btrfs_block_group *cache, u32 sectorsize)
{
	u64 next_bitmap_offset;
	int ret;

	test_msg("running bitmap only tests");

	ret = test_add_free_space_entry(cache, 0, SZ_4M, 1);
	if (ret) {
		test_err("couldn't create a bitmap entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, SZ_4M);
	if (ret) {
		test_err("error removing bitmap full range %d", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, SZ_4M)) {
		test_err("left some space in bitmap");
		return -1;
	}

	ret = test_add_free_space_entry(cache, 0, SZ_4M, 1);
	if (ret) {
		test_err("couldn't add to our bitmap entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, SZ_1M, SZ_2M);
	if (ret) {
		test_err("couldn't remove middle chunk %d", ret);
		return ret;
	}

	/*
	 * The first bitmap we have starts at offset 0 so the next one is just
	 * at the end of the first bitmap.
	 */
	next_bitmap_offset = (u64)(BITS_PER_BITMAP * sectorsize);

	/* Test a bit straddling two bitmaps */
	ret = test_add_free_space_entry(cache, next_bitmap_offset - SZ_2M,
					SZ_4M, 1);
	if (ret) {
		test_err("couldn't add space that straddles two bitmaps %d",
				ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, next_bitmap_offset - SZ_1M, SZ_2M);
	if (ret) {
		test_err("couldn't remove overlapping space %d", ret);
		return ret;
	}

	if (test_check_exists(cache, next_bitmap_offset - SZ_1M, SZ_2M)) {
		test_err("left some space when removing overlapping");
		return -1;
	}

	btrfs_remove_free_space_cache(cache);

	return 0;
}

/* This is the high grade jackassery */
static int test_bitmaps_and_extents(struct btrfs_block_group *cache,
				    u32 sectorsize)
{
	u64 bitmap_offset = (u64)(BITS_PER_BITMAP * sectorsize);
	int ret;

	test_msg("running bitmap and extent tests");

	/*
	 * First let's do something simple, an extent at the same offset as the
	 * bitmap, but the free space completely in the extent and then
	 * completely in the bitmap.
	 */
	ret = test_add_free_space_entry(cache, SZ_4M, SZ_1M, 1);
	if (ret) {
		test_err("couldn't create bitmap entry %d", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, 0, SZ_1M, 0);
	if (ret) {
		test_err("couldn't add extent entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, SZ_1M);
	if (ret) {
		test_err("couldn't remove extent entry %d", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, SZ_1M)) {
		test_err("left remnants after our remove");
		return -1;
	}

	/* Now to add back the extent entry and remove from the bitmap */
	ret = test_add_free_space_entry(cache, 0, SZ_1M, 0);
	if (ret) {
		test_err("couldn't re-add extent entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, SZ_4M, SZ_1M);
	if (ret) {
		test_err("couldn't remove from bitmap %d", ret);
		return ret;
	}

	if (test_check_exists(cache, SZ_4M, SZ_1M)) {
		test_err("left remnants in the bitmap");
		return -1;
	}

	/*
	 * Ok so a little more evil, extent entry and bitmap at the same offset,
	 * removing an overlapping chunk.
	 */
	ret = test_add_free_space_entry(cache, SZ_1M, SZ_4M, 1);
	if (ret) {
		test_err("couldn't add to a bitmap %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, SZ_512K, 3 * SZ_1M);
	if (ret) {
		test_err("couldn't remove overlapping space %d", ret);
		return ret;
	}

	if (test_check_exists(cache, SZ_512K, 3 * SZ_1M)) {
		test_err("left over pieces after removing overlapping");
		return -1;
	}

	btrfs_remove_free_space_cache(cache);

	/* Now with the extent entry offset into the bitmap */
	ret = test_add_free_space_entry(cache, SZ_4M, SZ_4M, 1);
	if (ret) {
		test_err("couldn't add space to the bitmap %d", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, SZ_2M, SZ_2M, 0);
	if (ret) {
		test_err("couldn't add extent to the cache %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 3 * SZ_1M, SZ_4M);
	if (ret) {
		test_err("problem removing overlapping space %d", ret);
		return ret;
	}

	if (test_check_exists(cache, 3 * SZ_1M, SZ_4M)) {
		test_err("left something behind when removing space");
		return -1;
	}

	/*
	 * This has blown up in the past, the extent entry starts before the
	 * bitmap entry, but we're trying to remove an offset that falls
	 * completely within the bitmap range and is in both the extent entry
	 * and the bitmap entry, looks like this
	 *
	 *   [ extent ]
	 *      [ bitmap ]
	 *        [ del ]
	 */
	btrfs_remove_free_space_cache(cache);
	ret = test_add_free_space_entry(cache, bitmap_offset + SZ_4M, SZ_4M, 1);
	if (ret) {
		test_err("couldn't add bitmap %d", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, bitmap_offset - SZ_1M,
					5 * SZ_1M, 0);
	if (ret) {
		test_err("couldn't add extent entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, bitmap_offset + SZ_1M, 5 * SZ_1M);
	if (ret) {
		test_err("failed to free our space %d", ret);
		return ret;
	}

	if (test_check_exists(cache, bitmap_offset + SZ_1M, 5 * SZ_1M)) {
		test_err("left stuff over");
		return -1;
	}

	btrfs_remove_free_space_cache(cache);

	/*
	 * This blew up before, we have part of the free space in a bitmap and
	 * then the entirety of the rest of the space in an extent.  This used
	 * to return -EAGAIN back from btrfs_remove_extent, make sure this
	 * doesn't happen.
	 */
	ret = test_add_free_space_entry(cache, SZ_1M, SZ_2M, 1);
	if (ret) {
		test_err("couldn't add bitmap entry %d", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, 3 * SZ_1M, SZ_1M, 0);
	if (ret) {
		test_err("couldn't add extent entry %d", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, SZ_1M, 3 * SZ_1M);
	if (ret) {
		test_err("error removing bitmap and extent overlapping %d", ret);
		return ret;
	}

	btrfs_remove_free_space_cache(cache);
	return 0;
}

/* Used by test_steal_space_from_bitmap_to_extent(). */
static bool test_use_bitmap(struct btrfs_free_space_ctl *ctl,
			    struct btrfs_free_space *info)
{
	return ctl->free_extents > 0;
}

/* Used by test_steal_space_from_bitmap_to_extent(). */
static int
check_num_extents_and_bitmaps(const struct btrfs_block_group *cache,
			      const int num_extents,
			      const int num_bitmaps)
{
	if (cache->free_space_ctl->free_extents != num_extents) {
		test_err(
		"incorrect # of extent entries in the cache: %d, expected %d",
			 cache->free_space_ctl->free_extents, num_extents);
		return -EINVAL;
	}
	if (cache->free_space_ctl->total_bitmaps != num_bitmaps) {
		test_err(
		"incorrect # of extent entries in the cache: %d, expected %d",
			 cache->free_space_ctl->total_bitmaps, num_bitmaps);
		return -EINVAL;
	}
	return 0;
}

/* Used by test_steal_space_from_bitmap_to_extent(). */
static int check_cache_empty(struct btrfs_block_group *cache)
{
	u64 offset;
	u64 max_extent_size;

	/*
	 * Now lets confirm that there's absolutely no free space left to
	 * allocate.
	 */
	if (cache->free_space_ctl->free_space != 0) {
		test_err("cache free space is not 0");
		return -EINVAL;
	}

	/* And any allocation request, no matter how small, should fail now. */
	offset = btrfs_find_space_for_alloc(cache, 0, 4096, 0,
					    &max_extent_size);
	if (offset != 0) {
		test_err("space allocation did not fail, returned offset: %llu",
			 offset);
		return -EINVAL;
	}

	/* And no extent nor bitmap entries in the cache anymore. */
	return check_num_extents_and_bitmaps(cache, 0, 0);
}

/*
 * Before we were able to steal free space from a bitmap entry to an extent
 * entry, we could end up with 2 entries representing a contiguous free space.
 * One would be an extent entry and the other a bitmap entry. Since in order
 * to allocate space to a caller we use only 1 entry, we couldn't return that
 * whole range to the caller if it was requested. This forced the caller to
 * either assume ENOSPC or perform several smaller space allocations, which
 * wasn't optimal as they could be spread all over the block group while under
 * concurrency (extra overhead and fragmentation).
 *
 * This stealing approach is beneficial, since we always prefer to allocate
 * from extent entries, both for clustered and non-clustered allocation
 * requests.
 */
static int
test_steal_space_from_bitmap_to_extent(struct btrfs_block_group *cache,
				       u32 sectorsize)
{
	int ret;
	u64 offset;
	u64 max_extent_size;
	const struct btrfs_free_space_op test_free_space_ops = {
		.use_bitmap = test_use_bitmap,
	};
	const struct btrfs_free_space_op *orig_free_space_ops;

	test_msg("running space stealing from bitmap to extent tests");

	/*
	 * For this test, we want to ensure we end up with an extent entry
	 * immediately adjacent to a bitmap entry, where the bitmap starts
	 * at an offset where the extent entry ends. We keep adding and
	 * removing free space to reach into this state, but to get there
	 * we need to reach a point where marking new free space doesn't
	 * result in adding new extent entries or merging the new space
	 * with existing extent entries - the space ends up being marked
	 * in an existing bitmap that covers the new free space range.
	 *
	 * To get there, we need to reach the threshold defined set at
	 * cache->free_space_ctl->extents_thresh, which currently is
	 * 256 extents on a x86_64 system at least, and a few other
	 * conditions (check free_space_cache.c). Instead of making the
	 * test much longer and complicated, use a "use_bitmap" operation
	 * that forces use of bitmaps as soon as we have at least 1
	 * extent entry.
	 */
	orig_free_space_ops = cache->free_space_ctl->op;
	cache->free_space_ctl->op = &test_free_space_ops;

	/*
	 * Extent entry covering free space range [128Mb - 256Kb, 128Mb - 128Kb[
	 */
	ret = test_add_free_space_entry(cache, SZ_128M - SZ_256K, SZ_128K, 0);
	if (ret) {
		test_err("couldn't add extent entry %d", ret);
		return ret;
	}

	/* Bitmap entry covering free space range [128Mb + 512Kb, 256Mb[ */
	ret = test_add_free_space_entry(cache, SZ_128M + SZ_512K,
					SZ_128M - SZ_512K, 1);
	if (ret) {
		test_err("couldn't add bitmap entry %d", ret);
		return ret;
	}

	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * Now make only the first 256Kb of the bitmap marked as free, so that
	 * we end up with only the following ranges marked as free space:
	 *
	 * [128Mb - 256Kb, 128Mb - 128Kb[
	 * [128Mb + 512Kb, 128Mb + 768Kb[
	 */
	ret = btrfs_remove_free_space(cache,
				      SZ_128M + 768 * SZ_1K,
				      SZ_128M - 768 * SZ_1K);
	if (ret) {
		test_err("failed to free part of bitmap space %d", ret);
		return ret;
	}

	/* Confirm that only those 2 ranges are marked as free. */
	if (!test_check_exists(cache, SZ_128M - SZ_256K, SZ_128K)) {
		test_err("free space range missing");
		return -ENOENT;
	}
	if (!test_check_exists(cache, SZ_128M + SZ_512K, SZ_256K)) {
		test_err("free space range missing");
		return -ENOENT;
	}

	/*
	 * Confirm that the bitmap range [128Mb + 768Kb, 256Mb[ isn't marked
	 * as free anymore.
	 */
	if (test_check_exists(cache, SZ_128M + 768 * SZ_1K,
			      SZ_128M - 768 * SZ_1K)) {
		test_err("bitmap region not removed from space cache");
		return -EINVAL;
	}

	/*
	 * Confirm that the region [128Mb + 256Kb, 128Mb + 512Kb[, which is
	 * covered by the bitmap, isn't marked as free.
	 */
	if (test_check_exists(cache, SZ_128M + SZ_256K, SZ_256K)) {
		test_err("invalid bitmap region marked as free");
		return -EINVAL;
	}

	/*
	 * Confirm that the region [128Mb, 128Mb + 256Kb[, which is covered
	 * by the bitmap too, isn't marked as free either.
	 */
	if (test_check_exists(cache, SZ_128M, SZ_256K)) {
		test_err("invalid bitmap region marked as free");
		return -EINVAL;
	}

	/*
	 * Now lets mark the region [128Mb, 128Mb + 512Kb[ as free too. But,
	 * lets make sure the free space cache marks it as free in the bitmap,
	 * and doesn't insert a new extent entry to represent this region.
	 */
	ret = btrfs_add_free_space(cache, SZ_128M, SZ_512K);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}
	/* Confirm the region is marked as free. */
	if (!test_check_exists(cache, SZ_128M, SZ_512K)) {
		test_err("bitmap region not marked as free");
		return -ENOENT;
	}

	/*
	 * Confirm that no new extent entries or bitmap entries were added to
	 * the cache after adding that free space region.
	 */
	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * Now lets add a small free space region to the right of the previous
	 * one, which is not contiguous with it and is part of the bitmap too.
	 * The goal is to test that the bitmap entry space stealing doesn't
	 * steal this space region.
	 */
	ret = btrfs_add_free_space(cache, SZ_128M + SZ_16M, sectorsize);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}

	/*
	 * Confirm that no new extent entries or bitmap entries were added to
	 * the cache after adding that free space region.
	 */
	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * Now mark the region [128Mb - 128Kb, 128Mb[ as free too. This will
	 * expand the range covered by the existing extent entry that represents
	 * the free space [128Mb - 256Kb, 128Mb - 128Kb[.
	 */
	ret = btrfs_add_free_space(cache, SZ_128M - SZ_128K, SZ_128K);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}
	/* Confirm the region is marked as free. */
	if (!test_check_exists(cache, SZ_128M - SZ_128K, SZ_128K)) {
		test_err("extent region not marked as free");
		return -ENOENT;
	}

	/*
	 * Confirm that our extent entry didn't stole all free space from the
	 * bitmap, because of the small 4Kb free space region.
	 */
	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * So now we have the range [128Mb - 256Kb, 128Mb + 768Kb[ as free
	 * space. Without stealing bitmap free space into extent entry space,
	 * we would have all this free space represented by 2 entries in the
	 * cache:
	 *
	 * extent entry covering range: [128Mb - 256Kb, 128Mb[
	 * bitmap entry covering range: [128Mb, 128Mb + 768Kb[
	 *
	 * Attempting to allocate the whole free space (1Mb) would fail, because
	 * we can't allocate from multiple entries.
	 * With the bitmap free space stealing, we get a single extent entry
	 * that represents the 1Mb free space, and therefore we're able to
	 * allocate the whole free space at once.
	 */
	if (!test_check_exists(cache, SZ_128M - SZ_256K, SZ_1M)) {
		test_err("expected region not marked as free");
		return -ENOENT;
	}

	if (cache->free_space_ctl->free_space != (SZ_1M + sectorsize)) {
		test_err("cache free space is not 1Mb + %u", sectorsize);
		return -EINVAL;
	}

	offset = btrfs_find_space_for_alloc(cache,
					    0, SZ_1M, 0,
					    &max_extent_size);
	if (offset != (SZ_128M - SZ_256K)) {
		test_err(
	"failed to allocate 1Mb from space cache, returned offset is: %llu",
			 offset);
		return -EINVAL;
	}

	/*
	 * All that remains is a sectorsize free space region in a bitmap.
	 * Confirm.
	 */
	ret = check_num_extents_and_bitmaps(cache, 1, 1);
	if (ret)
		return ret;

	if (cache->free_space_ctl->free_space != sectorsize) {
		test_err("cache free space is not %u", sectorsize);
		return -EINVAL;
	}

	offset = btrfs_find_space_for_alloc(cache,
					    0, sectorsize, 0,
					    &max_extent_size);
	if (offset != (SZ_128M + SZ_16M)) {
		test_err("failed to allocate %u, returned offset : %llu",
			 sectorsize, offset);
		return -EINVAL;
	}

	ret = check_cache_empty(cache);
	if (ret)
		return ret;

	btrfs_remove_free_space_cache(cache);

	/*
	 * Now test a similar scenario, but where our extent entry is located
	 * to the right of the bitmap entry, so that we can check that stealing
	 * space from a bitmap to the front of an extent entry works.
	 */

	/*
	 * Extent entry covering free space range [128Mb + 128Kb, 128Mb + 256Kb[
	 */
	ret = test_add_free_space_entry(cache, SZ_128M + SZ_128K, SZ_128K, 0);
	if (ret) {
		test_err("couldn't add extent entry %d", ret);
		return ret;
	}

	/* Bitmap entry covering free space range [0, 128Mb - 512Kb[ */
	ret = test_add_free_space_entry(cache, 0, SZ_128M - SZ_512K, 1);
	if (ret) {
		test_err("couldn't add bitmap entry %d", ret);
		return ret;
	}

	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * Now make only the last 256Kb of the bitmap marked as free, so that
	 * we end up with only the following ranges marked as free space:
	 *
	 * [128Mb + 128b, 128Mb + 256Kb[
	 * [128Mb - 768Kb, 128Mb - 512Kb[
	 */
	ret = btrfs_remove_free_space(cache, 0, SZ_128M - 768 * SZ_1K);
	if (ret) {
		test_err("failed to free part of bitmap space %d", ret);
		return ret;
	}

	/* Confirm that only those 2 ranges are marked as free. */
	if (!test_check_exists(cache, SZ_128M + SZ_128K, SZ_128K)) {
		test_err("free space range missing");
		return -ENOENT;
	}
	if (!test_check_exists(cache, SZ_128M - 768 * SZ_1K, SZ_256K)) {
		test_err("free space range missing");
		return -ENOENT;
	}

	/*
	 * Confirm that the bitmap range [0, 128Mb - 768Kb[ isn't marked
	 * as free anymore.
	 */
	if (test_check_exists(cache, 0, SZ_128M - 768 * SZ_1K)) {
		test_err("bitmap region not removed from space cache");
		return -EINVAL;
	}

	/*
	 * Confirm that the region [128Mb - 512Kb, 128Mb[, which is
	 * covered by the bitmap, isn't marked as free.
	 */
	if (test_check_exists(cache, SZ_128M - SZ_512K, SZ_512K)) {
		test_err("invalid bitmap region marked as free");
		return -EINVAL;
	}

	/*
	 * Now lets mark the region [128Mb - 512Kb, 128Mb[ as free too. But,
	 * lets make sure the free space cache marks it as free in the bitmap,
	 * and doesn't insert a new extent entry to represent this region.
	 */
	ret = btrfs_add_free_space(cache, SZ_128M - SZ_512K, SZ_512K);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}
	/* Confirm the region is marked as free. */
	if (!test_check_exists(cache, SZ_128M - SZ_512K, SZ_512K)) {
		test_err("bitmap region not marked as free");
		return -ENOENT;
	}

	/*
	 * Confirm that no new extent entries or bitmap entries were added to
	 * the cache after adding that free space region.
	 */
	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * Now lets add a small free space region to the left of the previous
	 * one, which is not contiguous with it and is part of the bitmap too.
	 * The goal is to test that the bitmap entry space stealing doesn't
	 * steal this space region.
	 */
	ret = btrfs_add_free_space(cache, SZ_32M, 2 * sectorsize);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}

	/*
	 * Now mark the region [128Mb, 128Mb + 128Kb[ as free too. This will
	 * expand the range covered by the existing extent entry that represents
	 * the free space [128Mb + 128Kb, 128Mb + 256Kb[.
	 */
	ret = btrfs_add_free_space(cache, SZ_128M, SZ_128K);
	if (ret) {
		test_err("error adding free space: %d", ret);
		return ret;
	}
	/* Confirm the region is marked as free. */
	if (!test_check_exists(cache, SZ_128M, SZ_128K)) {
		test_err("extent region not marked as free");
		return -ENOENT;
	}

	/*
	 * Confirm that our extent entry didn't stole all free space from the
	 * bitmap, because of the small 2 * sectorsize free space region.
	 */
	ret = check_num_extents_and_bitmaps(cache, 2, 1);
	if (ret)
		return ret;

	/*
	 * So now we have the range [128Mb - 768Kb, 128Mb + 256Kb[ as free
	 * space. Without stealing bitmap free space into extent entry space,
	 * we would have all this free space represented by 2 entries in the
	 * cache:
	 *
	 * extent entry covering range: [128Mb, 128Mb + 256Kb[
	 * bitmap entry covering range: [128Mb - 768Kb, 128Mb[
	 *
	 * Attempting to allocate the whole free space (1Mb) would fail, because
	 * we can't allocate from multiple entries.
	 * With the bitmap free space stealing, we get a single extent entry
	 * that represents the 1Mb free space, and therefore we're able to
	 * allocate the whole free space at once.
	 */
	if (!test_check_exists(cache, SZ_128M - 768 * SZ_1K, SZ_1M)) {
		test_err("expected region not marked as free");
		return -ENOENT;
	}

	if (cache->free_space_ctl->free_space != (SZ_1M + 2 * sectorsize)) {
		test_err("cache free space is not 1Mb + %u", 2 * sectorsize);
		return -EINVAL;
	}

	offset = btrfs_find_space_for_alloc(cache, 0, SZ_1M, 0,
					    &max_extent_size);
	if (offset != (SZ_128M - 768 * SZ_1K)) {
		test_err(
	"failed to allocate 1Mb from space cache, returned offset is: %llu",
			 offset);
		return -EINVAL;
	}

	/*
	 * All that remains is 2 * sectorsize free space region
	 * in a bitmap. Confirm.
	 */
	ret = check_num_extents_and_bitmaps(cache, 1, 1);
	if (ret)
		return ret;

	if (cache->free_space_ctl->free_space != 2 * sectorsize) {
		test_err("cache free space is not %u", 2 * sectorsize);
		return -EINVAL;
	}

	offset = btrfs_find_space_for_alloc(cache,
					    0, 2 * sectorsize, 0,
					    &max_extent_size);
	if (offset != SZ_32M) {
		test_err("failed to allocate %u, offset: %llu",
			 2 * sectorsize, offset);
		return -EINVAL;
	}

	ret = check_cache_empty(cache);
	if (ret)
		return ret;

	cache->free_space_ctl->op = orig_free_space_ops;
	btrfs_remove_free_space_cache(cache);

	return 0;
}

static bool bytes_index_use_bitmap(struct btrfs_free_space_ctl *ctl,
				   struct btrfs_free_space *info)
{
	return true;
}

static int test_bytes_index(struct btrfs_block_group *cache, u32 sectorsize)
{
	const struct btrfs_free_space_op test_free_space_ops = {
		.use_bitmap = bytes_index_use_bitmap,
	};
	const struct btrfs_free_space_op *orig_free_space_ops;
	struct btrfs_free_space_ctl *ctl = cache->free_space_ctl;
	struct btrfs_free_space *entry;
	struct rb_node *node;
	u64 offset, max_extent_size, bytes;
	int ret, i;

	test_msg("running bytes index tests");

	/* First just validate that it does everything in order. */
	offset = 0;
	for (i = 0; i < 10; i++) {
		bytes = (i + 1) * SZ_1M;
		ret = test_add_free_space_entry(cache, offset, bytes, 0);
		if (ret) {
			test_err("couldn't add extent entry %d\n", ret);
			return ret;
		}
		offset += bytes + sectorsize;
	}

	for (node = rb_first_cached(&ctl->free_space_bytes), i = 9; node;
	     node = rb_next(node), i--) {
		entry = rb_entry(node, struct btrfs_free_space, bytes_index);
		bytes = (i + 1) * SZ_1M;
		if (entry->bytes != bytes) {
			test_err("invalid bytes index order, found %llu expected %llu",
				 entry->bytes, bytes);
			return -EINVAL;
		}
	}

	/* Now validate bitmaps do the correct thing. */
	btrfs_remove_free_space_cache(cache);
	for (i = 0; i < 2; i++) {
		offset = i * BITS_PER_BITMAP * sectorsize;
		bytes = (i + 1) * SZ_1M;
		ret = test_add_free_space_entry(cache, offset, bytes, 1);
		if (ret) {
			test_err("couldn't add bitmap entry");
			return ret;
		}
	}

	for (node = rb_first_cached(&ctl->free_space_bytes), i = 1; node;
	     node = rb_next(node), i--) {
		entry = rb_entry(node, struct btrfs_free_space, bytes_index);
		bytes = (i + 1) * SZ_1M;
		if (entry->bytes != bytes) {
			test_err("invalid bytes index order, found %llu expected %llu",
				 entry->bytes, bytes);
			return -EINVAL;
		}
	}

	/* Now validate bitmaps with different ->max_extent_size. */
	btrfs_remove_free_space_cache(cache);
	orig_free_space_ops = cache->free_space_ctl->op;
	cache->free_space_ctl->op = &test_free_space_ops;

	ret = test_add_free_space_entry(cache, 0, sectorsize, 1);
	if (ret) {
		test_err("couldn't add bitmap entry");
		return ret;
	}

	offset = BITS_PER_BITMAP * sectorsize;
	ret = test_add_free_space_entry(cache, offset, sectorsize, 1);
	if (ret) {
		test_err("couldn't add bitmap_entry");
		return ret;
	}

	/*
	 * Now set a bunch of sectorsize extents in the first entry so it's
	 * ->bytes is large.
	 */
	for (i = 2; i < 20; i += 2) {
		offset = sectorsize * i;
		ret = btrfs_add_free_space(cache, offset, sectorsize);
		if (ret) {
			test_err("error populating sparse bitmap %d", ret);
			return ret;
		}
	}

	/*
	 * Now set a contiguous extent in the second bitmap so its
	 * ->max_extent_size is larger than the first bitmaps.
	 */
	offset = (BITS_PER_BITMAP * sectorsize) + sectorsize;
	ret = btrfs_add_free_space(cache, offset, sectorsize);
	if (ret) {
		test_err("error adding contiguous extent %d", ret);
		return ret;
	}

	/*
	 * Since we don't set ->max_extent_size unless we search everything
	 * should be indexed on bytes.
	 */
	entry = rb_entry(rb_first_cached(&ctl->free_space_bytes),
			 struct btrfs_free_space, bytes_index);
	if (entry->bytes != (10 * sectorsize)) {
		test_err("error, wrong entry in the first slot in bytes_index");
		return -EINVAL;
	}

	max_extent_size = 0;
	offset = btrfs_find_space_for_alloc(cache, cache->start, sectorsize * 3,
					    0, &max_extent_size);
	if (offset != 0) {
		test_err("found space to alloc even though we don't have enough space");
		return -EINVAL;
	}

	if (max_extent_size != (2 * sectorsize)) {
		test_err("got the wrong max_extent size %llu expected %llu",
			 max_extent_size, (unsigned long long)(2 * sectorsize));
		return -EINVAL;
	}

	/*
	 * The search should have re-arranged the bytes index to use the
	 * ->max_extent_size, validate it's now what we expect it to be.
	 */
	entry = rb_entry(rb_first_cached(&ctl->free_space_bytes),
			 struct btrfs_free_space, bytes_index);
	if (entry->bytes != (2 * sectorsize)) {
		test_err("error, the bytes index wasn't recalculated properly");
		return -EINVAL;
	}

	/* Add another sectorsize to re-arrange the tree back to ->bytes. */
	offset = (BITS_PER_BITMAP * sectorsize) - sectorsize;
	ret = btrfs_add_free_space(cache, offset, sectorsize);
	if (ret) {
		test_err("error adding extent to the sparse entry %d", ret);
		return ret;
	}

	entry = rb_entry(rb_first_cached(&ctl->free_space_bytes),
			 struct btrfs_free_space, bytes_index);
	if (entry->bytes != (11 * sectorsize)) {
		test_err("error, wrong entry in the first slot in bytes_index");
		return -EINVAL;
	}

	/*
	 * Now make sure we find our correct entry after searching that will
	 * result in a re-arranging of the tree.
	 */
	max_extent_size = 0;
	offset = btrfs_find_space_for_alloc(cache, cache->start, sectorsize * 2,
					    0, &max_extent_size);
	if (offset != (BITS_PER_BITMAP * sectorsize)) {
		test_err("error, found %llu instead of %llu for our alloc",
			 offset,
			 (unsigned long long)(BITS_PER_BITMAP * sectorsize));
		return -EINVAL;
	}

	cache->free_space_ctl->op = orig_free_space_ops;
	btrfs_remove_free_space_cache(cache);
	return 0;
}

int btrfs_test_free_space_cache(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_block_group *cache;
	struct btrfs_root *root = NULL;
	int ret = -ENOMEM;

	test_msg("running btrfs free space cache tests");
	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	/*
	 * For ppc64 (with 64k page size), bytes per bitmap might be
	 * larger than 1G.  To make bitmap test available in ppc64,
	 * alloc dummy block group whose size cross bitmaps.
	 */
	cache = btrfs_alloc_dummy_block_group(fs_info,
				      BITS_PER_BITMAP * sectorsize + PAGE_SIZE);
	if (!cache) {
		test_std_err(TEST_ALLOC_BLOCK_GROUP);
		btrfs_free_dummy_fs_info(fs_info);
		return 0;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(root);
		goto out;
	}

	root->root_key.objectid = BTRFS_EXTENT_TREE_OBJECTID;
	root->root_key.type = BTRFS_ROOT_ITEM_KEY;
	root->root_key.offset = 0;
	btrfs_global_root_insert(root);

	ret = test_extents(cache);
	if (ret)
		goto out;
	ret = test_bitmaps(cache, sectorsize);
	if (ret)
		goto out;
	ret = test_bitmaps_and_extents(cache, sectorsize);
	if (ret)
		goto out;

	ret = test_steal_space_from_bitmap_to_extent(cache, sectorsize);
	if (ret)
		goto out;
	ret = test_bytes_index(cache, sectorsize);
out:
	btrfs_free_dummy_block_group(cache);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}
