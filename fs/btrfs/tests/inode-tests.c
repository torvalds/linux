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
#include "../accessors.h"

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

	/*
	 * Passing a NULL trans handle is fine here, we have a dummy root eb
	 * and the tree is a single node (level 0).
	 */
	btrfs_setup_item_for_insert(NULL, root, &path, &key, value_len);
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

	/*
	 * Passing a NULL trans handle is fine here, we have a dummy root eb
	 * and the tree is a single node (level 0).
	 */
	btrfs_setup_item_for_insert(NULL, root, &path, &key, value_len);
}

/*
 * Build the most complicated map of extents the earth has ever seen.  We want
 * this so we can test all of the corner cases of btrfs_get_extent.  Here is a
 * diagram of how the extents will look though this may not be possible we still
 * want to make sure everything acts normally (the last number is not inclusive)
 *
 * [0  - 6][     6 - 4096     ][ 4096 - 4100][4100 - 8195][8195  -  12291]
 * [inline][hole but no extent][    hole    ][   regular ][regular1 split]
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

	/*
	 * Tree-checker has strict limits on inline extents that they can only
	 * exist at file offset 0, thus we can only have one inline file extent
	 * at most.
	 */
	insert_extent(root, offset, 6, 6, 0, 0, 0, BTRFS_FILE_EXTENT_INLINE, 0,
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
		      disk_bytenr, sectorsize - 1, BTRFS_FILE_EXTENT_REG, 0, slot);
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

static u32 prealloc_only = 0;
static u32 compressed_only = 0;
static u32 vacancy_only = 0;

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
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, sectorsize);
	if (IS_ERR(em)) {
		em = NULL;
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->disk_bytenr);
		goto out;
	}
	btrfs_free_extent_map(em);
	btrfs_drop_extent_map_range(BTRFS_I(inode), 0, (u64)-1, false);

	/*
	 * All of the magic numbers are based on the mapping setup in
	 * setup_file_extents, so if you change anything there you need to
	 * update the comment and update the expected values below.
	 */
	setup_file_extents(root, sectorsize);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, (u64)-1);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_INLINE) {
		test_err("expected an inline, got %llu", em->disk_bytenr);
		goto out;
	}

	/*
	 * For inline extent, we always round up the em to sectorsize, as
	 * they are either:
	 *
	 * a) a hidden hole
	 *    The range will be zeroed at inline extent read time.
	 *
	 * b) a file extent with unaligned bytenr
	 *    Tree checker will reject it.
	 */
	if (em->start != 0 || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start 0 len %u, got start %llu len %llu",
			sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	/*
	 * We don't test anything else for inline since it doesn't get set
	 * unless we have a page for it to write into.  Maybe we should change
	 * this?
	 */
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != 4) {
		test_err(
	"unexpected extent wanted start %llu len 4, got start %llu len %llu",
			offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* Regular extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize - 1) {
		test_err(
	"unexpected extent wanted start %llu len 4095, got start %llu len %llu",
			offset, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* The next 3 are split extents */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
		"unexpected extent start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	disk_bytenr = btrfs_extent_map_block_start(em);
	orig_start = em->start;
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->start - em->offset != orig_start) {
		test_err("wrong offset, em->start=%llu em->offset=%llu orig_start=%llu",
			 em->start, em->offset, orig_start);
		goto out;
	}
	disk_bytenr += (em->start - orig_start);
	if (btrfs_extent_map_block_start(em) != disk_bytenr) {
		test_err("wrong block start, want %llu, have %llu",
			 disk_bytenr, btrfs_extent_map_block_start(em));
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* Prealloc extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %u have %u",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* The next 3 are a half written prealloc extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %u have %u",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	disk_bytenr = btrfs_extent_map_block_start(em);
	orig_start = em->start;
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_HOLE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->start - em->offset != orig_start) {
		test_err("unexpected offset, wanted %llu, have %llu",
			 em->start - orig_start, em->offset);
		goto out;
	}
	if (btrfs_extent_map_block_start(em) != disk_bytenr + em->offset) {
		test_err("unexpected block start, wanted %llu, have %llu",
			 disk_bytenr + em->offset, btrfs_extent_map_block_start(em));
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != prealloc_only) {
		test_err("unexpected flags set, want %u have %u",
			 prealloc_only, em->flags);
		goto out;
	}
	if (em->start - em->offset != orig_start) {
		test_err("wrong offset, em->start=%llu em->offset=%llu orig_start=%llu",
			 em->start, em->offset, orig_start);
		goto out;
	}
	if (btrfs_extent_map_block_start(em) != disk_bytenr + em->offset) {
		test_err("unexpected block start, wanted %llu, have %llu",
			 disk_bytenr + em->offset, btrfs_extent_map_block_start(em));
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* Now for the compressed extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %u have %u",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	if (btrfs_extent_map_compression(em) != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, btrfs_extent_map_compression(em));
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* Split compressed extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %u have %u",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	if (btrfs_extent_map_compression(em) != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, btrfs_extent_map_compression(em));
		goto out;
	}
	disk_bytenr = btrfs_extent_map_block_start(em);
	orig_start = em->start;
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (btrfs_extent_map_block_start(em) != disk_bytenr) {
		test_err("block start does not match, want %llu got %llu",
			 disk_bytenr, btrfs_extent_map_block_start(em));
		goto out;
	}
	if (em->start != offset || em->len != 2 * sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, 2 * sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != compressed_only) {
		test_err("unexpected flags set, want %u have %u",
			 compressed_only, em->flags);
		goto out;
	}
	if (em->start - em->offset != orig_start) {
		test_err("wrong offset, em->start=%llu em->offset=%llu orig_start=%llu",
			 em->start, em->offset, orig_start);
		goto out;
	}
	if (btrfs_extent_map_compression(em) != BTRFS_COMPRESS_ZLIB) {
		test_err("unexpected compress type, wanted %d, got %d",
			 BTRFS_COMPRESS_ZLIB, btrfs_extent_map_compression(em));
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	/* A hole between regular extents but no hole extent */
	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset + 6, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, SZ_4M);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_HOLE) {
		test_err("expected a hole extent, got %llu", em->disk_bytenr);
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
		test_err("unexpected flags set, want %u have %u",
			 vacancy_only, em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong offset, want 0, have %llu", em->offset);
		goto out;
	}
	offset = em->start + em->len;
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, offset, sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr >= EXTENT_MAP_LAST_BYTE) {
		test_err("expected a real extent, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != offset || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %llu len %u, got start %llu len %llu",
			offset, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, want 0 have %u", em->flags);
		goto out;
	}
	if (em->offset != 0) {
		test_err("wrong orig offset, want 0, have %llu", em->offset);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		btrfs_free_extent_map(em);
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
	em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, 2 * sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (em->disk_bytenr != EXTENT_MAP_HOLE) {
		test_err("expected a hole, got %llu", em->disk_bytenr);
		goto out;
	}
	if (em->start != 0 || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start 0 len %u, got start %llu len %llu",
			sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != vacancy_only) {
		test_err("wrong flags, wanted %u, have %u", vacancy_only,
			 em->flags);
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_get_extent(BTRFS_I(inode), NULL, sectorsize, 2 * sectorsize);
	if (IS_ERR(em)) {
		test_err("got an error when we shouldn't have");
		goto out;
	}
	if (btrfs_extent_map_block_start(em) != sectorsize) {
		test_err("expected a real extent, got %llu",
			 btrfs_extent_map_block_start(em));
		goto out;
	}
	if (em->start != sectorsize || em->len != sectorsize) {
		test_err(
	"unexpected extent wanted start %u len %u, got start %llu len %llu",
			sectorsize, sectorsize, em->start, em->len);
		goto out;
	}
	if (em->flags != 0) {
		test_err("unexpected flags set, wanted 0 got %u",
			 em->flags);
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR(em))
		btrfs_free_extent_map(em);
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

	/* [BTRFS_MAX_EXTENT_SIZE] */
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode), 0,
					BTRFS_MAX_EXTENT_SIZE - 1, 0, NULL);
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
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode), BTRFS_MAX_EXTENT_SIZE,
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
	ret = btrfs_clear_extent_bit(&BTRFS_I(inode)->io_tree,
				     BTRFS_MAX_EXTENT_SIZE >> 1,
				     (BTRFS_MAX_EXTENT_SIZE >> 1) + sectorsize - 1,
				     EXTENT_DELALLOC | EXTENT_DELALLOC_NEW, NULL);
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
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode), BTRFS_MAX_EXTENT_SIZE >> 1,
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
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode),
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
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode),
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
	ret = btrfs_clear_extent_bit(&BTRFS_I(inode)->io_tree,
				     BTRFS_MAX_EXTENT_SIZE + sectorsize,
				     BTRFS_MAX_EXTENT_SIZE + 2 * sectorsize - 1,
				     EXTENT_DELALLOC | EXTENT_DELALLOC_NEW, NULL);
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
	ret = btrfs_set_extent_delalloc(BTRFS_I(inode),
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
	ret = btrfs_clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, (u64)-1,
				     EXTENT_DELALLOC | EXTENT_DELALLOC_NEW, NULL);
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
		btrfs_clear_extent_bit(&BTRFS_I(inode)->io_tree, 0, (u64)-1,
				       EXTENT_DELALLOC | EXTENT_DELALLOC_NEW, NULL);
	iput(inode);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

int btrfs_test_inodes(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("running inode tests");

	compressed_only |= EXTENT_FLAG_COMPRESS_ZLIB;
	prealloc_only |= EXTENT_FLAG_PREALLOC;

	ret = test_btrfs_get_extent(sectorsize, nodesize);
	if (ret)
		return ret;
	ret = test_hole_first(sectorsize, nodesize);
	if (ret)
		return ret;
	return test_extent_accounting(sectorsize, nodesize);
}
