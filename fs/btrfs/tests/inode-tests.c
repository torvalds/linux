// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../btrfs_inode.h"
#include "../disk-io.h"
#include "../extent_io.h"
#include "../volumes.h"
#include "../compression.h"

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
 * [0 - 5][5 -  6][     6 - 4096     ][ 4096 - 4100][4100 - 8195][8195 - 12291]
 * [hole ][inline][hole but no extent][  hole   ][   regular ][regular1 split]
 *
 * [12291 - 16387][16387 - 24579][24579 - 28675][ 28675 - 32771][32771 - 36867 ]
 * [    hole    ][regular1 split][   prealloc ][   prealloc1  ][prealloc1 written]
 *
 * [36867 - 45059][45059 - 53251][53251 - 57347][57347 - 61443][61443- 69635]
 * [  prealloc1  ][ compressed  ][ compressed1 ][    regular  ][ compressed1]
 *
 * [69635-73731][   73731 - 86019   ][86019-90115]
 * [  regular  ][ hole but no extent][  regular  ]
 */
static void setup_file_extents(struct btrfs_root *root, u32 sectorsize)
{
	int slot = 0;
	u64 disk_bytenr = SZ_1M;
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
	offset = sectorsize;

	/* Now another hole */
	insert_extent(root, offset, 4, 4, 0, 0, 0, BTRFS_FILE_EXTENT_REG, 0,
		      slot);
	slot++;
	offset += 4;

	/* Now for a regular extent */
	insert_extent(root, offset, sectorsize - 1, sectorsize - 1, 0,
		      disk_bytenr, sectorsize, BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	disk_bytenr += sectorsize;
	offset += sectorsize - 1;

	/*
	 * Now for 3 extents that were split from a hole punch so we test
	 * offsets properly.
	 */
	insert_extent(root, offset, sectorsize, 4 * sectorsize, 0, disk_bytenr,
		      4 * sectorsize, BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, sectorsize, sectorsize, 0, 0, 0,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, 2 * sectorsize, 4 * sectorsize,
		      2 * sectorsize, disk_bytenr, 4 * sectorsize,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 2 * sectorsize;
	disk_bytenr += 4 * sectorsize;

	/* Now for a unwritten prealloc extent */
	insert_extent(root, offset, sectorsize, sectorsize, 0, disk_bytenr,
		sectorsize, BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += sectorsize;

	/*
	 * We want to jack up disk_bytenr a little more so the em stuff doesn't
	 * merge our records.
	 */
	disk_bytenr += 2 * sectorsize;

	/*
	 * Now for a partially written prealloc extent, basically the same as
	 * the hole punch example above.  Ram_bytes never changes when you mark
	 * extents written btw.
	 */
	insert_extent(root, offset, sectorsize, 4 * sectorsize, 0, disk_bytenr,
		      4 * sectorsize, BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, sectorsize, 4 * sectorsize, sectorsize,
		      disk_bytenr, 4 * sectorsize, BTRFS_FILE_EXTENT_REG, 0,
		      slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, 2 * sectorsize, 4 * sectorsize,
		      2 * sectorsize, disk_bytenr, 4 * sectorsize,
		      BTRFS_FILE_EXTENT_PREALLOC, 0, slot);
	slot++;
	offset += 2 * sectorsize;
	disk_bytenr += 4 * sectorsize;

	/* Now a normal compressed extent */
	insert_extent(root, offset, 2 * sectorsize, 2 * sectorsize, 0,
		      disk_bytenr, sectorsize, BTRFS_FILE_EXTENT_REG,
		      BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += 2 * sectorsize;
	/* No merges */
	disk_bytenr += 2 * sectorsize;

	/* Now a split compressed extent */
	insert_extent(root, offset, sectorsize, 4 * sectorsize, 0, disk_bytenr,
		      sectorsize, BTRFS_FILE_EXTENT_REG,
		      BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, sectorsize, sectorsize, 0,
		      disk_bytenr + sectorsize, sectorsize,
		      BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += sectorsize;
	insert_extent(root, offset, 2 * sectorsize, 4 * sectorsize,
		      2 * sectorsize, disk_bytenr, sectorsize,
		      BTRFS_FILE_EXTENT_REG, BTRFS_COMPRESS_ZLIB, slot);
	slot++;
	offset += 2 * sectorsize;
	disk_bytenr += 2 * sectorsize;

	/* Now extents that have a hole but no hole extent */
	insert_extent(root, offset, sectorsize, sectorsize, 0, disk_bytenr,
		      sectorsize, BTRFS_FILE_EXTENT_REG, 0, slot);
	slot++;
	offset += 4 * sectorsize;
	disk_bytenr += sectorsize;
	insert_extent(root, offset, sectorsize, sectorsize, 0, disk_bytenr,
		      sectorsize, BTRFS_FILE_EXTENT_REG, 0, slot);
}

static unsigned long prealloc_only = 0;
static unsigned long compressed_only = 0;
static unsigned long vacancy_only = 0;

static noinline int test_btrfs_get_extent(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct inode *inode = NULL;
	struct btrfs_root *root = NULL;
	struct extent_map *em = NULL;
	u64 orig_start;
	u64 disk_bytenr;
	u64 offset;
	int ret = -ENOMEM;

	test_msg("running btrfs_get_extent tests");

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		return ret;
	}

	inode->i_mode = S_IFREG;
	BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
	BTRFS_I(inode)->location.objectid = BTRFS_FIRST_FREE_OBJECTID;
	BTRFS_I(inode)->location.offset = 0;

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		goto out;
	}

	root->node = alloc_dummy_extent_buffer(fs_info, nodesize);
	if (!root->node) {
		test_std_err(TEST_ALLOC_ROOT);
		goto out;
	}

	btrfs_set_header_nritems(root->node, 0);
	btrfs_set_header_level(root->node, 0);
	ret = -EINVAL;

	/* First with no extents */
	BTRFS_I(inode)->root = root;
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, 0, sectorsize, 0);
	if (IS_ERR(em)) {
		em = NULL;
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->block_start);
		goto out;
	}
	free_extent_map(em);
	btrfs_drop_extent_cache(BTRFS_I(inode), 0, (u64)-1, 0);

	/*
	 * All of the magic numbers are based on the mapping setup in
	 * setup_file_extents, so if you change anything there you need to
	 * update the comment and update the expected values below.
	 */
	setup_file_extents(root, sectorsize);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, 0, (u64)-1, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->block_start);
		goto out;
	}
	if (em->start != 0 || em->len != 5) {
		test_err(
		"unexpected extent wanted start 0 len 5, got start %llu len %llu",
			em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_INLINE) {
		test_err("expected an inline, got %llu", em->block_start);
		goto out;
	}

	if (em->start != offset || em->len != (sectorsize - 5)) {
		test_err(
	"unexpected extent wanted start %llu len 1, got start %llu len %llu",
			offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	/*
	 * We don't test anything else for inline since it doesn't get set
	 * unless we have a page for it to write into.  Maybe we should change
	 * this?
	 */
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 4) {
		test_err(
	"unexpected extent wanted start %llu len 4, got start %llu len %llu",
			offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Regular extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize - 1) {
		test_err(
	"unexpected extent wanted start %llu len 4095, got start %llu len %llu",
			offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* The next 3 are split extents */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
		"unexpected extent start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_err("wrong orig offset, want %llu, have %llu",
			 orig_start, em->orig_start);
		goto out;
	}
	disk_bytenr += (em->start - orig_start);
	if (em->block_start != disk_bytenr) {
		test_err("wrong block start, want %llu, have %llu",
			 disk_bytenr, em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Prealloc extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* The next 3 are a half written prealloc extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_HOLE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_err("unexpected orig offset, wanted %llu, have %llu",
			 orig_start, em->orig_start);
		goto out;
	}
	if (em->block_start != (disk_bytenr + (em->start - em->orig_start))) {
		test_err("unexpected block start, wanted %llu, have %llu",
			 disk_bytenr + (em->start - em->orig_start),
			 em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_err("wrong orig offset, want %llu, have %llu", orig_start,
			 em->orig_start);
		goto out;
	}
	if (em->block_start != (disk_bytenr + (em->start - em->orig_start))) {
		test_err("unexpected block start, wanted %llu, have %llu",
			 disk_bytenr + (em->start - em->orig_start),
			 em->block_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Now for the compressed extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu",
			 em->start, em->orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* Split compressed extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu",
			 em->start, em->orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	disk_bytenr = em->block_start;
	orig_start = em->start;
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != disk_bytenr) {
		test_err("block start does not match, want %llu got %llu",
			 disk_bytenr, em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->orig_start != orig_start) {
		test_err("wrong orig offset, want %llu, have %llu",
			 em->start, orig_start);
		goto out;
	}
	if (em->compress_type != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, em->compress_type);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	/* A hole between regular extents but no hole extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset + 6,
			sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, SZ_4M, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole extent, got %llu", em->block_start);
		goto out;
	}
	/*
	 * Currently we just return a length that we requested rather than the
	 * length of the actual hole, if this changes we'll have to change this
	 * test.
	 */
	if (em->start != offset || em->len != 3 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 3 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != vacancy_only) {
		test_err("unexpected flags set, want %lu have %lu",
			 vacancy_only, em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	offset = em->start + em->len;
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, offset, sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %lu", em->flags);
		goto out;
	}
	if (em->orig_start != em->start) {
		test_err("wrong orig offset, want %llu, have %llu", em->start,
			 em->orig_start);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		free_extent_map(em);
	iput(inode);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

static int test_hole_first(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct inode *inode = NULL;
	struct btrfs_root *root = NULL;
	struct extent_map *em = NULL;
	int ret = -ENOMEM;

	test_msg("running hole first btrfs_get_extent test");

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		return ret;
	}

	BTRFS_I(inode)->location.type = BTRFS_INODE_ITEM_KEY;
	BTRFS_I(inode)->location.objectid = BTRFS_FIRST_FREE_OBJECTID;
	BTRFS_I(inode)->location.offset = 0;

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		goto out;
	}

	root->node = alloc_dummy_extent_buffer(fs_info, nodesize);
	if (!root->node) {
		test_std_err(TEST_ALLOC_ROOT);
		goto out;
	}

	btrfs_set_header_nritems(root->node, 0);
	btrfs_set_header_level(root->node, 0);
	BTRFS_I(inode)->root = root;
	ret = -EINVAL;

	/*
	 * Need a blank inode item here just so we don't confuse
	 * btrfs_get_extent.
	 */
	insert_inode_item_key(root);
	insert_extent(root, sectorsize, sectorsize, sectorsize, 0, sectorsize,
		      sectorsize, BTRFS_FILE_EXTENT_REG, 0, 1);
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, 0, 2 * sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->block_start);
		goto out;
	}
	if (em->start != 0 || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start 0 len %u, got start %llu len %llu",
			sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != vacancy_only) {
		test_err("wrong flags, wanted %lu, have %lu", vacancy_only,
			 em->flags);
		goto out;
	}
	free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, sectorsize,
			2 * sectorsize, 0);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->block_start != sectorsize) {
		test_err("expected a real extent, got %llu", em->block_start);
		goto out;
	}
	if (em->start != sectorsize || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %u len %u, got start %llu len %llu",
			sectorsize, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, wanted 0 got %lu",
			 em->flags);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		free_extent_map(em);
	iput(inode);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

static int test_extent_accounting(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct inode *inode = NULL;
	struct btrfs_root *root = NULL;
	int ret = -ENOMEM;

	test_msg("running outstanding_extents tests");

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		return ret;
	}

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		goto out;
	}

	BTRFS_I(inode)->root = root;
	btrfs_test_inode_set_ops(inode);

	/* [BTRFS_MAX_EXTENT_SIZE] */
	ret = btrfs_set_extent_delalloc(inode, 0, BTRFS_MAX_EXTENT_SIZE - 1, 0,
					NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 1) {
		ret = -EINVAL;
		test_err("miscount, wanted 1, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/* [BTRFS_MAX_EXTENT_SIZE][sectorsize] */
	ret = btrfs_set_extent_delalloc(inode, BTRFS_MAX_EXTENT_SIZE,
					BTRFS_MAX_EXTENT_SIZE + sectorsize - 1,
					0, NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 2) {
		ret = -EINVAL;
		test_err("miscount, wanted 2, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/* [BTRFS_MAX_EXTENT_SIZE/2][sectorsize HOLE][the rest] */
	ret = clear_extent_bit(&BTRFS_I(inode)->io_tree,
			       BTRFS_MAX_EXTENT_SIZE >> 1,
			       (BTRFS_MAX_EXTENT_SIZE >> 1) + sectorsize - 1,
			       EXTENT_DELALLOC | EXTENT_UPTODATE, 0, 0, NULL);
	if (ret) {
		test_err("clear_extent_bit returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 2) {
		ret = -EINVAL;
		test_err("miscount, wanted 2, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/* [BTRFS_MAX_EXTENT_SIZE][sectorsize] */
	ret = btrfs_set_extent_delalloc(inode, BTRFS_MAX_EXTENT_SIZE >> 1,
					(BTRFS_MAX_EXTENT_SIZE >> 1)
					+ sectorsize - 1,
					0, NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 2) {
		ret = -EINVAL;
		test_err("miscount, wanted 2, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/*
	 * [BTRFS_MAX_EXTENT_SIZE+sectorsize][sectorsize HOLE][BTRFS_MAX_EXTENT_SIZE+sectorsize]
	 */
	ret = btrfs_set_extent_delalloc(inode,
			BTRFS_MAX_EXTENT_SIZE + 2 * sectorsize,
			(BTRFS_MAX_EXTENT_SIZE << 1) + 3 * sectorsize - 1,
			0, NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 4) {
		ret = -EINVAL;
		test_err("miscount, wanted 4, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/*
	* [BTRFS_MAX_EXTENT_SIZE+sectorsize][sectorsize][BTRFS_MAX_EXTENT_SIZE+sectorsize]
	*/
	ret = btrfs_set_extent_delalloc(inode,
			BTRFS_MAX_EXTENT_SIZE + sectorsize,
			BTRFS_MAX_EXTENT_SIZE + 2 * sectorsize - 1, 0, NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 3) {
		ret = -EINVAL;
		test_err("miscount, wanted 3, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/* [BTRFS_MAX_EXTENT_SIZE+4k][4K HOLE][BTRFS_MAX_EXTENT_SIZE+4k] */
	ret = clear_extent_bit(&BTRFS_I(inode)->io_tree,
			       BTRFS_MAX_EXTENT_SIZE + sectorsize,
			       BTRFS_MAX_EXTENT_SIZE + 2 * sectorsize - 1,
			       EXTENT_DELALLOC | EXTENT_UPTODATE, 0, 0, NULL);
	if (ret) {
		test_err("clear_extent_bit returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 4) {
		ret = -EINVAL;
		test_err("miscount, wanted 4, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/*
	 * Refill the hole again just for good measure, because I thought it
	 * might fail and I'd rather satisfy my paranoia at this point.
	 */
	ret = btrfs_set_extent_delalloc(inode,
			BTRFS_MAX_EXTENT_SIZE + sectorsize,
			BTRFS_MAX_EXTENT_SIZE + 2 * sectorsize - 1, 0, NULL);
	if (ret) {
		test_err("btrfs_set_extent_delalloc returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents != 3) {
		ret = -EINVAL;
		test_err("miscount, wanted 3, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}

	/* Empty */
	ret = clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, (u64)-1,
			       EXTENT_DELALLOC | EXTENT_UPTODATE, 0, 0, NULL);
	if (ret) {
		test_err("clear_extent_bit returned %d", ret);
		goto out;
	}
	if (BTRFS_I(inode)->outstanding_extents) {
		ret = -EINVAL;
		test_err("miscount, wanted 0, got %u",
			 BTRFS_I(inode)->outstanding_extents);
		goto out;
	}
	ret = 0;
out:
	if (ret)
		clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, (u64)-1,
				 EXTENT_DELALLOC | EXTENT_UPTODATE, 0, 0, NULL);
	iput(inode);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

int btrfs_test_inodes(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("running inode tests");

	set_bit(EXTENT_FLAG_COMPRESSED, &compressed_only);
	set_bit(EXTENT_FLAG_PREALLOC, &prealloc_only);

	ret = test_btrfs_get_extent(sectorsize, nodesize);
	if (ret)
		return ret;
	ret = test_hole_first(sectorsize, nodesize);
	if (ret)
		return ret;
	return test_extent_accounting(sectorsize, nodesize);
}
