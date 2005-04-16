#ifndef _ISO_FS_I
#define _ISO_FS_I

#include <linux/fs.h>

enum isofs_file_format {
	isofs_file_normal = 0,
	isofs_file_sparse = 1,
	isofs_file_compressed = 2,
};
	
/*
 * iso fs inode data in memory
 */
struct iso_inode_info {
	unsigned long i_iget5_block;
	unsigned long i_iget5_offset;
	unsigned int i_first_extent;
	unsigned char i_file_format;
	unsigned char i_format_parm[3];
	unsigned long i_next_section_block;
	unsigned long i_next_section_offset;
	off_t i_section_size;
	struct inode vfs_inode;
};

#endif
