/*
 *  linux/fs/ext2/xip.h
 *
 * Copyright (C) 2005 IBM Corporation
 * Author: Carsten Otte (cotte@de.ibm.com)
 */

#ifdef CONFIG_EXT2_FS_XIP
extern void ext2_xip_verify_sb (struct super_block *);
static inline int ext2_use_xip (struct super_block *sb)
{
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	return (sbi->s_mount_opt & EXT2_MOUNT_XIP);
}
#else
#define ext2_xip_verify_sb(sb)			do { } while (0)
#define ext2_use_xip(sb)			0
#endif
