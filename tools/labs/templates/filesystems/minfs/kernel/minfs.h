#ifndef _MINFS_H
#define _MINFS_H	1

#define MINFS_MAGIC		0xDEADF00D
#define MINFS_NAME_LEN		16
#define MINFS_BLOCK_SIZE	4096
#define MINFS_NUM_INODES	32
#define MINFS_NUM_ENTRIES	32

#define MINFS_ROOT_INODE	0

/*
 * Filesystem layout:
 *
 *      SB      IZONE 	     DATA
 *    ^	    ^ (1 block)
 *    |     |
 *    +-0   +-- 4096
 */

#define MINFS_SUPER_BLOCK	0
#define MINFS_INODE_BLOCK	1
#define MINFS_FIRST_DATA_BLOCK	2

struct minfs_super_block {
	unsigned long magic;
	__u8 version;
	unsigned long imap;
};

struct minfs_dir_entry {
	__u32 ino;
	char name[MINFS_NAME_LEN];
};

/* A minfs inode uses a single block. */
struct minfs_inode {
	__u32 mode;
	__u32 uid;
	__u32 gid;
	__u32 size;
	__u16 data_block;
};

#endif /* _MINFS_H */
