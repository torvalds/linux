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

#define XFS_BMAP_MAGIC	0x424d4150	/* 'BMAP' */

struct xfs_btree_cur;
struct xfs_btree_lblock;
struct xfs_mount;
struct xfs_inode;

/*
 * Bmap root header, on-disk form only.
 */
typedef struct xfs_bmdr_block {
	__be16		bb_level;	/* 0 is a leaf */
	__be16		bb_numrecs;	/* current # of data records */
} xfs_bmdr_block_t;

/*
 * Bmap btree record and extent descriptor.
 * For 32-bit kernels,
 *  l0:31 is an extent flag (value 1 indicates non-normal).
 *  l0:0-30 and l1:9-31 are startoff.
 *  l1:0-8, l2:0-31, and l3:21-31 are startblock.
 *  l3:0-20 are blockcount.
 * For 64-bit kernels,
 *  l0:63 is an extent flag (value 1 indicates non-normal).
 *  l0:9-62 are startoff.
 *  l0:0-8 and l1:21-63 are startblock.
 *  l1:0-20 are blockcount.
 */

#ifndef XFS_NATIVE_HOST

#define BMBT_TOTAL_BITLEN	128	/* 128 bits, 16 bytes */
#define BMBT_EXNTFLAG_BITOFF	0
#define BMBT_EXNTFLAG_BITLEN	1
#define BMBT_STARTOFF_BITOFF	(BMBT_EXNTFLAG_BITOFF + BMBT_EXNTFLAG_BITLEN)
#define BMBT_STARTOFF_BITLEN	54
#define BMBT_STARTBLOCK_BITOFF	(BMBT_STARTOFF_BITOFF + BMBT_STARTOFF_BITLEN)
#define BMBT_STARTBLOCK_BITLEN	52
#define BMBT_BLOCKCOUNT_BITOFF	\
	(BMBT_STARTBLOCK_BITOFF + BMBT_STARTBLOCK_BITLEN)
#define BMBT_BLOCKCOUNT_BITLEN	(BMBT_TOTAL_BITLEN - BMBT_BLOCKCOUNT_BITOFF)

#else

#define BMBT_TOTAL_BITLEN	128	/* 128 bits, 16 bytes */
#define BMBT_EXNTFLAG_BITOFF	63
#define BMBT_EXNTFLAG_BITLEN	1
#define BMBT_STARTOFF_BITOFF	(BMBT_EXNTFLAG_BITOFF - BMBT_STARTOFF_BITLEN)
#define BMBT_STARTOFF_BITLEN	54
#define BMBT_STARTBLOCK_BITOFF	85 /* 128 - 43 (other 9 is in first word) */
#define BMBT_STARTBLOCK_BITLEN	52
#define BMBT_BLOCKCOUNT_BITOFF	64 /* Start of second 64 bit container */
#define BMBT_BLOCKCOUNT_BITLEN	21

#endif /* XFS_NATIVE_HOST */


#define BMBT_USE_64	1

typedef struct xfs_bmbt_rec_32
{
	__uint32_t		l0, l1, l2, l3;
} xfs_bmbt_rec_32_t;
typedef struct xfs_bmbt_rec_64
{
	__uint64_t		l0, l1;
} xfs_bmbt_rec_64_t;

typedef __uint64_t	xfs_bmbt_rec_base_t;	/* use this for casts */
typedef xfs_bmbt_rec_64_t xfs_bmbt_rec_t, xfs_bmdr_rec_t;

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

#define ISNULLSTARTBLOCK(x)	isnullstartblock(x)
static inline int isnullstartblock(xfs_fsblock_t x)
{
	return ((x) & STARTBLOCKMASK) == STARTBLOCKMASK;
}

#define ISNULLDSTARTBLOCK(x)	isnulldstartblock(x)
static inline int isnulldstartblock(xfs_dfsbno_t x)
{
	return ((x) & DSTARTBLOCKMASK) == DSTARTBLOCKMASK;
}

#define NULLSTARTBLOCK(k)	nullstartblock(k)
static inline xfs_fsblock_t nullstartblock(int k)
{
	ASSERT(k < (1 << STARTBLOCKVALBITS));
	return STARTBLOCKMASK | (k);
}

#define STARTBLOCKVAL(x)	startblockval(x)
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
	(XFS_SB_VERSION_HASEXTFLGBIT(&((x)->i_mount->m_sb)) ? \
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

/* btree block header type */
typedef struct xfs_btree_lblock xfs_bmbt_block_t;

#define XFS_BUF_TO_BMBT_BLOCK(bp)	((xfs_bmbt_block_t *)XFS_BUF_PTR(bp))

#define XFS_BMAP_IBLOCK_SIZE(lev,cur)	(1 << (cur)->bc_blocklog)
#define XFS_BMAP_RBLOCK_DSIZE(lev,cur)	((cur)->bc_private.b.forksize)
#define XFS_BMAP_RBLOCK_ISIZE(lev,cur)	\
	((int)XFS_IFORK_PTR((cur)->bc_private.b.ip, \
		    (cur)->bc_private.b.whichfork)->if_broot_bytes)

#define XFS_BMAP_BLOCK_DSIZE(lev,cur)	\
	(((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_DSIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur)))
#define XFS_BMAP_BLOCK_ISIZE(lev,cur)	\
	(((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BMAP_RBLOCK_ISIZE(lev,cur) : XFS_BMAP_IBLOCK_SIZE(lev,cur)))

#define XFS_BMAP_BLOCK_DMAXRECS(lev,cur) \
	(((lev) == (cur)->bc_nlevels - 1 ? \
		XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_RBLOCK_DSIZE(lev,cur), \
			xfs_bmdr, (lev) == 0) : \
		((cur)->bc_mp->m_bmap_dmxr[(lev) != 0])))
#define XFS_BMAP_BLOCK_IMAXRECS(lev,cur) \
	(((lev) == (cur)->bc_nlevels - 1 ? \
			XFS_BTREE_BLOCK_MAXRECS(XFS_BMAP_RBLOCK_ISIZE(lev,cur),\
				xfs_bmbt, (lev) == 0) : \
			((cur)->bc_mp->m_bmap_dmxr[(lev) != 0])))

#define XFS_BMAP_BLOCK_DMINRECS(lev,cur) \
	(((lev) == (cur)->bc_nlevels - 1 ? \
			XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_RBLOCK_DSIZE(lev,cur),\
				xfs_bmdr, (lev) == 0) : \
			((cur)->bc_mp->m_bmap_dmnr[(lev) != 0])))
#define XFS_BMAP_BLOCK_IMINRECS(lev,cur) \
	(((lev) == (cur)->bc_nlevels - 1 ? \
			XFS_BTREE_BLOCK_MINRECS(XFS_BMAP_RBLOCK_ISIZE(lev,cur),\
				xfs_bmbt, (lev) == 0) : \
			((cur)->bc_mp->m_bmap_dmnr[(lev) != 0])))

#define XFS_BMAP_REC_DADDR(bb,i,cur)	\
	(XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_DSIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_DMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))
#define XFS_BMAP_REC_IADDR(bb,i,cur)	\
	(XFS_BTREE_REC_ADDR(XFS_BMAP_BLOCK_ISIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_IMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))

#define XFS_BMAP_KEY_DADDR(bb,i,cur)	\
	(XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_DSIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_DMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))
#define XFS_BMAP_KEY_IADDR(bb,i,cur)	\
	(XFS_BTREE_KEY_ADDR(XFS_BMAP_BLOCK_ISIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_IMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))

#define XFS_BMAP_PTR_DADDR(bb,i,cur)	\
	(XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_DSIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_DMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))
#define XFS_BMAP_PTR_IADDR(bb,i,cur)	\
	(XFS_BTREE_PTR_ADDR(XFS_BMAP_BLOCK_ISIZE(			\
			be16_to_cpu((bb)->bb_level), cur),		\
			xfs_bmbt, bb, i, XFS_BMAP_BLOCK_IMAXRECS(	\
				be16_to_cpu((bb)->bb_level), cur)))

/*
 * These are to be used when we know the size of the block and
 * we don't have a cursor.
 */
#define XFS_BMAP_BROOT_REC_ADDR(bb,i,sz) \
	(XFS_BTREE_REC_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz)))
#define XFS_BMAP_BROOT_KEY_ADDR(bb,i,sz) \
	(XFS_BTREE_KEY_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz)))
#define XFS_BMAP_BROOT_PTR_ADDR(bb,i,sz) \
	(XFS_BTREE_PTR_ADDR(sz,xfs_bmbt,bb,i,XFS_BMAP_BROOT_MAXRECS(sz)))

#define XFS_BMAP_BROOT_NUMRECS(bb)	be16_to_cpu((bb)->bb_numrecs)
#define XFS_BMAP_BROOT_MAXRECS(sz)	XFS_BTREE_BLOCK_MAXRECS(sz,xfs_bmbt,0)

#define XFS_BMAP_BROOT_SPACE_CALC(nrecs) \
	(int)(sizeof(xfs_bmbt_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))

#define XFS_BMAP_BROOT_SPACE(bb) \
	(XFS_BMAP_BROOT_SPACE_CALC(be16_to_cpu((bb)->bb_numrecs)))
#define XFS_BMDR_SPACE_CALC(nrecs) \
	(int)(sizeof(xfs_bmdr_block_t) + \
	       ((nrecs) * (sizeof(xfs_bmbt_key_t) + sizeof(xfs_bmbt_ptr_t))))

/*
 * Maximum number of bmap btree levels.
 */
#define XFS_BM_MAXLEVELS(mp,w)		((mp)->m_bm_maxlevels[(w)])

#define XFS_BMAP_SANITY_CHECK(mp,bb,level) \
	(be32_to_cpu((bb)->bb_magic) == XFS_BMAP_MAGIC && \
	 be16_to_cpu((bb)->bb_level) == level && \
	 be16_to_cpu((bb)->bb_numrecs) > 0 && \
	 be16_to_cpu((bb)->bb_numrecs) <= (mp)->m_bmap_dmxr[(level) != 0])


#ifdef __KERNEL__

#if defined(XFS_BMBT_TRACE)
/*
 * Trace buffer entry types.
 */
#define XFS_BMBT_KTRACE_ARGBI	1
#define XFS_BMBT_KTRACE_ARGBII	2
#define XFS_BMBT_KTRACE_ARGFFFI 3
#define XFS_BMBT_KTRACE_ARGI	4
#define XFS_BMBT_KTRACE_ARGIFK	5
#define XFS_BMBT_KTRACE_ARGIFR	6
#define XFS_BMBT_KTRACE_ARGIK	7
#define XFS_BMBT_KTRACE_CUR	8

#define XFS_BMBT_TRACE_SIZE	4096	/* size of global trace buffer */
#define XFS_BMBT_KTRACE_SIZE	32	/* size of per-inode trace buffer */
extern ktrace_t	*xfs_bmbt_trace_buf;
#endif

/*
 * Prototypes for xfs_bmap.c to call.
 */
extern void xfs_bmdr_to_bmbt(xfs_bmdr_block_t *, int, xfs_bmbt_block_t *, int);
extern int xfs_bmbt_decrement(struct xfs_btree_cur *, int, int *);
extern int xfs_bmbt_delete(struct xfs_btree_cur *, int *);
extern void xfs_bmbt_get_all(xfs_bmbt_rec_t *r, xfs_bmbt_irec_t *s);
extern xfs_bmbt_block_t *xfs_bmbt_get_block(struct xfs_btree_cur *cur,
						int, struct xfs_buf **bpp);
extern xfs_filblks_t xfs_bmbt_get_blockcount(xfs_bmbt_rec_t *r);
extern xfs_fsblock_t xfs_bmbt_get_startblock(xfs_bmbt_rec_t *r);
extern xfs_fileoff_t xfs_bmbt_get_startoff(xfs_bmbt_rec_t *r);
extern xfs_exntst_t xfs_bmbt_get_state(xfs_bmbt_rec_t *r);

#ifndef XFS_NATIVE_HOST
extern void xfs_bmbt_disk_get_all(xfs_bmbt_rec_t *r, xfs_bmbt_irec_t *s);
extern xfs_exntst_t xfs_bmbt_disk_get_state(xfs_bmbt_rec_t *r);
extern xfs_filblks_t xfs_bmbt_disk_get_blockcount(xfs_bmbt_rec_t *r);
extern xfs_fsblock_t xfs_bmbt_disk_get_startblock(xfs_bmbt_rec_t *r);
extern xfs_fileoff_t xfs_bmbt_disk_get_startoff(xfs_bmbt_rec_t *r);
#else
#define xfs_bmbt_disk_get_all(r, s)	xfs_bmbt_get_all(r, s)
#define xfs_bmbt_disk_get_state(r)	xfs_bmbt_get_state(r)
#define xfs_bmbt_disk_get_blockcount(r)	xfs_bmbt_get_blockcount(r)
#define xfs_bmbt_disk_get_startblock(r)	xfs_bmbt_get_blockcount(r)
#define xfs_bmbt_disk_get_startoff(r)	xfs_bmbt_get_startoff(r)
#endif /* XFS_NATIVE_HOST */

extern int xfs_bmbt_increment(struct xfs_btree_cur *, int, int *);
extern int xfs_bmbt_insert(struct xfs_btree_cur *, int *);
extern void xfs_bmbt_log_block(struct xfs_btree_cur *, struct xfs_buf *, int);
extern void xfs_bmbt_log_recs(struct xfs_btree_cur *, struct xfs_buf *, int,
				int);
extern int xfs_bmbt_lookup_eq(struct xfs_btree_cur *, xfs_fileoff_t,
				xfs_fsblock_t, xfs_filblks_t, int *);
extern int xfs_bmbt_lookup_ge(struct xfs_btree_cur *, xfs_fileoff_t,
				xfs_fsblock_t, xfs_filblks_t, int *);

/*
 * Give the bmap btree a new root block.  Copy the old broot contents
 * down into a real block and make the broot point to it.
 */
extern int xfs_bmbt_newroot(struct xfs_btree_cur *cur, int *lflags, int *stat);

extern void xfs_bmbt_set_all(xfs_bmbt_rec_t *r, xfs_bmbt_irec_t *s);
extern void xfs_bmbt_set_allf(xfs_bmbt_rec_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);
extern void xfs_bmbt_set_blockcount(xfs_bmbt_rec_t *r, xfs_filblks_t v);
extern void xfs_bmbt_set_startblock(xfs_bmbt_rec_t *r, xfs_fsblock_t v);
extern void xfs_bmbt_set_startoff(xfs_bmbt_rec_t *r, xfs_fileoff_t v);
extern void xfs_bmbt_set_state(xfs_bmbt_rec_t *r, xfs_exntst_t v);

#ifndef XFS_NATIVE_HOST
extern void xfs_bmbt_disk_set_all(xfs_bmbt_rec_t *r, xfs_bmbt_irec_t *s);
extern void xfs_bmbt_disk_set_allf(xfs_bmbt_rec_t *r, xfs_fileoff_t o,
			xfs_fsblock_t b, xfs_filblks_t c, xfs_exntst_t v);
#else
#define xfs_bmbt_disk_set_all(r, s)		xfs_bmbt_set_all(r, s)
#define xfs_bmbt_disk_set_allf(r, o, b, c, v)	xfs_bmbt_set_allf(r, o, b, c, v)
#endif /* XFS_NATIVE_HOST */

extern void xfs_bmbt_to_bmdr(xfs_bmbt_block_t *, int, xfs_bmdr_block_t *, int);
extern int xfs_bmbt_update(struct xfs_btree_cur *, xfs_fileoff_t,
				xfs_fsblock_t, xfs_filblks_t, xfs_exntst_t);

#ifdef DEBUG
/*
 * Get the data from the pointed-to record.
 */
extern int xfs_bmbt_get_rec(struct xfs_btree_cur *, xfs_fileoff_t *,
				xfs_fsblock_t *, xfs_filblks_t *,
				xfs_exntst_t *, int *);
#endif

#endif	/* __KERNEL__ */

#endif	/* __XFS_BMAP_BTREE_H__ */
