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

#include "btrfs-tests.h"
#include "../ctree.h"
#include "../btrfs_inode.h"
#include "../disk-io.h"
#include "../extent_io.h"
#include "../volumes.h"

static void insert_extent(struct btrfs_root *root, u64 start, u64 len,
			  u64 ram_bytes, u64 offset, u64 disk_bytenr,
			  u64 disk_len, u32 type, u8 compression, int slot)
{
	struct btrfs_path path;
	struct btrfs_file_extent_item *fi;
	struct extent_buffer *leaf = root->node;
	struct btrfs_key key;
	u32 value_len = sizeof(struct btrfs_file_extent_item);

	if (type == BTRFS_FILE_EXTENT_INLINE)
		value_len += len;
	memset(&path, 0, sizeof(path));

	path.nodes[0] = leaf;
	path.slots[0] = slot;

	key.objectid = BTRFS_FIRST_FREE_OBJECTID;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = start;

	setup_items_for_insert(root, &path, &key, &value_len, value_len,
			       value_len + sizeof(struct btrfs_item), 1);
	fi = btrfs_item_ptr(leaf, slot, struct btrfs_file_extent_item);
	btrfs_set_file_extent_generation(leaf, fi, 1);
	btrfs_set_file_extent_type(leaf, fi, type);
	btrfs_set_file_extent_disk_bytenr(leaf, fi, disk_bytenr);
	btrfs_set_file_extent_disk_num_bytes(leaf, fi, disk_len);
	btrfs_set_file_extent_offset(leaf, fi, offset);
	btrfs_set_file_extent_num_bytes(leaf, fi, len);
	btrfs_set_file_extent_ram_bytes(leaf, fi, ram_bytes);
	btrfs_set_file_extent_compression(leaf, fi, compression);
	btrfs_set_file_extent_encryption(leaf, fi, 0);
	btrfs_set_file_extent_other_encoding(leaf, fi, 0);
}

static void insert_inode_item_key(struct btrfs_root *root)
{
	struct btrfs_path path;
	struct extent_buffer *leaf = root->node;
	struct btrfs_key key;
	u32 value_len = 0;

	memset(&path, 0, sizeof(path));

	path.nodes[0] = leaf;
	path.slots[0] = 0;

	key.objectid = BTRFS_INODE_ITEM_KEY;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	setup_items_for_insert(root, &path, &key, &value_len, value_len,
			       value_len + sizeof(struct btrfs_item), 1);
}

/*
 * Build the most complicated map of extents the earth has ever seen.  We want
 * this so we can test all of the corner cases of btrfs_get_extent.  Here is a
 * diagram of how the extents will look though this may not be possible we still
 * want to make sure everything acts normally (the last number is not inclusive)
 *
 * [0 - 5][5 -  6][6 - 10][10 - 4096][  4096 - 8192 ][8192 - 12288]
 * [hole ][inline][ hole ][ regular ][regular1 split][    hole    ]
 *
 * [ 12288 - 20480][20480 - 24576][  24576 - 28672  ][28672 - 36864][36864 - 45056]
 * [regular1 split][   prealloc1 ][prealloc1 written][   prealloc1 ][ compressed  ]
 *
 * [45056 - 49152][49152-53248][53248-61440][61440-65536][     65536+81920   ]
 * [ compressed1 ][  regular  ][compressed1][  regular  ][ hole but no extent]
 *
 * [81920-86016]
 * [  regular  ]
 */
static void setup_file_extents(struct btrfs_root *root)
{
	int slot = 0;
	u64 disk_bytenr = 1 * 1024 * 1024;
	u64 offset = 0;

	/* First we want a hole */
	insert_extent(root, offset, 5, 5, 0, 0, 0, BTRFS_FILE_EXTENT_REG, 0,
		      slot);
	slot++;
	offset += 5;

	/*
	 * Now we want an inline extent, I don't think this is possible but hey
	 * why not?  Also keep in mind if we have an inline extent it counts as
	 * the whole first page.  If we were to expand it we would have to cow
	 * and we wouldn't have an inline extent anymore.
	 */
	insert_extent(root, offset, 1, 1, 0, 0, 0, BTRFS_FILE_EXTENT_INLINE, 0,
		      slot);
	slot++;
	offset = 4096;

	/* Now another hole */
	insert_extent(root, offset, 4, 4, 0, 0, 0, BTRFS_FILE_EXTENT_REG, 0,
		      slot);
	slot++;
	offset += 4;

	/* Now for a regular extent */
	insert_extent(root, offset, 4095, 4095, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	disk_bytenr += 4096;
	offset += 4095;

	/*
	 * Now for 3 extents that were split from a hole punch so we test
	 * offsets properly.
	 */
	insert_extent(root, offset, 4096, 16384, 0, disk_bytenr, 16384,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 4096, 4096, 0, 0, 0, BTRFS_FILE_EXTENT_REG,
		      0, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 8192, 16384, 8192, disk_bytenr, 16384,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 8192;
	disk_bytenr += 16384;

	/* Now for a unwritten prealloc extent */
	insert_extent(root, offset, 4096, 4096, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += 4096;

	/*
	 * We want to jack up disk_bytenr a little more so the em stuff doesn't
	 * merge our records.
	 */
	disk_bytenr += 8192;

	/*
	 * Now for a partially written prealloc extent, basically the same as
	 * the hole punch example above.  Ram_bytes never changes when you mark
	 * extents written btw.
	 */
	insert_extent(root, offset, 4096, 16384, 0, disk_bytenr, 16384,
		      BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 4096, 16384, 4096, disk_bytenr, 16384,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 8192, 16384, 8192, disk_bytenr, 16384,
		      BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += 8192;
	disk_bytenr += 16384;

	/* Now a normal compressed extent */
	insert_extent(root, offset, 8192, 8192, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += 8192;
	/* No merges */
	disk_bytenr += 8192;

	/* Now a split compressed extent */
	insert_extent(root, offset, 4096, 16384, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 4096, 4096, 0, disk_bytenr + 4096, 4096,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 4096;
	insert_extent(root, offset, 8192, 16384, 8192, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += 8192;
	disk_bytenr += 8192;

	/* Now extents that have a hole but no hole extent */
	insert_extent(root, offset, 4096, 4096, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 16384;
	disk_bytenr += 4096;
	insert_extent(root, offset, 4096, 4096, 0, disk_bytenr, 4096,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
}

static unsigned long prealloc_only = 0;
static unsigned long compressed_only = 0;
static unsigned long vacancy_only = 0;

static noinline int test_btrfs_get_extent(void)
{
	struct inode *inode = NULL;
	struct btrfs_root *root = NULL;
	struct extent_map *em = NULL;
	u64 orig_start;
	u64 disk_bytenr;
	u64 offset;
	int ret = -ENOMEM;

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_msg("Couldn't allocate inode\n");
		return ret;
	}

	BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
	BTRFS_I(inode)->location.objectid = BTRFS_FIRST_FREE_OBJECTID;
	BTRFS_I(inode)->location.offset = 0;

	root = btrfs_alloc_dummy_root();
	if (IS_ERR(root)) {
		test_msg("Couldn't allocate root\n");
		goto out;
	}

	/*
	 * We do this since btrfs_get_extent wants to assign em->bdev to
	 * root->fs_info->fs_devices->latest_bdev.
	 */
	root->fs_info = btrfs_alloc_dummy_fs_info();
	if (!root->fs_info) {
		test_msg("Couldn't allocate dummy fs info\n");
		goto out;
	}

	root->node = alloc_dummy_extent_buffer(NULL, 4096);
	if (!root->node) {
		test_msg("Couldn't allocate dummy buffer\n");
		goto out;
	}

	/*
	 * We will just free a dummy node if it's ref count is 2 so we need an
	 * extra ref so our searches don't accidently release our page.
	 */
	extent_buffer_get(root->node);
	btrfs_set_header_nritems(root->node, 0);
	btrfs_set_header_level(root->node, 0);
	ret = -EINVAL;

	/* First with no extents */
	BTRFS_I(inode)->root = root;
	em = btrfs_get_extent(inode, NULL, 0, 0, 4096, 0);
	if (IS_ERR(em)) {
		em = NULL;
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole, got %llu\n", em->block_start);
		goto out;
	}
	if (!test_bit(EXTENT_FLAG_VACANCY, &em->flags)) {
		test_msg("Vacancy flag wasn't set properly\n");
		goto out;
	}
	free_extent_map(em);
	btrfs_drop_extent_cache(inode, 0, (u64)-1, 0);

	/*
	 * All of the magic numbers are based on the mapping setup in
	 * setup_file_extents, so if you change anything there you need to
	 * update the comment and update the expected values below.
	 */
	setup_file_extents(root);

	em = btrfs_get_extent(inode, NULL, 0, 0, (u64)-1, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != 0 || em->len != 5) {
		test_msg("Unexpected extent wanted start 0 len 5, got start "
			 "%llu len %llu\n", em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_INLINE) {
		test_msg("Expected an inline, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4091) {
		test_msg("Unexpected extent wanted start %llu len 1, got start "
			 "%llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	/*
	 * We don't test anything else for inline since it doesn't get set
	 * unless we have a page for it to write into.  Maybe we should change
	 * this?
	 */
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4) {
		test_msg("Unexpected extent wanted start %llu len 4, got start "
			 "%llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Regular extent */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4095) {
		test_msg("Unexpected extent wanted start %llu len 4095, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* The next 3 are split extents */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 8192) {
		test_msg("Unexpected extent wanted start %llu len 8192, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n",
			 orig_start, em->orig_start);
		goto out;
	}
	disk_bytenr += (em->start - orig_start);
	if (em->block_start != disk_bytenr) {
		test_msg("Wrong block start, want %llu, have %llu\n",
			 disk_bytenr, em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Prealloc extent */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* The next 3 are a half written prealloc extent */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_HOLE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_msg("Unexpected orig offset, wanted %llu, have %llu\n",
			 orig_start, em->orig_start);
		goto out;
	}
	if (em->block_start != (disk_bytenr + (em->start - em->orig_start))) {
		test_msg("Unexpected block start, wanted %llu, have %llu\n",
			 disk_bytenr + (em->start - em->orig_start),
			 em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 8192) {
		test_msg("Unexpected extent wanted start %llu len 8192, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", orig_start,
			 em->orig_start);
		goto out;
	}
	if (em->block_start != (disk_bytenr + (em->start - em->orig_start))) {
		test_msg("Unexpected block start, wanted %llu, have %llu\n",
			 disk_bytenr + (em->start - em->orig_start),
			 em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Now for the compressed extent */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 8192) {
		test_msg("Unexpected extent wanted start %llu len 8192, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n",
			 em->start, em->orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_msg("Unexpected compress type, wanted %d, got %d\n",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Split compressed extent */
	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n",
			 em->start, em->orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_msg("Unexpected compress type, wanted %d, got %d\n",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != disk_bytenr) {
		test_msg("Block start does not match, want %llu got %llu\n",
			 disk_bytenr, em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 8192) {
		test_msg("Unexpected extent wanted start %llu len 8192, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n",
			 em->start, orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_msg("Unexpected compress type, wanted %d, got %d\n",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* A hole between regular extents but no hole extent */
	em = btrfs_get_extent(inode, NULL, 0, offset + 6, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096 * 1024, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole extent, got %llu\n", em->block_start);
		goto out;
	}
	/*
	 * Currently we just return a length that we requested rather than the
	 * length of the actual hole, if this changes we'll have to change this
	 * test.
	 */
	if (em->start != offset || em->len != 12288) {
		test_msg("Unexpected extent wanted start %llu len 12288, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != vacancy_only) {
		test_msg("Unexpected flags set, want %lu have %lu\n",
			 vacancy_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, offset, 4096, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4096) {
		test_msg("Unexpected extent wanted start %llu len 4096, got "
			 "start %llu len %llu\n", offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, want 0 have %lu\n", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_msg("Wrong orig offset, want %llu, have %llu\n", em->start,
			 em->orig_start);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		free_extent_map(em);
	iput(inode);
	btrfs_free_dummy_root(root);
	return ret;
}

static int test_hole_first(void)
{
	struct inode *inode = NULL;
	struct btrfs_root *root = NULL;
	struct extent_map *em = NULL;
	int ret = -ENOMEM;

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_msg("Couldn't allocate inode\n");
		return ret;
	}

	BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
	BTRFS_I(inode)->location.objectid = BTRFS_FIRST_FREE_OBJECTID;
	BTRFS_I(inode)->location.offset = 0;

	root = btrfs_alloc_dummy_root();
	if (IS_ERR(root)) {
		test_msg("Couldn't allocate root\n");
		goto out;
	}

	root->fs_info = btrfs_alloc_dummy_fs_info();
	if (!root->fs_info) {
		test_msg("Couldn't allocate dummy fs info\n");
		goto out;
	}

	root->node = alloc_dummy_extent_buffer(NULL, 4096);
	if (!root->node) {
		test_msg("Couldn't allocate dummy buffer\n");
		goto out;
	}

	extent_buffer_get(root->node);
	btrfs_set_header_nritems(root->node, 0);
	btrfs_set_header_level(root->node, 0);
	BTRFS_I(inode)->root = root;
	ret = -EINVAL;

	/*
	 * Need a blank inode item here just so we don't confuse
	 * btrfs_get_extent.
	 */
	insert_inode_item_key(root);
	insert_extent(root, 4096, 4096, 4096, 0, 4096, 4096,
		      BTRFS_FILE_EXTENT_REG, 0, 1);
	em = btrfs_get_extent(inode, NULL, 0, 0, 8192, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_msg("Expected a hole, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != 0 || em->len != 4096) {
		test_msg("Unexpected extent wanted start 0 len 4096, got start "
			 "%llu len %llu\n", em->start, em->len);
		goto out;
	}
	if (em->flags != vacancy_only) {
		test_msg("Wrong flags, wanted %lu, have %lu\n", vacancy_only,
			 em->flags);
		goto out;
	}
	free_extent_map(em);

	em = btrfs_get_extent(inode, NULL, 0, 4096, 8192, 0);
	if (IS_ERR(em)) {
		test_msg("Got an error when we shouldn't have\n");
		goto out;
	}
	if (em->block_start != 4096) {
		test_msg("Expected a real extent, got %llu\n", em->block_start);
		goto out;
	}
	if (em->start != 4096 || em->len != 4096) {
		test_msg("Unexpected extent wanted start 4096 len 4096, got "
			 "start %llu len %llu\n", em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_msg("Unexpected flags set, wanted 0 got %lu\n",
			 em->flags);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		free_extent_map(em);
	iput(inode);
	btrfs_free_dummy_root(root);
	return ret;
}

int btrfs_test_inodes(void)
{
	int ret;

	set_bit(EXTENT_FLAG_COMPRESSED, &compressed_only);
	set_bit(EXTENT_FLAG_VACANCY, &vacancy_only);
	set_bit(EXTENT_FLAG_PREALLOC, &prealloc_only);

	test_msg("Running btrfs_get_extent tests\n");
	ret = test_btrfs_get_extent();
	if (ret)
		return ret;
	test_msg("Running hole first btrfs_get_extent test\n");
	return test_hole_first();
}
