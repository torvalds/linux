// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DA_FORMAT_H__
#define __XFS_DA_FORMAT_H__

/*
 * This structure is common to both leaf nodes and non-leaf nodes in the Btree.
 *
 * It is used to manage a doubly linked list of all blocks at the same
 * level in the Btree, and to identify which type of block this is.
 */
#define XFS_DA_NODE_MAGIC	0xfebe	/* magic number: non-leaf blocks */
#define XFS_ATTR_LEAF_MAGIC	0xfbee	/* magic number: attribute leaf blks */
#define	XFS_DIR2_LEAF1_MAGIC	0xd2f1	/* magic number: v2 dirlf single blks */
#define	XFS_DIR2_LEAFN_MAGIC	0xd2ff	/* magic number: v2 dirlf multi blks */

typedef struct xfs_da_blkinfo {
	__be32		forw;			/* previous block in list */
	__be32		back;			/* following block in list */
	__be16		magic;			/* validity check on block */
	__be16		pad;			/* unused */
} xfs_da_blkinfo_t;

/*
 * CRC enabled directory structure types
 *
 * The headers change size for the additional verification information, but
 * otherwise the tree layouts and contents are unchanged. Hence the da btree
 * code can use the struct xfs_da_blkinfo for manipulating the tree links and
 * magic numbers without modification for both v2 and v3 nodes.
 */
#define XFS_DA3_NODE_MAGIC	0x3ebe	/* magic number: non-leaf blocks */
#define XFS_ATTR3_LEAF_MAGIC	0x3bee	/* magic number: attribute leaf blks */
#define	XFS_DIR3_LEAF1_MAGIC	0x3df1	/* magic number: v2 dirlf single blks */
#define	XFS_DIR3_LEAFN_MAGIC	0x3dff	/* magic number: v2 dirlf multi blks */

struct xfs_da3_blkinfo {
	/*
	 * the node link manipulation code relies on the fact that the first
	 * element of this structure is the struct xfs_da_blkinfo so it can
	 * ignore the differences in the rest of the structures.
	 */
	struct xfs_da_blkinfo	hdr;
	__be32			crc;	/* CRC of block */
	__be64			blkno;	/* first block of the buffer */
	__be64			lsn;	/* sequence number of last write */
	uuid_t			uuid;	/* filesystem we belong to */
	__be64			owner;	/* inode that owns the block */
};

/*
 * This is the structure of the root and intermediate nodes in the Btree.
 * The leaf nodes are defined above.
 *
 * Entries are not packed.
 *
 * Since we have duplicate keys, use a binary search but always follow
 * all match in the block, not just the first match found.
 */
#define	XFS_DA_NODE_MAXDEPTH	5	/* max depth of Btree */

typedef struct xfs_da_node_hdr {
	struct xfs_da_blkinfo	info;	/* block type, links, etc. */
	__be16			__count; /* count of active entries */
	__be16			__level; /* level above leaves (leaf == 0) */
} xfs_da_node_hdr_t;

struct xfs_da3_node_hdr {
	struct xfs_da3_blkinfo	info;	/* block type, links, etc. */
	__be16			__count; /* count of active entries */
	__be16			__level; /* level above leaves (leaf == 0) */
	__be32			__pad32;
};

#define XFS_DA3_NODE_CRC_OFF	(offsetof(struct xfs_da3_node_hdr, info.crc))

typedef struct xfs_da_node_entry {
	__be32	hashval;	/* hash value for this descendant */
	__be32	before;		/* Btree block before this key */
} xfs_da_node_entry_t;

typedef struct xfs_da_intnode {
	struct xfs_da_node_hdr	hdr;
	struct xfs_da_node_entry __btree[];
} xfs_da_intnode_t;

struct xfs_da3_intnode {
	struct xfs_da3_node_hdr	hdr;
	struct xfs_da_node_entry __btree[];
};

/*
 * Directory version 2.
 *
 * There are 4 possible formats:
 *  - shortform - embedded into the inode
 *  - single block - data with embedded leaf at the end
 *  - multiple data blocks, single leaf+freeindex block
 *  - data blocks, node and leaf blocks (btree), freeindex blocks
 *
 * Note: many node blocks structures and constants are shared with the attr
 * code and defined in xfs_da_btree.h.
 */

#define	XFS_DIR2_BLOCK_MAGIC	0x58443242	/* XD2B: single block dirs */
#define	XFS_DIR2_DATA_MAGIC	0x58443244	/* XD2D: multiblock dirs */
#define	XFS_DIR2_FREE_MAGIC	0x58443246	/* XD2F: free index blocks */

/*
 * Directory Version 3 With CRCs.
 *
 * The tree formats are the same as for version 2 directories.  The difference
 * is in the block header and dirent formats. In many cases the v3 structures
 * use v2 definitions as they are no different and this makes code sharing much
 * easier.
 *
 * Also, the xfs_dir3_*() functions handle both v2 and v3 formats - if the
 * format is v2 then they switch to the existing v2 code, or the format is v3
 * they implement the v3 functionality. This means the existing dir2 is a mix of
 * xfs_dir2/xfs_dir3 calls and functions. The xfs_dir3 functions are called
 * where there is a difference in the formats, otherwise the code is unchanged.
 *
 * Where it is possible, the code decides what to do based on the magic numbers
 * in the blocks rather than feature bits in the superblock. This means the code
 * is as independent of the external XFS code as possible as doesn't require
 * passing struct xfs_mount pointers into places where it isn't really
 * necessary.
 *
 * Version 3 includes:
 *
 *	- a larger block header for CRC and identification purposes and so the
 *	offsets of all the structures inside the blocks are different.
 *
 *	- new magic numbers to be able to detect the v2/v3 types on the fly.
 */

#define	XFS_DIR3_BLOCK_MAGIC	0x58444233	/* XDB3: single block dirs */
#define	XFS_DIR3_DATA_MAGIC	0x58444433	/* XDD3: multiblock dirs */
#define	XFS_DIR3_FREE_MAGIC	0x58444633	/* XDF3: free index blocks */

/*
 * Dirents in version 3 directories have a file type field. Additions to this
 * list are an on-disk format change, requiring feature bits. Valid values
 * are as follows:
 */
#define XFS_DIR3_FT_UNKNOWN		0
#define XFS_DIR3_FT_REG_FILE		1
#define XFS_DIR3_FT_DIR			2
#define XFS_DIR3_FT_CHRDEV		3
#define XFS_DIR3_FT_BLKDEV		4
#define XFS_DIR3_FT_FIFO		5
#define XFS_DIR3_FT_SOCK		6
#define XFS_DIR3_FT_SYMLINK		7
#define XFS_DIR3_FT_WHT			8

#define XFS_DIR3_FT_MAX			9

/*
 * Byte offset in data block and shortform entry.
 */
typedef uint16_t	xfs_dir2_data_off_t;
#define	NULLDATAOFF	0xffffU
typedef uint		xfs_dir2_data_aoff_t;	/* argument form */

/*
 * Offset in data space of a data entry.
 */
typedef uint32_t	xfs_dir2_dataptr_t;
#define	XFS_DIR2_MAX_DATAPTR	((xfs_dir2_dataptr_t)0xffffffff)
#define	XFS_DIR2_NULL_DATAPTR	((xfs_dir2_dataptr_t)0)

/*
 * Byte offset in a directory.
 */
typedef	xfs_off_t	xfs_dir2_off_t;

/*
 * Directory block number (logical dirblk in file)
 */
typedef uint32_t	xfs_dir2_db_t;

#define XFS_INO32_SIZE	4
#define XFS_INO64_SIZE	8
#define XFS_INO64_DIFF	(XFS_INO64_SIZE - XFS_INO32_SIZE)

#define	XFS_DIR2_MAX_SHORT_INUM	((xfs_ino_t)0xffffffffULL)

/*
 * Directory layout when stored internal to an inode.
 *
 * Small directories are packed as tightly as possible so as to fit into the
 * literal area of the inode.  These "shortform" directories consist of a
 * single xfs_dir2_sf_hdr header followed by zero or more xfs_dir2_sf_entry
 * structures.  Due the different inode number storage size and the variable
 * length name field in the xfs_dir2_sf_entry all these structure are
 * variable length, and the accessors in this file should be used to iterate
 * over them.
 */
typedef struct xfs_dir2_sf_hdr {
	uint8_t			count;		/* count of entries */
	uint8_t			i8count;	/* count of 8-byte inode #s */
	uint8_t			parent[8];	/* parent dir inode number */
} __packed xfs_dir2_sf_hdr_t;

typedef struct xfs_dir2_sf_entry {
	__u8			namelen;	/* actual name length */
	__u8			offset[2];	/* saved offset */
	__u8			name[];		/* name, variable size */
	/*
	 * A single byte containing the file type field follows the inode
	 * number for version 3 directory entries.
	 *
	 * A 64-bit or 32-bit inode number follows here, at a variable offset
	 * after the name.
	 */
} xfs_dir2_sf_entry_t;

static inline int xfs_dir2_sf_hdr_size(int i8count)
{
	return sizeof(struct xfs_dir2_sf_hdr) -
		(i8count == 0) * XFS_INO64_DIFF;
}

static inline xfs_dir2_data_aoff_t
xfs_dir2_sf_get_offset(xfs_dir2_sf_entry_t *sfep)
{
	return get_unaligned_be16(sfep->offset);
}

static inline void
xfs_dir2_sf_put_offset(xfs_dir2_sf_entry_t *sfep, xfs_dir2_data_aoff_t off)
{
	put_unaligned_be16(off, sfep->offset);
}

static inline struct xfs_dir2_sf_entry *
xfs_dir2_sf_firstentry(struct xfs_dir2_sf_hdr *hdr)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)hdr + xfs_dir2_sf_hdr_size(hdr->i8count));
}

/*
 * Data block structures.
 *
 * A pure data block looks like the following drawing on disk:
 *
 *    +-------------------------------------------------+
 *    | xfs_dir2_data_hdr_t                             |
 *    +-------------------------------------------------+
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t |
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t |
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t |
 *    | ...                                             |
 *    +-------------------------------------------------+
 *    | unused space                                    |
 *    +-------------------------------------------------+
 *
 * As all the entries are variable size structures the accessors below should
 * be used to iterate over them.
 *
 * In addition to the pure data blocks for the data and node formats,
 * most structures are also used for the combined data/freespace "block"
 * format below.
 */

#define	XFS_DIR2_DATA_ALIGN_LOG	3		/* i.e., 8 bytes */
#define	XFS_DIR2_DATA_ALIGN	(1 << XFS_DIR2_DATA_ALIGN_LOG)
#define	XFS_DIR2_DATA_FREE_TAG	0xffff
#define	XFS_DIR2_DATA_FD_COUNT	3

/*
 * Directory address space divided into sections,
 * spaces separated by 32GB.
 */
#define	XFS_DIR2_SPACE_SIZE	(1ULL << (32 + XFS_DIR2_DATA_ALIGN_LOG))
#define	XFS_DIR2_DATA_SPACE	0
#define	XFS_DIR2_DATA_OFFSET	(XFS_DIR2_DATA_SPACE * XFS_DIR2_SPACE_SIZE)

/*
 * Describe a free area in the data block.
 *
 * The freespace will be formatted as a xfs_dir2_data_unused_t.
 */
typedef struct xfs_dir2_data_free {
	__be16			offset;		/* start of freespace */
	__be16			length;		/* length of freespace */
} xfs_dir2_data_free_t;

/*
 * Header for the data blocks.
 *
 * The code knows that XFS_DIR2_DATA_FD_COUNT is 3.
 */
typedef struct xfs_dir2_data_hdr {
	__be32			magic;		/* XFS_DIR2_DATA_MAGIC or */
						/* XFS_DIR2_BLOCK_MAGIC */
	xfs_dir2_data_free_t	bestfree[XFS_DIR2_DATA_FD_COUNT];
} xfs_dir2_data_hdr_t;

/*
 * define a structure for all the verification fields we are adding to the
 * directory block structures. This will be used in several structures.
 * The magic number must be the first entry to align with all the dir2
 * structures so we determine how to decode them just by the magic number.
 */
struct xfs_dir3_blk_hdr {
	__be32			magic;	/* magic number */
	__be32			crc;	/* CRC of block */
	__be64			blkno;	/* first block of the buffer */
	__be64			lsn;	/* sequence number of last write */
	uuid_t			uuid;	/* filesystem we belong to */
	__be64			owner;	/* inode that owns the block */
};

struct xfs_dir3_data_hdr {
	struct xfs_dir3_blk_hdr	hdr;
	xfs_dir2_data_free_t	best_free[XFS_DIR2_DATA_FD_COUNT];
	__be32			pad;	/* 64 bit alignment */
};

#define XFS_DIR3_DATA_CRC_OFF  offsetof(struct xfs_dir3_data_hdr, hdr.crc)

/*
 * Active entry in a data block.
 *
 * Aligned to 8 bytes.  After the variable length name field there is a
 * 2 byte tag field, which can be accessed using xfs_dir3_data_entry_tag_p.
 *
 * For dir3 structures, there is file type field between the name and the tag.
 * This can only be manipulated by helper functions. It is packed hard against
 * the end of the name so any padding for rounding is between the file type and
 * the tag.
 */
typedef struct xfs_dir2_data_entry {
	__be64			inumber;	/* inode number */
	__u8			namelen;	/* name length */
	__u8			name[];		/* name bytes, no null */
     /* __u8			filetype; */	/* type of inode we point to */
     /*	__be16                  tag; */		/* starting offset of us */
} xfs_dir2_data_entry_t;

/*
 * Unused entry in a data block.
 *
 * Aligned to 8 bytes.  Tag appears as the last 2 bytes and must be accessed
 * using xfs_dir2_data_unused_tag_p.
 */
typedef struct xfs_dir2_data_unused {
	__be16			freetag;	/* XFS_DIR2_DATA_FREE_TAG */
	__be16			length;		/* total free length */
						/* variable offset */
	__be16			tag;		/* starting offset of us */
} xfs_dir2_data_unused_t;

/*
 * Pointer to a freespace's tag word.
 */
static inline __be16 *
xfs_dir2_data_unused_tag_p(struct xfs_dir2_data_unused *dup)
{
	return (__be16 *)((char *)dup +
			be16_to_cpu(dup->length) - sizeof(__be16));
}

/*
 * Leaf block structures.
 *
 * A pure leaf block looks like the following drawing on disk:
 *
 *    +---------------------------+
 *    | xfs_dir2_leaf_hdr_t       |
 *    +---------------------------+
 *    | xfs_dir2_leaf_entry_t     |
 *    | xfs_dir2_leaf_entry_t     |
 *    | xfs_dir2_leaf_entry_t     |
 *    | xfs_dir2_leaf_entry_t     |
 *    | ...                       |
 *    +---------------------------+
 *    | xfs_dir2_data_off_t       |
 *    | xfs_dir2_data_off_t       |
 *    | xfs_dir2_data_off_t       |
 *    | ...                       |
 *    +---------------------------+
 *    | xfs_dir2_leaf_tail_t      |
 *    +---------------------------+
 *
 * The xfs_dir2_data_off_t members (bests) and tail are at the end of the block
 * for single-leaf (magic = XFS_DIR2_LEAF1_MAGIC) blocks only, but not present
 * for directories with separate leaf nodes and free space blocks
 * (magic = XFS_DIR2_LEAFN_MAGIC).
 *
 * As all the entries are variable size structures the accessors below should
 * be used to iterate over them.
 */

/*
 * Offset of the leaf/node space.  First block in this space
 * is the btree root.
 */
#define	XFS_DIR2_LEAF_SPACE	1
#define	XFS_DIR2_LEAF_OFFSET	(XFS_DIR2_LEAF_SPACE * XFS_DIR2_SPACE_SIZE)

/*
 * Leaf block header.
 */
typedef struct xfs_dir2_leaf_hdr {
	xfs_da_blkinfo_t	info;		/* header for da routines */
	__be16			count;		/* count of entries */
	__be16			stale;		/* count of stale entries */
} xfs_dir2_leaf_hdr_t;

struct xfs_dir3_leaf_hdr {
	struct xfs_da3_blkinfo	info;		/* header for da routines */
	__be16			count;		/* count of entries */
	__be16			stale;		/* count of stale entries */
	__be32			pad;		/* 64 bit alignment */
};

/*
 * Leaf block entry.
 */
typedef struct xfs_dir2_leaf_entry {
	__be32			hashval;	/* hash value of name */
	__be32			address;	/* address of data entry */
} xfs_dir2_leaf_entry_t;

/*
 * Leaf block tail.
 */
typedef struct xfs_dir2_leaf_tail {
	__be32			bestcount;
} xfs_dir2_leaf_tail_t;

/*
 * Leaf block.
 */
typedef struct xfs_dir2_leaf {
	xfs_dir2_leaf_hdr_t	hdr;			/* leaf header */
	xfs_dir2_leaf_entry_t	__ents[];		/* entries */
} xfs_dir2_leaf_t;

struct xfs_dir3_leaf {
	struct xfs_dir3_leaf_hdr	hdr;		/* leaf header */
	struct xfs_dir2_leaf_entry	__ents[];	/* entries */
};

#define XFS_DIR3_LEAF_CRC_OFF  offsetof(struct xfs_dir3_leaf_hdr, info.crc)

/*
 * Get address of the bests array in the single-leaf block.
 */
static inline __be16 *
xfs_dir2_leaf_bests_p(struct xfs_dir2_leaf_tail *ltp)
{
	return (__be16 *)ltp - be32_to_cpu(ltp->bestcount);
}

/*
 * Free space block definitions for the node format.
 */

/*
 * Offset of the freespace index.
 */
#define	XFS_DIR2_FREE_SPACE	2
#define	XFS_DIR2_FREE_OFFSET	(XFS_DIR2_FREE_SPACE * XFS_DIR2_SPACE_SIZE)

typedef	struct xfs_dir2_free_hdr {
	__be32			magic;		/* XFS_DIR2_FREE_MAGIC */
	__be32			firstdb;	/* db of first entry */
	__be32			nvalid;		/* count of valid entries */
	__be32			nused;		/* count of used entries */
} xfs_dir2_free_hdr_t;

typedef struct xfs_dir2_free {
	xfs_dir2_free_hdr_t	hdr;		/* block header */
	__be16			bests[];	/* best free counts */
						/* unused entries are -1 */
} xfs_dir2_free_t;

struct xfs_dir3_free_hdr {
	struct xfs_dir3_blk_hdr	hdr;
	__be32			firstdb;	/* db of first entry */
	__be32			nvalid;		/* count of valid entries */
	__be32			nused;		/* count of used entries */
	__be32			pad;		/* 64 bit alignment */
};

struct xfs_dir3_free {
	struct xfs_dir3_free_hdr hdr;
	__be16			bests[];	/* best free counts */
						/* unused entries are -1 */
};

#define XFS_DIR3_FREE_CRC_OFF  offsetof(struct xfs_dir3_free, hdr.hdr.crc)

/*
 * Single block format.
 *
 * The single block format looks like the following drawing on disk:
 *
 *    +-------------------------------------------------+
 *    | xfs_dir2_data_hdr_t                             |
 *    +-------------------------------------------------+
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t |
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t |
 *    | xfs_dir2_data_entry_t OR xfs_dir2_data_unused_t :
 *    | ...                                             |
 *    +-------------------------------------------------+
 *    | unused space                                    |
 *    +-------------------------------------------------+
 *    | ...                                             |
 *    | xfs_dir2_leaf_entry_t                           |
 *    | xfs_dir2_leaf_entry_t                           |
 *    +-------------------------------------------------+
 *    | xfs_dir2_block_tail_t                           |
 *    +-------------------------------------------------+
 *
 * As all the entries are variable size structures the accessors below should
 * be used to iterate over them.
 */

typedef struct xfs_dir2_block_tail {
	__be32		count;			/* count of leaf entries */
	__be32		stale;			/* count of stale lf entries */
} xfs_dir2_block_tail_t;

/*
 * Pointer to the leaf entries embedded in a data block (1-block format)
 */
static inline struct xfs_dir2_leaf_entry *
xfs_dir2_block_leaf_p(struct xfs_dir2_block_tail *btp)
{
	return ((struct xfs_dir2_leaf_entry *)btp) - be32_to_cpu(btp->count);
}


/*
 * Attribute storage layout
 *
 * Attribute lists are structured around Btrees where all the data
 * elements are in the leaf nodes.  Attribute names are hashed into an int,
 * then that int is used as the index into the Btree.  Since the hashval
 * of an attribute name may not be unique, we may have duplicate keys.  The
 * internal links in the Btree are logical block offsets into the file.
 *
 * Struct leaf_entry's are packed from the top.  Name/values grow from the
 * bottom but are not packed.  The freemap contains run-length-encoded entries
 * for the free bytes after the leaf_entry's, but only the N largest such,
 * smaller runs are dropped.  When the freemap doesn't show enough space
 * for an allocation, we compact the name/value area and try again.  If we
 * still don't have enough space, then we have to split the block.  The
 * name/value structs (both local and remote versions) must be 32bit aligned.
 *
 * Since we have duplicate hash keys, for each key that matches, compare
 * the actual name string.  The root and intermediate node search always
 * takes the first-in-the-block key match found, so we should only have
 * to work "forw"ard.  If none matches, continue with the "forw"ard leaf
 * nodes until the hash key changes or the attribute name is found.
 *
 * We store the fact that an attribute is a ROOT/USER/SECURE attribute in
 * the leaf_entry.  The namespaces are independent only because we also look
 * at the namespace bit when we are looking for a matching attribute name.
 *
 * We also store an "incomplete" bit in the leaf_entry.  It shows that an
 * attribute is in the middle of being created and should not be shown to
 * the user if we crash during the time that the bit is set.  We clear the
 * bit when we have finished setting up the attribute.  We do this because
 * we cannot create some large attributes inside a single transaction, and we
 * need some indication that we weren't finished if we crash in the middle.
 */
#define XFS_ATTR_LEAF_MAPSIZE	3	/* how many freespace slots */

/*
 * Entries are packed toward the top as tight as possible.
 */
typedef struct xfs_attr_shortform {
	struct xfs_attr_sf_hdr {	/* constant-structure header block */
		__be16	totsize;	/* total bytes in shortform list */
		__u8	count;	/* count of active entries */
		__u8	padding;
	} hdr;
	struct xfs_attr_sf_entry {
		uint8_t namelen;	/* actual length of name (no NULL) */
		uint8_t valuelen;	/* actual length of value (no NULL) */
		uint8_t flags;	/* flags bits (see xfs_attr_leaf.h) */
		uint8_t nameval[1];	/* name & value bytes concatenated */
	} list[1];			/* variable sized array */
} xfs_attr_shortform_t;

typedef struct xfs_attr_leaf_map {	/* RLE map of free bytes */
	__be16	base;			  /* base of free region */
	__be16	size;			  /* length of free region */
} xfs_attr_leaf_map_t;

typedef struct xfs_attr_leaf_hdr {	/* constant-structure header block */
	xfs_da_blkinfo_t info;		/* block type, links, etc. */
	__be16	count;			/* count of active leaf_entry's */
	__be16	usedbytes;		/* num bytes of names/values stored */
	__be16	firstused;		/* first used byte in name area */
	__u8	holes;			/* != 0 if blk needs compaction */
	__u8	pad1;
	xfs_attr_leaf_map_t freemap[XFS_ATTR_LEAF_MAPSIZE];
					/* N largest free regions */
} xfs_attr_leaf_hdr_t;

typedef struct xfs_attr_leaf_entry {	/* sorted on key, not name */
	__be32	hashval;		/* hash value of name */
	__be16	nameidx;		/* index into buffer of name/value */
	__u8	flags;			/* LOCAL/ROOT/SECURE/INCOMPLETE flag */
	__u8	pad2;			/* unused pad byte */
} xfs_attr_leaf_entry_t;

typedef struct xfs_attr_leaf_name_local {
	__be16	valuelen;		/* number of bytes in value */
	__u8	namelen;		/* length of name bytes */
	__u8	nameval[1];		/* name/value bytes */
} xfs_attr_leaf_name_local_t;

typedef struct xfs_attr_leaf_name_remote {
	__be32	valueblk;		/* block number of value bytes */
	__be32	valuelen;		/* number of bytes in value */
	__u8	namelen;		/* length of name bytes */
	__u8	name[1];		/* name bytes */
} xfs_attr_leaf_name_remote_t;

typedef struct xfs_attr_leafblock {
	xfs_attr_leaf_hdr_t	hdr;	/* constant-structure header block */
	xfs_attr_leaf_entry_t	entries[1];	/* sorted on key, not name */
	/*
	 * The rest of the block contains the following structures after the
	 * leaf entries, growing from the bottom up. The variables are never
	 * referenced and definining them can actually make gcc optimize away
	 * accesses to the 'entries' array above index 0 so don't do that.
	 *
	 * xfs_attr_leaf_name_local_t namelist;
	 * xfs_attr_leaf_name_remote_t valuelist;
	 */
} xfs_attr_leafblock_t;

/*
 * CRC enabled leaf structures. Called "version 3" structures to match the
 * version number of the directory and dablk structures for this feature, and
 * attr2 is already taken by the variable inode attribute fork size feature.
 */
struct xfs_attr3_leaf_hdr {
	struct xfs_da3_blkinfo	info;
	__be16			count;
	__be16			usedbytes;
	__be16			firstused;
	__u8			holes;
	__u8			pad1;
	struct xfs_attr_leaf_map freemap[XFS_ATTR_LEAF_MAPSIZE];
	__be32			pad2;		/* 64 bit alignment */
};

#define XFS_ATTR3_LEAF_CRC_OFF	(offsetof(struct xfs_attr3_leaf_hdr, info.crc))

struct xfs_attr3_leafblock {
	struct xfs_attr3_leaf_hdr	hdr;
	struct xfs_attr_leaf_entry	entries[1];

	/*
	 * The rest of the block contains the following structures after the
	 * leaf entries, growing from the bottom up. The variables are never
	 * referenced, the locations accessed purely from helper functions.
	 *
	 * struct xfs_attr_leaf_name_local
	 * struct xfs_attr_leaf_name_remote
	 */
};

/*
 * Special value to represent fs block size in the leaf header firstused field.
 * Only used when block size overflows the 2-bytes available on disk.
 */
#define XFS_ATTR3_LEAF_NULLOFF	0

/*
 * Flags used in the leaf_entry[i].flags field.
 * NOTE: the INCOMPLETE bit must not collide with the flags bits specified
 * on the system call, they are "or"ed together for various operations.
 */
#define	XFS_ATTR_LOCAL_BIT	0	/* attr is stored locally */
#define	XFS_ATTR_ROOT_BIT	1	/* limit access to trusted attrs */
#define	XFS_ATTR_SECURE_BIT	2	/* limit access to secure attrs */
#define	XFS_ATTR_INCOMPLETE_BIT	7	/* attr in middle of create/delete */
#define XFS_ATTR_LOCAL		(1 << XFS_ATTR_LOCAL_BIT)
#define XFS_ATTR_ROOT		(1 << XFS_ATTR_ROOT_BIT)
#define XFS_ATTR_SECURE		(1 << XFS_ATTR_SECURE_BIT)
#define XFS_ATTR_INCOMPLETE	(1 << XFS_ATTR_INCOMPLETE_BIT)

/*
 * Conversion macros for converting namespace bits from argument flags
 * to ondisk flags.
 */
#define XFS_ATTR_NSP_ARGS_MASK		(ATTR_ROOT | ATTR_SECURE)
#define XFS_ATTR_NSP_ONDISK_MASK	(XFS_ATTR_ROOT | XFS_ATTR_SECURE)
#define XFS_ATTR_NSP_ONDISK(flags)	((flags) & XFS_ATTR_NSP_ONDISK_MASK)
#define XFS_ATTR_NSP_ARGS(flags)	((flags) & XFS_ATTR_NSP_ARGS_MASK)
#define XFS_ATTR_NSP_ARGS_TO_ONDISK(x)	(((x) & ATTR_ROOT ? XFS_ATTR_ROOT : 0) |\
					 ((x) & ATTR_SECURE ? XFS_ATTR_SECURE : 0))
#define XFS_ATTR_NSP_ONDISK_TO_ARGS(x)	(((x) & XFS_ATTR_ROOT ? ATTR_ROOT : 0) |\
					 ((x) & XFS_ATTR_SECURE ? ATTR_SECURE : 0))

/*
 * Alignment for namelist and valuelist entries (since they are mixed
 * there can be only one alignment value)
 */
#define	XFS_ATTR_LEAF_NAME_ALIGN	((uint)sizeof(xfs_dablk_t))

static inline int
xfs_attr3_leaf_hdr_size(struct xfs_attr_leafblock *leafp)
{
	if (leafp->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC))
		return sizeof(struct xfs_attr3_leaf_hdr);
	return sizeof(struct xfs_attr_leaf_hdr);
}

static inline struct xfs_attr_leaf_entry *
xfs_attr3_leaf_entryp(xfs_attr_leafblock_t *leafp)
{
	if (leafp->hdr.info.magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC))
		return &((struct xfs_attr3_leafblock *)leafp)->entries[0];
	return &leafp->entries[0];
}

/*
 * Cast typed pointers for "local" and "remote" name/value structs.
 */
static inline char *
xfs_attr3_leaf_name(xfs_attr_leafblock_t *leafp, int idx)
{
	struct xfs_attr_leaf_entry *entries = xfs_attr3_leaf_entryp(leafp);

	return &((char *)leafp)[be16_to_cpu(entries[idx].nameidx)];
}

static inline xfs_attr_leaf_name_remote_t *
xfs_attr3_leaf_name_remote(xfs_attr_leafblock_t *leafp, int idx)
{
	return (xfs_attr_leaf_name_remote_t *)xfs_attr3_leaf_name(leafp, idx);
}

static inline xfs_attr_leaf_name_local_t *
xfs_attr3_leaf_name_local(xfs_attr_leafblock_t *leafp, int idx)
{
	return (xfs_attr_leaf_name_local_t *)xfs_attr3_leaf_name(leafp, idx);
}

/*
 * Calculate total bytes used (including trailing pad for alignment) for
 * a "local" name/value structure, a "remote" name/value structure, and
 * a pointer which might be either.
 */
static inline int xfs_attr_leaf_entsize_remote(int nlen)
{
	return ((uint)sizeof(xfs_attr_leaf_name_remote_t) - 1 + (nlen) + \
		XFS_ATTR_LEAF_NAME_ALIGN - 1) & ~(XFS_ATTR_LEAF_NAME_ALIGN - 1);
}

static inline int xfs_attr_leaf_entsize_local(int nlen, int vlen)
{
	return ((uint)sizeof(xfs_attr_leaf_name_local_t) - 1 + (nlen) + (vlen) +
		XFS_ATTR_LEAF_NAME_ALIGN - 1) & ~(XFS_ATTR_LEAF_NAME_ALIGN - 1);
}

static inline int xfs_attr_leaf_entsize_local_max(int bsize)
{
	return (((bsize) >> 1) + ((bsize) >> 2));
}



/*
 * Remote attribute block format definition
 *
 * There is one of these headers per filesystem block in a remote attribute.
 * This is done to ensure there is a 1:1 mapping between the attribute value
 * length and the number of blocks needed to store the attribute. This makes the
 * verification of a buffer a little more complex, but greatly simplifies the
 * allocation, reading and writing of these attributes as we don't have to guess
 * the number of blocks needed to store the attribute data.
 */
#define XFS_ATTR3_RMT_MAGIC	0x5841524d	/* XARM */

struct xfs_attr3_rmt_hdr {
	__be32	rm_magic;
	__be32	rm_offset;
	__be32	rm_bytes;
	__be32	rm_crc;
	uuid_t	rm_uuid;
	__be64	rm_owner;
	__be64	rm_blkno;
	__be64	rm_lsn;
};

#define XFS_ATTR3_RMT_CRC_OFF	offsetof(struct xfs_attr3_rmt_hdr, rm_crc)

#define XFS_ATTR3_RMT_BUF_SPACE(mp, bufsize)	\
	((bufsize) - (xfs_sb_version_hascrc(&(mp)->m_sb) ? \
			sizeof(struct xfs_attr3_rmt_hdr) : 0))

/* Number of bytes in a directory block. */
static inline unsigned int xfs_dir2_dirblock_bytes(struct xfs_sb *sbp)
{
	return 1 << (sbp->sb_blocklog + sbp->sb_dirblklog);
}

xfs_failaddr_t xfs_da3_blkinfo_verify(struct xfs_buf *bp,
				      struct xfs_da3_blkinfo *hdr3);

#endif /* __XFS_DA_FORMAT_H__ */
