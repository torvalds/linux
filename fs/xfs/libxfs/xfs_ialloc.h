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
struct xfs_perag;

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
 * Allocate an inode on disk.  Mode is used to tell whether the new inode will
 * need space, and whether it is a directory.
 */
int xfs_dialloc(struct xfs_trans **tpp, xfs_ino_t parent, umode_t mode,
		xfs_ino_t *new_ino);

int xfs_difree(struct xfs_trans *tp, struct xfs_perag *pag,
		xfs_ino_t ino, struct xfs_icluster *ifree);

/*
 * Return the location of the inode in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	struct xfs_perag *pag,
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
	uint32_t	fields);	/* bitmask of fields to log */

int xfs_read_agi(struct xfs_perag *pag, struct xfs_trans *tp,
		struct xfs_buf **agibpp);
int xfs_ialloc_read_agi(struct xfs_perag *pag, struct xfs_trans *tp,
		struct xfs_buf **agibpp);

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


union xfs_btree_rec;
void xfs_inobt_btrec_to_irec(struct xfs_mount *mp,
		const union xfs_btree_rec *rec,
		struct xfs_inobt_rec_incore *irec);
xfs_failaddr_t xfs_inobt_check_irec(struct xfs_btree_cur *cur,
		const struct xfs_inobt_rec_incore *irec);
int xfs_ialloc_has_inodes_at_extent(struct xfs_btree_cur *cur,
		xfs_agblock_t bno, xfs_extlen_t len,
		enum xbtree_recpacking *outcome);
int xfs_ialloc_count_inodes(struct xfs_btree_cur *cur, xfs_agino_t *count,
		xfs_agino_t *freecount);
int xfs_inobt_insert_rec(struct xfs_btree_cur *cur, uint16_t holemask,
		uint8_t count, int32_t freecount, xfs_inofree_t free,
		int *stat);

int xfs_ialloc_cluster_alignment(struct xfs_mount *mp);
void xfs_ialloc_setup_geometry(struct xfs_mount *mp);
xfs_ino_t xfs_ialloc_calc_rootino(struct xfs_mount *mp, int sunit);

int xfs_ialloc_check_shrink(struct xfs_perag *pag, struct xfs_trans *tp,
		struct xfs_buf *agibp, xfs_agblock_t new_length);

#endif	/* __XFS_IALLOC_H__ */
