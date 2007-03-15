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

int mkfs(int fd, u64 num_blocks, u32 blocksize)
{
	struct btrfs_super_block super;
	struct btrfs_leaf *empty_leaf;
	struct btrfs_root_item root_item;
	struct btrfs_item item;
	struct btrfs_extent_item extent_item;
	char *block;
	int ret;
	u32 itemoff;
	u32 start_block = BTRFS_SUPER_INFO_OFFSET / blocksize;

	btrfs_set_super_blocknr(&super, start_block);
	btrfs_set_super_root(&super, start_block + 1);
	strcpy((char *)(&super.magic), BTRFS_MAGIC);
	btrfs_set_super_blocksize(&super, blocksize);
	btrfs_set_super_total_blocks(&super, num_blocks);
	btrfs_set_super_blocks_used(&super, 0);

	block = malloc(blocksize);
	memset(block, 0, blocksize);
	BUG_ON(sizeof(super) > blocksize);
	memcpy(block, &super, sizeof(super));
	ret = pwrite(fd, block, blocksize, BTRFS_SUPER_INFO_OFFSET);
	BUG_ON(ret != blocksize);

	/* create the tree of root objects */
	empty_leaf = malloc(blocksize);
	memset(empty_leaf, 0, blocksize);
	btrfs_set_header_parentid(&empty_leaf->header,
				  BTRFS_ROOT_TREE_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 1);
	btrfs_set_header_nritems(&empty_leaf->header, 2);

	/* create the items for the root tree */
	btrfs_set_root_blocknr(&root_item, start_block + 2);
	btrfs_set_root_refs(&root_item, 1);
	itemoff = __BTRFS_LEAF_DATA_SIZE(blocksize) - sizeof(root_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_item_size(&item, sizeof(root_item));
	btrfs_set_disk_key_objectid(&item.key, BTRFS_EXTENT_TREE_OBJECTID);
	btrfs_set_disk_key_offset(&item.key, 0);
	btrfs_set_disk_key_flags(&item.key, 0);
	btrfs_set_disk_key_type(&item.key, BTRFS_ROOT_ITEM_KEY);
	memcpy(empty_leaf->items, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + itemoff,
		&root_item, sizeof(root_item));

	btrfs_set_root_blocknr(&root_item, start_block + 3);
	itemoff = itemoff - sizeof(root_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_disk_key_objectid(&item.key, BTRFS_FS_TREE_OBJECTID);
	memcpy(empty_leaf->items + 1, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + itemoff,
		&root_item, sizeof(root_item));
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 1) * blocksize);

	/* create the items for the extent tree */
	btrfs_set_header_parentid(&empty_leaf->header,
				  BTRFS_EXTENT_TREE_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 2);
	btrfs_set_header_nritems(&empty_leaf->header, 4);

	/* item1, reserve blocks 0-16 */
	btrfs_set_disk_key_objectid(&item.key, 0);
	btrfs_set_disk_key_offset(&item.key, start_block + 1);
	btrfs_set_disk_key_flags(&item.key, 0);
	btrfs_set_disk_key_type(&item.key, BTRFS_EXTENT_ITEM_KEY);
	itemoff = __BTRFS_LEAF_DATA_SIZE(blocksize) -
			sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_item_size(&item, sizeof(struct btrfs_extent_item));
	btrfs_set_extent_refs(&extent_item, 1);
	btrfs_set_extent_owner(&extent_item, 0);
	memcpy(empty_leaf->items, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));

	/* item2, give block 17 to the root */
	btrfs_set_disk_key_objectid(&item.key, start_block + 1);
	btrfs_set_disk_key_offset(&item.key, 1);
	itemoff = itemoff - sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_extent_owner(&extent_item, BTRFS_ROOT_TREE_OBJECTID);
	memcpy(empty_leaf->items + 1, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));

	/* item3, give block 18 to the extent root */
	btrfs_set_disk_key_objectid(&item.key, start_block + 2);
	btrfs_set_disk_key_offset(&item.key, 1);
	itemoff = itemoff - sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_extent_owner(&extent_item, BTRFS_EXTENT_TREE_OBJECTID);
	memcpy(empty_leaf->items + 2, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));

	/* item4, give block 19 to the FS root */
	btrfs_set_disk_key_objectid(&item.key, start_block + 3);
	btrfs_set_disk_key_offset(&item.key, 1);
	itemoff = itemoff - sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_extent_owner(&extent_item, BTRFS_FS_TREE_OBJECTID);
	memcpy(empty_leaf->items + 3, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 2) * blocksize);
	if (ret != blocksize)
		return -1;

	/* finally create the FS root */
	btrfs_set_header_parentid(&empty_leaf->header, BTRFS_FS_TREE_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 3);
	btrfs_set_header_nritems(&empty_leaf->header, 0);
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 3) * blocksize);
	if (ret != blocksize)
		return -1;
	return 0;
}
