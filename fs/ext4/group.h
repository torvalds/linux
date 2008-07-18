/*
 *  linux/fs/ext4/group.h
 *
 * Copyright (C) 2007 Cluster File Systems, Inc
 *
 * Author: Andreas Dilger <adilger@clusterfs.com>
 */

#ifndef _LINUX_EXT4_GROUP_H
#define _LINUX_EXT4_GROUP_H

extern __le16 ext4_group_desc_csum(struct ext4_sb_info *sbi, __u32 group,
				   struct ext4_group_desc *gdp);
extern int ext4_group_desc_csum_verify(struct ext4_sb_info *sbi, __u32 group,
				       struct ext4_group_desc *gdp);
struct buffer_head *ext4_read_block_bitmap(struct super_block *sb,
				      ext4_group_t block_group);
extern unsigned ext4_init_block_bitmap(struct super_block *sb,
				       struct buffer_head *bh,
				       ext4_group_t group,
				       struct ext4_group_desc *desc);
#define ext4_free_blocks_after_init(sb, group, desc)			\
		ext4_init_block_bitmap(sb, NULL, group, desc)
extern unsigned ext4_init_inode_bitmap(struct super_block *sb,
				       struct buffer_head *bh,
				       ext4_group_t group,
				       struct ext4_group_desc *desc);
extern void mark_bitmap_end(int start_bit, int end_bit, char *bitmap);
#endif /* _LINUX_EXT4_GROUP_H */
