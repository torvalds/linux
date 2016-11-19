/*
 * io.h
 */

struct buffer_head *befs_bread_iaddr(struct super_block *sb,
				     befs_inode_addr iaddr);

