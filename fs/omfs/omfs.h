#ifndef _OMFS_H
#define _OMFS_H

#include <linux/module.h>
#include <linux/fs.h>

#include "omfs_fs.h"

/* In-memory structures */
struct omfs_sb_info {
	u64 s_num_blocks;
	u64 s_bitmap_ino;
	u64 s_root_ino;
	u32 s_blocksize;
	u32 s_mirrors;
	u32 s_sys_blocksize;
	u32 s_clustersize;
	int s_block_shift;
	unsigned long **s_imap;
	int s_imap_size;
	struct mutex s_bitmap_lock;
	int s_uid;
	int s_gid;
	int s_dmask;
	int s_fmask;
};

/* convert a cluster number to a scaled block number */
static inline sector_t clus_to_blk(struct omfs_sb_info *sbi, sector_t block)
{
	return block << sbi->s_block_shift;
}

static inline struct omfs_sb_info *OMFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* bitmap.c */
extern unsigned long omfs_count_free(struct super_block *sb);
extern int omfs_allocate_block(struct super_block *sb, u64 block);
extern int omfs_allocate_range(struct super_block *sb, int min_request,
			int max_request, u64 *return_block, int *return_size);
extern int omfs_clear_range(struct super_block *sb, u64 block, int count);

/* dir.c */
extern const struct file_operations omfs_dir_operations;
extern const struct inode_operations omfs_dir_inops;
extern int omfs_make_empty(struct inode *inode, struct super_block *sb);
extern int omfs_is_bad(struct omfs_sb_info *sbi, struct omfs_header *header,
			u64 fsblock);

/* file.c */
extern const struct file_operations omfs_file_operations;
extern const struct inode_operations omfs_file_inops;
extern const struct address_space_operations omfs_aops;
extern void omfs_make_empty_table(struct buffer_head *bh, int offset);
extern int omfs_shrink_inode(struct inode *inode);

/* inode.c */
extern struct buffer_head *omfs_bread(struct super_block *sb, sector_t block);
extern struct inode *omfs_iget(struct super_block *sb, ino_t inode);
extern struct inode *omfs_new_inode(struct inode *dir, int mode);
extern int omfs_reserve_block(struct super_block *sb, sector_t block);
extern int omfs_find_empty_block(struct super_block *sb, int mode, ino_t *ino);
extern int omfs_sync_inode(struct inode *inode);

#endif
