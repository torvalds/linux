#define _XOPEN_SOURCE 500
#ifndef __CHECKER__
#include <sys/ioctl.h>
#include <sys/mount.h>
#endif
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

#ifdef __CHECKER__
#define BLKGETSIZE64 0
static inline int ioctl(int fd, int define, u64 *size) { return 0; }
#endif

#if 0
#if defined(__linux__) && defined(_IOR) && !defined(BLKGETSIZE64)
#   define BLKGETSIZE64 _IOR(0x12, 114, __u64)
#endif
#endif

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
	btrfs_set_super_blocks_used(&super, start_block + 5);

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
	btrfs_set_header_nritems(&empty_leaf->header, 3);

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
	btrfs_set_disk_key_objectid(&item.key, BTRFS_INODE_MAP_OBJECTID);
	memcpy(empty_leaf->items + 1, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + itemoff,
		&root_item, sizeof(root_item));

	btrfs_set_root_blocknr(&root_item, start_block + 4);
	itemoff = itemoff - sizeof(root_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_disk_key_objectid(&item.key, BTRFS_FS_TREE_OBJECTID);
	memcpy(empty_leaf->items + 2, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + itemoff,
		&root_item, sizeof(root_item));
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 1) * blocksize);

	/* create the items for the extent tree */
	btrfs_set_header_parentid(&empty_leaf->header,
				  BTRFS_EXTENT_TREE_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 2);
	btrfs_set_header_nritems(&empty_leaf->header, 5);

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

	/* item4, give block 19 to the inode map */
	btrfs_set_disk_key_objectid(&item.key, start_block + 3);
	btrfs_set_disk_key_offset(&item.key, 1);
	itemoff = itemoff - sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_extent_owner(&extent_item, BTRFS_INODE_MAP_OBJECTID);
	memcpy(empty_leaf->items + 3, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 2) * blocksize);
	if (ret != blocksize)
		return -1;

	/* item5, give block 20 to the FS root */
	btrfs_set_disk_key_objectid(&item.key, start_block + 4);
	btrfs_set_disk_key_offset(&item.key, 1);
	itemoff = itemoff - sizeof(struct btrfs_extent_item);
	btrfs_set_item_offset(&item, itemoff);
	btrfs_set_extent_owner(&extent_item, BTRFS_FS_TREE_OBJECTID);
	memcpy(empty_leaf->items + 4, &item, sizeof(item));
	memcpy(btrfs_leaf_data(empty_leaf) + btrfs_item_offset(&item),
		&extent_item, btrfs_item_size(&item));
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 2) * blocksize);
	if (ret != blocksize)
		return -1;

	/* create the inode map */
	btrfs_set_header_parentid(&empty_leaf->header,
				  BTRFS_INODE_MAP_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 3);
	btrfs_set_header_nritems(&empty_leaf->header, 0);
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 3) * blocksize);
	if (ret != blocksize)
		return -1;

	/* finally create the FS root */
	btrfs_set_header_parentid(&empty_leaf->header, BTRFS_FS_TREE_OBJECTID);
	btrfs_set_header_blocknr(&empty_leaf->header, start_block + 4);
	btrfs_set_header_nritems(&empty_leaf->header, 0);
	ret = pwrite(fd, empty_leaf, blocksize, (start_block + 4) * blocksize);
	if (ret != blocksize)
		return -1;
	return 0;
}

u64 device_size(int fd, struct stat *st)
{
	u64 size;
	if (S_ISREG(st->st_mode)) {
		return st->st_size;
	}
	if (!S_ISBLK(st->st_mode)) {
		return 0;
	}
	if (ioctl(fd, BLKGETSIZE64, &size) >= 0) {
		return size;
	}
	return 0;
}

int main(int ac, char **av)
{
	char *file;
	u64 block_count = 0;
	int fd;
	struct stat st;
	int ret;
	int i;
	char *buf = malloc(4096);
	if (ac >= 2) {
		file = av[1];
		if (ac == 3) {
			block_count = atoi(av[2]);
			if (!block_count) {
				fprintf(stderr, "error finding block count\n");
				exit(1);
			}
		}
	} else {
		fprintf(stderr, "usage: mkfs.btrfs file [block count]\n");
		exit(1);
	}
	fd = open(file, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "unable to open %s\n", file);
		exit(1);
	}
	ret = fstat(fd, &st);
	if (ret < 0) {
		fprintf(stderr, "unable to stat %s\n", file);
		exit(1);
	}
	if (block_count == 0) {
		block_count = device_size(fd, &st);
		if (block_count == 0) {
			fprintf(stderr, "unable to find %s size\n", file);
			exit(1);
		}
	}
	block_count /= 4096;
	if (block_count < 256) {
		fprintf(stderr, "device %s is too small\n", file);
		exit(1);
	}
	memset(buf, 0, 4096);
	for(i = 0; i < 6; i++) {
		ret = write(fd, buf, 4096);
		if (ret != 4096) {
			fprintf(stderr, "unable to zero fill device\n");
			exit(1);
		}
	}
	ret = mkfs(fd, block_count, 4096);
	if (ret) {
		fprintf(stderr, "error during mkfs %d\n", ret);
		exit(1);
	}
	printf("fs created on %s blocksize %d blocks %Lu\n",
	       file, 4096, block_count);
	return 0;
}

