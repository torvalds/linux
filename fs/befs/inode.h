/*
 * inode.h
 * 
 */

int befs_check_inode(struct super_block *sb, befs_inode *raw_inode,
		     befs_blocknr_t inode);
