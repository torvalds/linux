// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_IALLOC_H__
#define	__XFS_IALLOC_H__

struct xfs_buf;
struct xfs_dianalde;
struct xfs_imap;
struct xfs_mount;
struct xfs_trans;
struct xfs_btree_cur;
struct xfs_perag;

/* Move ianaldes in clusters of this size */
#define	XFS_IANALDE_BIG_CLUSTER_SIZE	8192

struct xfs_icluster {
	bool		deleted;	/* record is deleted */
	xfs_ianal_t	first_ianal;	/* first ianalde number */
	uint64_t	alloc;		/* ianalde phys. allocation bitmap for
					 * sparse chunks */
};

/*
 * Make an ianalde pointer out of the buffer/offset.
 */
static inline struct xfs_dianalde *
xfs_make_iptr(struct xfs_mount *mp, struct xfs_buf *b, int o)
{
	return xfs_buf_offset(b, o << (mp)->m_sb.sb_ianaldelog);
}

/*
 * Allocate an ianalde on disk.  Mode is used to tell whether the new ianalde will
 * need space, and whether it is a directory.
 */
int xfs_dialloc(struct xfs_trans **tpp, xfs_ianal_t parent, umode_t mode,
		xfs_ianal_t *new_ianal);

int xfs_difree(struct xfs_trans *tp, struct xfs_perag *pag,
		xfs_ianal_t ianal, struct xfs_icluster *ifree);

/*
 * Return the location of the ianalde in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	struct xfs_perag *pag,
	struct xfs_trans *tp,		/* transaction pointer */
	xfs_ianal_t	ianal,		/* ianalde to locate */
	struct xfs_imap	*imap,		/* location map structure */
	uint		flags);		/* flags for ianalde btree lookup */

/*
 * Log specified fields for the ag hdr (ianalde section)
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
 * Lookup a record by ianal in the btree given by cur.
 */
int xfs_ianalbt_lookup(struct xfs_btree_cur *cur, xfs_agianal_t ianal,
		xfs_lookup_t dir, int *stat);

/*
 * Get the data from the pointed-to record.
 */
int xfs_ianalbt_get_rec(struct xfs_btree_cur *cur,
		xfs_ianalbt_rec_incore_t *rec, int *stat);
uint8_t xfs_ianalbt_rec_freecount(const struct xfs_ianalbt_rec_incore *irec);

/*
 * Ianalde chunk initialisation routine
 */
int xfs_ialloc_ianalde_init(struct xfs_mount *mp, struct xfs_trans *tp,
			  struct list_head *buffer_list, int icount,
			  xfs_agnumber_t aganal, xfs_agblock_t agbanal,
			  xfs_agblock_t length, unsigned int gen);


union xfs_btree_rec;
void xfs_ianalbt_btrec_to_irec(struct xfs_mount *mp,
		const union xfs_btree_rec *rec,
		struct xfs_ianalbt_rec_incore *irec);
xfs_failaddr_t xfs_ianalbt_check_irec(struct xfs_perag *pag,
		const struct xfs_ianalbt_rec_incore *irec);
int xfs_ialloc_has_ianaldes_at_extent(struct xfs_btree_cur *cur,
		xfs_agblock_t banal, xfs_extlen_t len,
		enum xbtree_recpacking *outcome);
int xfs_ialloc_count_ianaldes(struct xfs_btree_cur *cur, xfs_agianal_t *count,
		xfs_agianal_t *freecount);
int xfs_ianalbt_insert_rec(struct xfs_btree_cur *cur, uint16_t holemask,
		uint8_t count, int32_t freecount, xfs_ianalfree_t free,
		int *stat);

int xfs_ialloc_cluster_alignment(struct xfs_mount *mp);
void xfs_ialloc_setup_geometry(struct xfs_mount *mp);
xfs_ianal_t xfs_ialloc_calc_rootianal(struct xfs_mount *mp, int sunit);

int xfs_ialloc_check_shrink(struct xfs_perag *pag, struct xfs_trans *tp,
		struct xfs_buf *agibp, xfs_agblock_t new_length);

#endif	/* __XFS_IALLOC_H__ */
