// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IALLOC_H__
#define	__XFS_IALLOC_H__

struct xfs_buf;
struct xfs_dinode;
struct xfs_imap;
struct xfs_mount;
struct xfs_trans;
struct xfs_btree_cur;

/* Move inodes in clusters of this size */
#define	XFS_INODE_BIG_CLUSTER_SIZE	8192

struct xfs_icluster {
	bool		deleted;	/* record is deleted */
	xfs_ino_t	first_ino;	/* first inode number */
	uint64_t	alloc;		/* inode phys. allocation bitmap for
					 * sparse chunks */
};

/*
 * Make an inode pointer out of the buffer/offset.
 */
static inline struct xfs_dinode *
xfs_make_iptr(struct xfs_mount *mp, struct xfs_buf *b, int o)
{
	return xfs_buf_offset(b, o << (mp)->m_sb.sb_inodelog);
}

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 *
 * There are two phases to inode allocation: selecting an AG and ensuring
 * that it contains free inodes, followed by allocating one of the free
 * inodes. xfs_dialloc_select_ag() does the former and returns a locked AGI
 * to the caller, ensuring that followup call to xfs_dialloc_ag() will
 * have free inodes to allocate from. xfs_dialloc_ag() will return the inode
 * number of the free inode we allocated.
 */
int					/* error */
xfs_dialloc_select_ag(
	struct xfs_trans **tpp,		/* double pointer of transaction */
	xfs_ino_t	parent,		/* parent inode (directory) */
	umode_t		mode,		/* mode bits for new inode */
	struct xfs_buf	**IO_agbp);

int
xfs_dialloc_ag(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_ino_t		parent,
	xfs_ino_t		*inop);

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
	struct xfs_icluster *ifree);	/* cluster info if deleted */

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

/*
 * Inode chunk initialisation routine
 */
int xfs_ialloc_inode_init(struct xfs_mount *mp, struct xfs_trans *tp,
			  struct list_head *buffer_list, int icount,
			  xfs_agnumber_t agno, xfs_agblock_t agbno,
			  xfs_agblock_t length, unsigned int gen);

int xfs_read_agi(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agno, struct xfs_buf **bpp);

union xfs_btree_rec;
void xfs_inobt_btrec_to_irec(struct xfs_mount *mp, union xfs_btree_rec *rec,
		struct xfs_inobt_rec_incore *irec);
int xfs_ialloc_has_inodes_at_extent(struct xfs_btree_cur *cur,
		xfs_agblock_t bno, xfs_extlen_t len, bool *exists);
int xfs_ialloc_has_inode_record(struct xfs_btree_cur *cur, xfs_agino_t low,
		xfs_agino_t high, bool *exists);
int xfs_ialloc_count_inodes(struct xfs_btree_cur *cur, xfs_agino_t *count,
		xfs_agino_t *freecount);
int xfs_inobt_insert_rec(struct xfs_btree_cur *cur, uint16_t holemask,
		uint8_t count, int32_t freecount, xfs_inofree_t free,
		int *stat);

int xfs_ialloc_cluster_alignment(struct xfs_mount *mp);
void xfs_ialloc_setup_geometry(struct xfs_mount *mp);
xfs_ino_t xfs_ialloc_calc_rootino(struct xfs_mount *mp, int sunit);

#endif	/* __XFS_IALLOC_H__ */
