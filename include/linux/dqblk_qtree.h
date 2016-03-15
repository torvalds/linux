/*
 *	Definitions of structures and functions for quota formats using trie
 */

#ifndef _LINUX_DQBLK_QTREE_H
#define _LINUX_DQBLK_QTREE_H

#include <linux/types.h>

/* Numbers of blocks needed for updates - we count with the smallest
 * possible block size (1024) */
#define QTREE_INIT_ALLOC 4
#define QTREE_INIT_REWRITE 2
#define QTREE_DEL_ALLOC 0
#define QTREE_DEL_REWRITE 6

struct dquot;

/* Operations */
struct qtree_fmt_operations {
	void (*mem2disk_dqblk)(void *disk, struct dquot *dquot);	/* Convert given entry from in memory format to disk one */
	void (*disk2mem_dqblk)(struct dquot *dquot, void *disk);	/* Convert given entry from disk format to in memory one */
	int (*is_id)(void *disk, struct dquot *dquot);	/* Is this structure for given id? */
};

/* Inmemory copy of version specific information */
struct qtree_mem_dqinfo {
	struct super_block *dqi_sb;	/* Sb quota is on */
	int dqi_type;			/* Quota type */
	unsigned int dqi_blocks;	/* # of blocks in quota file */
	unsigned int dqi_free_blk;	/* First block in list of free blocks */
	unsigned int dqi_free_entry;	/* First block with free entry */
	unsigned int dqi_blocksize_bits;	/* Block size of quota file */
	unsigned int dqi_entry_size;	/* Size of quota entry in quota file */
	unsigned int dqi_usable_bs;	/* Space usable in block for quota data */
	unsigned int dqi_qtree_depth;	/* Precomputed depth of quota tree */
	const struct qtree_fmt_operations *dqi_ops; /* Operations for entry manipulation */
};

int qtree_write_dquot(struct qtree_mem_dqinfo *info, struct dquot *dquot);
int qtree_read_dquot(struct qtree_mem_dqinfo *info, struct dquot *dquot);
int qtree_delete_dquot(struct qtree_mem_dqinfo *info, struct dquot *dquot);
int qtree_release_dquot(struct qtree_mem_dqinfo *info, struct dquot *dquot);
int qtree_entry_unused(struct qtree_mem_dqinfo *info, char *disk);
static inline int qtree_depth(struct qtree_mem_dqinfo *info)
{
	unsigned int epb = info->dqi_usable_bs >> 2;
	unsigned long long entries = epb;
	int i;

	for (i = 1; entries < (1ULL << 32); i++)
		entries *= epb;
	return i;
}

#endif /* _LINUX_DQBLK_QTREE_H */
