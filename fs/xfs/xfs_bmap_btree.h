/*
 * Copyright (c) 2000,2002-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_BMAP_BTREE_H__
#define __XFS_BMAP_BTREE_H__

#define XFS_BMAP_MAGIC		0x424d4150	/* 'BMAP' */
#define XFS_BMAP_CRC_MAGIC	0x424d4133	/* 'BMA3' */

struct xfs_btree_cur;
struct xfs_btree_block;
struct xfs_mount;
struct xfs_inode;
struct xfs_trans;

/*
 * Bmap root header, on-disk form only.
 */
typedef struct xfs_bmdr_block {
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
} xfs_bmdr_block_t;

/*
 * Bmap btree record and extent descriptor.
 *  l0:63 is an extent flag (value 1 indicates non-normal).
 *  l0:9-62 are startoff.
 *  l0:0-8 and l1:21-63 are startblock.
 *  l1:0-20 are blockcount.
 */
#define BMBT_EXNTFLAG_BITLEN	1
#define BMBT_STARTOFF_BITLEN	54
#define BMBT_STARTBLOCK_BITLEN	52
#define BMBT_BLOCKCOUNT_BITLEN	21

typedef struct xfs_bmbt_rec {
	__be64			l0, l1;
} xfs_bmbt_rec_t;

typedef __uint64_t	xfs_bmbt_rec_base_t;	/* use this for casts */
typedef xfs_bmbt_rec_t xfs_bmdr_rec_t;

typedef struct xfs_bmbt_rec_host {
	__uint64_t		l0, l1;
} xfs_bmbt_rec_host_t;

/*
 * Values and macros for delayed-allocation startblock fields.
 */
#define STARTBLOCKVALBITS	17
#define STARTBLOCKMASKBITS	(15 + XFS_BIG_BLKNOS * 20)
#define DSTARTBLOCKMASKBITS	(15 + 20)
#define STARTBLOCKMASK		\
	(((((xfs_fsblock_t)1) << STARTBLOCKMASKBITS) - 1) << STARTBLOCKVALBITS)
#define DSTARTBLOCKMASK		\
	(((((xfs_dfsbno_t)1) << DSTARTBLOCKMASKBITS) - 1) << STARTBLOCKVALBITS)

static inline int isnullstartblock(xfs_fsblock_t x)
{
	return ((x) & STARTBLOCKMASK) == STARTBLOCKMASK;
}

static inline int isnulldstartblock(xfs_dfsbno_t x)
{
	return ((x) & DSTARTBLOCKMASK) == DSTARTBLOCKMASK;
}

static inline xfs_fsblock_t nullstartblock(int k)
{
	ASSERT(k < (1 << STARTBLOCKVALBITS));
	return STARTBLOCKMASK | (k);
}

static inline xfs_filblks_t startblockval(xfs_fsblock_t x)
{
	return (xfs_filblks_t)((x) & ~STARTBLOCKMASK);
}

/*
 * Possible extent formats.
 */
typedef enum {
	XFS_EXTFMT_NOSTATE = 0,
	XFS_EXTFMT_HASSTATE
} xfs_exntfmt_t;

/*
 * Possible extent states.
 */
typedef enum {
	XFS_EXT_NORM, XFS_EXT_UNWRITTEN,
	XFS_EXT_DMAPI_OFFLINE, XFS_EXT_INVALID
} xfs_exntst_t;

/*
 * Extent state and extent format macros.
 */
#define XFS_EXTFMT_INODE(x)	\
	(xfs_sb_version_hasextflgbit(&((x)->i_mount->m_sb)) ? \
		XFS_EXTFMT_HASSTATE : XFS_EXTFMT_NOSTATE)
#define ISUNWRITTEN(x)	((x)->br_state == XFS_EXT_UNWRITTEN)

/*
 * Incore version of above.
 */
typedef struct xfs_bmbt_irec
{
	xfs_fileoff_t	br_startoff;	/* starting file offset */
	xfs_fsblock_t	br_startblock;	/* starting block number */
	xfs_filblks_t	br_blockcount;	/* number of blocks */
	xfs_exntst_t	br_state;	/* extent state */
} xfs_bmbt_irec_t;

/*
 * Key structure for non-leaf levels of the tree.
 */
typedef struct xfs_bmbt_key {
	__be64		br_startoff;	/* starting file offset */
} xfs_bmbt_key_t, xfs_bmdr_key_t;

/* btree pointer type */
typedef __be64 xfs_bmbt_ptr_t, xfs_bmdr_ptr_t;

/*
 * Btree block header size depends on a superblock flag.
 */
#define XFS_BMBT_BLOCK_LEN(mp) \
	(xfs_sb_version_hascrc(&((mp)->m_sb)) ? \
		XFS_BTREE_LBLOCK_CRC_LEN : XFS_BTREE_LBLOCK_LEN)

#define XFS_BMBT_REC_ADDR(mp, block, index) \
	((xfs_bmbt_rec_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_bmbt_rec_t)))

#define XFS_BMBT_KEY_ADDR(mp, block, index) \
	((xfs_bmbt_key_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 ((index) - 1) * sizeof(xfs_bmbt_key_t)))

#define XFS_BMBT_PTR_ADDR(mp, block, index, maxrecs) \
	((xfs_bmbt_ptr_t *) \
		((char *)(block) + \
		 XFS_BMBT_BLOCK_LEN(mp) + \
		 (maxrecs) * sizeof(xfs_bmbt_key_t) + \
		 ((index) - 1) * sizeof(xfs_bmbt_ptr_t)))

#define XFS_BMDR_REC_ADDR(block, index) \
	((xfs_bmdr_rec_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
	         ((index) - 1) * sizeof(xfs_bmdr_rec_t)))

#define XFS_BMDR_KEY_ADDR(block, index) \
	((xfs_bmdr_key_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
		 ((index) - 1) * sizeof(xfs_bmdr_key_t)))

#define XFS_BMDR_PTR_ADDR(block, index, maxrecs) \
	((xfs_bmdr_ptr_t *) \
		((char *)(block) + \
		 sizeof(struct xfs_bmdr_block) + \
		 (maxrecs) * sizeof(xfs_bmdr_key_t) + \
		 ((index) - 1) * sizeof(xfs_bmdr_ptr_t)))

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define XFS_BMAP_BROOT_PTR_ADDR(mp, bb, i, sz) \
	XFS_BMBT_PTR_ADDR(mp, bb, i, xfs_bmbt_maxrecs(mp, sz, 0))

#define XFS_BMAP_BROOT_SPACE_CALC(mp, nrecs) \
	(int)(XFS_BMBT_BLOCK_LEN(mp) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))

#define XFS_BMAP_BROOT_SPACE(mp, bb) \
	(XFS_BMAP_BROOT_SPACE_CALC(mp, be16_to_cpu((bb)->bb_numrecs)))
#define XFS_BMDR_SPACE_CALC(nrecs) \
	(int)(sizeof(xfs_bmdr_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))
#define XFS_BMAP_BMDR_SPACE(bb) \
	(XFS_BMDR_SPACE_CALC(be16_to_cpu((bb)->bb_numrecs)))

/*
 * Maximum number of bmap btree levels.
 */
#define XFS_BM_MAXLEVELS(mp,w)		((mp)->m_bm_maxlevels[(w)])

/*
 * Prototypes for xfs_bmap.c to call.
 */
extern void xfs_bmdr_to_bmbt(struct xfs_inode *, xfs_bmdr_block_t *, int,
			struct xfs_btree_block *, int);
extern void xfs_bmbt_get_all(xfs_bmbt_rec_host_t *r, xfs_bmbt_irec_t *s);
extern xfs_filblks_t xfs_bmbt_get_blockcount(xfs_bmbt_rec_host_t *r);
extern xfs_fsblock_t xfs_bmbt_get_startblock(xfs_bmbt_rec_host_t *r);
extern xfs_fileoff_t xfs_bmbt_get_startoff(xfs_bmbt_rec_host_t *r);
extern xfs_exntst_t xfs_bmbt_get_state(xfs_bmbt_rec_host_t *r);

extern xfs_filblks_t xfs_bmbt_disk_get_blockcount(xfs_bmbt_rec_t *r);
extern xfs_fileoff_t xfs_bmbt_disk_get_startoff(xfs_bmbt_rec_t *r);

extern void xfs_bmbt_set_all(xfs_bmbt_rec_host_t *r, xfs_bmbt_irec_t *s);
extern void xfs_bmbt_set_allf(xfs_bmbt_rec_host_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);
extern void xfs_bmbt_set_blockcount(xfs_bmbt_rec_host_t *r, xfs_filblks_t v);
extern void xfs_bmbt_set_startblock(xfs_bmbt_rec_host_t *r, xfs_fsblock_t v);
extern void xfs_bmbt_set_startoff(xfs_bmbt_rec_host_t *r, xfs_fileoff_t v);
extern void xfs_bmbt_set_state(xfs_bmbt_rec_host_t *r, xfs_exntst_t v);

extern void xfs_bmbt_disk_set_allf(xfs_bmbt_rec_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);

extern void xfs_bmbt_to_bmdr(struct xfs_mount *, struct xfs_btree_block *, int,
			xfs_bmdr_block_t *, int);

extern int xfs_bmbt_get_maxrecs(struct xfs_btree_cur *, int level);
extern int xfs_bmdr_maxrecs(struct xfs_mount *, int blocklen, int leaf);
extern int xfs_bmbt_maxrecs(struct xfs_mount *, int blocklen, int leaf);

extern int xfs_bmbt_change_owner(struct xfs_trans *tp, struct xfs_inode *ip,
				 int whichfork, xfs_ino_t new_owner,
				 struct list_head *buffer_list);

extern struct xfs_btree_cur *xfs_bmbt_init_cursor(struct xfs_mount *,
		struct xfs_trans *, struct xfs_inode *, int);

extern const struct xfs_buf_ops xfs_bmbt_buf_ops;

#endif	/* __XFS_BMAP_BTREE_H__ */
