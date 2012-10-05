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
 * Flags for xfs_bmapi_*
 */
#define XFS_BMAPI_ENTIRE	0x001	/* return entire extent, not trimmed */
#define XFS_BMAPI_METADATA	0x002	/* mapping metadata not user data */
#define XFS_BMAPI_ATTRFORK	0x004	/* use attribute fork not data */
#define XFS_BMAPI_PREALLOC	0x008	/* preallocation op: unwritten space */
#define XFS_BMAPI_IGSTATE	0x010	/* Ignore state - */
					/* combine contig. space */
#define XFS_BMAPI_CONTIG	0x020	/* must allocate only one extent */
/*
 * unwritten extent conversion - this needs write cache flushing and no additional
 * allocation alignments. When specified with XFS_BMAPI_PREALLOC it converts
 * from written to unwritten, otherwise convert from unwritten to written.
 */
#define XFS_BMAPI_CONVERT	0x040
#define XFS_BMAPI_STACK_SWITCH	0x080

#define XFS_BMAPI_FLAGS \
	{ XFS_BMAPI_ENTIRE,	"ENTIRE" }, \
	{ XFS_BMAPI_METADATA,	"METADATA" }, \
	{ XFS_BMAPI_ATTRFORK,	"ATTRFORK" }, \
	{ XFS_BMAPI_PREALLOC,	"PREALLOC" }, \
	{ XFS_BMAPI_IGSTATE,	"IGSTATE" }, \
	{ XFS_BMAPI_CONTIG,	"CONTIG" }, \
	{ XFS_BMAPI_CONVERT,	"CONVERT" }, \
	{ XFS_BMAPI_STACK_SWITCH, "STACK_SWITCH" }


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
	xfs_fsblock_t		*firstblock; /* i/o first block allocated */
	struct xfs_bmap_free	*flist;	/* bmap freelist */
	struct xfs_trans	*tp;	/* transaction pointer */
	struct xfs_inode	*ip;	/* incore inode pointer */
	struct xfs_bmbt_irec	prev;	/* extent before the new one */
	struct xfs_bmbt_irec	got;	/* extent after, or delayed */

	xfs_fileoff_t		offset;	/* offset in file filling in */
	xfs_extlen_t		length;	/* i/o length asked/allocated */
	xfs_fsblock_t		blkno;	/* starting block of new extent */

	struct xfs_btree_cur	*cur;	/* btree cursor */
	xfs_extnum_t		idx;	/* current extent index */
	int			nallocs;/* number of extents alloc'd */
	int			logflags;/* flags for transaction logging */

	xfs_extlen_t		total;	/* total blocks needed for xaction */
	xfs_extlen_t		minlen;	/* minimum allocation size (blocks) */
	xfs_extlen_t		minleft; /* amount must be left after alloc */
	char			eof;	/* set if allocating past last extent */
	char			wasdel;	/* replacing a delayed allocation */
	char			userdata;/* set if is user data */
	char			aeof;	/* allocated space at eof */
	char			conv;	/* overwriting unwritten extents */
	char			stack_switch;
	int			flags;
	struct completion	*done;
	struct work_struct	work;
	int			result;
} xfs_bmalloca_t;

/*
 * Flags for xfs_bmap_add_extent*.
 */
#define BMAP_LEFT_CONTIG	(1 << 0)
#define BMAP_RIGHT_CONTIG	(1 << 1)
#define BMAP_LEFT_FILLING	(1 << 2)
#define BMAP_RIGHT_FILLING	(1 << 3)
#define BMAP_LEFT_DELAY		(1 << 4)
#define BMAP_RIGHT_DELAY	(1 << 5)
#define BMAP_LEFT_VALID		(1 << 6)
#define BMAP_RIGHT_VALID	(1 << 7)
#define BMAP_ATTRFORK		(1 << 8)

#define XFS_BMAP_EXT_FLAGS \
	{ BMAP_LEFT_CONTIG,	"LC" }, \
	{ BMAP_RIGHT_CONTIG,	"RC" }, \
	{ BMAP_LEFT_FILLING,	"LF" }, \
	{ BMAP_RIGHT_FILLING,	"RF" }, \
	{ BMAP_ATTRFORK,	"ATTR" }

#if defined(__KERNEL) && defined(DEBUG)
void	xfs_bmap_trace_exlist(struct xfs_inode *ip, xfs_extnum_t cnt,
		int whichfork, unsigned long caller_ip);
#define	XFS_BMAP_TRACE_EXLIST(ip,c,w)	\
	xfs_bmap_trace_exlist(ip,c,w, _THIS_IP_)
#else
#define	XFS_BMAP_TRACE_EXLIST(ip,c,w)
#endif

int	xfs_bmap_add_attrfork(struct xfs_inode *ip, int size, int rsvd);
void	xfs_bmap_add_free(xfs_fsblock_t bno, xfs_filblks_t len,
		struct xfs_bmap_free *flist, struct xfs_mount *mp);
void	xfs_bmap_cancel(struct xfs_bmap_free *flist);
void	xfs_bmap_compute_maxlevels(struct xfs_mount *mp, int whichfork);
int	xfs_bmap_first_unused(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_extlen_t len, xfs_fileoff_t *unused, int whichfork);
int	xfs_bmap_last_before(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t *last_block, int whichfork);
int	xfs_bmap_last_offset(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t *unused, int whichfork);
int	xfs_bmap_one_block(struct xfs_inode *ip, int whichfork);
int	xfs_bmap_read_extents(struct xfs_trans *tp, struct xfs_inode *ip,
		int whichfork);
int	xfs_bmapi_read(struct xfs_inode *ip, xfs_fileoff_t bno,
		xfs_filblks_t len, struct xfs_bmbt_irec *mval,
		int *nmap, int flags);
int	xfs_bmapi_delay(struct xfs_inode *ip, xfs_fileoff_t bno,
		xfs_filblks_t len, struct xfs_bmbt_irec *mval,
		int *nmap, int flags);
int	xfs_bmapi_write(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t bno, xfs_filblks_t len, int flags,
		xfs_fsblock_t *firstblock, xfs_extlen_t total,
		struct xfs_bmbt_irec *mval, int *nmap,
		struct xfs_bmap_free *flist);
int	xfs_bunmapi(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t bno, xfs_filblks_t len, int flags,
		xfs_extnum_t nexts, xfs_fsblock_t *firstblock,
		struct xfs_bmap_free *flist, int *done);
int	xfs_check_nostate_extents(struct xfs_ifork *ifp, xfs_extnum_t idx,
		xfs_extnum_t num);
uint	xfs_default_attroffset(struct xfs_inode *ip);

#ifdef __KERNEL__
/* bmap to userspace formatter - copy to user & advance pointer */
typedef int (*xfs_bmap_format_t)(void **, struct getbmapx *, int *);

int	xfs_bmap_finish(struct xfs_trans **tp, struct xfs_bmap_free *flist,
		int *committed);
int	xfs_getbmap(struct xfs_inode *ip, struct getbmapx *bmv,
		xfs_bmap_format_t formatter, void *arg);
int	xfs_bmap_eof(struct xfs_inode *ip, xfs_fileoff_t endoff,
		int whichfork, int *eof);
int	xfs_bmap_count_blocks(struct xfs_trans *tp, struct xfs_inode *ip,
		int whichfork, int *count);
int	xfs_bmap_punch_delalloc_range(struct xfs_inode *ip,
		xfs_fileoff_t start_fsb, xfs_fileoff_t length);

xfs_daddr_t xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb);

#endif	/* __KERNEL__ */

#endif	/* __XFS_BMAP_H__ */
