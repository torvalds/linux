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

int	xfs_bmap_rtalloc(struct xfs_bmalloca *ap);
int	xfs_bmap_eof(struct xfs_inode *ip, xfs_fileoff_t endoff,
		     int whichfork, int *eof);
int	xfs_bmap_punch_delalloc_range(struct xfs_inode *ip,
		xfs_fileoff_t start_fsb, xfs_fileoff_t length);

/* bmap to userspace formatter - copy to user & advance pointer */
typedef int (*xfs_bmap_format_t)(void **, struct getbmapx *);
int	xfs_getbmap(struct xfs_inode *ip, struct getbmapx *bmv,
		xfs_bmap_format_t formatter, void *arg);

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

#endif	/* __XFS_BMAP_UTIL_H__ */
