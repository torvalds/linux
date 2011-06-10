/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file describes UBIFS on-flash format and contains definitions of all the
 * relevant data structures and constants.
 *
 * All UBIFS on-flash objects are stored in the form of nodes. All nodes start
 * with the UBIFS node magic number and have the same common header. Nodes
 * always sit at 8-byte aligned positions on the media and node header sizes are
 * also 8-byte aligned (except for the indexing node and the padding node).
 */

#ifndef __UBIFS_MEDIA_H__
#define __UBIFS_MEDIA_H__

/* UBIFS node magic number (must not have the padding byte first or last) */
#define UBIFS_NODE_MAGIC  0x06101831

/*
 * UBIFS on-flash format version. This version is increased when the on-flash
 * format is changing. If this happens, UBIFS is will support older versions as
 * well. But older UBIFS code will not support newer formats. Format changes
 * will be rare and only when absolutely necessary, e.g. to fix a bug or to add
 * a new feature.
 *
 * UBIFS went into mainline kernel with format version 4. The older formats
 * were development formats.
 */
#define UBIFS_FORMAT_VERSION 4

/*
 * Read-only compatibility version. If the UBIFS format is changed, older UBIFS
 * implementations will not be able to mount newer formats in read-write mode.
 * However, depending on the change, it may be possible to mount newer formats
 * in R/O mode. This is indicated by the R/O compatibility version which is
 * stored in the super-block.
 *
 * This is needed to support boot-loaders which only need R/O mounting. With
 * this flag it is possible to do UBIFS format changes without a need to update
 * boot-loaders.
 */
#define UBIFS_RO_COMPAT_VERSION 0

/* Minimum logical eraseblock size in bytes */
#define UBIFS_MIN_LEB_SZ (15*1024)

/* Initial CRC32 value used when calculating CRC checksums */
#define UBIFS_CRC32_INIT 0xFFFFFFFFU

/*
 * UBIFS does not try to compress data if its length is less than the below
 * constant.
 */
#define UBIFS_MIN_COMPR_LEN 128

/*
 * If compressed data length is less than %UBIFS_MIN_COMPRESS_DIFF bytes
 * shorter than uncompressed data length, UBIFS prefers to leave this data
 * node uncompress, because it'll be read faster.
 */
#define UBIFS_MIN_COMPRESS_DIFF 64

/* Root inode number */
#define UBIFS_ROOT_INO 1

/* Lowest inode number used for regular inodes (not UBIFS-only internal ones) */
#define UBIFS_FIRST_INO 64

/*
 * Maximum file name and extended attribute length (must be a multiple of 8,
 * minus 1).
 */
#define UBIFS_MAX_NLEN 255

/* Maximum number of data journal heads */
#define UBIFS_MAX_JHEADS 1

/*
 * Size of UBIFS data block. Note, UBIFS is not a block oriented file-system,
 * which means that it does not treat the underlying media as consisting of
 * blocks like in case of hard drives. Do not be confused. UBIFS block is just
 * the maximum amount of data which one data node can have or which can be
 * attached to an inode node.
 */
#define UBIFS_BLOCK_SIZE  4096
#define UBIFS_BLOCK_SHIFT 12

/* UBIFS padding byte pattern (must not be first or last byte of node magic) */
#define UBIFS_PADDING_BYTE 0xCE

/* Maximum possible key length */
#define UBIFS_MAX_KEY_LEN 16

/* Key length ("simple" format) */
#define UBIFS_SK_LEN 8

/* Minimum index tree fanout */
#define UBIFS_MIN_FANOUT 3

/* Maximum number of levels in UBIFS indexing B-tree */
#define UBIFS_MAX_LEVELS 512

/* Maximum amount of data attached to an inode in bytes */
#define UBIFS_MAX_INO_DATA UBIFS_BLOCK_SIZE

/* LEB Properties Tree fanout (must be power of 2) and fanout shift */
#define UBIFS_LPT_FANOUT 4
#define UBIFS_LPT_FANOUT_SHIFT 2

/* LEB Properties Tree bit field sizes */
#define UBIFS_LPT_CRC_BITS 16
#define UBIFS_LPT_CRC_BYTES 2
#define UBIFS_LPT_TYPE_BITS 4

/* The key is always at the same position in all keyed nodes */
#define UBIFS_KEY_OFFSET offsetof(struct ubifs_ino_node, key)

/* Garbage collector journal head number */
#define UBIFS_GC_HEAD   0
/* Base journal head number */
#define UBIFS_BASE_HEAD 1
/* Data journal head number */
#define UBIFS_DATA_HEAD 2

/*
 * LEB Properties Tree node types.
 *
 * UBIFS_LPT_PNODE: LPT leaf node (contains LEB properties)
 * UBIFS_LPT_NNODE: LPT internal node
 * UBIFS_LPT_LTAB: LPT's own lprops table
 * UBIFS_LPT_LSAVE: LPT's save table (big model only)
 * UBIFS_LPT_NODE_CNT: count of LPT node types
 * UBIFS_LPT_NOT_A_NODE: all ones (15 for 4 bits) is never a valid node type
 */
enum {
	UBIFS_LPT_PNODE,
	UBIFS_LPT_NNODE,
	UBIFS_LPT_LTAB,
	UBIFS_LPT_LSAVE,
	UBIFS_LPT_NODE_CNT,
	UBIFS_LPT_NOT_A_NODE = (1 << UBIFS_LPT_TYPE_BITS) - 1,
};

/*
 * UBIFS inode types.
 *
 * UBIFS_ITYPE_REG: regular file
 * UBIFS_ITYPE_DIR: directory
 * UBIFS_ITYPE_LNK: soft link
 * UBIFS_ITYPE_BLK: block device node
 * UBIFS_ITYPE_CHR: character device node
 * UBIFS_ITYPE_FIFO: fifo
 * UBIFS_ITYPE_SOCK: socket
 * UBIFS_ITYPES_CNT: count of supported file types
 */
enum {
	UBIFS_ITYPE_REG,
	UBIFS_ITYPE_DIR,
	UBIFS_ITYPE_LNK,
	UBIFS_ITYPE_BLK,
	UBIFS_ITYPE_CHR,
	UBIFS_ITYPE_FIFO,
	UBIFS_ITYPE_SOCK,
	UBIFS_ITYPES_CNT,
};

/*
 * Supported key hash functions.
 *
 * UBIFS_KEY_HASH_R5: R5 hash
 * UBIFS_KEY_HASH_TEST: test hash which just returns first 4 bytes of the name
 */
enum {
	UBIFS_KEY_HASH_R5,
	UBIFS_KEY_HASH_TEST,
};

/*
 * Supported key formats.
 *
 * UBIFS_SIMPLE_KEY_FMT: simple key format
 */
enum {
	UBIFS_SIMPLE_KEY_FMT,
};

/*
 * The simple key format uses 29 bits for storing UBIFS block number and hash
 * value.
 */
#define UBIFS_S_KEY_BLOCK_BITS 29
#define UBIFS_S_KEY_BLOCK_MASK 0x1FFFFFFF
#define UBIFS_S_KEY_HASH_BITS  UBIFS_S_KEY_BLOCK_BITS
#define UBIFS_S_KEY_HASH_MASK  UBIFS_S_KEY_BLOCK_MASK

/*
 * Key types.
 *
 * UBIFS_INO_KEY: inode node key
 * UBIFS_DATA_KEY: data node key
 * UBIFS_DENT_KEY: directory entry node key
 * UBIFS_XENT_KEY: extended attribute entry key
 * UBIFS_KEY_TYPES_CNT: number of supported key types
 */
enum {
	UBIFS_INO_KEY,
	UBIFS_DATA_KEY,
	UBIFS_DENT_KEY,
	UBIFS_XENT_KEY,
	UBIFS_KEY_TYPES_CNT,
};

/* Count of LEBs reserved for the superblock area */
#define UBIFS_SB_LEBS 1
/* Count of LEBs reserved for the master area */
#define UBIFS_MST_LEBS 2

/* First LEB of the superblock area */
#define UBIFS_SB_LNUM 0
/* First LEB of the master area */
#define UBIFS_MST_LNUM (UBIFS_SB_LNUM + UBIFS_SB_LEBS)
/* First LEB of the log area */
#define UBIFS_LOG_LNUM (UBIFS_MST_LNUM + UBIFS_MST_LEBS)

/*
 * The below constants define the absolute minimum values for various UBIFS
 * media areas. Many of them actually depend of flash geometry and the FS
 * configuration (number of journal heads, orphan LEBs, etc). This means that
 * the smallest volume size which can be used for UBIFS cannot be pre-defined
 * by these constants. The file-system that meets the below limitation will not
 * necessarily mount. UBIFS does run-time calculations and validates the FS
 * size.
 */

/* Minimum number of logical eraseblocks in the log */
#define UBIFS_MIN_LOG_LEBS 2
/* Minimum number of bud logical eraseblocks (one for each head) */
#define UBIFS_MIN_BUD_LEBS 3
/* Minimum number of journal logical eraseblocks */
#define UBIFS_MIN_JNL_LEBS (UBIFS_MIN_LOG_LEBS + UBIFS_MIN_BUD_LEBS)
/* Minimum number of LPT area logical eraseblocks */
#define UBIFS_MIN_LPT_LEBS 2
/* Minimum number of orphan area logical eraseblocks */
#define UBIFS_MIN_ORPH_LEBS 1
/*
 * Minimum number of main area logical eraseblocks (buds, 3 for the index, 1
 * for GC, 1 for deletions, and at least 1 for committed data).
 */
#define UBIFS_MIN_MAIN_LEBS (UBIFS_MIN_BUD_LEBS + 6)

/* Minimum number of logical eraseblocks */
#define UBIFS_MIN_LEB_CNT (UBIFS_SB_LEBS + UBIFS_MST_LEBS + \
			   UBIFS_MIN_LOG_LEBS + UBIFS_MIN_LPT_LEBS + \
			   UBIFS_MIN_ORPH_LEBS + UBIFS_MIN_MAIN_LEBS)

/* Node sizes (N.B. these are guaranteed to be multiples of 8) */
#define UBIFS_CH_SZ        sizeof(struct ubifs_ch)
#define UBIFS_INO_NODE_SZ  sizeof(struct ubifs_ino_node)
#define UBIFS_DATA_NODE_SZ sizeof(struct ubifs_data_node)
#define UBIFS_DENT_NODE_SZ sizeof(struct ubifs_dent_node)
#define UBIFS_TRUN_NODE_SZ sizeof(struct ubifs_trun_node)
#define UBIFS_PAD_NODE_SZ  sizeof(struct ubifs_pad_node)
#define UBIFS_SB_NODE_SZ   sizeof(struct ubifs_sb_node)
#define UBIFS_MST_NODE_SZ  sizeof(struct ubifs_mst_node)
#define UBIFS_REF_NODE_SZ  sizeof(struct ubifs_ref_node)
#define UBIFS_IDX_NODE_SZ  sizeof(struct ubifs_idx_node)
#define UBIFS_CS_NODE_SZ   sizeof(struct ubifs_cs_node)
#define UBIFS_ORPH_NODE_SZ sizeof(struct ubifs_orph_node)
/* Extended attribute entry nodes are identical to directory entry nodes */
#define UBIFS_XENT_NODE_SZ UBIFS_DENT_NODE_SZ
/* Only this does not have to be multiple of 8 bytes */
#define UBIFS_BRANCH_SZ    sizeof(struct ubifs_branch)

/* Maximum node sizes (N.B. these are guaranteed to be multiples of 8) */
#define UBIFS_MAX_DATA_NODE_SZ  (UBIFS_DATA_NODE_SZ + UBIFS_BLOCK_SIZE)
#define UBIFS_MAX_INO_NODE_SZ   (UBIFS_INO_NODE_SZ + UBIFS_MAX_INO_DATA)
#define UBIFS_MAX_DENT_NODE_SZ  (UBIFS_DENT_NODE_SZ + UBIFS_MAX_NLEN + 1)
#define UBIFS_MAX_XENT_NODE_SZ  UBIFS_MAX_DENT_NODE_SZ

/* The largest UBIFS node */
#define UBIFS_MAX_NODE_SZ UBIFS_MAX_INO_NODE_SZ

/*
 * On-flash inode flags.
 *
 * UBIFS_COMPR_FL: use compression for this inode
 * UBIFS_SYNC_FL:  I/O on this inode has to be synchronous
 * UBIFS_IMMUTABLE_FL: inode is immutable
 * UBIFS_APPEND_FL: writes to the inode may only append data
 * UBIFS_DIRSYNC_FL: I/O on this directory inode has to be synchronous
 * UBIFS_XATTR_FL: this inode is the inode for an extended attribute value
 *
 * Note, these are on-flash flags which correspond to ioctl flags
 * (@FS_COMPR_FL, etc). They have the same values now, but generally, do not
 * have to be the same.
 */
enum {
	UBIFS_COMPR_FL     = 0x01,
	UBIFS_SYNC_FL      = 0x02,
	UBIFS_IMMUTABLE_FL = 0x04,
	UBIFS_APPEND_FL    = 0x08,
	UBIFS_DIRSYNC_FL   = 0x10,
	UBIFS_XATTR_FL     = 0x20,
};

/* Inode flag bits used by UBIFS */
#define UBIFS_FL_MASK 0x0000001F

/*
 * UBIFS compression algorithms.
 *
 * UBIFS_COMPR_NONE: no compression
 * UBIFS_COMPR_LZO: LZO compression
 * UBIFS_COMPR_ZLIB: ZLIB compression
 * UBIFS_COMPR_TYPES_CNT: count of supported compression types
 */
enum {
	UBIFS_COMPR_NONE,
	UBIFS_COMPR_LZO,
	UBIFS_COMPR_ZLIB,
	UBIFS_COMPR_TYPES_CNT,
};

/*
 * UBIFS node types.
 *
 * UBIFS_INO_NODE: inode node
 * UBIFS_DATA_NODE: data node
 * UBIFS_DENT_NODE: directory entry node
 * UBIFS_XENT_NODE: extended attribute node
 * UBIFS_TRUN_NODE: truncation node
 * UBIFS_PAD_NODE: padding node
 * UBIFS_SB_NODE: superblock node
 * UBIFS_MST_NODE: master node
 * UBIFS_REF_NODE: LEB reference node
 * UBIFS_IDX_NODE: index node
 * UBIFS_CS_NODE: commit start node
 * UBIFS_ORPH_NODE: orphan node
 * UBIFS_NODE_TYPES_CNT: count of supported node types
 *
 * Note, we index arrays by these numbers, so keep them low and contiguous.
 * Node type constants for inodes, direntries and so on have to be the same as
 * corresponding key type constants.
 */
enum {
	UBIFS_INO_NODE,
	UBIFS_DATA_NODE,
	UBIFS_DENT_NODE,
	UBIFS_XENT_NODE,
	UBIFS_TRUN_NODE,
	UBIFS_PAD_NODE,
	UBIFS_SB_NODE,
	UBIFS_MST_NODE,
	UBIFS_REF_NODE,
	UBIFS_IDX_NODE,
	UBIFS_CS_NODE,
	UBIFS_ORPH_NODE,
	UBIFS_NODE_TYPES_CNT,
};

/*
 * Master node flags.
 *
 * UBIFS_MST_DIRTY: rebooted uncleanly - master node is dirty
 * UBIFS_MST_NO_ORPHS: no orphan inodes present
 * UBIFS_MST_RCVRY: written by recovery
 */
enum {
	UBIFS_MST_DIRTY = 1,
	UBIFS_MST_NO_ORPHS = 2,
	UBIFS_MST_RCVRY = 4,
};

/*
 * Node group type (used by recovery to recover whole group or none).
 *
 * UBIFS_NO_NODE_GROUP: this node is not part of a group
 * UBIFS_IN_NODE_GROUP: this node is a part of a group
 * UBIFS_LAST_OF_NODE_GROUP: this node is the last in a group
 */
enum {
	UBIFS_NO_NODE_GROUP = 0,
	UBIFS_IN_NODE_GROUP,
	UBIFS_LAST_OF_NODE_GROUP,
};

/*
 * Superblock flags.
 *
 * UBIFS_FLG_BIGLPT: if "big" LPT model is used if set
 * UBIFS_FLG_SPACE_FIXUP: first-mount "fixup" of free space within LEBs needed
 */
enum {
	UBIFS_FLG_BIGLPT = 0x02,
	UBIFS_FLG_SPACE_FIXUP = 0x04,
};

/**
 * struct ubifs_ch - common header node.
 * @magic: UBIFS node magic number (%UBIFS_NODE_MAGIC)
 * @crc: CRC-32 checksum of the node header
 * @sqnum: sequence number
 * @len: full node length
 * @node_type: node type
 * @group_type: node group type
 * @padding: reserved for future, zeroes
 *
 * Every UBIFS node starts with this common part. If the node has a key, the
 * key always goes next.
 */
struct ubifs_ch {
	__le32 magic;
	__le32 crc;
	__le64 sqnum;
	__le32 len;
	__u8 node_type;
	__u8 group_type;
	__u8 padding[2];
} __packed;

/**
 * union ubifs_dev_desc - device node descriptor.
 * @new: new type device descriptor
 * @huge: huge type device descriptor
 *
 * This data structure describes major/minor numbers of a device node. In an
 * inode is a device node then its data contains an object of this type. UBIFS
 * uses standard Linux "new" and "huge" device node encodings.
 */
union ubifs_dev_desc {
	__le32 new;
	__le64 huge;
} __packed;

/**
 * struct ubifs_ino_node - inode node.
 * @ch: common header
 * @key: node key
 * @creat_sqnum: sequence number at time of creation
 * @size: inode size in bytes (amount of uncompressed data)
 * @atime_sec: access time seconds
 * @ctime_sec: creation time seconds
 * @mtime_sec: modification time seconds
 * @atime_nsec: access time nanoseconds
 * @ctime_nsec: creation time nanoseconds
 * @mtime_nsec: modification time nanoseconds
 * @nlink: number of hard links
 * @uid: owner ID
 * @gid: group ID
 * @mode: access flags
 * @flags: per-inode flags (%UBIFS_COMPR_FL, %UBIFS_SYNC_FL, etc)
 * @data_len: inode data length
 * @xattr_cnt: count of extended attributes this inode has
 * @xattr_size: summarized size of all extended attributes in bytes
 * @padding1: reserved for future, zeroes
 * @xattr_names: sum of lengths of all extended attribute names belonging to
 *               this inode
 * @compr_type: compression type used for this inode
 * @padding2: reserved for future, zeroes
 * @data: data attached to the inode
 *
 * Note, even though inode compression type is defined by @compr_type, some
 * nodes of this inode may be compressed with different compressor - this
 * happens if compression type is changed while the inode already has data
 * nodes. But @compr_type will be use for further writes to the inode.
 *
 * Note, do not forget to amend 'zero_ino_node_unused()' function when changing
 * the padding fields.
 */
struct ubifs_ino_node {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le64 creat_sqnum;
	__le64 size;
	__le64 atime_sec;
	__le64 ctime_sec;
	__le64 mtime_sec;
	__le32 atime_nsec;
	__le32 ctime_nsec;
	__le32 mtime_nsec;
	__le32 nlink;
	__le32 uid;
	__le32 gid;
	__le32 mode;
	__le32 flags;
	__le32 data_len;
	__le32 xattr_cnt;
	__le32 xattr_size;
	__u8 padding1[4]; /* Watch 'zero_ino_node_unused()' if changing! */
	__le32 xattr_names;
	__le16 compr_type;
	__u8 padding2[26]; /* Watch 'zero_ino_node_unused()' if changing! */
	__u8 data[];
} __packed;

/**
 * struct ubifs_dent_node - directory entry node.
 * @ch: common header
 * @key: node key
 * @inum: target inode number
 * @padding1: reserved for future, zeroes
 * @type: type of the target inode (%UBIFS_ITYPE_REG, %UBIFS_ITYPE_DIR, etc)
 * @nlen: name length
 * @padding2: reserved for future, zeroes
 * @name: zero-terminated name
 *
 * Note, do not forget to amend 'zero_dent_node_unused()' function when
 * changing the padding fields.
 */
struct ubifs_dent_node {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le64 inum;
	__u8 padding1;
	__u8 type;
	__le16 nlen;
	__u8 padding2[4]; /* Watch 'zero_dent_node_unused()' if changing! */
	__u8 name[];
} __packed;

/**
 * struct ubifs_data_node - data node.
 * @ch: common header
 * @key: node key
 * @size: uncompressed data size in bytes
 * @compr_type: compression type (%UBIFS_COMPR_NONE, %UBIFS_COMPR_LZO, etc)
 * @padding: reserved for future, zeroes
 * @data: data
 *
 * Note, do not forget to amend 'zero_data_node_unused()' function when
 * changing the padding fields.
 */
struct ubifs_data_node {
	struct ubifs_ch ch;
	__u8 key[UBIFS_MAX_KEY_LEN];
	__le32 size;
	__le16 compr_type;
	__u8 padding[2]; /* Watch 'zero_data_node_unused()' if changing! */
	__u8 data[];
} __packed;

/**
 * struct ubifs_trun_node - truncation node.
 * @ch: common header
 * @inum: truncated inode number
 * @padding: reserved for future, zeroes
 * @old_size: size before truncation
 * @new_size: size after truncation
 *
 * This node exists only in the journal and never goes to the main area. Note,
 * do not forget to amend 'zero_trun_node_unused()' function when changing the
 * padding fields.
 */
struct ubifs_trun_node {
	struct ubifs_ch ch;
	__le32 inum;
	__u8 padding[12]; /* Watch 'zero_trun_node_unused()' if changing! */
	__le64 old_size;
	__le64 new_size;
} __packed;

/**
 * struct ubifs_pad_node - padding node.
 * @ch: common header
 * @pad_len: how many bytes after this node are unused (because padded)
 * @padding: reserved for future, zeroes
 */
struct ubifs_pad_node {
	struct ubifs_ch ch;
	__le32 pad_len;
} __packed;

/**
 * struct ubifs_sb_node - superblock node.
 * @ch: common header
 * @padding: reserved for future, zeroes
 * @key_hash: type of hash function used in keys
 * @key_fmt: format of the key
 * @flags: file-system flags (%UBIFS_FLG_BIGLPT, etc)
 * @min_io_size: minimal input/output unit size
 * @leb_size: logical eraseblock size in bytes
 * @leb_cnt: count of LEBs used by file-system
 * @max_leb_cnt: maximum count of LEBs used by file-system
 * @max_bud_bytes: maximum amount of data stored in buds
 * @log_lebs: log size in logical eraseblocks
 * @lpt_lebs: number of LEBs used for lprops table
 * @orph_lebs: number of LEBs used for recording orphans
 * @jhead_cnt: count of journal heads
 * @fanout: tree fanout (max. number of links per indexing node)
 * @lsave_cnt: number of LEB numbers in LPT's save table
 * @fmt_version: UBIFS on-flash format version
 * @default_compr: default compression algorithm (%UBIFS_COMPR_LZO, etc)
 * @padding1: reserved for future, zeroes
 * @rp_uid: reserve pool UID
 * @rp_gid: reserve pool GID
 * @rp_size: size of the reserved pool in bytes
 * @padding2: reserved for future, zeroes
 * @time_gran: time granularity in nanoseconds
 * @uuid: UUID generated when the file system image was created
 * @ro_compat_version: UBIFS R/O compatibility version
 */
struct ubifs_sb_node {
	struct ubifs_ch ch;
	__u8 padding[2];
	__u8 key_hash;
	__u8 key_fmt;
	__le32 flags;
	__le32 min_io_size;
	__le32 leb_size;
	__le32 leb_cnt;
	__le32 max_leb_cnt;
	__le64 max_bud_bytes;
	__le32 log_lebs;
	__le32 lpt_lebs;
	__le32 orph_lebs;
	__le32 jhead_cnt;
	__le32 fanout;
	__le32 lsave_cnt;
	__le32 fmt_version;
	__le16 default_compr;
	__u8 padding1[2];
	__le32 rp_uid;
	__le32 rp_gid;
	__le64 rp_size;
	__le32 time_gran;
	__u8 uuid[16];
	__le32 ro_compat_version;
	__u8 padding2[3968];
} __packed;

/**
 * struct ubifs_mst_node - master node.
 * @ch: common header
 * @highest_inum: highest inode number in the committed index
 * @cmt_no: commit number
 * @flags: various flags (%UBIFS_MST_DIRTY, etc)
 * @log_lnum: start of the log
 * @root_lnum: LEB number of the root indexing node
 * @root_offs: offset within @root_lnum
 * @root_len: root indexing node length
 * @gc_lnum: LEB reserved for garbage collection (%-1 value means the LEB was
 * not reserved and should be reserved on mount)
 * @ihead_lnum: LEB number of index head
 * @ihead_offs: offset of index head
 * @index_size: size of index on flash
 * @total_free: total free space in bytes
 * @total_dirty: total dirty space in bytes
 * @total_used: total used space in bytes (includes only data LEBs)
 * @total_dead: total dead space in bytes (includes only data LEBs)
 * @total_dark: total dark space in bytes (includes only data LEBs)
 * @lpt_lnum: LEB number of LPT root nnode
 * @lpt_offs: offset of LPT root nnode
 * @nhead_lnum: LEB number of LPT head
 * @nhead_offs: offset of LPT head
 * @ltab_lnum: LEB number of LPT's own lprops table
 * @ltab_offs: offset of LPT's own lprops table
 * @lsave_lnum: LEB number of LPT's save table (big model only)
 * @lsave_offs: offset of LPT's save table (big model only)
 * @lscan_lnum: LEB number of last LPT scan
 * @empty_lebs: number of empty logical eraseblocks
 * @idx_lebs: number of indexing logical eraseblocks
 * @leb_cnt: count of LEBs used by file-system
 * @padding: reserved for future, zeroes
 */
struct ubifs_mst_node {
	struct ubifs_ch ch;
	__le64 highest_inum;
	__le64 cmt_no;
	__le32 flags;
	__le32 log_lnum;
	__le32 root_lnum;
	__le32 root_offs;
	__le32 root_len;
	__le32 gc_lnum;
	__le32 ihead_lnum;
	__le32 ihead_offs;
	__le64 index_size;
	__le64 total_free;
	__le64 total_dirty;
	__le64 total_used;
	__le64 total_dead;
	__le64 total_dark;
	__le32 lpt_lnum;
	__le32 lpt_offs;
	__le32 nhead_lnum;
	__le32 nhead_offs;
	__le32 ltab_lnum;
	__le32 ltab_offs;
	__le32 lsave_lnum;
	__le32 lsave_offs;
	__le32 lscan_lnum;
	__le32 empty_lebs;
	__le32 idx_lebs;
	__le32 leb_cnt;
	__u8 padding[344];
} __packed;

/**
 * struct ubifs_ref_node - logical eraseblock reference node.
 * @ch: common header
 * @lnum: the referred logical eraseblock number
 * @offs: start offset in the referred LEB
 * @jhead: journal head number
 * @padding: reserved for future, zeroes
 */
struct ubifs_ref_node {
	struct ubifs_ch ch;
	__le32 lnum;
	__le32 offs;
	__le32 jhead;
	__u8 padding[28];
} __packed;

/**
 * struct ubifs_branch - key/reference/length branch
 * @lnum: LEB number of the target node
 * @offs: offset within @lnum
 * @len: target node length
 * @key: key
 */
struct ubifs_branch {
	__le32 lnum;
	__le32 offs;
	__le32 len;
	__u8 key[];
} __packed;

/**
 * struct ubifs_idx_node - indexing node.
 * @ch: common header
 * @child_cnt: number of child index nodes
 * @level: tree level
 * @branches: LEB number / offset / length / key branches
 */
struct ubifs_idx_node {
	struct ubifs_ch ch;
	__le16 child_cnt;
	__le16 level;
	__u8 branches[];
} __packed;

/**
 * struct ubifs_cs_node - commit start node.
 * @ch: common header
 * @cmt_no: commit number
 */
struct ubifs_cs_node {
	struct ubifs_ch ch;
	__le64 cmt_no;
} __packed;

/**
 * struct ubifs_orph_node - orphan node.
 * @ch: common header
 * @cmt_no: commit number (also top bit is set on the last node of the commit)
 * @inos: inode numbers of orphans
 */
struct ubifs_orph_node {
	struct ubifs_ch ch;
	__le64 cmt_no;
	__le64 inos[];
} __packed;

#endif /* __UBIFS_MEDIA_H__ */
