/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DIR2_DATA_H__
#define	__XFS_DIR2_DATA_H__

/*
 * Directory format 2, data block structures.
 */

struct xfs_dabuf;
struct xfs_da_args;
struct xfs_inode;
struct xfs_trans;

/*
 * Constants.
 */
#define	XFS_DIR2_DATA_MAGIC	0x58443244	/* XD2D: for multiblock dirs */
#define	XFS_DIR2_DATA_ALIGN_LOG	3		/* i.e., 8 bytes */
#define	XFS_DIR2_DATA_ALIGN	(1 << XFS_DIR2_DATA_ALIGN_LOG)
#define	XFS_DIR2_DATA_FREE_TAG	0xffff
#define	XFS_DIR2_DATA_FD_COUNT	3

/*
 * Directory address space divided into sections,
 * spaces separated by 32gb.
 */
#define	XFS_DIR2_SPACE_SIZE	(1ULL << (32 + XFS_DIR2_DATA_ALIGN_LOG))
#define	XFS_DIR2_DATA_SPACE	0
#define	XFS_DIR2_DATA_OFFSET	(XFS_DIR2_DATA_SPACE * XFS_DIR2_SPACE_SIZE)
#define	XFS_DIR2_DATA_FIRSTDB(mp)	\
	xfs_dir2_byte_to_db(mp, XFS_DIR2_DATA_OFFSET)

/*
 * Offsets of . and .. in data space (always block 0)
 */
#define	XFS_DIR2_DATA_DOT_OFFSET	\
	((xfs_dir2_data_aoff_t)sizeof(xfs_dir2_data_hdr_t))
#define	XFS_DIR2_DATA_DOTDOT_OFFSET	\
	(XFS_DIR2_DATA_DOT_OFFSET + xfs_dir2_data_entsize(1))
#define	XFS_DIR2_DATA_FIRST_OFFSET		\
	(XFS_DIR2_DATA_DOTDOT_OFFSET + xfs_dir2_data_entsize(2))

/*
 * Structures.
 */

/*
 * Describe a free area in the data block.
 * The freespace will be formatted as a xfs_dir2_data_unused_t.
 */
typedef struct xfs_dir2_data_free {
	__be16			offset;		/* start of freespace */
	__be16			length;		/* length of freespace */
} xfs_dir2_data_free_t;

/*
 * Header for the data blocks.
 * Always at the beginning of a directory-sized block.
 * The code knows that XFS_DIR2_DATA_FD_COUNT is 3.
 */
typedef struct xfs_dir2_data_hdr {
	__be32			magic;		/* XFS_DIR2_DATA_MAGIC */
						/* or XFS_DIR2_BLOCK_MAGIC */
	xfs_dir2_data_free_t	bestfree[XFS_DIR2_DATA_FD_COUNT];
} xfs_dir2_data_hdr_t;

/*
 * Active entry in a data block.  Aligned to 8 bytes.
 * Tag appears as the last 2 bytes.
 */
typedef struct xfs_dir2_data_entry {
	__be64			inumber;	/* inode number */
	__u8			namelen;	/* name length */
	__u8			name[1];	/* name bytes, no null */
						/* variable offset */
	__be16			tag;		/* starting offset of us */
} xfs_dir2_data_entry_t;

/*
 * Unused entry in a data block.  Aligned to 8 bytes.
 * Tag appears as the last 2 bytes.
 */
typedef struct xfs_dir2_data_unused {
	__be16			freetag;	/* XFS_DIR2_DATA_FREE_TAG */
	__be16			length;		/* total free length */
						/* variable offset */
	__be16			tag;		/* starting offset of us */
} xfs_dir2_data_unused_t;

typedef union {
	xfs_dir2_data_entry_t	entry;
	xfs_dir2_data_unused_t	unused;
} xfs_dir2_data_union_t;

/*
 * Generic data block structure, for xfs_db.
 */
typedef struct xfs_dir2_data {
	xfs_dir2_data_hdr_t	hdr;		/* magic XFS_DIR2_DATA_MAGIC */
	xfs_dir2_data_union_t	u[1];
} xfs_dir2_data_t;

/*
 * Macros.
 */

/*
 * Size of a data entry.
 */
static inline int xfs_dir2_data_entsize(int n)
{
	return (int)roundup(offsetof(xfs_dir2_data_entry_t, name[0]) + (n) + \
		 (uint)sizeof(xfs_dir2_data_off_t), XFS_DIR2_DATA_ALIGN);
}

/*
 * Pointer to an entry's tag word.
 */
static inline __be16 *
xfs_dir2_data_entry_tag_p(xfs_dir2_data_entry_t *dep)
{
	return (__be16 *)((char *)dep +
		xfs_dir2_data_entsize(dep->namelen) - sizeof(__be16));
}

/*
 * Pointer to a freespace's tag word.
 */
static inline __be16 *
xfs_dir2_data_unused_tag_p(xfs_dir2_data_unused_t *dup)
{
	return (__be16 *)((char *)dup +
			be16_to_cpu(dup->length) - sizeof(__be16));
}

/*
 * Function declarations.
 */
#ifdef DEBUG
extern void xfs_dir2_data_check(struct xfs_inode *dp, struct xfs_dabuf *bp);
#else
#define	xfs_dir2_data_check(dp,bp)
#endif
extern xfs_dir2_data_free_t *xfs_dir2_data_freefind(xfs_dir2_data_t *d,
				xfs_dir2_data_unused_t *dup);
extern xfs_dir2_data_free_t *xfs_dir2_data_freeinsert(xfs_dir2_data_t *d,
				xfs_dir2_data_unused_t *dup, int *loghead);
extern void xfs_dir2_data_freescan(struct xfs_mount *mp, xfs_dir2_data_t *d,
				int *loghead);
extern int xfs_dir2_data_init(struct xfs_da_args *args, xfs_dir2_db_t blkno,
				struct xfs_dabuf **bpp);
extern void xfs_dir2_data_log_entry(struct xfs_trans *tp, struct xfs_dabuf *bp,
				xfs_dir2_data_entry_t *dep);
extern void xfs_dir2_data_log_header(struct xfs_trans *tp,
				struct xfs_dabuf *bp);
extern void xfs_dir2_data_log_unused(struct xfs_trans *tp, struct xfs_dabuf *bp,
				xfs_dir2_data_unused_t *dup);
extern void xfs_dir2_data_make_free(struct xfs_trans *tp, struct xfs_dabuf *bp,
				xfs_dir2_data_aoff_t offset,
				xfs_dir2_data_aoff_t len, int *needlogp,
				int *needscanp);
extern void xfs_dir2_data_use_free(struct xfs_trans *tp, struct xfs_dabuf *bp,
			       xfs_dir2_data_unused_t *dup,
			       xfs_dir2_data_aoff_t offset,
			       xfs_dir2_data_aoff_t len, int *needlogp,
			       int *needscanp);

#endif	/* __XFS_DIR2_DATA_H__ */
