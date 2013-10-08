/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_DIR2_FORMAT_H__
#define __XFS_DIR2_FORMAT_H__

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
typedef	__uint16_t	xfs_dir2_data_off_t;
#define	NULLDATAOFF	0xffffU
typedef uint		xfs_dir2_data_aoff_t;	/* argument form */

/*
 * Normalized offset (in a data block) of the entry, really xfs_dir2_data_off_t.
 * Only need 16 bits, this is the byte offset into the single block form.
 */
typedef struct { __uint8_t i[2]; } __arch_pack xfs_dir2_sf_off_t;

/*
 * Offset in data space of a data entry.
 */
typedef	__uint32_t	xfs_dir2_dataptr_t;
#define	XFS_DIR2_MAX_DATAPTR	((xfs_dir2_dataptr_t)0xffffffff)
#define	XFS_DIR2_NULL_DATAPTR	((xfs_dir2_dataptr_t)0)

/*
 * Byte offset in a directory.
 */
typedef	xfs_off_t	xfs_dir2_off_t;

/*
 * Directory block number (logical dirblk in file)
 */
typedef	__uint32_t	xfs_dir2_db_t;

/*
 * Inode number stored as 8 8-bit values.
 */
typedef	struct { __uint8_t i[8]; } xfs_dir2_ino8_t;

/*
 * Inode number stored as 4 8-bit values.
 * Works a lot of the time, when all the inode numbers in a directory
 * fit in 32 bits.
 */
typedef struct { __uint8_t i[4]; } xfs_dir2_ino4_t;

typedef union {
	xfs_dir2_ino8_t	i8;
	xfs_dir2_ino4_t	i4;
} xfs_dir2_inou_t;
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
	__uint8_t		count;		/* count of entries */
	__uint8_t		i8count;	/* count of 8-byte inode #s */
	xfs_dir2_inou_t		parent;		/* parent dir inode number */
} __arch_pack xfs_dir2_sf_hdr_t;

typedef struct xfs_dir2_sf_entry {
	__u8			namelen;	/* actual name length */
	xfs_dir2_sf_off_t	offset;		/* saved offset */
	__u8			name[];		/* name, variable size */
	/*
	 * A single byte containing the file type field follows the inode
	 * number for version 3 directory entries.
	 *
	 * A xfs_dir2_ino8_t or xfs_dir2_ino4_t follows here, at a
	 * variable offset after the name.
	 */
} __arch_pack xfs_dir2_sf_entry_t;

static inline int xfs_dir2_sf_hdr_size(int i8count)
{
	return sizeof(struct xfs_dir2_sf_hdr) -
		(i8count == 0) *
		(sizeof(xfs_dir2_ino8_t) - sizeof(xfs_dir2_ino4_t));
}

static inline xfs_dir2_data_aoff_t
xfs_dir2_sf_get_offset(xfs_dir2_sf_entry_t *sfep)
{
	return get_unaligned_be16(&sfep->offset.i);
}

static inline void
xfs_dir2_sf_put_offset(xfs_dir2_sf_entry_t *sfep, xfs_dir2_data_aoff_t off)
{
	put_unaligned_be16(off, &sfep->offset.i);
}

static inline struct xfs_dir2_sf_entry *
xfs_dir2_sf_firstentry(struct xfs_dir2_sf_hdr *hdr)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)hdr + xfs_dir2_sf_hdr_size(hdr->i8count));
}

static inline int
xfs_dir3_sf_entsize(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	int			len)
{
	int count = sizeof(struct xfs_dir2_sf_entry); 	/* namelen + offset */

	count += len;					/* name */
	count += hdr->i8count ? sizeof(xfs_dir2_ino8_t) :
				sizeof(xfs_dir2_ino4_t); /* ino # */
	if (xfs_sb_version_hasftype(&mp->m_sb))
		count += sizeof(__uint8_t);		/* file type */
	return count;
}

static inline struct xfs_dir2_sf_entry *
xfs_dir3_sf_nextentry(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)sfep + xfs_dir3_sf_entsize(mp, hdr, sfep->namelen));
}

/*
 * in dir3 shortform directories, the file type field is stored at a variable
 * offset after the inode number. Because it's only a single byte, endian
 * conversion is not necessary.
 */
static inline __uint8_t *
xfs_dir3_sfe_ftypep(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return (__uint8_t *)&sfep->name[sfep->namelen];
}

static inline __uint8_t
xfs_dir3_sfe_get_ftype(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	__uint8_t	*ftp;

	if (!xfs_sb_version_hasftype(&mp->m_sb))
		return XFS_DIR3_FT_UNKNOWN;

	ftp = xfs_dir3_sfe_ftypep(hdr, sfep);
	if (*ftp >= XFS_DIR3_FT_MAX)
		return XFS_DIR3_FT_UNKNOWN;
	return *ftp;
}

static inline void
xfs_dir3_sfe_put_ftype(
	struct xfs_mount	*mp,
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep,
	__uint8_t		ftype)
{
	__uint8_t	*ftp;

	ASSERT(ftype < XFS_DIR3_FT_MAX);

	if (!xfs_sb_version_hasftype(&mp->m_sb))
		return;
	ftp = xfs_dir3_sfe_ftypep(hdr, sfep);
	*ftp = ftype;
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
#define	XFS_DIR2_DATA_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_DATA_OFFSET)

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

static inline struct xfs_dir2_data_free *
xfs_dir3_data_bestfree_p(struct xfs_dir2_data_hdr *hdr)
{
	if (hdr->magic == cpu_to_be32(XFS_DIR3_DATA_MAGIC) ||
	    hdr->magic == cpu_to_be32(XFS_DIR3_BLOCK_MAGIC)) {
		struct xfs_dir3_data_hdr *hdr3 = (struct xfs_dir3_data_hdr *)hdr;
		return hdr3->best_free;
	}
	return hdr->bestfree;
}

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
 * Size of a data entry.
 */
static inline int
__xfs_dir3_data_entsize(
	bool	ftype,
	int	n)
{
	int	size = offsetof(struct xfs_dir2_data_entry, name[0]);

	size += n;
	size += sizeof(xfs_dir2_data_off_t);
	if (ftype)
		size += sizeof(__uint8_t);
	return roundup(size, XFS_DIR2_DATA_ALIGN);
}
static inline int
xfs_dir3_data_entsize(
	struct xfs_mount	*mp,
	int			n)
{
	bool ftype = xfs_sb_version_hasftype(&mp->m_sb) ? true : false;
	return __xfs_dir3_data_entsize(ftype, n);
}

static inline __uint8_t
xfs_dir3_dirent_get_ftype(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_entry *dep)
{
	if (xfs_sb_version_hasftype(&mp->m_sb)) {
		__uint8_t	type = dep->name[dep->namelen];

		ASSERT(type < XFS_DIR3_FT_MAX);
		if (type < XFS_DIR3_FT_MAX)
			return type;

	}
	return XFS_DIR3_FT_UNKNOWN;
}

static inline void
xfs_dir3_dirent_put_ftype(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_entry *dep,
	__uint8_t		type)
{
	ASSERT(type < XFS_DIR3_FT_MAX);
	ASSERT(dep->namelen != 0);

	if (xfs_sb_version_hasftype(&mp->m_sb))
		dep->name[dep->namelen] = type;
}

/*
 * Pointer to an entry's tag word.
 */
static inline __be16 *
xfs_dir3_data_entry_tag_p(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_entry *dep)
{
	return (__be16 *)((char *)dep +
		xfs_dir3_data_entsize(mp, dep->namelen) - sizeof(__be16));
}

/*
 * Pointer to a freespace's tag word.
 */
static inline __be16 *
xfs_dir2_data_unused_tag_p(struct xfs_dir2_data_unused *dup)
{
	return (__be16 *)((char *)dup +
			be16_to_cpu(dup->length) - sizeof(__be16));
}

static inline size_t
xfs_dir3_data_hdr_size(bool dir3)
{
	if (dir3)
		return sizeof(struct xfs_dir3_data_hdr);
	return sizeof(struct xfs_dir2_data_hdr);
}

static inline size_t
xfs_dir3_data_entry_offset(struct xfs_dir2_data_hdr *hdr)
{
	bool dir3 = hdr->magic == cpu_to_be32(XFS_DIR3_DATA_MAGIC) ||
		    hdr->magic == cpu_to_be32(XFS_DIR3_BLOCK_MAGIC);
	return xfs_dir3_data_hdr_size(dir3);
}

static inline struct xfs_dir2_data_entry *
xfs_dir3_data_entry_p(struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_entry_offset(hdr));
}

static inline struct xfs_dir2_data_unused *
xfs_dir3_data_unused_p(struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_unused *)
		((char *)hdr + xfs_dir3_data_entry_offset(hdr));
}

/*
 * Offsets of . and .. in data space (always block 0)
 *
 * XXX: there is scope for significant optimisation of the logic here. Right
 * now we are checking for "dir3 format" over and over again. Ideally we should
 * only do it once for each operation.
 */
static inline xfs_dir2_data_aoff_t
xfs_dir3_data_dot_offset(struct xfs_mount *mp)
{
	return xfs_dir3_data_hdr_size(xfs_sb_version_hascrc(&mp->m_sb));
}

static inline xfs_dir2_data_aoff_t
xfs_dir3_data_dotdot_offset(struct xfs_mount *mp)
{
	return xfs_dir3_data_dot_offset(mp) +
		xfs_dir3_data_entsize(mp, 1);
}

static inline xfs_dir2_data_aoff_t
xfs_dir3_data_first_offset(struct xfs_mount *mp)
{
	return xfs_dir3_data_dotdot_offset(mp) +
		xfs_dir3_data_entsize(mp, 2);
}

/*
 * location of . and .. in data space (always block 0)
 */
static inline struct xfs_dir2_data_entry *
xfs_dir3_data_dot_entry_p(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_dot_offset(mp));
}

static inline struct xfs_dir2_data_entry *
xfs_dir3_data_dotdot_entry_p(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_dotdot_offset(mp));
}

static inline struct xfs_dir2_data_entry *
xfs_dir3_data_first_entry_p(
	struct xfs_mount	*mp,
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_first_offset(mp));
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
#define	XFS_DIR2_LEAF_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_LEAF_OFFSET)

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

struct xfs_dir3_icleaf_hdr {
	__uint32_t		forw;
	__uint32_t		back;
	__uint16_t		magic;
	__uint16_t		count;
	__uint16_t		stale;
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

extern void xfs_dir3_leaf_hdr_from_disk(struct xfs_dir3_icleaf_hdr *to,
					struct xfs_dir2_leaf *from);

static inline int
xfs_dir3_leaf_hdr_size(struct xfs_dir2_leaf *lp)
{
	if (lp->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) ||
	    lp->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAFN_MAGIC))
		return sizeof(struct xfs_dir3_leaf_hdr);
	return sizeof(struct xfs_dir2_leaf_hdr);
}

static inline int
xfs_dir3_max_leaf_ents(struct xfs_mount *mp, struct xfs_dir2_leaf *lp)
{
	return (mp->m_dirblksize - xfs_dir3_leaf_hdr_size(lp)) /
		(uint)sizeof(struct xfs_dir2_leaf_entry);
}

/*
 * Get address of the bestcount field in the single-leaf block.
 */
static inline struct xfs_dir2_leaf_entry *
xfs_dir3_leaf_ents_p(struct xfs_dir2_leaf *lp)
{
	if (lp->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAF1_MAGIC) ||
	    lp->hdr.info.magic == cpu_to_be16(XFS_DIR3_LEAFN_MAGIC)) {
		struct xfs_dir3_leaf *lp3 = (struct xfs_dir3_leaf *)lp;
		return lp3->__ents;
	}
	return lp->__ents;
}

/*
 * Get address of the bestcount field in the single-leaf block.
 */
static inline struct xfs_dir2_leaf_tail *
xfs_dir2_leaf_tail_p(struct xfs_mount *mp, struct xfs_dir2_leaf *lp)
{
	return (struct xfs_dir2_leaf_tail *)
		((char *)lp + mp->m_dirblksize -
		  sizeof(struct xfs_dir2_leaf_tail));
}

/*
 * Get address of the bests array in the single-leaf block.
 */
static inline __be16 *
xfs_dir2_leaf_bests_p(struct xfs_dir2_leaf_tail *ltp)
{
	return (__be16 *)ltp - be32_to_cpu(ltp->bestcount);
}

/*
 * DB blocks here are logical directory block numbers, not filesystem blocks.
 */

/*
 * Convert dataptr to byte in file space
 */
static inline xfs_dir2_off_t
xfs_dir2_dataptr_to_byte(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return (xfs_dir2_off_t)dp << XFS_DIR2_DATA_ALIGN_LOG;
}

/*
 * Convert byte in file space to dataptr.  It had better be aligned.
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_byte_to_dataptr(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_dataptr_t)(by >> XFS_DIR2_DATA_ALIGN_LOG);
}

/*
 * Convert byte in space to (DB) block
 */
static inline xfs_dir2_db_t
xfs_dir2_byte_to_db(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_db_t)
		(by >> (mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog));
}

/*
 * Convert dataptr to a block number
 */
static inline xfs_dir2_db_t
xfs_dir2_dataptr_to_db(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_db(mp, xfs_dir2_dataptr_to_byte(mp, dp));
}

/*
 * Convert byte in space to offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_byte_to_off(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return (xfs_dir2_data_aoff_t)(by &
		((1 << (mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog)) - 1));
}

/*
 * Convert dataptr to a byte offset in a block
 */
static inline xfs_dir2_data_aoff_t
xfs_dir2_dataptr_to_off(struct xfs_mount *mp, xfs_dir2_dataptr_t dp)
{
	return xfs_dir2_byte_to_off(mp, xfs_dir2_dataptr_to_byte(mp, dp));
}

/*
 * Convert block and offset to byte in space
 */
static inline xfs_dir2_off_t
xfs_dir2_db_off_to_byte(struct xfs_mount *mp, xfs_dir2_db_t db,
			xfs_dir2_data_aoff_t o)
{
	return ((xfs_dir2_off_t)db <<
		(mp->m_sb.sb_blocklog + mp->m_sb.sb_dirblklog)) + o;
}

/*
 * Convert block (DB) to block (dablk)
 */
static inline xfs_dablk_t
xfs_dir2_db_to_da(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return (xfs_dablk_t)(db << mp->m_sb.sb_dirblklog);
}

/*
 * Convert byte in space to (DA) block
 */
static inline xfs_dablk_t
xfs_dir2_byte_to_da(struct xfs_mount *mp, xfs_dir2_off_t by)
{
	return xfs_dir2_db_to_da(mp, xfs_dir2_byte_to_db(mp, by));
}

/*
 * Convert block and offset to dataptr
 */
static inline xfs_dir2_dataptr_t
xfs_dir2_db_off_to_dataptr(struct xfs_mount *mp, xfs_dir2_db_t db,
			   xfs_dir2_data_aoff_t o)
{
	return xfs_dir2_byte_to_dataptr(mp, xfs_dir2_db_off_to_byte(mp, db, o));
}

/*
 * Convert block (dablk) to block (DB)
 */
static inline xfs_dir2_db_t
xfs_dir2_da_to_db(struct xfs_mount *mp, xfs_dablk_t da)
{
	return (xfs_dir2_db_t)(da >> mp->m_sb.sb_dirblklog);
}

/*
 * Convert block (dablk) to byte offset in space
 */
static inline xfs_dir2_off_t
xfs_dir2_da_to_byte(struct xfs_mount *mp, xfs_dablk_t da)
{
	return xfs_dir2_db_off_to_byte(mp, xfs_dir2_da_to_db(mp, da), 0);
}

/*
 * Free space block defintions for the node format.
 */

/*
 * Offset of the freespace index.
 */
#define	XFS_DIR2_FREE_SPACE	2
#define	XFS_DIR2_FREE_OFFSET	(XFS_DIR2_FREE_SPACE * XFS_DIR2_SPACE_SIZE)
#define	XFS_DIR2_FREE_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_FREE_OFFSET)

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
 * In core version of the free block header, abstracted away from on-disk format
 * differences. Use this in the code, and convert to/from the disk version using
 * xfs_dir3_free_hdr_from_disk/xfs_dir3_free_hdr_to_disk.
 */
struct xfs_dir3_icfree_hdr {
	__uint32_t	magic;
	__uint32_t	firstdb;
	__uint32_t	nvalid;
	__uint32_t	nused;

};

void xfs_dir3_free_hdr_from_disk(struct xfs_dir3_icfree_hdr *to,
				 struct xfs_dir2_free *from);

static inline int
xfs_dir3_free_hdr_size(struct xfs_mount *mp)
{
	if (xfs_sb_version_hascrc(&mp->m_sb))
		return sizeof(struct xfs_dir3_free_hdr);
	return sizeof(struct xfs_dir2_free_hdr);
}

static inline int
xfs_dir3_free_max_bests(struct xfs_mount *mp)
{
	return (mp->m_dirblksize - xfs_dir3_free_hdr_size(mp)) /
		sizeof(xfs_dir2_data_off_t);
}

static inline __be16 *
xfs_dir3_free_bests_p(struct xfs_mount *mp, struct xfs_dir2_free *free)
{
	return (__be16 *)((char *)free + xfs_dir3_free_hdr_size(mp));
}

/*
 * Convert data space db to the corresponding free db.
 */
static inline xfs_dir2_db_t
xfs_dir2_db_to_fdb(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return XFS_DIR2_FREE_FIRSTDB(mp) + db / xfs_dir3_free_max_bests(mp);
}

/*
 * Convert data space db to the corresponding index in a free db.
 */
static inline int
xfs_dir2_db_to_fdindex(struct xfs_mount *mp, xfs_dir2_db_t db)
{
	return db % xfs_dir3_free_max_bests(mp);
}

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
 * Pointer to the leaf header embedded in a data block (1-block format)
 */
static inline struct xfs_dir2_block_tail *
xfs_dir2_block_tail_p(struct xfs_mount *mp, struct xfs_dir2_data_hdr *hdr)
{
	return ((struct xfs_dir2_block_tail *)
		((char *)hdr + mp->m_dirblksize)) - 1;
}

/*
 * Pointer to the leaf entries embedded in a data block (1-block format)
 */
static inline struct xfs_dir2_leaf_entry *
xfs_dir2_block_leaf_p(struct xfs_dir2_block_tail *btp)
{
	return ((struct xfs_dir2_leaf_entry *)btp) - be32_to_cpu(btp->count);
}

#endif /* __XFS_DIR2_FORMAT_H__ */
