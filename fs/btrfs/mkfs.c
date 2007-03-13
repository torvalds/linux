#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "kerncompat.h"
#include "radix-tree.h"
#include "ctree.h"
#include "disk-io.h"

int mkfs(int fd)
{
	struct ctree_root_info info[2];
	struct leaf empty_leaf;
	struct btrfs_item item;
	struct extent_item extent_item;
	int ret;

	/* setup the super block area */
	memset(info, 0, sizeof(info));
	info[0].blocknr = 16;
	info[0].objectid = 1;
	info[0].tree_root = 17;

	info[1].blocknr = 16;
	info[1].objectid = 2;
	info[1].tree_root = 18;
	ret = pwrite(fd, info, sizeof(info),
		     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
	if (ret != sizeof(info))
		return -1;

	/* create leaves for the tree root and extent root */
	memset(&empty_leaf, 0, sizeof(empty_leaf));
	btrfs_set_header_parentid(&empty_leaf.header, 1);
	btrfs_set_header_blocknr(&empty_leaf.header, 17);
	ret = pwrite(fd, &empty_leaf, sizeof(empty_leaf), 17 * CTREE_BLOCKSIZE);
	if (ret != sizeof(empty_leaf))
		return -1;

	btrfs_set_header_parentid(&empty_leaf.header, 2);
	btrfs_set_header_blocknr(&empty_leaf.header, 18);
	btrfs_set_header_nritems(&empty_leaf.header, 3);

	/* item1, reserve blocks 0-16 */
	btrfs_set_key_objectid(&item.key, 0);
	btrfs_set_key_offset(&item.key, 17);
	btrfs_set_key_flags(&item.key, 0);
	btrfs_set_item_offset(&item,
			      LEAF_DATA_SIZE - sizeof(struct extent_item));
	btrfs_set_item_size(&item, sizeof(struct extent_item));
	extent_item.refs = 1;
	extent_item.owner = 0;
	memcpy(empty_leaf.items, &item, sizeof(item));
	memcpy(empty_leaf.data + btrfs_item_offset(&item), &extent_item,
		btrfs_item_size(&item));

	/* item2, give block 17 to the root */
	btrfs_set_key_objectid(&item.key, 17);
	btrfs_set_key_offset(&item.key, 1);
	btrfs_set_item_offset(&item,
			      LEAF_DATA_SIZE - sizeof(struct extent_item) * 2);
	extent_item.owner = 1;
	memcpy(empty_leaf.items + 1, &item, sizeof(item));
	memcpy(empty_leaf.data + btrfs_item_offset(&item), &extent_item,
		btrfs_item_size(&item));

	/* item3, give block 18 for the extent root */
	btrfs_set_key_objectid(&item.key, 18);
	btrfs_set_key_offset(&item.key, 1);
	btrfs_set_item_offset(&item,
			      LEAF_DATA_SIZE - sizeof(struct extent_item) * 3);
	extent_item.owner = 2;
	memcpy(empty_leaf.items + 2, &item, sizeof(item));
	memcpy(empty_leaf.data + btrfs_item_offset(&item), &extent_item,
		btrfs_item_size(&item));
	ret = pwrite(fd, &empty_leaf, sizeof(empty_leaf), 18 * CTREE_BLOCKSIZE);
	if (ret != sizeof(empty_leaf))
		return -1;
	return 0;
}
