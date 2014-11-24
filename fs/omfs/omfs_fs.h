#ifndef _OMFS_FS_H
#define _OMFS_FS_H

/* OMFS On-disk structures */

#define OMFS_MAGIC 0xC2993D87
#define OMFS_IMAGIC 0xD2

#define OMFS_DIR 'D'
#define OMFS_FILE 'F'
#define OMFS_INODE_NORMAL 'e'
#define OMFS_INODE_CONTINUATION 'c'
#define OMFS_INODE_SYSTEM 's'
#define OMFS_NAMELEN 256
#define OMFS_DIR_START 0x1b8
#define OMFS_EXTENT_START 0x1d0
#define OMFS_EXTENT_CONT 0x40
#define OMFS_XOR_COUNT 19
#define OMFS_MAX_BLOCK_SIZE 8192
#define OMFS_MAX_CLUSTER_SIZE 8
#define OMFS_MAX_BLOCKS (1ul << 31)

struct omfs_super_block {
	char s_fill1[256];
	__be64 s_root_block;		/* block number of omfs_root_block */
	__be64 s_num_blocks;		/* total number of FS blocks */
	__be32 s_magic;			/* OMFS_MAGIC */
	__be32 s_blocksize;		/* size of a block */
	__be32 s_mirrors;		/* # of mirrors of system blocks */
	__be32 s_sys_blocksize;		/* size of non-data blocks */
};

struct omfs_header {
	__be64 h_self;			/* FS block where this is located */
	__be32 h_body_size;		/* size of useful data after header */
	__be16 h_crc;			/* crc-ccitt of body_size bytes */
	char h_fill1[2];
	u8 h_version;			/* version, always 1 */
	char h_type;			/* OMFS_INODE_X */
	u8 h_magic;			/* OMFS_IMAGIC */
	u8 h_check_xor;			/* XOR of header bytes before this */
	__be32 h_fill2;
};

struct omfs_root_block {
	struct omfs_header r_head;	/* header */
	__be64 r_fill1;
	__be64 r_num_blocks;		/* total number of FS blocks */
	__be64 r_root_dir;		/* block # of root directory */
	__be64 r_bitmap;		/* block # of free space bitmap */
	__be32 r_blocksize;		/* size of a block */
	__be32 r_clustersize;		/* size allocated for data blocks */
	__be64 r_mirrors;		/* # of mirrors of system blocks */
	char r_name[OMFS_NAMELEN];	/* partition label */
};

struct omfs_inode {
	struct omfs_header i_head;	/* header */
	__be64 i_parent;		/* parent containing this inode */
	__be64 i_sibling;		/* next inode in hash bucket */
	__be64 i_ctime;			/* ctime, in milliseconds */
	char i_fill1[35];
	char i_type;			/* OMFS_[DIR,FILE] */
	__be32 i_fill2;
	char i_fill3[64];
	char i_name[OMFS_NAMELEN];	/* filename */
	__be64 i_size;			/* size of file, in bytes */
};

struct omfs_extent_entry {
	__be64 e_cluster;		/* start location of a set of blocks */
	__be64 e_blocks;		/* number of blocks after e_cluster */
};

struct omfs_extent {
	__be64 e_next;			/* next extent table location */
	__be32 e_extent_count;		/* total # extents in this table */
	__be32 e_fill;
	struct omfs_extent_entry e_entry;	/* start of extent entries */
};

#endif
