/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#ifndef __XFS_BMAP_H__
#define	__XFS_BMAP_H__

struct getbmap;
struct xfs_bmbt_irec;
struct xfs_ifork;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

extern kmem_zone_t	*xfs_bmap_free_item_zone;

/*
 * DELTA: describe a change to the in-core extent list.
 *
 * Internally the use of xed_blockount is somewhat funky.
 * xed_blockcount contains an offset much of the time because this
 * makes merging changes easier.  (xfs_fileoff_t and xfs_filblks_t are
 * the same underlying type).
 */
typedef struct xfs_extdelta
{
	xfs_fileoff_t		xed_startoff;	/* offset of range */
	xfs_filblks_t		xed_blockcount;	/* blocks in range */
} xfs_extdelta_t;

/*
 * List of extents to be free "later".
 * The list is kept sorted on xbf_startblock.
 */
typedef struct xfs_bmap_free_item
{
	xfs_fsblock_t		xbfi_startblock;/* starting fs block number */
	xfs_extlen_t		xbfi_blockcount;/* number of blocks in extent */
	struct xfs_bmap_free_item *xbfi_next;	/* link to next entry */
} xfs_bmap_free_item_t;

/*
 * Header for free extent list.
 *
 * xbf_low is used by the allocator to activate the lowspace algorithm -
 * when free space is running low the extent allocator may choose to
 * allocate an extent from an AG without leaving sufficient space for
 * a btree split when inserting the new extent.  In this case the allocator
 * will enable the lowspace algorithm which is supposed to allow further
 * allocations (such as btree splits and newroots) to allocate from
 * sequential AGs.  In order to avoid locking AGs out of order the lowspace
 * algorithm will start searching for free space from AG 0.  If the correct
 * transaction reservations have been made then this algorithm will eventually
 * find all the space it needs.
 */
typedef	struct xfs_bmap_free
{
	xfs_bmap_free_item_t	*xbf_first;	/* list of to-be-free extents */
	int			xbf_count;	/* count of items on list */
	int			xbf_low;	/* alloc in low mode */
} xfs_bmap_free_t;

#define	XFS_BMAP_MAX_NMAP	4

/*
 * Flags for xfs_bmapi
 */
#define	XFS_BMAPI_WRITE		0x001	/* write operation: allocate space */
#define XFS_BMAPI_DELAY		0x002	/* delayed write operation */
#define XFS_BMAPI_ENTIRE	0x004	/* return entire extent, not trimmed */
#define XFS_BMAPI_METADATA	0x008	/* mapping metadata not user data */
#define XFS_BMAPI_EXACT		0x010	/* allocate only to spec'd bounds */
#define XFS_BMAPI_ATTRFORK	0x020	/* use attribute fork not data */
#define XFS_BMAPI_ASYNC		0x040	/* bunmapi xactions can be async */
#define XFS_BMAPI_RSVBLOCKS	0x080	/* OK to alloc. reserved data blocks */
#define	XFS_BMAPI_PREALLOC	0x100	/* preallocation op: unwritten space */
#define	XFS_BMAPI_IGSTATE	0x200	/* Ignore state - */
					/* combine contig. space */
#define	XFS_BMAPI_CONTIG	0x400	/* must allocate only one extent */
/*	XFS_BMAPI_DIRECT_IO	0x800	*/
#define XFS_BMAPI_CONVERT	0x1000	/* unwritten extent conversion - */
					/* need write cache flushing and no */
					/* additional allocation alignments */

static inline int xfs_bmapi_aflag(int w)
{
	return (w == XFS_ATTR_FORK ? XFS_BMAPI_ATTRFORK : 0);
}

/*
 * Special values for xfs_bmbt_irec_t br_startblock field.
 */
#define	DELAYSTARTBLOCK		((xfs_fsblock_t)-1LL)
#define	HOLESTARTBLOCK		((xfs_fsblock_t)-2LL)

static inline void xfs_bmap_init(xfs_bmap_free_t *flp, xfs_fsblock_t *fbp)
{
	((flp)->xbf_first = NULL, (flp)->xbf_count = 0, \
		(flp)->xbf_low = 0, *(fbp) = NULLFSBLOCK);
}

/*
 * Argument structure for xfs_bmap_alloc.
 */
typedef struct xfs_bmalloca {
	xfs_fsblock_t		firstblock; /* i/o first block allocated */
	xfs_fsblock_t		rval;	/* starting block of new extent */
	xfs_fileoff_t		off;	/* offset in file filling in */
	struct xfs_trans	*tp;	/* transaction pointer */
	struct xfs_inode	*ip;	/* incore inode pointer */
	struct xfs_bmbt_irec	*prevp;	/* extent before the new one */
	struct xfs_bmbt_irec	*gotp;	/* extent after, or delayed */
	xfs_extlen_t		alen;	/* i/o length asked/allocated */
	xfs_extlen_t		total;	/* total blocks needed for xaction */
	xfs_extlen_t		minlen;	/* mininum allocation size (blocks) */
	xfs_extlen_t		minleft; /* amount must be left after alloc */
	char			eof;	/* set if allocating past last extent */
	char			wasdel;	/* replacing a delayed allocation */
	char			userdata;/* set if is user data */
	char			low;	/* low on space, using seq'l ags */
	char			aeof;	/* allocated space at eof */
	char			conv;	/* overwriting unwritten extents */
} xfs_bmalloca_t;

#if defined(__KERNEL__) && defined(XFS_BMAP_TRACE)
/*
 * Trace operations for bmap extent tracing
 */
#define	XFS_BMAP_KTRACE_DELETE	1
#define	XFS_BMAP_KTRACE_INSERT	2
#define	XFS_BMAP_KTRACE_PRE_UP	3
#define	XFS_BMAP_KTRACE_POST_UP	4

#define	XFS_BMAP_TRACE_SIZE	4096	/* size of global trace buffer */
#define	XFS_BMAP_KTRACE_SIZE	32	/* size of per-inode trace buffer */
extern ktrace_t	*xfs_bmap_trace_buf;

/*
 * Add bmap trace insert entries for all the contents of the extent list.
 */
void
xfs_bmap_trace_exlist(
	const char		*fname,		/* function name */
	struct xfs_inode	*ip,		/* incore inode pointer */
	xfs_extnum_t		cnt,		/* count of entries in list */
	int			whichfork);	/* data or attr fork */
#define	XFS_BMAP_TRACE_EXLIST(ip,c,w)	\
	xfs_bmap_trace_exlist(__func__,ip,c,w)

#else	/* __KERNEL__ && XFS_BMAP_TRACE */

#define	XFS_BMAP_TRACE_EXLIST(ip,c,w)

#endif	/* __KERNEL__ && XFS_BMAP_TRACE */

/*
 * Convert inode from non-attributed to attributed.
 * Must not be in a transaction, ip must not be locked.
 */
int					/* error code */
xfs_bmap_add_attrfork(
	struct xfs_inode	*ip,	/* incore inode pointer */
	int			size,	/* space needed for new attribute */
	int			rsvd);	/* flag for reserved block allocation */

/*
 * Add the extent to the list of extents to be free at transaction end.
 * The list is maintained sorted (by block number).
 */
void
xfs_bmap_add_free(
	xfs_fsblock_t		bno,		/* fs block number of extent */
	xfs_filblks_t		len,		/* length of extent */
	xfs_bmap_free_t		*flist,		/* list of extents */
	struct xfs_mount	*mp);		/* mount point structure */

/*
 * Routine to clean up the free list data structure when
 * an error occurs during a transaction.
 */
void
xfs_bmap_cancel(
	xfs_bmap_free_t		*flist);	/* free list to clean up */

/*
 * Compute and fill in the value of the maximum depth of a bmap btree
 * in this filesystem.  Done once, during mount.
 */
void
xfs_bmap_compute_maxlevels(
	struct xfs_mount	*mp,	/* file system mount structure */
	int			whichfork);	/* data or attr fork */

/*
 * Returns the file-relative block number of the first unused block in the file.
 * This is the lowest-address hole if the file has holes, else the first block
 * past the end of file.
 */
int						/* error */
xfs_bmap_first_unused(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_extlen_t		len,		/* size of hole to find */
	xfs_fileoff_t		*unused,	/* unused block num */
	int			whichfork);	/* data or attr fork */

/*
 * Returns the file-relative block number of the last block + 1 before
 * last_block (input value) in the file.
 * This is not based on i_size, it is based on the extent list.
 * Returns 0 for local files, as they do not have an extent list.
 */
int						/* error */
xfs_bmap_last_before(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		*last_block,	/* last block */
	int			whichfork);	/* data or attr fork */

/*
 * Returns the file-relative block number of the first block past eof in
 * the file.  This is not based on i_size, it is based on the extent list.
 * Returns 0 for local files, as they do not have an extent list.
 */
int						/* error */
xfs_bmap_last_offset(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		*unused,	/* last block num */
	int			whichfork);	/* data or attr fork */

/*
 * Returns whether the selected fork of the inode has exactly one
 * block or not.  For the data fork we check this matches di_size,
 * implying the file's range is 0..bsize-1.
 */
int
xfs_bmap_one_block(
	struct xfs_inode	*ip,		/* incore inode */
	int			whichfork);	/* data or attr fork */

/*
 * Read in the extents to iu_extents.
 * All inode fields are set up by caller, we just traverse the btree
 * and copy the records in.
 */
int						/* error */
xfs_bmap_read_extents(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	int			whichfork);	/* data or attr fork */

/*
 * Map file blocks to filesystem blocks.
 * File range is given by the bno/len pair.
 * Adds blocks to file if a write ("flags & XFS_BMAPI_WRITE" set)
 * into a hole or past eof.
 * Only allocates blocks from a single allocation group,
 * to avoid locking problems.
 * The returned value in "firstblock" from the first call in a transaction
 * must be remembered and presented to subsequent calls in "firstblock".
 * An upper bound for the number of blocks to be allocated is supplied to
 * the first call in "total"; if no allocation group has that many free
 * blocks then the call will fail (return NULLFSBLOCK in "firstblock").
 */
int						/* error */
xfs_bmapi(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting file offs. mapped */
	xfs_filblks_t		len,		/* length to map in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_fsblock_t		*firstblock,	/* first allocated block
						   controls a.g. for allocs */
	xfs_extlen_t		total,		/* total blocks needed */
	struct xfs_bmbt_irec	*mval,		/* output: map values */
	int			*nmap,		/* i/o: mval size/count */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	xfs_extdelta_t		*delta);	/* o: change made to incore
						   extents */

/*
 * Map file blocks to filesystem blocks, simple version.
 * One block only, read-only.
 * For flags, only the XFS_BMAPI_ATTRFORK flag is examined.
 * For the other flag values, the effect is as if XFS_BMAPI_METADATA
 * was set and all the others were clear.
 */
int						/* error */
xfs_bmapi_single(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	int			whichfork,	/* data or attr fork */
	xfs_fsblock_t		*fsb,		/* output: mapped block */
	xfs_fileoff_t		bno);		/* starting file offs. mapped */

/*
 * Unmap (remove) blocks from a file.
 * If nexts is nonzero then the number of extents to remove is limited to
 * that value.  If not all extents in the block range can be removed then
 * *done is set.
 */
int						/* error */
xfs_bunmapi(
	struct xfs_trans	*tp,		/* transaction pointer */
	struct xfs_inode	*ip,		/* incore inode */
	xfs_fileoff_t		bno,		/* starting offset to unmap */
	xfs_filblks_t		len,		/* length to unmap in file */
	int			flags,		/* XFS_BMAPI_... */
	xfs_extnum_t		nexts,		/* number of extents max */
	xfs_fsblock_t		*firstblock,	/* first allocated block
						   controls a.g. for allocs */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	xfs_extdelta_t		*delta,		/* o: change made to incore
						   extents */
	int			*done);		/* set if not done yet */

/*
 * Check an extent list, which has just been read, for
 * any bit in the extent flag field.
 */
int
xfs_check_nostate_extents(
	struct xfs_ifork	*ifp,
	xfs_extnum_t		idx,
	xfs_extnum_t		num);

#ifdef __KERNEL__

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.
 *
 * Return 1 if the given transaction was committed and a new one allocated,
 * and 0 otherwise.
 */
int						/* error */
xfs_bmap_finish(
	struct xfs_trans	**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	int			*committed);	/* xact committed or not */

/* bmap to userspace formatter - copy to user & advance pointer */
typedef int (*xfs_bmap_format_t)(void **, struct getbmapx *, int *);

/*
 * Get inode's extents as described in bmv, and format for output.
 */
int						/* error code */
xfs_getbmap(
	xfs_inode_t		*ip,
	struct getbmapx		*bmv,		/* user bmap structure */
	xfs_bmap_format_t	formatter,	/* format to user */
	void			*arg);		/* formatter arg */

/*
 * Check if the endoff is outside the last extent. If so the caller will grow
 * the allocation to a stripe unit boundary
 */
int
xfs_bmap_eof(
	struct xfs_inode        *ip,
	xfs_fileoff_t           endoff,
	int                     whichfork,
	int                     *eof);

/*
 * Count fsblocks of the given fork.
 */
int
xfs_bmap_count_blocks(
	xfs_trans_t		*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	int			*count);

/*
 * Search the extent records for the entry containing block bno.
 * If bno lies in a hole, point to the next entry.  If bno lies
 * past eof, *eofp will be set, and *prevp will contain the last
 * entry (null if none).  Else, *lastxp will be set to the index
 * of the found entry; *gotp will contain the entry.
 */
xfs_bmbt_rec_host_t *
xfs_bmap_search_multi_extents(struct xfs_ifork *, xfs_fileoff_t, int *,
			xfs_extnum_t *, xfs_bmbt_irec_t *, xfs_bmbt_irec_t *);

#endif	/* __KERNEL__ */

#endif	/* __XFS_BMAP_H__ */
