#ifndef _MINFS_H
#define _MINFS_H	1

#define MINFS_MAGIC 		0xDEADF00D
#define MINFS_NAME_LEN		16
#define MINFS_BLOCK_SIZE	4096

/*
 * filesystem layout:
 *
 *      SB      IZONE 	     DATA
 *    ^	    ^ (1 block)
 *    |     |
 *    +-0   +-- 4096
 */

struct minfs_super_block {
	unsigned long magic;
	__u8 version;
};

struct minfs_dir_entry {
	__u32 ino;
	char name[MINFS_NAME_LEN];
};

/* an minfs inode uses a single block */
struct minfs_inode {
	__u32 mode;
	__u32 uid;
	__u32 gid;
	__u32 size;
	__u16 data_block;
};

#endif
