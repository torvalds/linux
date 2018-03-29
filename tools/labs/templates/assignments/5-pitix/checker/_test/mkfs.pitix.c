#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <string.h>

#include "pitix.h"

/*
 * mkfs.pitix block_size file
 */

int main(int argc, char **argv)
{
	FILE *file;
	char buffer[4096];
	int block_size, bits, i;
	struct pitix_super_block psb;
	struct pitix_inode root_inode;

	if (argc != 3)
		return -1;

	block_size = atoi(argv[1]);

	switch (block_size) {
	case 512:
		bits = 9;
		break;
	case 1024:
		bits = 10;
		break;
	case 2048:
		bits = 11;
		break;
	case 4096:
		bits = 12;
		break;
	default:
		return -1;
	}

	file = fopen(argv[2], "w+");
	if (!file)
		return -1;

	memset(&psb, 0, sizeof(struct pitix_super_block));

	psb.magic = PITIX_MAGIC;
	psb.version = PITIX_VERSION;
	psb.block_size_bits = bits;
	psb.imap_block = PITIX_SUPERBLOCK_SIZE / block_size;
	psb.dmap_block = psb.imap_block + 1;
	psb.izone_block = psb.dmap_block + 1;
	psb.dzone_block = psb.izone_block + IZONE_BLOCKS;
	psb.bfree = 8 * block_size;
	psb.ffree = IZONE_BLOCKS * block_size / inode_size();

	printf("mkfs.pitix block_size=%d\n", block_size);

	/* zero disk */
	memset(buffer, 0, block_size);
	for (i = 0; i < psb.bfree + IZONE_BLOCKS + 1 + 1 + psb.imap_block; i++)
		fwrite(buffer, block_size, 1, file);

	fseek(file, 0, SEEK_SET);

	/* alloc the 1st block and inode to the roor dir */
	psb.bfree--; psb.ffree--;
	/* initialize super block */
	fwrite(&psb, sizeof(psb), 1, file);

	fseek(file, PITIX_SUPERBLOCK_SIZE, SEEK_SET);
	memset(buffer, 0, block_size);
	buffer[0] = 0x01;
	/* alloc inode 0 */
	fwrite(buffer, block_size, 1, file);
	/* alloc block 0 */
	fwrite(buffer, block_size, 1, file);

	/* initialize root inode */
	memset(&root_inode, 0, sizeof(root_inode));
	root_inode.mode = S_IFDIR;
	root_inode.size = block_size;
	fseek(file, psb.izone_block * block_size, SEEK_SET);
	fwrite(&root_inode, sizeof(root_inode), 1, file);

	return 0;
}
