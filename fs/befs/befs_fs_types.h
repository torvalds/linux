/*
 * fs/befs/befs_fs_types.h
 *
 * Copyright (C) 2001 Will Dyson (will@cs.earlham.edu)
 *
 *
 *
 * from linux/include/linux/befs_fs.h
 *
 * Copyright (C) 1999 Makoto Kato (m_kato@ga2.so-net.ne.jp)
 *
 */

#ifndef _LINUX_BEFS_FS_TYPES
#define _LINUX_BEFS_FS_TYPES

#ifdef __KERNEL__
#include <linux/types.h>
#endif /*__KERNEL__*/

#define PACKED __attribute__ ((__packed__))

/*
 * Max name lengths of BFS
 */

#define BEFS_NAME_LEN 255

#define BEFS_SYMLINK_LEN 144
#define BEFS_NUM_DIRECT_BLOCKS 12
#define B_OS_NAME_LENGTH 32

/* The datastream blocks mapped by the double-indirect
 * block are always 4 fs blocks long.
 * This eliminates the need for linear searches among
 * the potentially huge number of indirect blocks
 *
 * Err. Should that be 4 fs blocks or 4k???
 * It matters on large blocksize volumes
 */
#define BEFS_DBLINDIR_BRUN_LEN 4

/*
 * Flags of superblock
 */

enum super_flags {
	BEFS_BYTESEX_BE,
	BEFS_BYTESEX_LE,
	BEFS_CLEAN = 0x434c454e,
	BEFS_DIRTY = 0x44495254,
	BEFS_SUPER_MAGIC1 = 0x42465331,	/* BFS1 */
	BEFS_SUPER_MAGIC2 = 0xdd121031,
	BEFS_SUPER_MAGIC3 = 0x15b6830e,
};

#define BEFS_BYTEORDER_NATIVE 0x42494745
#define BEFS_BYTEORDER_NATIVE_LE (__force fs32)cpu_to_le32(BEFS_BYTEORDER_NATIVE)
#define BEFS_BYTEORDER_NATIVE_BE (__force fs32)cpu_to_be32(BEFS_BYTEORDER_NATIVE)

#define BEFS_SUPER_MAGIC BEFS_SUPER_MAGIC1
#define BEFS_SUPER_MAGIC1_LE (__force fs32)cpu_to_le32(BEFS_SUPER_MAGIC1)
#define BEFS_SUPER_MAGIC1_BE (__force fs32)cpu_to_be32(BEFS_SUPER_MAGIC1)

/*
 * Flags of inode
 */

#define BEFS_INODE_MAGIC1 0x3bbe0ad9

enum inode_flags {
	BEFS_INODE_IN_USE = 0x00000001,
	BEFS_ATTR_INODE = 0x00000004,
	BEFS_INODE_LOGGED = 0x00000008,
	BEFS_INODE_DELETED = 0x00000010,
	BEFS_LONG_SYMLINK = 0x00000040,
	BEFS_PERMANENT_FLAG = 0x0000ffff,
	BEFS_INODE_NO_CREATE = 0x00010000,
	BEFS_INODE_WAS_WRITTEN = 0x00020000,
	BEFS_NO_TRANSACTION = 0x00040000,
};
/* 
 * On-Disk datastructures of BeFS
 */

typedef u64 __bitwise fs64;
typedef u32 __bitwise fs32;
typedef u16 __bitwise fs16;

typedef u64 befs_off_t;
typedef fs64 befs_time_t;

/* Block runs */
typedef struct {
	fs32 allocation_group;
	fs16 start;
	fs16 len;
} PACKED befs_disk_block_run;

typedef struct {
	u32 allocation_group;
	u16 start;
	u16 len;
} PACKED befs_block_run;

typedef befs_disk_block_run befs_disk_inode_addr;
typedef befs_block_run befs_inode_addr;

/*
 * The Superblock Structure
 */
typedef struct {
	char name[B_OS_NAME_LENGTH];
	fs32 magic1;
	fs32 fs_byte_order;

	fs32 block_size;
	fs32 block_shift;

	fs64 num_blocks;
	fs64 used_blocks;

	fs32 inode_size;

	fs32 magic2;
	fs32 blocks_per_ag;
	fs32 ag_shift;
	fs32 num_ags;

	fs32 flags;

	befs_disk_block_run log_blocks;
	fs64 log_start;
	fs64 log_end;

	fs32 magic3;
	befs_disk_inode_addr root_dir;
	befs_disk_inode_addr indices;

} PACKED befs_super_block;

/* 
 * Note: the indirect and dbl_indir block_runs may
 * be longer than one block!
 */
typedef struct {
	befs_disk_block_run direct[BEFS_NUM_DIRECT_BLOCKS];
	fs64 max_direct_range;
	befs_disk_block_run indirect;
	fs64 max_indirect_range;
	befs_disk_block_run double_indirect;
	fs64 max_double_indirect_range;
	fs64 size;
} PACKED befs_disk_data_stream;

typedef struct {
	befs_block_run direct[BEFS_NUM_DIRECT_BLOCKS];
	befs_off_t max_direct_range;
	befs_block_run indirect;
	befs_off_t max_indirect_range;
	befs_block_run double_indirect;
	befs_off_t max_double_indirect_range;
	befs_off_t size;
} PACKED befs_data_stream;

/* Attribute */
typedef struct {
	fs32 type;
	fs16 name_size;
	fs16 data_size;
	char name[1];
} PACKED befs_small_data;

/* Inode structure */
typedef struct {
	fs32 magic1;
	befs_disk_inode_addr inode_num;
	fs32 uid;
	fs32 gid;
	fs32 mode;
	fs32 flags;
	befs_time_t create_time;
	befs_time_t last_modified_time;
	befs_disk_inode_addr parent;
	befs_disk_inode_addr attributes;
	fs32 type;

	fs32 inode_size;
	fs32 etc;		/* not use */

	union {
		befs_disk_data_stream datastream;
		char symlink[BEFS_SYMLINK_LEN];
	} data;

	fs32 pad[4];		/* not use */
	befs_small_data small_data[1];
} PACKED befs_inode;

/*
 * B+tree superblock
 */

#define BEFS_BTREE_MAGIC 0x69f6c2e8

enum btree_types {
	BTREE_STRING_TYPE = 0,
	BTREE_INT32_TYPE = 1,
	BTREE_UINT32_TYPE = 2,
	BTREE_INT64_TYPE = 3,
	BTREE_UINT64_TYPE = 4,
	BTREE_FLOAT_TYPE = 5,
	BTREE_DOUBLE_TYPE = 6
};

typedef struct {
	fs32 magic;
	fs32 node_size;
	fs32 max_depth;
	fs32 data_type;
	fs64 root_node_ptr;
	fs64 free_node_ptr;
	fs64 max_size;
} PACKED befs_disk_btree_super;

typedef struct {
	u32 magic;
	u32 node_size;
	u32 max_depth;
	u32 data_type;
	befs_off_t root_node_ptr;
	befs_off_t free_node_ptr;
	befs_off_t max_size;
} PACKED befs_btree_super;

/*
 * Header stucture of each btree node
 */
typedef struct {
	fs64 left;
	fs64 right;
	fs64 overflow;
	fs16 all_key_count;
	fs16 all_key_length;
} PACKED befs_btree_nodehead;

typedef struct {
	befs_off_t left;
	befs_off_t right;
	befs_off_t overflow;
	u16 all_key_count;
	u16 all_key_length;
} PACKED befs_host_btree_nodehead;

#endif				/* _LINUX_BEFS_FS_TYPES */
