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
	info[0].alloc_extent.blocknr = 0;
	info[0].alloc_extent.num_blocks = 64;
	/* 0-17 are used (inclusive) */
	info[0].alloc_extent.num_used = 18;

	info[1].blocknr = 16;
	info[1].objectid = 2;
	info[1].tree_root = 64;
	info[1].alloc_extent.blocknr = 64;
	info[1].alloc_extent.num_blocks = 64;
	info[1].alloc_extent.num_used = 1;
	ret = pwrite(fd, info, sizeof(info),
		     CTREE_SUPER_INFO_OFFSET(CTREE_BLOCKSIZE));
	if (ret != sizeof(info))
		return -1;

	/* create leaves for the tree root and extent root */
	memset(&empty_leaf, 0, sizeof(empty_leaf));
	empty_leaf.header.parentid = 1;
	empty_leaf.header.blocknr = 17;
	ret = pwrite(fd, &empty_leaf, sizeof(empty_leaf), 17 * CTREE_BLOCKSIZE);
	if (ret != sizeof(empty_leaf))
		return -1;

	empty_leaf.header.parentid = 2;
	empty_leaf.header.blocknr = 64;
	empty_leaf.header.nritems = 2;
	item.key.objectid = 0;
	item.key.offset = 64;
	item.key.flags = 0;
	item.offset = LEAF_DATA_SIZE - sizeof(struct extent_item);
	item.size = sizeof(struct extent_item);
	extent_item.refs = 1;
	extent_item.owner = 1;
	memcpy(empty_leaf.items, &item, sizeof(item));
	memcpy(empty_leaf.data + item.offset, &extent_item, item.size);
	item.key.objectid = 64;
	item.key.offset = 64;
	item.offset = LEAF_DATA_SIZE - sizeof(struct extent_item) * 2;
	extent_item.owner = 2;
	memcpy(empty_leaf.items + 1, &item, sizeof(item));
	memcpy(empty_leaf.data + item.offset, &extent_item, item.size);
	ret = pwrite(fd, &empty_leaf, sizeof(empty_leaf), 64 * CTREE_BLOCKSIZE);
	if (ret != sizeof(empty_leaf))
		return -1;
	return 0;
}
