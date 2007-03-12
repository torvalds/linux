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
	struct item item;
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
	item.key.objectid = 0;
	item.key.offset = 17;
	item.key.flags = 0;
	item.offset = LEAF_DATA_SIZE - sizeof(struct extent_item);
	item.size = sizeof(struct extent_item);
	extent_item.refs = 1;
	extent_item.owner = 0;
	memcpy(empty_leaf.items, &item, sizeof(item));
	memcpy(empty_leaf.data + item.offset, &extent_item, item.size);

	/* item2, give block 17 to the root */
	item.key.objectid = 17;
	item.key.offset = 1;
	item.offset = LEAF_DATA_SIZE - sizeof(struct extent_item) * 2;
	extent_item.owner = 1;
	memcpy(empty_leaf.items + 1, &item, sizeof(item));
	memcpy(empty_leaf.data + item.offset, &extent_item, item.size);

	/* item3, give block 18 for the extent root */
	item.key.objectid = 18;
	item.key.offset = 1;
	item.offset = LEAF_DATA_SIZE - sizeof(struct extent_item) * 3;
	extent_item.owner = 2;
	memcpy(empty_leaf.items + 2, &item, sizeof(item));
	memcpy(empty_leaf.data + item.offset, &extent_item, item.size);
	ret = pwrite(fd, &empty_leaf, sizeof(empty_leaf), 18 * CTREE_BLOCKSIZE);
	if (ret != sizeof(empty_leaf))
		return -1;
	return 0;
}
