// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_BMAP_UTIL_H__
#define	__XFS_BMAP_UTIL_H__

/* Kernel only BMAP related definitions and functions */

struct xfs_bmbt_irec;
struct xfs_extent_free_item;
struct xfs_ifork;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;
struct xfs_bmalloca;

#ifdef CONFIG_XFS_RT
int	xfs_bmap_rtalloc(struct xfs_bmalloca *ap);
#else /* !CONFIG_XFS_RT */
/*
 * Attempts to allocate RT extents when RT is disable indicates corruption and
 * should trigger a shutdown.
 */
static inline int
xfs_bmap_rtalloc(struct xfs_bmalloca *ap)
{
	return -EFSCORRUPTED;
}
#endif /* CONFIG_XFS_RT */

int	xfs_bmap_eof(struct xfs_inode *ip, xfs_fileoff_t endoff,
		     int whichfork, int *eof);
int	xfs_bmap_punch_delalloc_range(struct xfs_inode *ip,
		xfs_fileoff_t start_fsb, xfs_fileoff_t length);

struct kgetbmap {
	__s64		bmv_offset;	/* file offset of segment in blocks */
	__s64		bmv_block;	/* starting block (64-bit daddr_t)  */
	__s64		bmv_length;	/* length of segment, blocks	    */
	__s32		bmv_oflags;	/* output flags */
};
int	xfs_getbmap(struct xfs_inode *ip, struct getbmapx *bmv,
		struct kgetbmap *out);

/* functions in xfs_bmap.c that are only needed by xfs_bmap_util.c */
int	xfs_bmap_extsize_align(struct xfs_mount *mp, struct xfs_bmbt_irec *gotp,
			       struct xfs_bmbt_irec *prevp, xfs_extlen_t extsz,
			       int rt, int eof, int delay, int convert,
			       xfs_fileoff_t *offp, xfs_extlen_t *lenp);
void	xfs_bmap_adjacent(struct xfs_bmalloca *ap);
int	xfs_bmap_last_extent(struct xfs_trans *tp, struct xfs_inode *ip,
			     int whichfork, struct xfs_bmbt_irec *rec,
			     int *is_empty);

/* preallocation and hole punch interface */
int	xfs_alloc_file_space(struct xfs_inode *ip, xfs_off_t offset,
			     xfs_off_t len, int alloc_type);
int	xfs_free_file_space(struct xfs_inode *ip, xfs_off_t offset,
			    xfs_off_t len);
int	xfs_zero_file_space(struct xfs_inode *ip, xfs_off_t offset,
			    xfs_off_t len);
int	xfs_collapse_file_space(struct xfs_inode *, xfs_off_t offset,
				xfs_off_t len);
int	xfs_insert_file_space(struct xfs_inode *, xfs_off_t offset,
				xfs_off_t len);

/* EOF block manipulation functions */
bool	xfs_can_free_eofblocks(struct xfs_inode *ip, bool force);
int	xfs_free_eofblocks(struct xfs_inode *ip);

int	xfs_swap_extents(struct xfs_inode *ip, struct xfs_inode *tip,
			 struct xfs_swapext *sx);

xfs_daddr_t xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb);

xfs_extnum_t xfs_bmap_count_leaves(struct xfs_ifork *ifp, xfs_filblks_t *count);
int xfs_bmap_count_blocks(struct xfs_trans *tp, struct xfs_inode *ip,
			  int whichfork, xfs_extnum_t *nextents,
			  xfs_filblks_t *count);

#endif	/* __XFS_BMAP_UTIL_H__ */
