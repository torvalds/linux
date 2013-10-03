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

#include <linux/slab.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../free-space-cache.h"

#define BITS_PER_BITMAP		(PAGE_CACHE_SIZE * 8)
static struct btrfs_block_group_cache *init_test_block_group(void)
{
	struct btrfs_block_group_cache *cache;

	cache = kzalloc(sizeof(*cache), GFP_NOFS);
	if (!cache)
		return NULL;
	cache->free_space_ctl = kzalloc(sizeof(*cache->free_space_ctl),
					GFP_NOFS);
	if (!cache->free_space_ctl) {
		kfree(cache);
		return NULL;
	}

	cache->key.objectid = 0;
	cache->key.offset = 1024 * 1024 * 1024;
	cache->key.type = BTRFS_BLOCK_GROUP_ITEM_KEY;
	cache->sectorsize = 4096;

	spin_lock_init(&cache->lock);
	INIT_LIST_HEAD(&cache->list);
	INIT_LIST_HEAD(&cache->cluster_list);
	INIT_LIST_HEAD(&cache->new_bg_list);

	btrfs_init_free_space_ctl(cache);

	return cache;
}

/*
 * This test just does basic sanity checking, making sure we can add an exten
 * entry and remove space from either end and the middle, and make sure we can
 * remove space that covers adjacent extent entries.
 */
static int test_extents(struct btrfs_block_group_cache *cache)
{
	int ret = 0;

	test_msg("Running extent only tests\n");

	/* First just make sure we can remove an entire entry */
	ret = btrfs_add_free_space(cache, 0, 4 * 1024 * 1024);
	if (ret) {
		test_msg("Error adding initial extents %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, 4 * 1024 * 1024);
	if (ret) {
		test_msg("Error removing extent %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, 4 * 1024 * 1024)) {
		test_msg("Full remove left some lingering space\n");
		return -1;
	}

	/* Ok edge and middle cases now */
	ret = btrfs_add_free_space(cache, 0, 4 * 1024 * 1024);
	if (ret) {
		test_msg("Error adding half extent %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 3 * 1024 * 1024, 1 * 1024 * 1024);
	if (ret) {
		test_msg("Error removing tail end %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, 1 * 1024 * 1024);
	if (ret) {
		test_msg("Error removing front end %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 2 * 1024 * 1024, 4096);
	if (ret) {
		test_msg("Error removing middle peice %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, 1 * 1024 * 1024)) {
		test_msg("Still have space at the front\n");
		return -1;
	}

	if (test_check_exists(cache, 2 * 1024 * 1024, 4096)) {
		test_msg("Still have space in the middle\n");
		return -1;
	}

	if (test_check_exists(cache, 3 * 1024 * 1024, 1 * 1024 * 1024)) {
		test_msg("Still have space at the end\n");
		return -1;
	}

	/* Cleanup */
	__btrfs_remove_free_space_cache(cache->free_space_ctl);

	return 0;
}

static int test_bitmaps(struct btrfs_block_group_cache *cache)
{
	u64 next_bitmap_offset;
	int ret;

	test_msg("Running bitmap only tests\n");

	ret = test_add_free_space_entry(cache, 0, 4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't create a bitmap entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, 4 * 1024 * 1024);
	if (ret) {
		test_msg("Error removing bitmap full range %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, 4 * 1024 * 1024)) {
		test_msg("Left some space in bitmap\n");
		return -1;
	}

	ret = test_add_free_space_entry(cache, 0, 4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add to our bitmap entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 1 * 1024 * 1024, 2 * 1024 * 1024);
	if (ret) {
		test_msg("Couldn't remove middle chunk %d\n", ret);
		return ret;
	}

	/*
	 * The first bitmap we have starts at offset 0 so the next one is just
	 * at the end of the first bitmap.
	 */
	next_bitmap_offset = (u64)(BITS_PER_BITMAP * 4096);

	/* Test a bit straddling two bitmaps */
	ret = test_add_free_space_entry(cache, next_bitmap_offset -
				   (2 * 1024 * 1024), 4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add space that straddles two bitmaps %d\n",
				ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, next_bitmap_offset -
				      (1 * 1024 * 1024), 2 * 1024 * 1024);
	if (ret) {
		test_msg("Couldn't remove overlapping space %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, next_bitmap_offset - (1 * 1024 * 1024),
			 2 * 1024 * 1024)) {
		test_msg("Left some space when removing overlapping\n");
		return -1;
	}

	__btrfs_remove_free_space_cache(cache->free_space_ctl);

	return 0;
}

/* This is the high grade jackassery */
static int test_bitmaps_and_extents(struct btrfs_block_group_cache *cache)
{
	u64 bitmap_offset = (u64)(BITS_PER_BITMAP * 4096);
	int ret;

	test_msg("Running bitmap and extent tests\n");

	/*
	 * First let's do something simple, an extent at the same offset as the
	 * bitmap, but the free space completely in the extent and then
	 * completely in the bitmap.
	 */
	ret = test_add_free_space_entry(cache, 4 * 1024 * 1024, 1 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't create bitmap entry %d\n", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, 0, 1 * 1024 * 1024, 0);
	if (ret) {
		test_msg("Couldn't add extent entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 0, 1 * 1024 * 1024);
	if (ret) {
		test_msg("Couldn't remove extent entry %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 0, 1 * 1024 * 1024)) {
		test_msg("Left remnants after our remove\n");
		return -1;
	}

	/* Now to add back the extent entry and remove from the bitmap */
	ret = test_add_free_space_entry(cache, 0, 1 * 1024 * 1024, 0);
	if (ret) {
		test_msg("Couldn't re-add extent entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 4 * 1024 * 1024, 1 * 1024 * 1024);
	if (ret) {
		test_msg("Couldn't remove from bitmap %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 4 * 1024 * 1024, 1 * 1024 * 1024)) {
		test_msg("Left remnants in the bitmap\n");
		return -1;
	}

	/*
	 * Ok so a little more evil, extent entry and bitmap at the same offset,
	 * removing an overlapping chunk.
	 */
	ret = test_add_free_space_entry(cache, 1 * 1024 * 1024, 4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add to a bitmap %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 512 * 1024, 3 * 1024 * 1024);
	if (ret) {
		test_msg("Couldn't remove overlapping space %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 512 * 1024, 3 * 1024 * 1024)) {
		test_msg("Left over peices after removing overlapping\n");
		return -1;
	}

	__btrfs_remove_free_space_cache(cache->free_space_ctl);

	/* Now with the extent entry offset into the bitmap */
	ret = test_add_free_space_entry(cache, 4 * 1024 * 1024, 4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add space to the bitmap %d\n", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, 2 * 1024 * 1024, 2 * 1024 * 1024, 0);
	if (ret) {
		test_msg("Couldn't add extent to the cache %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 3 * 1024 * 1024, 4 * 1024 * 1024);
	if (ret) {
		test_msg("Problem removing overlapping space %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, 3 * 1024 * 1024, 4 * 1024 * 1024)) {
		test_msg("Left something behind when removing space");
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
	__btrfs_remove_free_space_cache(cache->free_space_ctl);
	ret = test_add_free_space_entry(cache, bitmap_offset + 4 * 1024 * 1024,
				   4 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add bitmap %d\n", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, bitmap_offset - 1 * 1024 * 1024,
				   5 * 1024 * 1024, 0);
	if (ret) {
		test_msg("Couldn't add extent entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, bitmap_offset + 1 * 1024 * 1024,
				      5 * 1024 * 1024);
	if (ret) {
		test_msg("Failed to free our space %d\n", ret);
		return ret;
	}

	if (test_check_exists(cache, bitmap_offset + 1 * 1024 * 1024,
			 5 * 1024 * 1024)) {
		test_msg("Left stuff over\n");
		return -1;
	}

	__btrfs_remove_free_space_cache(cache->free_space_ctl);

	/*
	 * This blew up before, we have part of the free space in a bitmap and
	 * then the entirety of the rest of the space in an extent.  This used
	 * to return -EAGAIN back from btrfs_remove_extent, make sure this
	 * doesn't happen.
	 */
	ret = test_add_free_space_entry(cache, 1 * 1024 * 1024, 2 * 1024 * 1024, 1);
	if (ret) {
		test_msg("Couldn't add bitmap entry %d\n", ret);
		return ret;
	}

	ret = test_add_free_space_entry(cache, 3 * 1024 * 1024, 1 * 1024 * 1024, 0);
	if (ret) {
		test_msg("Couldn't add extent entry %d\n", ret);
		return ret;
	}

	ret = btrfs_remove_free_space(cache, 1 * 1024 * 1024, 3 * 1024 * 1024);
	if (ret) {
		test_msg("Error removing bitmap and extent overlapping %d\n", ret);
		return ret;
	}

	__btrfs_remove_free_space_cache(cache->free_space_ctl);
	return 0;
}

int btrfs_test_free_space_cache(void)
{
	struct btrfs_block_group_cache *cache;
	int ret;

	test_msg("Running btrfs free space cache tests\n");

	cache = init_test_block_group();
	if (!cache) {
		test_msg("Couldn't run the tests\n");
		return 0;
	}

	ret = test_extents(cache);
	if (ret)
		goto out;
	ret = test_bitmaps(cache);
	if (ret)
		goto out;
	ret = test_bitmaps_and_extents(cache);
	if (ret)
		goto out;
out:
	__btrfs_remove_free_space_cache(cache->free_space_ctl);
	kfree(cache->free_space_ctl);
	kfree(cache);
	test_msg("Free space cache tests finished\n");
	return ret;
}
