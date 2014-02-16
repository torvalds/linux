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
#include "../extent_io.h"
#include "../disk-io.h"

static int test_btrfs_split_item(void)
{
	struct btrfs_path *path;
	struct btrfs_root *root;
	struct extent_buffer *eb;
	struct btrfs_item *item;
	char *value = "mary had a little lamb";
	char *split1 = "mary had a little";
	char *split2 = " lamb";
	char *split3 = "mary";
	char *split4 = " had a little";
	char buf[32];
	struct btrfs_key key;
	u32 value_len = strlen(value);
	int ret = 0;

	test_msg("Running btrfs_split_item tests\n");

	root = btrfs_alloc_dummy_root();
	if (IS_ERR(root)) {
		test_msg("Could not allocate root\n");
		return PTR_ERR(root);
	}

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Could not allocate path\n");
		kfree(root);
		return -ENOMEM;
	}

	path->nodes[0] = eb = alloc_dummy_extent_buffer(0, 4096);
	if (!eb) {
		test_msg("Could not allocate dummy buffer\n");
		ret = -ENOMEM;
		goto out;
	}
	path->slots[0] = 0;

	key.objectid = 0;
	key.type = BTRFS_EXTENT_CSUM_KEY;
	key.offset = 0;

	setup_items_for_insert(root, path, &key, &value_len, value_len,
			       value_len + sizeof(struct btrfs_item), 1);
	item = btrfs_item_nr(0);
	write_extent_buffer(eb, value, btrfs_item_ptr_offset(eb, 0),
			    value_len);

	key.offset = 3;

	/*
	 * Passing NULL trans here should be safe because we have plenty of
	 * space in this leaf to split the item without having to split the
	 * leaf.
	 */
	ret = btrfs_split_item(NULL, root, path, &key, 17);
	if (ret) {
		test_msg("Split item failed %d\n", ret);
		goto out;
	}

	/*
	 * Read the first slot, it should have the original key and contain only
	 * 'mary had a little'
	 */
	btrfs_item_key_to_cpu(eb, &key, 0);
	if (key.objectid != 0 || key.type != BTRFS_EXTENT_CSUM_KEY ||
	    key.offset != 0) {
		test_msg("Invalid key at slot 0\n");
		ret = -EINVAL;
		goto out;
	}

	item = btrfs_item_nr(0);
	if (btrfs_item_size(eb, item) != strlen(split1)) {
		test_msg("Invalid len in the first split\n");
		ret = -EINVAL;
		goto out;
	}

	read_extent_buffer(eb, buf, btrfs_item_ptr_offset(eb, 0),
			   strlen(split1));
	if (memcmp(buf, split1, strlen(split1))) {
		test_msg("Data in the buffer doesn't match what it should "
			 "in the first split have='%.*s' want '%s'\n",
			 (int)strlen(split1), buf, split1);
		ret = -EINVAL;
		goto out;
	}

	btrfs_item_key_to_cpu(eb, &key, 1);
	if (key.objectid != 0 || key.type != BTRFS_EXTENT_CSUM_KEY ||
	    key.offset != 3) {
		test_msg("Invalid key at slot 1\n");
		ret = -EINVAL;
		goto out;
	}

	item = btrfs_item_nr(1);
	if (btrfs_item_size(eb, item) != strlen(split2)) {
		test_msg("Invalid len in the second split\n");
		ret = -EINVAL;
		goto out;
	}

	read_extent_buffer(eb, buf, btrfs_item_ptr_offset(eb, 1),
			   strlen(split2));
	if (memcmp(buf, split2, strlen(split2))) {
		test_msg("Data in the buffer doesn't match what it should "
			 "in the second split\n");
		ret = -EINVAL;
		goto out;
	}

	key.offset = 1;
	/* Do it again so we test memmoving the other items in the leaf */
	ret = btrfs_split_item(NULL, root, path, &key, 4);
	if (ret) {
		test_msg("Second split item failed %d\n", ret);
		goto out;
	}

	btrfs_item_key_to_cpu(eb, &key, 0);
	if (key.objectid != 0 || key.type != BTRFS_EXTENT_CSUM_KEY ||
	    key.offset != 0) {
		test_msg("Invalid key at slot 0\n");
		ret = -EINVAL;
		goto out;
	}

	item = btrfs_item_nr(0);
	if (btrfs_item_size(eb, item) != strlen(split3)) {
		test_msg("Invalid len in the first split\n");
		ret = -EINVAL;
		goto out;
	}

	read_extent_buffer(eb, buf, btrfs_item_ptr_offset(eb, 0),
			   strlen(split3));
	if (memcmp(buf, split3, strlen(split3))) {
		test_msg("Data in the buffer doesn't match what it should "
			 "in the third split");
		ret = -EINVAL;
		goto out;
	}

	btrfs_item_key_to_cpu(eb, &key, 1);
	if (key.objectid != 0 || key.type != BTRFS_EXTENT_CSUM_KEY ||
	    key.offset != 1) {
		test_msg("Invalid key at slot 1\n");
		ret = -EINVAL;
		goto out;
	}

	item = btrfs_item_nr(1);
	if (btrfs_item_size(eb, item) != strlen(split4)) {
		test_msg("Invalid len in the second split\n");
		ret = -EINVAL;
		goto out;
	}

	read_extent_buffer(eb, buf, btrfs_item_ptr_offset(eb, 1),
			   strlen(split4));
	if (memcmp(buf, split4, strlen(split4))) {
		test_msg("Data in the buffer doesn't match what it should "
			 "in the fourth split\n");
		ret = -EINVAL;
		goto out;
	}

	btrfs_item_key_to_cpu(eb, &key, 2);
	if (key.objectid != 0 || key.type != BTRFS_EXTENT_CSUM_KEY ||
	    key.offset != 3) {
		test_msg("Invalid key at slot 2\n");
		ret = -EINVAL;
		goto out;
	}

	item = btrfs_item_nr(2);
	if (btrfs_item_size(eb, item) != strlen(split2)) {
		test_msg("Invalid len in the second split\n");
		ret = -EINVAL;
		goto out;
	}

	read_extent_buffer(eb, buf, btrfs_item_ptr_offset(eb, 2),
			   strlen(split2));
	if (memcmp(buf, split2, strlen(split2))) {
		test_msg("Data in the buffer doesn't match what it should "
			 "in the last chunk\n");
		ret = -EINVAL;
		goto out;
	}
out:
	btrfs_free_path(path);
	kfree(root);
	return ret;
}

int btrfs_test_extent_buffer_operations(void)
{
	test_msg("Running extent buffer operation tests");
	return test_btrfs_split_item();
}
