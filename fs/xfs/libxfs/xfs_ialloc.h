// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IALLOC_H__
#define	__XFS_IALLOC_H__

struct xfs_buf;
struct xfs_diyesde;
struct xfs_imap;
struct xfs_mount;
struct xfs_trans;
struct xfs_btree_cur;

/* Move iyesdes in clusters of this size */
#define	XFS_INODE_BIG_CLUSTER_SIZE	8192

struct xfs_icluster {
	bool		deleted;	/* record is deleted */
	xfs_iyes_t	first_iyes;	/* first iyesde number */
	uint64_t	alloc;		/* iyesde phys. allocation bitmap for
					 * sparse chunks */
};

/*
 * Make an iyesde pointer out of the buffer/offset.
 */
static inline struct xfs_diyesde *
xfs_make_iptr(struct xfs_mount *mp, struct xfs_buf *b, int o)
{
	return xfs_buf_offset(b, o << (mp)->m_sb.sb_iyesdelog);
}

/*
 * Allocate an iyesde on disk.
 * Mode is used to tell whether the new iyesde will need space, and whether
 * it is a directory.
 *
 * To work within the constraint of one allocation per transaction,
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to make more free iyesdes.  If an iyesde is
 * available without an allocation, agbp would be set to the current
 * agbp and alloc_done set to false.
 * If an allocation needed to be done, agbp would be set to the
 * iyesde header of the allocation group and alloc_done set to true.
 * The caller should then commit the current transaction and allocate a new
 * transaction.  xfs_dialloc() should then be called again with
 * the agbp value returned from the previous call.
 *
 * Once we successfully pick an iyesde its number is returned and the
 * on-disk data structures are updated.  The iyesde itself is yest read
 * in, since doing so would break ordering constraints with xfs_reclaim.
 *
 * *agbp should be set to NULL on the first call, *alloc_done set to FALSE.
 */
int					/* error */
xfs_dialloc(
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_iyes_t	parent,		/* parent iyesde (directory) */
	umode_t		mode,		/* mode bits for new iyesde */
	struct xfs_buf	**agbp,		/* buf for a.g. iyesde header */
	xfs_iyes_t	*iyesp);		/* iyesde number allocated */

/*
 * Free disk iyesde.  Carefully avoids touching the incore iyesde, all
 * manipulations incore are the caller's responsibility.
 * The on-disk iyesde is yest changed by this operation, only the
 * btree (free iyesde mask) is changed.
 */
int					/* error */
xfs_difree(
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_iyes_t	iyesde,		/* iyesde to be freed */
	struct xfs_icluster *ifree);	/* cluster info if deleted */

/*
 * Return the location of the iyesde in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_iyes_t	iyes,		/* iyesde to locate */
	struct xfs_imap	*imap,		/* location map structure */
	uint		flags);		/* flags for iyesde btree lookup */

/*
 * Log specified fields for the ag hdr (iyesde section)
 */
void
xfs_ialloc_log_agi(
	struct xfs_trans *tp,		/* transaction pointer */
	struct xfs_buf	*bp,		/* allocation group header buffer */
	int		fields);	/* bitmask of fields to log */

/*
 * Read in the allocation group header (iyesde allocation section)
 */
int					/* error */
xfs_ialloc_read_agi(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_agnumber_t	agyes,		/* allocation group number */
	struct xfs_buf	**bpp);		/* allocation group hdr buf */

/*
 * Read in the allocation group header to initialise the per-ag data
 * in the mount structure
 */
int
xfs_ialloc_pagi_init(
	struct xfs_mount *mp,		/* file system mount structure */
	struct xfs_trans *tp,		/* transaction pointer */
        xfs_agnumber_t  agyes);		/* allocation group number */

/*
 * Lookup a record by iyes in the btree given by cur.
 */
int xfs_iyesbt_lookup(struct xfs_btree_cur *cur, xfs_agiyes_t iyes,
		xfs_lookup_t dir, int *stat);

/*
 * Get the data from the pointed-to record.
 */
int xfs_iyesbt_get_rec(struct xfs_btree_cur *cur,
		xfs_iyesbt_rec_incore_t *rec, int *stat);

/*
 * Iyesde chunk initialisation routine
 */
int xfs_ialloc_iyesde_init(struct xfs_mount *mp, struct xfs_trans *tp,
			  struct list_head *buffer_list, int icount,
			  xfs_agnumber_t agyes, xfs_agblock_t agbyes,
			  xfs_agblock_t length, unsigned int gen);

int xfs_read_agi(struct xfs_mount *mp, struct xfs_trans *tp,
		xfs_agnumber_t agyes, struct xfs_buf **bpp);

union xfs_btree_rec;
void xfs_iyesbt_btrec_to_irec(struct xfs_mount *mp, union xfs_btree_rec *rec,
		struct xfs_iyesbt_rec_incore *irec);
int xfs_ialloc_has_iyesdes_at_extent(struct xfs_btree_cur *cur,
		xfs_agblock_t byes, xfs_extlen_t len, bool *exists);
int xfs_ialloc_has_iyesde_record(struct xfs_btree_cur *cur, xfs_agiyes_t low,
		xfs_agiyes_t high, bool *exists);
int xfs_ialloc_count_iyesdes(struct xfs_btree_cur *cur, xfs_agiyes_t *count,
		xfs_agiyes_t *freecount);
int xfs_iyesbt_insert_rec(struct xfs_btree_cur *cur, uint16_t holemask,
		uint8_t count, int32_t freecount, xfs_iyesfree_t free,
		int *stat);

int xfs_ialloc_cluster_alignment(struct xfs_mount *mp);
void xfs_ialloc_setup_geometry(struct xfs_mount *mp);
xfs_iyes_t xfs_ialloc_calc_rootiyes(struct xfs_mount *mp, int sunit);

#endif	/* __XFS_IALLOC_H__ */
