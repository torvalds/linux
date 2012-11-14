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
#ifndef __XFS_IALLOC_H__
#define	__XFS_IALLOC_H__

struct xfs_buf;
struct xfs_dinode;
struct xfs_imap;
struct xfs_mount;
struct xfs_trans;

/*
 * Allocation parameters for inode allocation.
 */
#define	XFS_IALLOC_INODES(mp)	(mp)->m_ialloc_inos
#define	XFS_IALLOC_BLOCKS(mp)	(mp)->m_ialloc_blks

/*
 * Move inodes in clusters of this size.
 */
#define	XFS_INODE_BIG_CLUSTER_SIZE	8192
#define	XFS_INODE_CLUSTER_SIZE(mp)	(mp)->m_inode_cluster_size

/*
 * Make an inode pointer out of the buffer/offset.
 */
static inline struct xfs_dinode *
xfs_make_iptr(struct xfs_mount *mp, struct xfs_buf *b, int o)
{
	return (xfs_dinode_t *)
		(xfs_buf_offset(b, o << (mp)->m_sb.sb_inodelog));
}

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 *
 * To work within the constraint of one allocation per transaction,
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to make more free inodes.  If an inode is
 * available without an allocation, agbp would be set to the current
 * agbp and alloc_done set to false.
 * If an allocation needed to be done, agbp would be set to the
 * inode header of the allocation group and alloc_done set to true.
 * The caller should then commit the current transaction and allocate a new
 * transaction.  xfs_dialloc() should then be called again with
 * the agbp value returned from the previous call.
 *
 * Once we successfully pick an inode its number is returned and the
 * on-disk data structures are updated.  The inode itself is not read
 * in, since doing so would break ordering constraints with xfs_reclaim.
 *
 * *agbp should be set to NULL on the first call, *alloc_done set to FALSE.
 */
int					/* error */
xfs_dialloc(
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	umode_t		mode,		/* mode bits for new inode */
	int		okalloc,	/* ok to allocate more space */
	struct xfs_buf	**agbp,		/* buf for a.g. inode header */
	xfs_ino_t	*inop);		/* inode number allocated */

/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 * The on-disk inode is not changed by this operation, only the
 * btree (free inode mask) is changed.
 */
int					/* error */
xfs_difree(
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_ino_t	inode,		/* inode to be freed */
	struct xfs_bmap_free *flist,	/* extents to free */
	int		*delete,	/* set if inode cluster was deleted */
	xfs_ino_t	*first_ino);	/* first inode in deleted cluster */

/*
 * Return the location of the inode in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode to locate */
	struct xfs_imap	*imap,		/* location map structure */
	uint		flags);		/* flags for inode btree lookup */

/*
 * Compute and fill in value of m_in_maxlevels.
 */
void
xfs_ialloc_compute_maxlevels(
	struct xfs_mount *mp);		/* file system mount structure */

/*
 * Log specified fields for the ag hdr (inode section)
 */
void
xfs_ialloc_log_agi(
	struct xfs_trans *tp,		/* transaction pointer */
	struct xfs_buf	*bp,		/* allocation group header buffer */
	int		fields);	/* bitmask of fields to log */

/*
 * Read in the allocation group header (inode allocation section)
 */
int					/* error */
xfs_ialloc_read_agi(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	struct xfs_buf	**bpp);		/* allocation group hdr buf */

/*
 * Read in the allocation group header to initialise the per-ag data
 * in the mount structure
 */
int
xfs_ialloc_pagi_init(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
        xfs_agnumber_t  agno);		/* allocation group number */

/*
 * Lookup a record by ino in the btree given by cur.
 */
int xfs_inobt_lookup(struct xfs_btree_cur *cur, xfs_agino_t ino,
		xfs_lookup_t dir, int *stat);

/*
 * Get the data from the pointed-to record.
 */
int xfs_inobt_get_rec(struct xfs_btree_cur *cur,
		xfs_inobt_rec_incore_t *rec, int *stat);

void xfs_agi_write_verify(struct xfs_buf *bp);

#endif	/* __XFS_IALLOC_H__ */
